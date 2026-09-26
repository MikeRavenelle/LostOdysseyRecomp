#pragma once

#include "frame_generation_present.h"

#include <plume_render_interface.h>

#include <array>
#include <cstddef>
#include <memory>

namespace gpu::frame_generation {

// A producer command batch records five independent copies. The pixels become
// stable only when that batch completes; the caller retains this packet through
// its submission fence and sets producerSerial/producerCompleted accordingly.
// This is an input capture, not proof of a HUD-less image or provider readiness.
struct ProducerSnapshot {
    temporal::TemporalFrameInputs inputs{};
    TextureLease sourceColor{}, depth{}, motion{}, motionInvalidity{};
    TextureLease sceneColorCandidate{};
    uint64_t producerSerial = 0;
    bool producerCompleted = false;
    bool producerDiscarded = false;
    UiSeparation ui = UiSeparation::Unavailable;
};

inline bool FullAllocation(const temporal::TextureRegion& region) {
    return region.Complete() && region.x == 0 && region.y == 0 &&
        region.width == region.allocation.width &&
        region.height == region.allocation.height;
}

inline std::shared_ptr<ProducerSnapshot> RecordProducerSnapshot(
    plume::RenderDevice* device, plume::RenderCommandList* commands,
    const temporal::TemporalFrameInputs& inputs,
    plume::RenderFormat sourceColorFormat, plume::RenderTexture* sceneCandidate,
    plume::RenderFormat sceneFormat, resolution::Size sceneExtent) {
    if (!device || !commands || !inputs.currentInputsComplete ||
        !inputs.CompleteForConsumer() ||
        !temporal::KnownDepthConvention(inputs.depthConvention) ||
        inputs.motionState == temporal::MotionState::Unavailable ||
        !FullAllocation(inputs.color) || !FullAllocation(inputs.depth) ||
        !FullAllocation(inputs.motion) || !FullAllocation(inputs.motionInvalidity) ||
        sourceColorFormat == plume::RenderFormat::UNKNOWN ||
        sceneFormat == plume::RenderFormat::UNKNOWN ||
        !sceneCandidate || !sceneExtent.width || !sceneExtent.height)
        return {};

    const auto sourceExtent = inputs.color.allocation;
    const auto sameSourceExtent = [&](const temporal::TextureRegion& region) {
        return region.allocation.width == sourceExtent.width &&
            region.allocation.height == sourceExtent.height;
    };
    if (!sameSourceExtent(inputs.depth) || !sameSourceExtent(inputs.motion) ||
        !sameSourceExtent(inputs.motionInvalidity))
        return {};

    const std::array<plume::RenderTexture*, 5> originals{
        inputs.color.texture, inputs.depth.texture, inputs.motion.texture,
        inputs.motionInvalidity.texture, sceneCandidate};
    const std::array<plume::RenderFormat, 5> formats{
        sourceColorFormat, plume::RenderFormat::R32_FLOAT,
        plume::RenderFormat::R16G16_FLOAT, plume::RenderFormat::R8_UNORM,
        sceneFormat};
    const std::array<resolution::Size, 5> sizes{
        sourceExtent, sourceExtent, sourceExtent, sourceExtent, sceneExtent};

    // Complete allocation before touching the command list. A failed image
    // allocation leaves no partially recorded capture in the producer batch.
    std::array<std::shared_ptr<plume::RenderTexture>, 5> copies;
    for (size_t i = 0; i < copies.size(); ++i) {
        auto image = device->createTexture(plume::RenderTextureDesc::Texture2D(
            sizes[i].width, sizes[i].height, 1, formats[i]));
        if (!image) return {};
        copies[i] = std::move(image);
    }
    auto snapshot = std::make_shared<ProducerSnapshot>();
    snapshot->inputs = inputs;
    auto lease = [&](size_t i) {
        TextureLease value;
        value.region = {copies[i].get(), sizes[i], 0, 0, sizes[i].width, sizes[i].height};
        value.lifetime = copies[i];
        return value;
    };
    snapshot->sourceColor = lease(0);
    snapshot->depth = lease(1);
    snapshot->motion = lease(2);
    snapshot->motionInvalidity = lease(3);
    snapshot->sceneColorCandidate = lease(4);
    snapshot->inputs.color = snapshot->sourceColor.region;
    snapshot->inputs.depth = snapshot->depth.region;
    snapshot->inputs.motion = snapshot->motion.region;
    snapshot->inputs.motionInvalidity = snapshot->motionInvalidity.region;
    // These optional views have no owned copies; their borrowed pointers and
    // provenance must not escape the producer slot through this snapshot.
    snapshot->inputs.materialInstability = {};
    snapshot->inputs.fsrMask = {};

    for (size_t i = 0; i < copies.size(); ++i) {
        commands->barriers(plume::RenderBarrierStage::COPY,
            plume::RenderTextureBarrier(originals[i], plume::RenderTextureLayout::COPY_SOURCE));
        commands->barriers(plume::RenderBarrierStage::COPY,
            plume::RenderTextureBarrier(copies[i].get(), plume::RenderTextureLayout::COPY_DEST));
        commands->copyTextureRegion(
            plume::RenderTextureCopyLocation::Subresource(copies[i].get()),
            plume::RenderTextureCopyLocation::Subresource(originals[i]));
        commands->barriers(plume::RenderBarrierStage::ALL,
            plume::RenderTextureBarrier(originals[i], plume::RenderTextureLayout::SHADER_READ));
        commands->barriers(plume::RenderBarrierStage::ALL,
            plume::RenderTextureBarrier(copies[i].get(), plume::RenderTextureLayout::SHADER_READ));
    }
    return snapshot;
}

} // namespace gpu::frame_generation
