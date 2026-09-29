# Optimization plan

Project-wide list, not only macOS. Ranked by expected gain against risk. Every item
is measured before and after (`LO_FRAME_TIMING=1`, the same scene, the same
resolution); a change that does not show up in the numbers is reverted, as the
depth-borrowing experiment on Metal was.

## Baseline to capture first

- One fixed benchmark per scene type: attract (title), menu open, Uhra city
  (`tools/drive_city.py`), a battle. Report CPU frame time, GPU frame time, draws,
  render passes (Metal) or barriers (D3D12/Vulkan), FPS min/avg.
- Same set on each platform so a change can be judged everywhere at once.
- Current macOS numbers (M1 Pro, 720p): attract 30.0 FPS locked, about 10.4 ms GPU
  per frame, 82 render + 18 blit passes per frame.

## Measurements (2026-09-29, M1 Pro, macOS, Numara city)

- **Above 30 FPS the limit is the GPU command-processing thread**, not game logic:
  a 10 s `sample` at the 120 FPS target shows the game's main thread sleeping in
  `KeDelayExecutionThread` 72% of the time while `GPU CmdProc` never idles. Its hot
  spots: per-draw register snapshots (`ReadRegisters` copies all 2,048 ALU constants,
  8 KB, for each of ~3,300 draws per frame), vertex-content change checks (memcmp,
  memmove, `VertexSampledContent::Matches`, `SampleHash`), packet decode, and
  (only with `LO_RENDER_TIMING`) timing calls (~9%).
- **Register promotion (below) is not worth it here:** the safe subset
  (`cr/xer/reserved_as_local`, `skip_msr`) gave 53.4 → 54.7 FPS, within noise;
  `non_argument_as_local` crashes at startup (guest access at 0xFFFFFFC4). Reverted.
- **Camera TAA is GPU-bound** (13 FPS; 52 ms of each 75 ms frame waiting for a
  drawable). Metal System Trace: ~1,080 render passes per frame with TAA vs ~220
  without; vertex/tiling work 700 vs 270 ms per second. The motion-vector replay
  draws into its own target between scene draws, splitting the scene pass at almost
  every draw. Fix: batch the replay draws (e.g. after the scene) instead of
  interleaving them; changes TAA timing semantics, so it needs care.
- **Without TAA at 30 FPS** the GPU is about half busy (~220 passes per frame).

## Guest CPU (all platforms)

1. **XenonRecomp local-variable options.** *Measured 2026-09-29: ~2%, reverted (see
   Measurements).* `LostOdysseyRecompLib/config/LostOdysseyRecomp.toml`
   has every register-promotion option off (`ctr_as_local`, `xer_as_local`,
   `reserved_as_local`, `cr_as_local`, `non_argument_as_local`,
   `non_volatile_as_local`, `skip_lr`, `skip_msr`). UnleashedRecomp turns most of
   them on: the compiler then keeps those registers in host registers instead of
   loading and storing the context structure around every instruction. Expected to
   be the single largest CPU win. Risk: needs a playthrough-level test for each
   option (setjmp/longjmp and exception paths), so it depends on automated testing.
   Enable one option at a time.
2. **PGO and ThinLTO in release builds.** `cmake/LoOptimization.cmake` already
   supports `LO_LTO_MODE=thin` and `LO_PGO_MODE=generate/use`; release CI uses
   neither. Profile from the automated scenarios, then build with the profile.
3. **Hot guest function overrides.** Profile (Instruments on macOS, perf on Linux,
   the existing `LO_PROFILE_DIAGNOSTIC` build) and replace the top guest routines
   (memcpy/memset, string, math, decompression) with native host implementations,
   as the runtime already does for some kernel calls.
4. **Vector code on AArch64.** simde maps VMX to NEON; check the hot VMX ops in the
   generated code for scalar fallbacks (permute, pack/unpack, `vpkd3d128`,
   dot products) and add direct NEON paths where simde falls back.

## Renderer CPU (all platforms, the real limit above 30 FPS)

1. Upload ALU constants only when they changed since the previous draw instead of
   snapshotting all 2,048 registers per draw.
2. Cheaper vertex-content change detection (the memcmp/hash path).

## GPU, all backends

1. **Rect lists without geometry shaders.** Vertex-stage expansion (six vertices
   per rect, fourth corner computed in the vertex shader). Required for correct
   Metal output; also removes the geometry shader stage on D3D12/Vulkan, where GS is
   slow on many GPUs.
2. **Framebuffer switches.** 68% of Metal pass breaks come from DrawImpl switching
   between the same color target with and without depth. Keeping depth attached
   when the guest leaves it bound but disabled would merge those passes. On
   desktop GPUs this reduces render-target changes; on Apple GPUs it avoids a
   store/load of every attachment.
3. **Barrier batching.** 31% of pass breaks come from barriers; batch resolves and
   copies at the end of a pass instead of between draws.
4. **Menu copy cost.** Menus copy full-screen targets every frame (identified in the
   worklog, not changed). Skip the copy when the source did not change.
5. **Shader and pipeline warm-up.** Keep the pipeline-recipe prebuild; extend the
   portable shader pack to macOS (pre-translated MSL or Metal binary archives) so a
   first launch does not translate about 28,000 shaders.

## Metal specific

- Binary archives (`MTLBinaryArchive`) for pipelines, to remove first-use stutter.
- Memoryless or `DontCare` load/store actions on transient targets (depth that is
  never read back).
- Argument buffer reuse across draws with identical bindings.
- MetalFX spatial/temporal upscaling behind the existing upscaler interface, once
  rendering is correct.

## Frame pacing and audio (all platforms)

- Keep the precise-sleep approach (`os/host_scheduling`) and check Windows/Linux
  pacing with the same oversleep measurement.
- Audio buffer size adaptive to frame-time spikes, so a slow frame does not
  underrun.

## Memory

- Metal shader modules are built lazily; also evict unused translated shaders
  (about 850 MB at the title screen before lazy modules).
- Watch peak memory during long play on 16 GB machines.

## Build and CI

- Separate the PPC library build (slow, rarely changes) from the runtime in CI with
  caching, so macOS and Linux jobs stay fast.
- ccache/sccache for local and CI builds.
