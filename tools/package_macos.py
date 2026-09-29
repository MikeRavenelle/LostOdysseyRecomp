"""Package the macOS runtime as an ad-hoc signed .app bundle in a ZIP."""
import argparse
import plistlib
import shutil
import subprocess
import tempfile
from pathlib import Path
from portable_shader_pack_payload import stage_portable_shader_pack

ROOT = Path(__file__).resolve().parents[1]
BUNDLE_ID = "io.github.freefrank.LostOdysseyRecomp"
ICON = ROOT / "packaging/linux/io.github.freefrank.LostOdysseyRecomp.png"
MINIMUM_MACOS = "14.0"


def git(*args):
    return subprocess.check_output(("git", *args), cwd=ROOT, text=True).strip()


def source_version(build):
    stamp = build / "LostOdysseyRecomp/source-version.txt"
    if not stamp.is_file():
        raise SystemExit("Build the runtime first: source-version.txt is missing.")
    return stamp.read_text(encoding="utf-8").strip()


def asset_tag(version, requested):
    if requested:
        return requested
    return f"v{version}-{git('rev-parse', 'HEAD')[:8]}-dev"


def make_icns(png, destination, temporary):
    """Build an .icns from the 256 px PNG with sips and iconutil (both ship with macOS)."""
    iconset = Path(temporary) / "LostOdysseyRecomp.iconset"
    iconset.mkdir()
    for size in (16, 32, 128, 256):
        for scale in (1, 2):
            pixels = size * scale
            if pixels > 256:
                continue
            suffix = "" if scale == 1 else "@2x"
            subprocess.run(["sips", "-z", str(pixels), str(pixels), str(png),
                            "--out", str(iconset / f"icon_{size}x{size}{suffix}.png")],
                           check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(destination)], check=True)


def info_plist(version):
    return {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": "Lost Odyssey Recompiled",
        "CFBundleExecutable": "LostOdysseyRecomp",
        "CFBundleIconFile": "LostOdysseyRecomp",
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "LostOdysseyRecomp",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSApplicationCategoryType": "public.app-category.role-playing-games",
        "LSMinimumSystemVersion": MINIMUM_MACOS,
        "NSHighResolutionCapable": True,
        # Game controllers keep working while the game window is focused.
        "GCSupportsControllerUserInteraction": True,
    }


def stage_licenses(licenses):
    licenses.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "LICENSE", licenses / "LostOdysseyRecomp.txt")
    shutil.copy2(ROOT / "thirdparty/miniz-UNLICENSE.txt", licenses / "miniz-UNLICENSE.txt")
    shutil.copy2(ROOT / "thirdparty/nlohmann-json-LICENSE.txt", licenses / "nlohmann-json-LICENSE.txt")
    shutil.copy2(ROOT / "thirdparty/lzokay/LICENSE", licenses / "lzokay-LICENSE.txt")
    shutil.copy2(ROOT / "LostOdysseyRecomp/install/FONT-PROVENANCE.md", licenses / "FONT-PROVENANCE.md")
    shutil.copy2(ROOT / "thirdparty/SDL/test/unifont-13.0.06-license.txt", licenses / "Unifont-OFL-1.1.txt")
    shutil.copytree(ROOT / "thirdparty/dxc-licenses", licenses / "DXC")
    # Statically linked on macOS: FFmpeg (LGPL, XMA decoder) and SPIRV-Cross (Metal shaders).
    shutil.copy2(ROOT / "thirdparty/ffmpeg-LICENSE.txt", licenses / "ffmpeg-LICENSE.txt")
    shutil.copy2(ROOT / "thirdparty/SPIRV-Cross/LICENSE", licenses / "SPIRV-Cross-LICENSE.txt")


def sign(bundle):
    """Ad-hoc sign: Apple Silicon requires a signature, and copying invalidates none."""
    subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none",
                    str(bundle / "Contents/MacOS/libdxcompiler.dylib")], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", "--timestamp=none", str(bundle)], check=True)
    subprocess.run(["codesign", "--verify", "--strict", str(bundle)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "out/build/macos-gpu")
    parser.add_argument("--output", type=Path, default=ROOT / "out/releases")
    parser.add_argument("--version", default="")
    parser.add_argument("--dry-layout", action="store_true", help="Create and list the bundle without zipping")
    args = parser.parse_args()
    build = args.build.resolve()
    runtime = build / "LostOdysseyRecomp/LostOdysseyRecomp"
    dxc = build / "LostOdysseyRecomp/libdxcompiler.dylib"
    for path in (runtime, dxc):
        if not path.is_file():
            raise SystemExit(f"Missing macOS build artifact: {path}")
    version = source_version(build)
    tag = asset_tag(version, args.version)
    name = f"LostOdysseyRecomp-macos-arm64-{tag}"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="macos-app-", dir=output) as temporary:
        bundle = Path(temporary) / "LostOdysseyRecomp.app"
        executables = bundle / "Contents/MacOS"
        resources = bundle / "Contents/Resources"
        executables.mkdir(parents=True)
        resources.mkdir(parents=True)
        shutil.copy2(runtime, executables / "LostOdysseyRecomp")
        # The runtime loads DXC from @executable_path.
        shutil.copy2(dxc, executables / "libdxcompiler.dylib")
        with (bundle / "Contents/Info.plist").open("wb") as plist:
            plistlib.dump(info_plist(version), plist)
        make_icns(ICON, resources / "LostOdysseyRecomp.icns", temporary)
        licenses = resources / "licenses"
        stage_licenses(licenses)
        stage_portable_shader_pack(runtime.parent, executables, licenses)
        sign(bundle)
        if args.dry_layout:
            print(f"Bundle: {bundle}")
            for path in sorted(bundle.rglob("*")):
                if path.is_file():
                    print(path.relative_to(bundle.parent).as_posix())
            print(f"SelectAsset: {name}.zip")
            return
        destination = output / f"{name}.zip"
        if destination.exists():
            raise SystemExit(f"Output already exists: {destination}")
        # ditto keeps the bundle's signature, permissions and extended attributes.
        subprocess.run(["ditto", "-c", "-k", "--keepParent", str(bundle), str(destination)], check=True)
        print(f"SelectAsset: {destination.name}")


if __name__ == "__main__":
    main()
