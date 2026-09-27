#pragma once

#include "temporal_frame_inputs.h"
#include <sl_consts.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>

namespace gpu::dlss_fg {

// The scene uses a reversed-depth projection with a positive depth pole and no
// finite far plane. FG receives an equivalent, finite projection and this
// affine remap of the sampled R32 depth. This is a projection change, not an
// assertion that the original guest camera had this far clip.
struct DepthRemap {
    float scale = 0, bias = 0;
    float nearDistance = 0, farDistance = 0;
};

namespace detail {
using Vec3 = std::array<double, 3>;
inline double Dot(const Vec3& a, const Vec3& b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
}
inline Vec3 Sub(const Vec3& a, const Vec3& b, double scale) {
    return {a[0]-b[0]*scale, a[1]-b[1]*scale, a[2]-b[2]*scale};
}
inline bool Near(const Vec3& a, const Vec3& b, double tolerance) {
    for (unsigned i=0; i<3; ++i) if (std::abs(a[i]-b[i]) > tolerance) return false;
    return true;
}
inline temporal::Matrix Multiply(const temporal::Matrix& a, const temporal::Matrix& b) {
    temporal::Matrix out{};
    for (unsigned i=0; i<4; ++i)
        for (unsigned j=0; j<4; ++j)
            for (unsigned k=0; k<4; ++k) out[4*i+j] += a[4*i+k]*b[4*k+j];
    return out;
}
inline bool ToSl(const temporal::Matrix& source, sl::float4x4& target) {
    for (unsigned i=0; i<4; ++i) {
        float value[4]{};
        for (unsigned j=0; j<4; ++j) {
            value[j] = float(source[4*i+j]);
            if (!std::isfinite(value[j])) return false;
        }
        target.setRow(i, {value[0],value[1],value[2],value[3]});
    }
    return true;
}
struct Camera {
    temporal::Matrix projection{}, hostVP{};
    Vec3 position{}, right{}, up{}, forward{};
    double nearDistance=0, farDistance=0, fov=0, aspect=0, scale=0, bias=0;
};
inline std::optional<Camera> Decompose(const temporal::Matrix& m, const temporal::Viewport& raster) {
    if (!temporal::Finite(m) || raster.x != 0 || raster.y != 0 ||
        !std::isfinite(raster.width) || !std::isfinite(raster.height) ||
        raster.width < 1 || raster.height < 1 ||
        (raster.ndcYSign != 1 && raster.ndcYSign != -1) ||
        !std::isfinite(raster.halfPixelNdcX) || !std::isfinite(raster.halfPixelNdcY)) return {};
    const Vec3 x{m[0],m[4],m[8]}, y{m[1],m[5],m[9]}, z{m[2],m[6],m[10]}, w{m[3],m[7],m[11]};
    const double s2=Dot(w,w), s=std::sqrt(s2);
    if (!(s > 1e-8)) return {};
    const Vec3 f{w[0]/s,w[1]/s,w[2]/s};
    const double a=Dot(z,w)/s2, ox=Dot(x,w)/s2, oy=Dot(y,w)/s2;
    const double q=-(m[14]-a*m[15])/s, pole=1-a;
    const Vec3 rx=Sub({x[0]/s,x[1]/s,x[2]/s},f,ox);
    const double fx=std::sqrt(Dot(rx,rx));
    if (!(a>0) || !(q>0) || !(fx>0) || !std::isfinite(q) ||
        std::abs(ox)>1e-3 || std::abs(oy)>1e-3 ||
        !Near({z[0]/s,z[1]/s,z[2]/s},{a*f[0],a*f[1],a*f[2]},1e-4)) return {};
    const Vec3 r{rx[0]/fx,rx[1]/fx,rx[2]/fx}, u=Cross(f,r);
    const Vec3 ys{y[0]/s,y[1]/s,y[2]/s};
    const double fy=Dot(ys,u), aspect=std::abs(fy)/fx;
    if (!(std::abs(fy)>1e-8) ||
        !Near(ys,{fy*u[0]+oy*f[0],fy*u[1]+oy*f[1],fy*u[2]+oy*f[2]},1e-4) ||
        std::abs(aspect-raster.width/raster.height)>1e-3*aspect) return {};
    const double nearPlane=q/a, farPlane=nearPlane*1'000'000.0;
    if (!(nearPlane>0) || !std::isfinite(farPlane) || farPlane>=double(sl::INVALID_FLOAT)) return {};
    const double tx=(m[12]/s-ox*m[15]/s)/fx;
    const double ty=(m[13]/s-oy*m[15]/s)/fy;
    const double tz=m[15]/s;
    Camera out{};
    out.position={-(tx*r[0]+ty*u[0]+tz*f[0]),-(tx*r[1]+ty*u[1]+tz*f[1]),-(tx*r[2]+ty*u[2]+tz*f[2])};
    out.right=r; out.up=u; out.forward=f;
    out.nearDistance=nearPlane; out.farDistance=farPlane;
    out.fov=2*std::atan(1/std::abs(fy)); out.aspect=aspect;
    const double finitePole=-nearPlane/(farPlane-nearPlane), finiteQ=nearPlane*farPlane/(farPlane-nearPlane);
    out.scale=finiteQ/q; out.bias=finitePole-out.scale*pole;
    out.projection={fx,0,0,0, 0,fy*raster.ndcYSign,0,0,
        ox+raster.halfPixelNdcX,oy*raster.ndcYSign+raster.halfPixelNdcY,finitePole,1,
        0,0,finiteQ,0};
    const temporal::Matrix view={r[0],u[0],f[0],0, r[1],u[1],f[1],0,
        r[2],u[2],f[2],0, tx,ty,tz,1};
    out.hostVP=Multiply(view,out.projection);
    if (!temporal::Finite(out.hostVP) || !temporal::Inverse(out.hostVP) ||
        !std::isfinite(out.scale) || !std::isfinite(out.bias)) return {};
    return out;
}
} // namespace detail

inline bool BuildConstants(const temporal::TemporalFrameInputs& inputs,
    const temporal::Matrix* previousVP, sl::Constants& result, DepthRemap* remap = nullptr,
    const temporal::Viewport* previousRaster = nullptr) {
    if (!inputs.cameraValid || inputs.depthConvention != temporal::DepthConvention::Reversed ||
        !inputs.depth.Complete() || !inputs.motion.Complete() ||
        inputs.depth.x || inputs.depth.y || inputs.motion.x || inputs.motion.y ||
        inputs.depth.width != inputs.motion.width || inputs.depth.height != inputs.motion.height ||
        inputs.cameraRaster.width != inputs.depth.width || inputs.cameraRaster.height != inputs.depth.height ||
        !std::isfinite(inputs.jitter.pixelX) || !std::isfinite(inputs.jitter.pixelY)) return false;
    auto current=detail::Decompose(inputs.cameraViewProjection,inputs.cameraRaster);
    if (!current) return false;
    auto previous=previousVP ? detail::Decompose(*previousVP,previousRaster ? *previousRaster : inputs.cameraRaster) : current;
    if (!previous) return false;
    const auto inverseProjection=temporal::Inverse(current->projection);
    const auto inverseCurrent=temporal::Inverse(current->hostVP);
    if (!inverseProjection || !inverseCurrent) return false;
    const auto currentToPrevious=detail::Multiply(*inverseCurrent,previous->hostVP);
    const auto previousToCurrent=temporal::Inverse(currentToPrevious);
    if (!previousToCurrent ||
        !detail::ToSl(current->projection,result.cameraViewToClip) ||
        !detail::ToSl(*inverseProjection,result.clipToCameraView) ||
        !detail::ToSl(currentToPrevious,result.clipToPrevClip) ||
        !detail::ToSl(*previousToCurrent,result.prevClipToClip)) return false;
    const auto finiteFloat=[](double value) {return std::isfinite(float(value)) && float(value)!=sl::INVALID_FLOAT;};
    for (double value : {current->nearDistance,current->farDistance,current->fov,current->aspect,current->scale,current->bias,
            current->position[0],current->position[1],current->position[2]}) if (!finiteFloat(value)) return false;
    result.cameraPos={float(current->position[0]),float(current->position[1]),float(current->position[2])};
    result.cameraRight={float(current->right[0]),float(current->right[1]),float(current->right[2])};
    result.cameraUp={float(current->up[0]),float(current->up[1]),float(current->up[2])};
    result.cameraFwd={float(current->forward[0]),float(current->forward[1]),float(current->forward[2])};
    result.cameraNear=float(current->nearDistance); result.cameraFar=float(current->farDistance);
    result.cameraFOV=float(current->fov); result.cameraAspectRatio=float(current->aspect);
    result.jitterOffset={float(inputs.jitter.pixelX),float(inputs.jitter.pixelY)};
    result.mvecScale={1.0f/inputs.motion.width,1.0f/inputs.motion.height};
    result.cameraPinholeOffset={0,0};
    result.depthInverted=sl::Boolean::eTrue;
    result.cameraMotionIncluded=sl::Boolean::eTrue;
    result.motionVectors3D=sl::Boolean::eFalse;
    result.motionVectorsJittered=sl::Boolean::eFalse;
    result.motionVectorsDilated=sl::Boolean::eFalse;
    result.orthographicProjection=sl::Boolean::eFalse;
    result.reset=previousVP ? sl::Boolean::eFalse : sl::Boolean::eTrue;
    if (remap) *remap={float(current->scale),float(current->bias),float(current->nearDistance),float(current->farDistance)};
    return true;
}
} // namespace gpu::dlss_fg
