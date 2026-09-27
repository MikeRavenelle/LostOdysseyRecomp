#include "dlss_frame_generation.h"
#if defined(LO_ENABLE_STREAMLINE_FG) && defined(_WIN32)
#include "dlss_fg_constants.h"
#include <os/logger.h>
#include <cstdlib>
#include <mutex>
#include <chrono>

namespace gpu::dlss_fg {
Session::Session(Runtime& runtime, plume::VulkanDevice& device, plume::VulkanCommandQueue& queue)
    : runtime_(runtime), device_(device), queue_(queue) {}
Session::~Session() { Shutdown(); }
bool Session::Check(sl::Result result, const char* operation) {
    if (result == sl::Result::eOk) return true;
    LOG_ERROR("DLSS FG: {} result={}", operation, int(result));
    failed_ = true;
    return false;
}
bool Session::Initialize() {
    std::string reason;
    if (!runtime_.ImportFunctions(reason)) { LOG_ERROR("DLSS FG: {}", reason); return false; }
    sl::ReflexOptions reflex{};
    reflex.mode = sl::ReflexMode::eLowLatency;
    if (!Check(runtime_.ReflexSetOptions(reflex), "Reflex options")) return false;
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!depth_.Initialize(&device_)) return false;
    if (vkCreateFence(device_.vk, &info, nullptr, &completion_) != VK_SUCCESS) return false;
    options_.numFramesToGenerate = 1;
    options_.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
    ready_ = true;
    return Mode(false);
}
bool Session::Mode(bool enabled) {
    options_.mode = enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    if (!Check(runtime_.DLSSGSetOptions(viewport_, options_), "FG options")) return false;
    if (enabled != enabled_) LOG_INFO("DLSS FG: mode={} input=composited_backbuffer ui_separation=unavailable multiplier=2", enabled ? "on" : "off");
    enabled_ = enabled;
    used_ |= enabled;
    return true;
}
void Session::Mark(sl::PCLMarker marker) {
    if (token_) Check(runtime_.PCLSetMarker(marker, *token_), "PCL marker");
}
bool Session::Prepare(const std::shared_ptr<frame_generation::ProducerSnapshot>& inputs,
    uint32_t width, uint32_t height, uint32_t buffers, VkFormat format, plume::RenderCommandList* commands,
    double producerWaitMs) {
    if (!ready_) return false;
    using Clock = std::chrono::steady_clock;
    const auto begin = Clock::now();
    DrainInputs(); // Retire the previous SDK frame only when its input slot is reused.
    const auto drained = Clock::now();
    token_ = nullptr;
    ++frame_;
    if (!Check(runtime_.NewFrameToken(token_, &frame_), "frame token") || !token_) { Disable(); return false; }
    const auto sleepBegin = Clock::now();
    Check(runtime_.ReflexSleep(*token_), "Reflex sleep");
    const auto slept = Clock::now();
    // These markers cover the host presentation transaction. Full guest input
    // latency instrumentation is separate from this experimental FG path.
    Mark(sl::PCLMarker::eSimulationStart);
    Mark(sl::PCLMarker::eSimulationEnd);
    const bool reset = !inputs || !enabled_ || inputs->inputs.resetHistory ||
        previousEpoch_ != inputs->inputs.temporalEpoch || previousFrame_ + 1 != inputs->inputs.renderFrameId;
    sl::Constants constants{};
    DepthRemap remap{};
    // Renderer snapshots are already submitted on this presenting queue.
    // The present semaphore orders their copies and this depth pass before
    // Streamline reads them (DLSS-G Vulkan guide section 16). CPU completion
    // is unnecessary; retained_ still protects them through the SDK drain.
    if (failed_ || !inputs || !inputs->producerSerial || inputs->producerDiscarded ||
        inputs->producerWaitFailed || inputs->lineageCanceled ||
        !commands || !BuildConstants(inputs->inputs, reset ? nullptr : &previousVP_, constants, &remap,
            reset ? nullptr : &previousRaster_)) {
        Disable();
        return false;
    }
    const auto& in = inputs->inputs;
    retained_ = inputs; // Retain source images before recording their GPU read.
    const auto depthBegin = Clock::now();
    auto* remappedDepth = depth_.Record(commands, in.depth, remap);
    const auto depthRecorded = Clock::now();
    if (!remappedDepth) { Disable(); return false; }
    options_.numBackBuffers = buffers;
    options_.colorWidth = width; options_.colorHeight = height; options_.colorBufferFormat = format;
    options_.mvecDepthWidth = in.depth.width; options_.mvecDepthHeight = in.depth.height;
    options_.depthBufferFormat = VK_FORMAT_R32_SFLOAT;
    options_.mvecBufferFormat = VK_FORMAT_R16G16_SFLOAT;
    if (!Mode(true)) { Disable(); return false; }
    sl::Resource resources[2] = {{sl::ResourceType::eTex2d, nullptr}, {sl::ResourceType::eTex2d, nullptr}};
    const temporal::TextureRegion regions[] = {{remappedDepth, {in.depth.width, in.depth.height},
        0, 0, in.depth.width, in.depth.height}, in.motion};
    sl::ResourceTag tags[] = {{nullptr, sl::kBufferTypeDepth, sl::eValidUntilPresent},
        {nullptr, sl::kBufferTypeMotionVectors, sl::eValidUntilPresent},
        {nullptr, sl::kBufferTypeHUDLessColor, sl::eValidUntilPresent},
        {nullptr, sl::kBufferTypeUIColorAndAlpha, sl::eValidUntilPresent}};
    for (unsigned i = 0; i < 2; ++i) {
        auto& texture = *static_cast<plume::VulkanTexture*>(regions[i].texture);
        resources[i] = {sl::ResourceType::eTex2d, reinterpret_cast<void*>(texture.vk),
            reinterpret_cast<void*>(texture.allocationInfo.deviceMemory), reinterpret_cast<void*>(texture.imageView),
            uint32_t(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)};
        resources[i].width = texture.desc.width; resources[i].height = texture.desc.height;
        resources[i].nativeFormat = texture.imageFormat; resources[i].mipLevels = 1; resources[i].arrayLayers = 1;
        resources[i].usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (texture.desc.flags & plume::RenderTextureFlag::RENDER_TARGET) resources[i].usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (texture.desc.flags & plume::RenderTextureFlag::STORAGE) resources[i].usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        tags[i].resource = &resources[i];
        tags[i].extent = {regions[i].y, regions[i].x, regions[i].width, regions[i].height};
    }
    if (!Check(runtime_.SetConstants(constants, *token_, viewport_), "constants") ||
        !Check(runtime_.SetTagForFrame(*token_, viewport_, tags, 4, nullptr), "composited inputs")) {
        Disable(); return false;
    }
    previousVP_ = in.cameraViewProjection;
    previousRaster_ = in.cameraRaster;
    previousFrame_ = in.renderFrameId; previousEpoch_ = in.temporalEpoch;
    if (frame_ % 60 == 0) {
        const auto ms = [](auto elapsed) { return std::chrono::duration<double, std::milli>(elapsed).count(); };
        LOG_INFO("DLSS FG timing: frame={} producer_acquire_ms={} input_drain_ms={} reflex_sleep_ms={} depth_record_ms={} prepare_ms={} scope=cpu_wall_not_gpu_execution",
            frame_, producerWaitMs, ms(drained - begin), ms(slept - sleepBegin),
            ms(depthRecorded - depthBegin), ms(Clock::now() - begin));
    }
    return true;
}
void Session::SubmitStart() { Mark(sl::PCLMarker::eRenderSubmitStart); }
void Session::SubmitEnd() { Mark(sl::PCLMarker::eRenderSubmitEnd); }
void Session::PresentStart() { Mark(sl::PCLMarker::ePresentStart); }
void Session::SignalInputCompletion() {
    if (!completion_) return;
    // BlockPresentingClientQueue makes the first submit following Present wait
    // for SDK input processing. A checked fence establishes the ownership end.
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkResult result = vkResetFences(device_.vk, 1, &completion_);
    if (result == VK_SUCCESS) {
        std::unique_lock lock(*queue_.queue->mutex);
        result = vkQueueSubmit(queue_.queue->vk, 1, &submit, completion_);
    }
    if (result != VK_SUCCESS) {
        LOG_ERROR("DLSS FG: input completion submit failed raw_vk={}; terminating with resources retained", int(result));
        std::fflush(nullptr);
        std::_Exit(EXIT_FAILURE);
    }
    completionPending_ = true;
}
void Session::DrainInputs() {
    if (!completionPending_) return;
    const auto result = vkWaitForFences(device_.vk, 1, &completion_, VK_TRUE, 10'000'000'000ull);
    if (result != VK_SUCCESS) {
        LOG_ERROR("DLSS FG: input completion wait failed raw_vk={}; resources retained", int(result));
        std::fflush(nullptr); std::_Exit(EXIT_FAILURE);
    }
    completionPending_ = false;
    retained_.reset();
    depth_.ReleaseAfterInputDrain();
}
void Session::Presented(bool accepted) {
    if (!ready_ || !token_) return;
    Mark(sl::PCLMarker::ePresentEnd);
    sl::DLSSGState state{};
    const bool stateOk = Check(runtime_.DLSSGGetState(viewport_, state, nullptr), "present state");
    actualPresents_ += state.numFramesActuallyPresented;
    generatedIntervals_ += accepted && enabled_ && stateOk && state.status == sl::DLSSGStatus::eOk && state.numFramesActuallyPresented > 1;
    if (frame_ <= 5 || frame_ % 60 == 0 || (enabled_ && state.status != sl::DLSSGStatus::eOk))
        LOG_INFO("DLSS FG: frame={} source_frame={} enabled={} accepted={} status={} actual_presents={} generated_intervals={} total_presents={} sdk_errors={}",
            frame_, previousFrame_, enabled_, accepted, unsigned(state.status), state.numFramesActuallyPresented,
            generatedIntervals_, actualPresents_, runtime_.ErrorCount());
    if (!accepted || !stateOk) Quiesce();
    else SignalInputCompletion();
    token_ = nullptr;
    if (!accepted || !stateOk) Disable();
}
void Session::Disable() {
    if (!ready_) return;
    // A failed Prepare may already have recorded the depth conversion into
    // the host list. Keep that allocation until Presented's checked fence.
    if (enabled_) Mode(false);
    previousFrame_ = previousEpoch_ = 0;
}
void Session::Quiesce() {
    if (!ready_) return;
    if (vkDeviceWaitIdle(device_.vk) != VK_SUCCESS) {
        LOG_ERROR("DLSS FG: SDK quiesce failed; resources retained until process exit");
        std::fflush(nullptr); std::_Exit(EXIT_FAILURE);
    }
    retained_.reset();
    completionPending_ = false;
    depth_.ReleaseAfterInputDrain();
    Mode(false);
    previousFrame_ = previousEpoch_ = 0;
}
void Session::Shutdown() {
    if (!ready_) return;
    DrainInputs();
    Disable();
    // Off applies at a later Present. Teardown does not issue a fake frame:
    // drain the interposer, then let swapchain destruction and slShutdown retire
    // SDK resources while the Vulkan device is still alive.
    if (vkDeviceWaitIdle(device_.vk) != VK_SUCCESS) {
        LOG_ERROR("DLSS FG: shutdown drain failed; resources retained until process exit");
        std::fflush(nullptr); std::_Exit(EXIT_FAILURE);
    }
    vkDestroyFence(device_.vk, completion_, nullptr);
    completion_ = VK_NULL_HANDLE; ready_ = false;
    LOG_INFO("DLSS FG: session ended generated_intervals={} actual_presents={}", generatedIntervals_, actualPresents_);
}
}
#endif
