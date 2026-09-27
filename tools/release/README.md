# Release packaging

Release packaging assembles the Windows ZIP, Linux AppImage and Flatpak bundle.
The current release workflow checks that the expected package files exist and
are nonempty before publication. It does not require a repository-wide hash,
provenance manifest or SHA-256 sidecar.

```sh
python tools/release/extract_release_notes.py \
  --version v0.7.3 --output /path/to/release-notes.md
```

The notes extractor reads only the matching changelog entry. ZIP installation
still performs ordinary archive parsing, CRC, path protection and rollback; it
does not add a downloaded-file size or SHA check and does not launch the game.

The release workflow will enable `--legacy-updater-manifest` only for the next
formal Windows transition ZIP. That package carries the old SHA map in `files`,
allowing already published v0.7.3 updaters to upgrade automatically. The new
updater ignores the map values and installs the downloaded archive directly
after ordinary HTTP/I/O, ZIP CRC, path and rollback handling. After that
transition package, the workflow flag is removed; ordinary packages may retain
`files` as path-to-size metadata, but it is not used for integrity
authentication.

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
The historical v0.7.3 workflow record reports AppImage fixture 8/8 and Flatpak
Python fixtures 6/6; those results are retained as release history. The
current packaging path checks package existence and size only. The historical
record also reports that the installed main ELF
SHA-256 matched the AppImage input; this is not a current runtime gate. Full
Release CI has not run this workflow; these checks do not rewrite the already
published v0.7.3 provenance. Evidence:
`out/release-workflow-reuse/flatpak-package.log`, `flatpak-source.json`, and
`install-check.log`.
