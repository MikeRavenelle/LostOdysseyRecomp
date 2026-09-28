# Release packaging

Release packaging assembles the Windows ZIP, Linux AppImage and Flatpak bundle.
The current release workflow checks that the expected package files exist and
are nonempty before publication. It does not require a repository-wide hash,
provenance manifest or SHA-256 sidecar.

```sh
python tools/release/extract_release_notes.py \
  --changelog CHANGELOG.md --version v0.7.9 --output /path/to/release-notes.md
```

The notes extractor reads only the matching changelog entry. ZIP installation
still performs ordinary archive parsing, CRC, path protection and rollback; it
does not add a downloaded-file size or SHA check and does not launch the game.

Release notes also appear in the automatic updater. Keep each language to 2–4
short, player-facing bullets; put test counts, commit IDs, CI logs and validation
details in technical notes rather than the changelog entry.

The v0.7.9 Windows transition ZIP carried the old SHA map in `files`, allowing
already published v0.7.3 updaters to upgrade automatically. The v0.7.9 updater
ignores the map values and installs the downloaded archive directly after
ordinary HTTP/I/O, ZIP CRC, path and rollback handling. Main after v0.7.9 no
longer generates this compatibility SHA map; ordinary packages may retain
`files` as path-to-size metadata, but it is not used for integrity
authentication.

For the v0.7.9 public release, publish only the Windows ZIP, Linux AppImage,
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
The v0.7.9 Release CI [36378342125](https://github.com/freefrank/LostOdysseyRecomp/actions/runs/36378342125)
passed all five jobs. Its Linux job installed `clang-tools-18` 18.1.8, built
the Ubuntu 22.04-compatible AppImage baseline, checked AppImage ABI/loader
behavior and reused the same AppDir `usr` tree for Flatpak. The three public
download URLs returned HTTP 200 after redirects. These checks did not run the
game and do not claim SHA verification. The historical packaging workflow
record reports AppImage fixture 8/8 and Flatpak Python fixtures 6/6; those
results are retained as release history. Evidence:
`out/release-workflow-reuse/flatpak-package.log`, `flatpak-source.json`, and
`install-check.log`. The Linux job compiles the source once, retains the
AppImage AppDir, and exports the stable Flatpak by reusing its `usr` tree.
