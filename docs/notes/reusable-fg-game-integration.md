# Reusable FG game integration

Development branch: `feature/reusable-fg-fsr-d3d12-mfg`.

## Code scope

Windows D3D12 game presentation selects one reusable provider: standalone FSR FG, DLSS FG, or capability-gated DLSS dynamic MFG. Native device/queue rendering is retained; creation hooks install the selected SDK's swapchain. The existing Windows Vulkan fixed-2x DLSS FG path remains separate and its FSR SR + DLSS FG maintainer acceptance is preserved.

`video.cpp` now forwards submission/present, unsubmitted cancellation, resize/window quiesce and owner-thread shutdown to the selected D3D12 bridge. `renderer.cpp` exports same-frame depth/motion snapshots for SR and native-resolution FG, retains submission ownership, and validates the actual final resolve identity before presentation. Input and SDK completion remain separate from generation/display telemetry. Unknown GPU completion remains a fatal safe-stop rather than an early resource release.

SR Off + FG On captures the depth/motion required by interpolation without invoking either SR SDK, selecting a fake SR consumer, enabling jitter, or changing the guest frame rate. SR Off + FG Off does not enable this path. Final composited color is used; HUD/UI separation is still outside this scope.

## Build

Use the existing full-game build process, additionally enabling one or both options:

- `LO_ENABLE_D3D12_DLSS_FG=ON` and `LO_STREAMLINE_SDK_ROOT` pointing at the official Streamline 2.14.1 SDK.
- `LO_ENABLE_FSR_FG=ON`, `LO_FSR_SDK_ROOT` pointing at FidelityFX 1.1.4, and `LO_FSR_FG_RUNTIME` pointing at its official `amd_fidelityfx_dx12.dll`.

The new FSR FG adapter does not require native DLSS SDK/SR or FSR SR to be enabled. Build options remain off by default. The generic adapter target is C++20, including in a C++23 parent project, to avoid the pinned Streamline header's C++23 alias issue.

## Run

Select D3D12 in the game's graphics settings and launch from PowerShell with one of these settings:

```powershell
# Independent AMD FSR frame generation, fixed 2x.
$env:LO_FG_PROVIDER='fsr'
$env:LO_FG_MODE='fixed'
$env:LO_FG_MULTIPLIER='2'
# Optional override for the packaged runtime DLL:
# $env:LO_FSR_FG_RUNTIME='C:\path\amd_fidelityfx_dx12.dll'
```

```powershell
# NVIDIA DLSS frame generation, fixed multiplier.
$env:LO_FG_PROVIDER='dlss'
$env:LO_FG_MODE='fixed'
$env:LO_FG_MULTIPLIER='2'
```

```powershell
# NVIDIA SDK-native dynamic MFG; actual support is queried at runtime.
$env:LO_FG_PROVIDER='dlss'
$env:LO_FG_MODE='dynamic'
$env:LO_FG_TARGET_FPS='144' # 0 requests SDK display-refresh detection.
```

`LO_FG_PROVIDER=off` overrides legacy `LO_DLSS_FG=1`. Unsupported provider/mode combinations are reported, never silently replaced with another algorithm. Dynamic MFG is not claimed for FSR or Vulkan by this integration.

## Validation boundary

The pre-integration reusable adapter CI passed on Windows and Linux. For this game wiring, local Linux renderer/video compilation, 19 game-input policy checks, 55 reusable core checks, and existing camera-constants math passed. The new workflow compiles the actual Windows renderer/video translation units and links the bridge with both adapters; its result must be read from the matching commit's CI, not inferred from earlier checks. This is not a full generated-guest executable build. No game, GPU runtime, image-quality, pacing, resize or full-playthrough acceptance has been performed for the new D3D12 paths; the maintainer will perform those checks.
