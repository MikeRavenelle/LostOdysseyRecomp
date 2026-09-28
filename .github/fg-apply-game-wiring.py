from pathlib import Path
p=Path('.')
source=(p/'LostOdysseyRecomp/gpu/dlss_fg_constants.h').read_text()
start=source.index('// The scene uses'); end=source.index('inline bool BuildConstants')
common=source[start:end]
a=common.index('inline bool ToSl('); b=common.index('struct Camera {',a)
common=common[:a]+common[b:]
common=common.replace('sl::INVALID_FLOAT','std::numeric_limits<float>::max()')
body=source[end:source.rindex('} // namespace gpu::dlss_fg')]
body=body.replace('BuildConstants(', 'BuildCamera(').replace('sl::Constants& result','framegen::Camera& result')
for a,b in [('current->projection,result.cameraViewToClip','current->projection,result.viewToClip'),('*inverseProjection,result.clipToCameraView','*inverseProjection,result.clipToView'),('currentToPrevious,result.clipToPrevClip','currentToPrevious,result.clipToPrevious'),('*previousToCurrent,result.prevClipToClip','*previousToCurrent,result.previousToClip')]:
 body=body.replace('!detail::ToSl('+a+')','!ToFloatMatrix('+b+')')
body=body.replace('sl::INVALID_FLOAT','std::numeric_limits<float>::max()')
for a,b in [('cameraPos','position'),('cameraRight','right'),('cameraUp','up'),('cameraFwd','forward'),('cameraNear','nearPlane'),('cameraFar','farPlane'),('cameraFOV','fovRadians'),('cameraAspectRatio','aspect')]: body=body.replace('result.'+a,'result.'+b)
a=body.index('    result.jitterOffset='); b=body.index('    if (remap)',a)
body=body[:a]+'''    result.jitterX=float(inputs.jitter.pixelX); result.jitterY=float(inputs.jitter.pixelY);
    result.depthReversed=true;
'''+body[b:]
prefix='''#pragma once
// Game camera extraction shared by every FG provider, without any SDK headers.
#include "temporal_frame_inputs.h"
#include "../../shared/frame_generation/camera.h"
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace gpu::frame_generation {
'''
floatmath='''inline bool ToFloatMatrix(const temporal::Matrix& source, framegen::Matrix& target) {
    for (unsigned i=0; i<16; ++i) {
        target[i]=float(source[i]);
        if (!std::isfinite(target[i])) return false;
    }
    return true;
}
'''
(p/'LostOdysseyRecomp/gpu/frame_generation_camera.h').write_text(prefix+common+floatmath+body+'} // namespace gpu::frame_generation\n')
(p/'LostOdysseyRecomp/gpu/dlss_fg_constants.h').write_text('''#pragma once
#include "frame_generation_camera.h"
#include <sl_consts.h>

namespace gpu::dlss_fg {
using frame_generation::DepthRemap;
namespace detail = frame_generation::detail;
inline bool BuildConstants(const temporal::TemporalFrameInputs& inputs,
    const temporal::Matrix* previousVP, sl::Constants& result, DepthRemap* remap = nullptr,
    const temporal::Viewport* previousRaster = nullptr) {
    framegen::Camera camera;
    if (!frame_generation::BuildCamera(inputs,previousVP,camera,remap,previousRaster)) return false;
    const auto matrix=[](const framegen::Matrix& from,sl::float4x4& to) {
        for (unsigned r=0;r<4;++r) to.setRow(r,{from[4*r],from[4*r+1],from[4*r+2],from[4*r+3]});
    };
    matrix(camera.viewToClip,result.cameraViewToClip); matrix(camera.clipToView,result.clipToCameraView);
    matrix(camera.clipToPrevious,result.clipToPrevClip); matrix(camera.previousToClip,result.prevClipToClip);
    result.cameraPos={camera.position[0],camera.position[1],camera.position[2]};
    result.cameraRight={camera.right[0],camera.right[1],camera.right[2]};
    result.cameraUp={camera.up[0],camera.up[1],camera.up[2]};
    result.cameraFwd={camera.forward[0],camera.forward[1],camera.forward[2]};
    result.cameraNear=camera.nearPlane; result.cameraFar=camera.farPlane;
    result.cameraFOV=camera.fovRadians; result.cameraAspectRatio=camera.aspect;
    result.jitterOffset={camera.jitterX,camera.jitterY};
    result.mvecScale={1.0f/inputs.motion.width,1.0f/inputs.motion.height};
    result.cameraPinholeOffset={0,0};
    result.depthInverted=sl::eTrue; result.cameraMotionIncluded=sl::eTrue;
    result.motionVectors3D=sl::eFalse; result.motionVectorsJittered=sl::eFalse;
    result.motionVectorsDilated=sl::eFalse; result.orthographicProjection=sl::eFalse;
    result.reset=previousVP ? sl::eFalse : sl::eTrue;
    return true;
}
} // namespace gpu::dlss_fg
''')
h=p/'LostOdysseyRecomp/gpu/dlss_fg_depth.h'; s=h.read_text()
s=s.replace('defined(LO_ENABLE_STREAMLINE_FG) && defined(_WIN32)','defined(_WIN32) && (defined(LO_ENABLE_STREAMLINE_FG) || defined(LO_ENABLE_D3D12_FG))')
s=s.replace('#include "dlss_fg_constants.h"','#include "frame_generation_camera.h"')
s=s.replace('namespace gpu::dlss_fg {','namespace gpu::dlss_fg {\nusing frame_generation::DepthRemap;'); h.write_text(s)
f=p/'LostOdysseyRecomp/gpu/dlss_fg_depth.cpp'; s=f.read_text()
s=s.replace('defined(LO_ENABLE_STREAMLINE_FG) && defined(_WIN32)','defined(_WIN32) && (defined(LO_ENABLE_STREAMLINE_FG) || defined(LO_ENABLE_D3D12_FG))')
s=s.replace('#include <cmath>','#include <cmath>\n#include <plume_d3d12.h>').replace('register(b0)', 'register(b1)')
s=s.replace('    if (!device || device->getCapabilities().shaderFormat != plume::RenderShaderFormat::SPIRV) return false;', '''    if (!device) return false;
    const auto format=device->getCapabilities().shaderFormat;
    if (format != plume::RenderShaderFormat::SPIRV && format != plume::RenderShaderFormat::DXIL) return false;
    const auto binary=format==plume::RenderShaderFormat::SPIRV ? xenos::ShaderBinaryFormat::Spirv : xenos::ShaderBinaryFormat::Dxil;''')
