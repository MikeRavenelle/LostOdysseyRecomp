# macOS releases

macOS builds are released from this fork (MikeRavenelle/LostOdysseyRecomp), not by
upstream. They are built, signed and notarized **locally**: GitHub CI never builds with
game data, and the runtime cannot be built without it.

## What a release contains

`LostOdysseyRecomp-macos-arm64-v<version>.zip` holds `LostOdysseyRecomp.app`:

- signed with a Developer ID Application certificate and the hardened runtime (no
  entitlements; `libdxcompiler.dylib` is signed with the same team, which library
  validation requires);
- notarized by Apple with the ticket stapled, so it opens without a Gatekeeper warning
  and without a network check;
- bundle identifier `io.github.mikeravenelle.LostOdysseyRecomp`.

The app never writes into its bundle. The game, settings, saves and shader cache live
in `~/Library/Application Support/LostOdysseyRecomp` (logs in `~/Library/Logs/LostOdysseyRecomp`),
so replacing the app keeps everything. On first launch without a game the importer opens;
the first launch then shows "Preparing shaders" for about 4 minutes on an M1 Pro.

Versions are the source version plus `-macos.<n>`, e.g. `0.7.20-macos.1`, tag
`v0.7.20-macos.1`. At startup the app reads this fork's latest release; if its tag
differs and it carries the macOS asset, the app offers **Download**, which opens the
release page. Settings → `automatic_updates=0` or `LO_NO_UPDATE=1` turn the check off.

## One-time setup

1. **Developer ID Application certificate** (needs the Account Holder role on the team):
   Xcode → Settings → Accounts → select the team → Manage Certificates → **+** →
   **Developer ID Application**. Check it:

   ```sh
   security find-identity -v -p codesigning
   #  1) ABCDEF… "Developer ID Application: Your Name (TEAMID1234)"
   ```

2. **Notary credentials.** Create an app-specific password at
   [account.apple.com](https://account.apple.com) → Sign-In and Security →
   App-Specific Passwords, then store it in the keychain under a profile name:

   ```sh
   xcrun notarytool store-credentials lo-notary \
       --apple-id you@example.com --team-id TEAMID1234 --password abcd-efgh-ijkl-mnop
   ```

## Each release

1. Commit everything, and run the regression suite (`python3 tools/regression.py run`).
2. Build, sign, notarize and package (the number is the macOS release counter for
   this source version):

   ```sh
   tools/release_macos.sh 1 "Developer ID Application: Your Name (TEAMID1234)"
   ```

   Notarization usually takes a few minutes. The script stops on any failed step.
3. Publish on GitHub: Releases → **Draft a new release** on this fork:
   - tag `v0.7.20-macos.1` on the commit you built (the tag must match the ZIP name);
   - not a pre-release (the updater reads the *latest* release, which skips pre-releases);
   - attach `out/releases/LostOdysseyRecomp-macos-arm64-v0.7.20-macos.1.zip`;
   - notes under a `### English` heading (the in-app update prompt shows that section).
4. Reset the local build's version for everyday work:
   `cmake -S . -B out/build/macos-gpu -DLO_VERSION_SUFFIX=`

## Local builds

`python3 tools/package_macos.py` without `--identity` makes an ad-hoc signed app
(`-dev` ZIP) that runs on the building Mac only. It does not use the hardened runtime:
an ad-hoc signature has no team, so library validation would reject the DXC library.
