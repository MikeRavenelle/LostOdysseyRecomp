# Release ZIP verification

`verify_package.py` checks a Windows release ZIP after packaging. Supply the
expected tag and full commit explicitly:

```sh
python tools/release/verify_package.py \
  --assets /path/to/release-assets --version v0.6.15 \
  --commit <40-character-commit> --output /path/to/package-verification.json
```

Use `--package /path/to/LostOdysseyRecomp-windows-x64-v0.6.15.zip` instead of
`--assets` to select one ZIP. The tool reads the ZIP and its adjacent
`.zip.sha256` sidecar. It checks the archive checksum, ZIP entry names, manifest
file list, formal version, clean source state, build and packaging commits, and
linked runtime provenance. It writes only the requested JSON report. Release
assets remain unchanged; the updater is neither extracted nor launched.

The archive checksum is read once. Payloads are not rehashed, and this tool
does not run the game or verify shader behavior. It handles the Windows ZIP
format; the Linux AppImage has no corresponding embedded ZIP manifest.

For the v0.7.3 public release, publish only the Windows ZIP, Linux AppImage,
and stable Linux Flatpak bundle. Do not publish the Flatpak runtime archive,
`release-source.json`, or standalone checksum/source-list assets; checksum and
source records remain CI or local internal validation artifacts. GitHub Release
should contain only the three installable packages.

Release workflow design: the Linux job compiles once, creates a persistent
AppImage AppDir, and exports the stable Flatpak by reusing that AppDir's
`usr` tree. It does not perform a second source compilation for Flatpak.
The workflow exports a stable Flatpak directly and uploads it alongside the
Windows ZIP and AppImage. After both platform jobs succeed, the publication
job checks that exactly those three packages are uploaded and nonempty before
publishing the draft. Re-runs validate an existing public release without
changing its publication state.
AppImage fixture passes 8/8 checks, Flatpak Python fixtures pass 6/6, the
workflow shell fragments pass `bash -n`, actionlint 1.7.12 passes, the Flatpak
reuse probe resolves 29 ELF files and four libraries, and the SDL
PipeWire/static focused compile passes. Stable bundle export exits 0, isolated
user installation and sandbox shell checks pass, and the installed main ELF
SHA-256 matches the AppImage input. Full Release CI has not run this workflow;
these checks do not rewrite the already published v0.7.3 provenance. Evidence:
`out/release-workflow-reuse/flatpak-package.log`, `flatpak-source.json`, and
`install-check.log`.