s=s.replace('"vs_6_0",xenos::ShaderBinaryFormat::Spirv', '"vs_6_0",binary').replace('"ps_6_0",xenos::ShaderBinaryFormat::Spirv', '"ps_6_0",binary')
s=s.replace('"vertex",plume::RenderShaderFormat::SPIRV','"vertex",format').replace('"pixel",plume::RenderShaderFormat::SPIRV','"pixel",format')
a=s.index('    const auto& image='); b=s.index('    std::unique_ptr<Active> next;',a)
s=s[:a]+'''    const bool vulkan=device_->getCapabilities().shaderFormat==plume::RenderShaderFormat::SPIRV;
    const plume::RenderTextureDesc* desc=nullptr;
    if (vulkan) {
        const auto& image=*static_cast<const plume::VulkanTexture*>(source.texture);
        if (image.device != device_ || !image.vk || !image.imageView || !image.allocation ||
            image.imageFormat != VK_FORMAT_R32_SFLOAT || image.textureLayout != plume::RenderTextureLayout::SHADER_READ) return nullptr;
        desc=&image.desc;
    } else {
        const auto& image=*static_cast<const plume::D3D12Texture*>(source.texture);
        if (image.device != device_ || !image.d3d ||
            !(image.resourceStates & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) return nullptr;
        desc=&image.desc;
    }
    if (desc->dimension != plume::RenderTextureDimension::TEXTURE_2D ||
        desc->format != plume::RenderFormat::R32_FLOAT ||
        desc->width != source.allocation.width || desc->height != source.allocation.height ||
        desc->mipLevels != 1 || desc->arraySize != 1) return nullptr;
'''+s[b:]
s=s.replace('UploadBuffer(sizeof(Constants),','UploadBuffer(256,').replace('next->constants.get(),sizeof(Constants)','next->constants.get(),256'); f.write_text(s)
