#include "temporal_upscaler.h"

#if defined(LO_GPU_PLUME)
#include "dlss_ngx.h"
#include "fsr_upscaler.h"
#include "fsr_projection.h"
#include <algorithm>
#include <cfloat>

#if LO_PLATFORM_MACOS
namespace plume {
    struct RenderCommandList; struct RenderTexture;
    bool EncodeMetalFxSpatialScale(RenderCommandList* commandList, const RenderTexture* input,
        const RenderTexture* output, uint32_t inputWidth, uint32_t inputHeight);
    void EncodeMetalQueueSignal(RenderCommandList* commandList);
    void EncodeMetalQueueWait(RenderCommandList* commandList);
}
#endif

namespace gpu {
namespace {
SrResultStatus Convert(dlss::SrStatus status) {
    switch (status) {
    case dlss::SrStatus::Executable: return SrResultStatus::Ready;
    case dlss::SrStatus::Bypass: return SrResultStatus::Unavailable;
    case dlss::SrStatus::NeedsReconfigure: return SrResultStatus::NeedsReconfigure;
    case dlss::SrStatus::Failed: return SrResultStatus::Failed;
    case dlss::SrStatus::DeviceLost: return SrResultStatus::DeviceLost;
    }
    return SrResultStatus::Failed;
}
SrResultStatus Convert(fsr::Status status) {
    switch (status) {
    case fsr::Status::Ready: return SrResultStatus::Ready;
    case fsr::Status::Unavailable: return SrResultStatus::Unavailable;
    case fsr::Status::NeedsReconfigure: return SrResultStatus::NeedsReconfigure;
    case fsr::Status::Failed: return SrResultStatus::Failed;
    case fsr::Status::DeviceLost: return SrResultStatus::DeviceLost;
    case fsr::Status::InputUnavailable: return SrResultStatus::InputUnavailable;
    }
    return SrResultStatus::Failed;
}
fsr::Config FsrConfig(const SrRequest& request) {
    return {request.inputs.color.width, request.inputs.color.height, request.plan.output.width,
        request.plan.output.height, request.plan.fsrQuality, request.plan.deviceEpoch};
}
fsr::FrameMetadata FsrMetadata(const SrRequest& request) {
    fsr::FrameMetadata frame{};
    frame.enableSharpening = request.options.fsrSharpening;
    frame.sharpness = request.options.fsrSharpness;
    if (!request.inputs.cameraValid || !request.inputs.color.height) return frame;
    const auto projection = fsr::DeriveProjection(request.inputs.cameraViewProjection,
        double(request.inputs.color.width) / request.inputs.color.height);
    if (!projection) return frame;
    frame.cameraValid = true;
    frame.cameraNear = FLT_MAX;
    frame.cameraFar = projection->nearDistance;
    frame.verticalFovRadians = projection->verticalFovRadians;
    // P1 uses the SDK default unit scale. Guest world units are not calibrated.
    frame.viewSpaceToMetersFactor = 1.0f;
    frame.frameTimeDeltaMilliseconds = request.inputs.frameTimeDeltaMilliseconds;
    frame.depthScale = projection->depthScale;
    frame.depthBias = projection->depthBias;
    return frame;
}
dlss::SrConfig DlssConfig(const SrRequest& request) {
    dlss::SrConfig config{};
    config.renderExtent = {request.inputs.color.width, request.inputs.color.height};
    config.outputExtent = {request.plan.output.width, request.plan.output.height};
    config.quality = request.plan.dlssQuality;
    config.deviceEpoch = request.plan.deviceEpoch;
    config.depthInverted = request.inputs.depthConvention == temporal::DepthConvention::Reversed;
    config.colorSpace = request.inputs.colorEncoding == temporal::ColorEncoding::Sdr ?
        dlss::SrColorSpace::DisplayEncoded : dlss::SrColorSpace::Linear;
    return config;
}
}

TemporalUpscaler::TemporalUpscaler(dlss::Controller& controller)
    : TemporalUpscaler(&controller) {}
TemporalUpscaler::TemporalUpscaler(dlss::Controller* controller)
    : dlss_(controller), fsr_(std::make_unique<fsr::Controller>()) {}
TemporalUpscaler::~TemporalUpscaler() = default;

upscaling::OutputSizing TemporalUpscaler::QuerySizing(const plume::VulkanInterface& api,
    const plume::VulkanDevice& device, const upscaling::SizingKey& key) {
    if (CanQueryNgxSizing(key) && dlss_)
        return dlss_->QueryOutputSizing(api, device, key);
    upscaling::OutputSizing unavailable{};
    unavailable.key = key;
    for (uint32_t i = 0; i < unavailable.modes.size(); ++i) {
        auto& mode = unavailable.modes[i];
        mode.state = upscaling::SizingState::Unavailable;
        if (key.provider == upscaling::Upscaler::Fsr) {
            if (const auto size = fsr::RecommendedRenderSize({key.outputWidth, key.outputHeight}, upscaling::FsrQuality(i))) {
                mode.state = upscaling::SizingState::Ready;
                mode.optimal = mode.minimum = mode.maximum = *size;
            }
        }
    }
    return unavailable;
}

SrResult TemporalUpscaler::Prepare(plume::VulkanDevice& device, const SrRequest& request) {
    SrResult result{};
    result.requestedProvider = request.plan.requestedUpscaler;
    if (!ValidSrRequest(request)) return result;
    if (request.plan.requestedUpscaler == upscaling::Upscaler::Fsr) {
        result.actualProvider = upscaling::Upscaler::Fsr;
        result.status = FsrMetadata(request).cameraValid ? Convert(fsr_->EnsureSession(device, FsrConfig(request))) :
            SrResultStatus::InputUnavailable;
        return result;
    }
    result.actualProvider = upscaling::Upscaler::Dlss;
    result.status = dlss_ ? Convert(dlss_->EnsureSession(device)) : SrResultStatus::Unavailable;
    return result;
}

SrResult TemporalUpscaler::RecordIsolated(plume::VulkanCommandList& commands,
    const SrRequest& request, plume::VulkanTexture& output, dlss::EvaluateCapture* capture) {
    SrResult result{};
    result.requestedProvider = request.plan.requestedUpscaler;
    if (!ValidSrRequest(request)) return result;
    if (request.plan.requestedUpscaler == upscaling::Upscaler::Fsr) {
        const auto attempt = fsr_->RecordIsolated(commands, FsrConfig(request), request.inputs,
            FsrMetadata(request), output, capture);
        result.actualProvider = upscaling::Upscaler::Fsr;
        result.status = Convert(attempt.status);
        result.rawResult = attempt.sdkResult;
        result.rawVkResult = attempt.vkResult;
        if (attempt.useId) result.token = {upscaling::Upscaler::Fsr, request.plan.deviceEpoch,
            attempt.useId, request.plan.requestSignature, request.plan.geometryEpoch};
        return result;
    }
    if (!dlss_) return result;
    const auto attempt = dlss_->RecordIsolated(commands, DlssConfig(request), request.inputs, output, capture);
    result.actualProvider = upscaling::Upscaler::Dlss;
    result.status = Convert(attempt.status);
    result.rawResult = attempt.rawNgxResult;
    result.rawVkResult = attempt.rawVkResult;
    if (attempt.useId) result.token = {upscaling::Upscaler::Dlss, request.plan.deviceEpoch, attempt.useId,
        request.plan.requestSignature, request.plan.geometryEpoch};
    return result;
}

#ifdef _WIN32
upscaling::OutputSizing TemporalUpscaler::QuerySizing(const plume::D3D12Device& device, const upscaling::SizingKey& key) {
    if (CanQueryNgxSizing(key) && dlss_)
        return dlss_->QueryOutputSizing(device, key);
    upscaling::OutputSizing unavailable{};
    unavailable.key = key;
    for (uint32_t i = 0; i < unavailable.modes.size(); ++i) {
        auto& mode = unavailable.modes[i];
        mode.state = upscaling::SizingState::Unavailable;
        if (key.provider == upscaling::Upscaler::Fsr) {
            if (const auto size = fsr::RecommendedRenderSize({key.outputWidth, key.outputHeight}, upscaling::FsrQuality(i))) {
                mode.state = upscaling::SizingState::Ready;
                mode.optimal = mode.minimum = mode.maximum = *size;
            }
        }
    }
    return unavailable;
}

SrResult TemporalUpscaler::Prepare(plume::D3D12Device& device, const SrRequest& request) {
    SrResult result{};
    result.requestedProvider = request.plan.requestedUpscaler;
    if (!ValidSrRequest(request)) return result;
    if (request.plan.requestedUpscaler == upscaling::Upscaler::Fsr) {
        result.actualProvider = upscaling::Upscaler::Fsr;
        result.status = FsrMetadata(request).cameraValid ? Convert(fsr_->EnsureSession(device, FsrConfig(request))) :
            SrResultStatus::InputUnavailable;
        return result;
    }
    result.actualProvider = upscaling::Upscaler::Dlss;
    result.status = dlss_ ? Convert(dlss_->EnsureSession(device)) : SrResultStatus::Unavailable;
    return result;
}

SrResult TemporalUpscaler::RecordIsolated(plume::D3D12CommandList& commands,
    const SrRequest& request, plume::D3D12Texture& output, dlss::EvaluateCapture* capture) {
    SrResult result{};
    result.requestedProvider = request.plan.requestedUpscaler;
    if (!ValidSrRequest(request)) return result;
    if (request.plan.requestedUpscaler == upscaling::Upscaler::Fsr) {
        const auto attempt = fsr_->RecordIsolated(commands, FsrConfig(request), request.inputs,
            FsrMetadata(request), output, capture);
        result.actualProvider = upscaling::Upscaler::Fsr;
        result.status = Convert(attempt.status);
        result.rawResult = attempt.sdkResult;
        result.rawVkResult = attempt.vkResult;
        if (attempt.useId) result.token = {upscaling::Upscaler::Fsr, request.plan.deviceEpoch,
            attempt.useId, request.plan.requestSignature, request.plan.geometryEpoch};
        return result;
    }
    if (!dlss_) return result;
    const auto attempt = dlss_->RecordIsolated(commands, DlssConfig(request), request.inputs, output, capture);
    result.actualProvider = upscaling::Upscaler::Dlss;
    result.status = Convert(attempt.status);
    result.rawResult = attempt.rawNgxResult;
    result.rawVkResult = attempt.rawVkResult;
    if (attempt.useId) result.token = {upscaling::Upscaler::Dlss, request.plan.deviceEpoch, attempt.useId,
        request.plan.requestSignature, request.plan.geometryEpoch};
    return result;
}

#endif

#if LO_PLATFORM_MACOS
// MetalFX has no fixed quality modes; the FSR ratios pick the render extent.
upscaling::OutputSizing TemporalUpscaler::QuerySizing(const plume::RenderDevice&, const upscaling::SizingKey& key) {
    upscaling::OutputSizing sizing{};
    sizing.key = key;
    for (uint32_t i = 0; i < sizing.modes.size(); ++i) {
        auto& mode = sizing.modes[i];
        mode.state = upscaling::SizingState::Unavailable;
        if (key.provider != upscaling::Upscaler::MetalFx || !key.outputWidth || !key.outputHeight) continue;
        static constexpr double kRatio[] = {1.5, 1.7, 2.0, 1.0};
        const resolution::Size size{std::max(1u, uint32_t(key.outputWidth / kRatio[i] + 0.5)),
            std::max(1u, uint32_t(key.outputHeight / kRatio[i] + 0.5))};
        mode.state = upscaling::SizingState::Ready;
        mode.optimal = mode.minimum = mode.maximum = size;
    }
    return sizing;
}

SrResult TemporalUpscaler::Prepare(plume::RenderDevice&, const SrRequest& request) {
    SrResult result{};
    result.requestedProvider = request.plan.requestedUpscaler;
    if (!ValidSrRequest(request) || request.plan.requestedUpscaler != upscaling::Upscaler::MetalFx) return result;
    result.actualProvider = upscaling::Upscaler::MetalFx;
    result.status = SrResultStatus::Ready;
    return result;
}

SrResult TemporalUpscaler::RecordIsolated(plume::RenderCommandList& commands, const SrRequest& request,
    plume::RenderTexture& output, dlss::EvaluateCapture*) {
    SrResult result{};
    result.requestedProvider = request.plan.requestedUpscaler;
    if (!ValidSrRequest(request) || request.plan.requestedUpscaler != upscaling::Upscaler::MetalFx) return result;
    result.actualProvider = upscaling::Upscaler::MetalFx;
    // The isolated list is the provider's own: open it, record, close it.
    // Phase 1 stand-in: spatial scaling proves the promotion path on Metal.
    const auto& color = request.inputs.color;
    // The renderer's prefix list signalled; its continuation waits for ours.
    commands.begin();
    plume::EncodeMetalQueueWait(&commands);
    const bool encoded = plume::EncodeMetalFxSpatialScale(&commands, color.texture, &output, color.width, color.height);
    if (encoded) plume::EncodeMetalQueueSignal(&commands);
    commands.end();
    result.status = encoded ? SrResultStatus::Ready : SrResultStatus::Failed;
    return result;
}
#endif

void TemporalUpscaler::OnSubmitted(SrUseToken token, uint64_t checkedSerial) {
    if (dlss_) RouteSubmitted(token, checkedSerial, *dlss_);
    if (token.provider == upscaling::Upscaler::Fsr && token.useId && checkedSerial) fsr_->OnBatchSubmitted(token.useId, checkedSerial);
}
void TemporalUpscaler::OnDiscarded(SrUseToken token) {
    if (dlss_) RouteDiscarded(token, *dlss_);
    if (token.provider == upscaling::Upscaler::Fsr && token.useId) fsr_->OnBatchDiscarded(token.useId);
}
void TemporalUpscaler::ReleaseCompleted(uint64_t serial) {
    if (!serial) return;
    if (dlss_) dlss_->ReleaseCompletedThrough(serial);
    fsr_->ReleaseCompletedThrough(serial);
}
bool TemporalUpscaler::HasFeatureState() const { return (dlss_ && dlss_->HasFeatureState()) || fsr_->HasFeatureState(); }
void TemporalUpscaler::ReleaseFeatureAfterGpuDrain() { if (dlss_) dlss_->ReleaseFeatureAfterGpuDrain(); fsr_->ReleaseFeatureAfterGpuDrain(); }
void TemporalUpscaler::ShutdownAfterGpuDrain() { if (dlss_) dlss_->ShutdownAfterGpuDrain(); fsr_->ShutdownAfterGpuDrain(); }
bool TemporalUpscaler::ShutdownComplete() const { return (!dlss_ || dlss_->ShutdownComplete()) && !fsr_->HasFeatureState(); }
void TemporalUpscaler::AbandonAfterDeviceLoss() { if (dlss_) dlss_->AbandonUsesAfterDeviceLoss(); fsr_->AbandonUsesAfterDeviceLoss(); }
} // namespace gpu
#endif
