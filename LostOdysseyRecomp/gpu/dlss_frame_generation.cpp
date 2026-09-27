#include "dlss_frame_generation.h"
#if defined(LO_ENABLE_STREAMLINE_FG) && defined(_WIN32)
#include "dlss_fg_constants.h"
#include <os/logger.h>
#include <cstdlib>
#include <cstdio>
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
    if (ready_) return !failed_;
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
    // A recorded/tagged input without Presented's marker has unknown use.
    // Do not overwrite its only retained owner with the next snapshot.
    if (retained_) FailClosed("prepare before previous input completion");
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
        inputs->inputs.motionState == temporal::MotionState::ResetInitialization ||
        previousEpoch_ != inputs->inputs.temporalEpoch || previousFrame_ + 1 != inputs->inputs.renderFrameId ||
        previousProvider_ != inputs->inputs.plan.requestedUpscaler ||
        options_.colorWidth != width || options_.colorHeight != height ||
        options_.colorBufferFormat != uint32_t(format) || options_.numBackBuffers != buffers ||
        options_.mvecDepthWidth != inputs->inputs.depth.width || options_.mvecDepthHeight != inputs->inputs.depth.height;
    sl::Constants constants{};
    DepthRemap remap{};
    // Renderer snapshots are already submitted on this presenting queue.
    // The present semaphore orders their copies and this depth pass before
    // Streamline reads them (DLSS-G Vulkan guide section 16). CPU completion
    // is unnecessary; retained_ still protects them through the SDK drain.
    if (failed_ || !inputs || !inputs->producerSerial || !inputs->producerOnPresentQueue ||
        !inputs->inputsQualifiedAtCapture || !inputs->inputs.CompleteForFrameGeneration() ||
        inputs->producerDiscarded || inputs->producerWaitFailed || inputs->lineageCanceled ||
        !width || !height || !buffers || format == VK_FORMAT_UNDEFINED ||
        width != inputs->inputs.plan.output.width || height != inputs->inputs.plan.output.height ||
        !commands || !BuildConstants(inputs->inputs, reset ? nullptr : &previousVP_, constants, &remap,
            reset ? nullptr : &previousRaster_)) {
        if (frame_ % 120 == 0 && std::getenv("LO_MV_LOG"))
            LOG_INFO("DLSS FG rejected: frame={} failed={} inputs={} qualified={} motion={} input_epoch={} producer_serial={} on_queue={} discarded={} wait_failed={} canceled={} output={}x{} plan_output={}x{} buffers={} format={} commands={} reset={}",
                frame_, failed_, bool(inputs), inputs && inputs->inputsQualifiedAtCapture,
                inputs ? uint32_t(inputs->inputs.motionState) : 0, inputs ? inputs->inputs.temporalEpoch : 0,
                inputs ? inputs->producerSerial : 0, inputs && inputs->producerOnPresentQueue,
                inputs && inputs->producerDiscarded, inputs && inputs->producerWaitFailed, inputs && inputs->lineageCanceled,
                width, height, inputs ? inputs->inputs.plan.output.width : 0, inputs ? inputs->inputs.plan.output.height : 0,
                buffers, uint32_t(format), commands != nullptr, reset);
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
    previousProvider_ = in.plan.requestedUpscaler;
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
[[noreturn]] void Session::FailClosed(const char* operation, int32_t nativeResult) {
    LOG_ERROR("DLSS FG: {} native_result={} frame={} submitted_input_serial={} completed_input_serial={} retained={}; terminating without releasing unresolved resources",
        operation, nativeResult, frame_, inputCompletion_.SubmittedSerial(),
        inputCompletion_.CompletedSerial(), bool(retained_));
    std::fflush(nullptr);
    std::_Exit(EXIT_FAILURE);
}
void Session::SignalInputCompletion() {
    if (!completion_ || !inputCompletion_.CanSubmit())
        FailClosed("missing or still-pending input completion fence");
    // BlockPresentingClientQueue makes the first submit following Present wait
    // for SDK input processing. Only a successful submit publishes a serial.
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkResult result = vkResetFences(device_.vk, 1, &completion_);
    if (result == VK_SUCCESS) {
        std::unique_lock lock(*queue_.queue->mutex);
        result = vkQueueSubmit(queue_.queue->vk, 1, &submit, completion_);
    }
    if (!inputCompletion_.Submitted(result == VK_SUCCESS))
        FailClosed("input completion submit failed", int32_t(result));
}
void Session::DrainInputs() {
    if (!inputCompletion_.Pending()) return;
    const auto serial = inputCompletion_.SubmittedSerial();
    const auto result = vkWaitForFences(device_.vk, 1, &completion_, VK_TRUE, 10'000'000'000ull);
    if (!inputCompletion_.Completed(serial, result == VK_SUCCESS))
        FailClosed("input completion wait failed", int32_t(result));
    // Neither a mode request nor device-idle can take this release path.
    retained_.reset();
    depth_.ReleaseAfterInputDrain();
    if (serial <= 5 || serial % 60 == 0)
        LOG_INFO("DLSS FG input completion: submitted_serial={} completed_serial={} pending=false retained=false evidence=checked_post_present_queue_fence",
            inputCompletion_.SubmittedSerial(), inputCompletion_.CompletedSerial());
}
void Session::Presented(bool accepted) {
    if (!ready_ || !token_) return;
    Mark(sl::PCLMarker::ePresentEnd);
    // A rejected Present provides no documented input-completion ordering.
    // Keep uncertain resources alive and stop instead of calling them drained.
    if (!accepted && (retained_ || enabled_)) FailClosed("present rejected with unresolved FG input");
    // GetState is telemetry, not the synchronization boundary. Even if it
    // fails, an accepted Present must still submit and wait its queue marker.
    if (accepted) SignalInputCompletion();
    sl::DLSSGState state{};
    const bool stateOk = Check(runtime_.DLSSGGetState(viewport_, state, nullptr), "present state");
    if (accepted && stateOk) actualPresents_ += state.numFramesActuallyPresented;
    generatedIntervals_ += accepted && enabled_ && stateOk && state.status == sl::DLSSGStatus::eOk && state.numFramesActuallyPresented > 1;
    if (frame_ <= 5 || frame_ % 60 == 0 || (enabled_ && state.status != sl::DLSSGStatus::eOk))
        LOG_INFO("DLSS FG: frame={} source_frame={} enabled={} accepted={} status={} actual_presents={} generated_intervals={} total_presents={} sdk_errors={}",
            frame_, previousFrame_, enabled_, accepted, unsigned(state.status), state.numFramesActuallyPresented,
            generatedIntervals_, actualPresents_, runtime_.ErrorCount());
    if (!accepted || !stateOk) Quiesce();
    token_ = nullptr;
    if (!accepted || !stateOk) Disable();
}
void Session::Disable() {
    if (!ready_) return;
    // A failed Prepare may already have recorded the depth conversion into
    // the host list. Keep that allocation until Presented's checked fence.
    if (enabled_ && !Mode(false)) FailClosed("disable options failed");
    previousFrame_ = previousEpoch_ = 0;
    previousProvider_ = upscaling::Upscaler::Off;
}
void Session::Quiesce() {
    if (!ready_) return;
    DrainInputs();
    // The caller must cancel an unsubmitted host list before discarding it.
    // This session has no such cancellation proof; never infer one from idle.
    if (retained_) FailClosed("quiesce without post-present input completion");
    Disable();
    const auto result = vkDeviceWaitIdle(device_.vk);
    if (result != VK_SUCCESS) FailClosed("SDK quiesce failed", int32_t(result));
    token_ = nullptr;
    LOG_INFO("DLSS FG quiesce: submitted_input_serial={} completed_input_serial={} pending=false retained=false",
        inputCompletion_.SubmittedSerial(), inputCompletion_.CompletedSerial());
}
void Session::Shutdown() {
    if (!ready_) return;
    // Off applies at a later Present. Do not issue a fake frame. First retire
    // inputs by their checked marker, then drain before SDK/device teardown.
    Quiesce();
    vkDestroyFence(device_.vk, completion_, nullptr);
    completion_ = VK_NULL_HANDLE; ready_ = false;
    LOG_INFO("DLSS FG: session ended generated_intervals={} actual_presents={} submitted_input_serial={} completed_input_serial={} cleanup=complete",
        generatedIntervals_, actualPresents_, inputCompletion_.SubmittedSerial(), inputCompletion_.CompletedSerial());
}
}
#endif
