#pragma once

#if defined(LO_ENABLE_STREAMLINE_FG) && defined(_WIN32)
#include "dlss_fg_constants.h"
#include <plume_vulkan.h>
#include <memory>

namespace gpu::dlss_fg {

// One recorded depth image remains owned by this object until the caller's
// checked post-Present input-completion fence. No image is recycled earlier.
class DepthRemapper {
public:
    bool Initialize(plume::RenderDevice* device);
    plume::RenderTexture* Record(plume::RenderCommandList* commands,
        const temporal::TextureRegion& source, const DepthRemap& mapping);
    void ReleaseAfterInputDrain();
    void DiscardUnsubmitted() { active_.reset(); }
private:
    struct Active {
        uint32_t width = 0, height = 0;
        std::unique_ptr<plume::RenderTexture> output;
        std::unique_ptr<plume::RenderBuffer> constants;
        std::unique_ptr<plume::RenderDescriptorSet> set;
        std::unique_ptr<plume::RenderFramebuffer> framebuffer;
    };
    plume::RenderDevice* device_ = nullptr;
    std::unique_ptr<plume::RenderPipelineLayout> layout_;
    std::unique_ptr<plume::RenderShader> vertex_, pixel_;
    std::unique_ptr<plume::RenderPipeline> pipeline_;
    std::unique_ptr<Active> active_;
    // Only ReleaseAfterInputDrain can move an in-flight batch here.
    std::unique_ptr<Active> available_;
};
} // namespace gpu::dlss_fg
#endif
