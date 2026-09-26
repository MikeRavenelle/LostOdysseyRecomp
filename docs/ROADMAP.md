# Roadmap

[简体中文](ROADMAP.zh-CN.md) · [Development status](STATUS.md) · [Changelog](../CHANGELOG.md) · [Maintainer Project](https://github.com/users/freefrank/projects/3)

Reviewed on 2026-09-26 against live Issues, Project fields, merged commits and release records. `[x]` means the stated scope is delivered; `[~]` means a concrete remainder is open; `[ ]` means planned work. Closing a tracker does not establish unrecorded gameplay or hardware validation.

## Current delivery

- [x] **v0.7.0 published:** source `4142f23`, Release CI [36228746088](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/36228746088). Windows and Linux packages are available on the [release page](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.7.0).
- [x] **v0.7.1 published:** source commit `c585ef820cb72993ad87a90a1a03c1c648fb654c`, tag `v0.7.1`, [release page](https://github.com/freefrank/LostOdysseyRecomp/releases/tag/v0.7.1) (2026-09-26T21:51:24Z), Release CI [36274702691](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/36274702691). Adds Gameplay → Import discs & DLC, safe restart into the importer, selective replacement and failure rollback. Public artifacts verified with matching SHA256 digests: Windows package `LostOdysseyRecomp-windows-x64-v0.7.1.zip` (243,762,106 bytes, SHA256: `e53753a71b06ab39c41a3a5b327a8477523db4b006543c54e70834b183c5291f`), Linux AppImage `LostOdysseyRecomp-linux-x64-v0.7.1.AppImage` (251,038,200 bytes, SHA256: `878d04f9a530771fc2ba752842c1c9b5ba1cfc3fea63555a401dd53b46dd6e65`), standalone Flatpak `LostOdysseyRecomp-linux-x64-v0.7.1.flatpak` (265,618,800 bytes, SHA256: `2efe0a4ba556037f9118894b36cba4b7667132b708c9ec3ea325db9c16f71775`, stable branch), and Flathub runtime input `LostOdysseyRecomp-linux-x64-v0.7.1-flatpak-runtime.tar.xz` (SHA256: `661838345ca5e1590dce99e35a9dba2bc1138d073c1c76d947aec34ea4db931f`). psvita user acceptance received for Flatpak (strictly bounded; does not extrapolate to performance or multi-scenario compatibility).

## Completed features and reconciled trackers

- [x] **Flatpak standalone release:** delivered ahead of schedule in v0.7.1 (originally planned in v0.8.0). Independent standalone bundle `LostOdysseyRecomp-linux-x64-v0.7.1.flatpak` published and psvita user accepted. Standalone Flatpak delivery is independent of Flathub listing. Flathub store submission is tracked separately (no PR created): Flathub's `requirements#generative-ai-policy` strictly prohibits AI generation or assistance for manifests and PRs, and the PR template requires an application demonstration video; maintainer must manually author a separate manifest/PR and provide the application video.

- [x] **DLSS/DLAA and FSR SR:** the v0.7.0 scope passed user acceptance within recorded Windows/native Linux coverage. FSR P1/P2 are complete; frame generation remains below.
- [x] **PlayStation prompts:** host and guest face/shoulder/Start/Back glyphs accepted and published in v0.7.0.
- [x] **Mod API v1 and Wiki:** `457ba24` / PR #68 delivers manifest resolution, disable/fallback, reload, LOTEX1/PNG tooling, native-menu atlas/font replacements and mod guides. Windows/Linux Mod API and Wiki CI passed. Arbitrary guest texture/model replacement and real MO2 acceptance are not included in this completed scope.
- [x] **IME, idle cursor and controller improvements:** shipped in v0.6.19; the old IME “Not started” item is closed. Broader device combinations remain regression coverage.
- [x] **PortForge manifest:** Windows/Linux manifest in `9abda30`; Issue #37 is closed. Local tracking now respects the Project's Done status.
- [x] **Issue triage automation:** deployed Issue-opened and `@codex` replies, including a verified [real public response](https://github.com/freefrank/LostOdysseyRecomp/issues/21#issuecomment-5669773986). Future bug repairs and answer-quality improvements are separate work.
- [x] **Audio trackers #54/#55:** both Issues and Project items are closed. This records maintainer closure, not proof that PR #66's diagnostic changes fixed every language or missing-line report. Prior feedback remains in the item evidence.
- [x] **Earlier shipped work:** live AF, Quit to Desktop/Title, Save Anywhere preference persistence, cheat navigation, ultrawide controls, portable Vulkan shaders, Linux AppImage, online PPC compilation, and bounded rendering/runtime repairs. Historical measurements and release details remain in [STATUS](STATUS.md) and [CHANGELOG](../CHANGELOG.md), rather than the active queue.

## Active work

- [~] **Open reports:** #49 physical-pixel window coordinates, #64 DLSS/FSR behavior and #67 Grand Staff sky flicker remain open. Existing sizing recovery and diagnostics do not close these reports.
- [~] **Issue #40 remaining mod scope:** PlayStation prompts, the v1 framework and Wiki are delivered; broader guest texture/model consumers and real external-manager integration remain. The Issue is still open.
- [ ] **Feature requests:** DoF controls (#30) and motion-sickness options (#48).
- [~] **Native motion and temporal color:** geometric/rigid/skinned replay infrastructure and qualified SDR inputs are implemented, with bounded battle and Hybrid SR evidence. Remaining work covers unmapped draws, broader skeletal/scene coverage, HDR/exposure and D3D12 replay PSO failure `0x80070057`.
- [~] **Linux/Steam Deck:** Linux x64 runtime, menus, importer, updater and AppImage are released; native AMD 8060S RADV evidence exists. Steam Deck hardware, Steam runtime/Flathub and broader playthrough coverage remain. APEX 15W evidence is not Deck hardware equivalence.
- [~] **Performance and shader startup:** bounded city/cache improvements and notified waits are already on main. Remaining stalls, two retained shader translation failures and broader 15W/full-game performance targets remain open; earlier unpublished-branch descriptions are historical.
- [ ] **Graphics follow-ups:** D3D12 portable shader pack, standalone experimental TAA removal, resource-only PSO coverage, and profile-led cache/motion optimizations. Spatial-AA fallback alone does not implement TAA-option removal or prove all flicker resolved.
- [ ] **Broader regression:** full playthrough, chapter/disc/save compatibility, audio/languages, controller/rumble, mixed-DPI/fullscreen and multi-GPU coverage. Individual completed fixes are not held open solely for these broader goals.

## v0.8.0 plans

Planned development sequence (ordered execution arrangement; does not construct artificial hard technical dependencies between subsequent and preceding items; P0/P3/P4 represent stage identifiers, not priority levels):

1. [~] **P0:** common temporal contracts are implemented; Streamline/NGX coexistence Gate 1 remains unpassed.
2. [ ] **P3:** production frame-generation presentation infrastructure, leases and UI separation.
3. [ ] **P4:** fixed 2× DLSS Frame Generation on Windows Vulkan, including DLSS/DLAA/FSR combinations and safe suspension/recovery.
4. [ ] **FSR Frame Generation:** independent target; API, platform and multiplier are not predetermined.
5. [ ] **D3D12 DLSS Frame Generation:** planning target for DLSS Frame Generation on the D3D12 backend, independent of the Windows Vulkan P4 milestone.
6. [ ] **Dynamic MFG:** independent target for dynamic multi-frame generation; platform and multiplier are not predetermined.
7. [ ] **Optional native 120 FPS:** independent game presentation, not generated frames; requires pacing, Ring, audio and cutscene validation, with 60 FPS as default.
8. [ ] **Remove the PM4 translator:** replacement architecture investigation starts early alongside preceding phases; execution follows frame pacing and presentation work.
9. [ ] **Linux AArch64:** platform delivery target; no official packages or hardware acceptance claimed.
10. [ ] **macOS AArch64 / Apple Silicon:** graphics backend and dependency feasibility investigation starts early; platform delivery target.
11. [ ] **Experimental Android:** exploratory platform target, no APK or device validation yet.

Parallel tracks:

- [x] **Flatpak release:** delivered ahead of schedule in v0.7.1 (standalone package published and user-verified; Flathub store submission requires manual authoring of manifest/PR and application demonstration video per Flathub AI policy, tracked separately).

## Later backlog

DX11, HDR output, higher-resolution shadows, SSAO/depth access, GI/reflections, ray tracing and the paused Switch work remain independent proposals. WMV playback, temporary protagonist damage controls and other unproven items retain their current Project scope; they were not marked complete without evidence.

See the [Project](https://github.com/users/freefrank/projects/3) for individual evidence and the [historical roadmap](archive/ROADMAP-2026-09-10.md) for earlier detail. This reconciliation did not rerun builds, games or tests.

<!-- Historical link compatibility. -->
<a id="v070-frame-generation"></a>
<a id="v070-upscaling"></a>
<a id="v080-frame-generation-macos"></a>
<a id="v090-pm4-translator"></a>
<a id="v050-pc-graphics"></a>
<a id="next-major-milestone-v050--pc-vulkan-and-direct3d-11"></a>
<a id="near-term-priorities"></a>
<a id="current-feedback-and-regression-work"></a>
<a id="published-milestone-v042--repairs-and-validation"></a>
<a id="phase-1-produce-compilable-output"></a>
<a id="phase-2-reach-the-main-menu"></a>
<a id="phase-3-work-toward-a-complete-playthrough"></a>
<a id="phase-4-modernization"></a>
<a id="phase-5-optional-exploration"></a>
