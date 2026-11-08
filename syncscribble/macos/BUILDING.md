# Apple Silicon DMG

The **macOS Apple Silicon DMG** GitHub Actions workflow builds an ARM64 app and
packages it using the existing `make MACOS=1 dmg` target. Download
`Write-macOS-arm64` from a successful workflow run's artifacts and unzip it to
get `Write-macOS-arm64.dmg`. Open the image and drag Write.app to Applications.

The workflow runs on pull requests and pushes to master, and can be started
manually after it is present on the default branch. It checks out the proposed
source revision, builds the macOS SDL branch, verifies the app is ARM64, applies
an ad-hoc signature, and verifies the disk image. No Apple signing credentials
are needed. The app is not Developer ID signed or notarized.

Maintainers can attach the resulting DMG to a GitHub release. Generated binaries
are distributed as build artifacts or release assets rather than committed to
the source tree. This workflow does not automatically publish releases.

To package a locally built ARM64 app, from `syncscribble/` run:

```sh
codesign --force --sign - MacRelease/Write.app
codesign --verify --verbose MacRelease/Write.app
make MACOS=1 dmg
hdiutil verify MacRelease/Write.dmg
```

The DMG contains Write.app, an Applications shortcut, and the project's macOS
readme. Packaging checks do not replace interactive testing of editing, saving,
export, or stylus input.
