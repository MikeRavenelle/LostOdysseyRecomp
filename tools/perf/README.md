# City performance capture

`drive-city.ps1` is an **active game driver**, not a read-only analyzer. It
launches the supplied Windows build, sends automated menu and movement inputs,
requests screenshots, writes logs and a summary, then stops the process it
started. The game can also change its save files. Provide an isolated build,
output directory, and the player's save directory explicitly. The script
compares save metadata before and after but does not restore saves.

```powershell
pwsh -File tools/perf/drive-city.ps1 -Help
pwsh -File tools/perf/drive-city.ps1 `
  -RunDirectory 'C:\path\to\isolated-game-build' `
  -OutputDirectory 'C:\path\to\capture-results' `
  -PlayerSaveDirectory 'C:\path\to\player-save'
```

The driver stores screenshots under `<RunDirectory>/shots2`, `pid.txt` under
`<RunDirectory>`, logs and input controls under the system temporary directory,
and `drive-summary.json` / `drive-status.json` under `<OutputDirectory>`. If
available it runs the adjacent `classify-city-timing.ps1` on the resulting log.

`analyze-city-comparison.py` only reads saved summary and log files; its JSON
results go to stdout. Relative `log` and `retained_log` fields resolve next to
the summary file, independent of the shell's working directory:

```sh
python tools/perf/analyze-city-comparison.py /path/to/run-a/drive-summary.json /path/to/run-b/drive-summary.json
```

## DLSS FG game capture

`run-fg-game.ps1` is the bounded game driver for experimental Windows
DLSS/FSR and Streamline FG paths. Use `-Backend D3D12|Vulkan` and
`-Upscaler Dlss|Fsr|Off` to override the isolated run configuration; `-Quality`
accepts values `0..3`. `-DisableObjectMotion` sets `LO_MV_REPLAY=0` for a
camera/depth hybrid comparison and records `object_motion=false` in `run.json`.
`-CaptureMode Diagnostic|Lightweight` selects the capture overhead; `Diagnostic`
is the default and enables `LO_RENDER_TIMING` plus motion logging, while
`Lightweight` leaves those diagnostics disabled for lower-overhead runs. The
mode is recorded in `run.json` as `capture_mode`, `render_timing`, and `mv_log`.
PowerShell syntax parsing passed. The parser check itself does not launch the
game; bounded runtime evidence is documented separately below.
`-Background` runs the bounded driver without foreground interaction and uses
the owned window close path; it cannot be combined with `-WindowCycle`.
`-DisableHybridMotion` sets `LO_SR_HYBRID_MV=0` only for the isolated child
process and records `hybrid_motion=false` in `run.json`. `-HiddenResizeCycle`
requires `-Background`; it resizes the owned hidden SDL window around 35 seconds
and restores its original client size around 50 seconds. The before/after window
and foreground measurements, actual client-size changes, and failures are saved
in `hidden-resize-cycle.json`.
`-CaptureScreenshots` asks the game to write serialised internal screenshots
through `screenshot-request.txt` in the run directory. Both options are
recorded in `run.json` and are intended for isolated evidence runs.
The request file must receive a new nonzero serial and count, for example
`1 1`, before the game writes a requested screenshot.
`-ValidationLayerDirectory` optionally injects the Vulkan validation layer from
the supplied directory. The directory must contain
`VkLayer_khronos_validation.json`; the driver enables synchronization
validation and records the requested directory and settings in `run.json`.
The validation log and loaded-layer evidence still determine whether a run is
clean; passing this option alone is not a validation result.
FG remains the experimental Windows Vulkan path. The
driver copies `settings.ini`, `save`,
`profile`, and `shaders` from the supplied baseline into a new output run,
copies the executable and required DLLs, sets `LO_DLSS_FG=1` (or `0` with
`-DisableFg`), mutes audio, uses an isolated shader cache, enters the Uhra route,
and sends bounded movement/input pulses. It stops only the process it started;
the script records executable identity, stdout/stderr, runtime log, termination
state, and baseline metadata preservation.

The bounded D3D12 Uhra camera-only runs used `-DisableObjectMotion` for DLSS
Quality, FSR Quality, DLAA and FSR Native AA; each ran 65 seconds with exit 0,
no forced stop and preserved the baseline. These runs cover the camera/depth
hybrid fallback and do not establish broader scene coverage or player
acceptance.

The FG game path must be built with `LO_ENABLE_STREAMLINE_FG=ON` and a local
pinned Streamline SDK. The runtime flag is opt-in and defaults to off. A typical paired
capture is:

```powershell
pwsh -File tools/perf/run-fg-game.ps1 `
  -BuildDirectory 'C:\path\to\fg-build' `
  -BaselineDirectory 'C:\path\to\baseline-install' `
  -OutputDirectory 'C:\path\to\fg-on-run' `
  -GameDirectory 'C:\path\to\game-data' `
  -Seconds 120

pwsh -File tools/perf/run-fg-game.ps1 `
  -BuildDirectory 'C:\path\to\fg-build' `
  -BaselineDirectory 'C:\path\to\baseline-install' `
  -OutputDirectory 'C:\path\to\fg-off-run' `
  -GameDirectory 'C:\path\to\game-data' `
  -Seconds 120 -DisableFg
```

This is a capture harness, not an acceptance test. The 2026-09-27 bounded
RTX 5080 Vulkan runs are retained under `out/fg-game-20260926/`: the first run
hit a 150% DPI present/render-awareness and `OUT_OF_DATE` rebuild loop;
`run02-dpi` then completed 100 seconds with 3,551 generated intervals and 8,111
presents after the PMv2 opt-in fix. `run03-immediate` completed 100 seconds with VSync
disabled, 4,162 generated intervals, 9,322 presents, SDK errors 0, and a valid
desktop capture. The application log's SDK error count is not a validation-layer
clean result. `run05-window-cycle` also completed 70 seconds with 2,599 generated
intervals and 6,199 presents, but its AltEnter attempts produced no AltEnter or
resize log, so it is not a WindowCycle pass. The driver only sends the shortcut;
a pass requires runtime mode, resize, and recovery logs. These are bounded runtime results;
they do not establish
physical 120 FPS, complete image-quality coverage, provider acceptance, or UI
separation. The UI separation path remains unavailable.

Issue #70 local substitute-scene evidence is retained under
`out/issue70-runtime/`. D3D12 and Vulkan each used 2560×1440 DLAA, 16× AF, a
120 FPS cap, FG disabled, object motion enabled, muted audio, background
handling, and isolated state for about 120 seconds; both exited 0 through the
owned-window close path and preserved the baseline. `scene_3195.png` and
`scene_2507.png` show the Uhra city/plaza route. The 75–115 second Diagnostic
window recorded 2,400 D3D12 accepted-present intervals with mean/p95/p99
`16.666/17.977/18.792 ms`, and 2,323 Vulkan intervals with
`17.219/20.471/23.045 ms`. Both recorded zero AF misses/table creates and zero
sampler-version splits, with two arena splits. This is a single substitute
scene window without before/after A/B or a final Lightweight rerun; it does not
establish an FPS gain or stable 60 FPS.
