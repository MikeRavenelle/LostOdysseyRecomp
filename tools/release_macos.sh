#!/bin/zsh
# Build, sign, notarize and package a macOS release of the fork.
# See docs/MACOS_RELEASE.md for the one-time certificate and notary setup.
#
#   tools/release_macos.sh <suffix-number> "<Developer ID Application: Name (TEAMID)>" [notary-profile]
#   tools/release_macos.sh 1 "Developer ID Application: Jane Doe (ABCDE12345)"
#
# The version is the source version plus -macos.<n> (tag v0.7.20-macos.1); the
# ZIP lands in out/releases as LostOdysseyRecomp-macos-arm64-<tag>.zip.
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ $# -lt 2 ]]; then
  sed -n 2,9p "$0"
  exit 2
fi
number=$1
identity=$2
profile=${3:-lo-notary}
build=out/build/macos-gpu

if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Commit or stash tracked changes first: a release is built from a clean tree." >&2
  exit 1
fi
if ! security find-identity -v -p codesigning | grep -qF "$identity"; then
  echo "Signing identity not found in the keychain: $identity" >&2
  security find-identity -v -p codesigning >&2
  exit 1
fi

cmake -S . -B "$build" -DLO_VERSION_SUFFIX="macos.$number" >/dev/null
cmake --build "$build" --target LostOdysseyRecomp
version=$(<"$build/LostOdysseyRecomp/source-version.txt")
echo "Version $version (tag v$version)"

python3 tools/package_macos.py --build "$build" --release --identity "$identity" --notarize "$profile"

# Later local builds keep reporting this release's version until the next one;
# reset with: cmake -S . -B out/build/macos-gpu -DLO_VERSION_SUFFIX=
echo
echo "Next: publish out/releases/LostOdysseyRecomp-macos-arm64-v$version.zip as release v$version"
echo "on github.com/MikeRavenelle/LostOdysseyRecomp (see docs/MACOS_RELEASE.md)."
