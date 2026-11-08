# Write #

Cross-platform (Windows, Mac, Linux, iOS, Android) application for handwritten notes.

[styluslabs.com](http://styluslabs.com) | [Help](http://styluslabs.com/write/Help.html) | [FAQ](http://styluslabs.com/faq)

<img alt="Screenshot" src="https://github.com/user-attachments/assets/cc3f1690-073b-4c0e-81ce-f1b4e5ff9fff" width="768">


## Building ##

Checkout: `git clone --recurse-submodules https://github.com/styluslabs/Write`

To build executable `syncscribble/Release/Write`:
* Linux: `cd syncscribble && make USE_SYSTEM_SDL=1`; On Debian/Ubuntu, `apt install build-essential libsdl2-dev`.  Copy fonts from scribbleres/fonts to syncscribble/Release before running Write.
* Android (on Linux): `cd syncscribble/android && ./gww installRelease`; to install Android SDK and NDK, run `gww --install-sdk`
* iOS:
 * build SDL: `cd SDL && git checkout write-mac && make -f ../scribbleres/SDL-Makefile.ios`
 * see [nanovgXC readme](https://github.com/styluslabs/nanovgXC?tab=readme-ov-file#example-app) for setup and then run `cd syncscribble && make` or use the [Xcode project](xcode/Write).
* macOS: `cd syncscribble && make MACOS=1`; for native Apple Silicon builds, see below.
* Windows:
 * install Visual Studio (free Community Edition is fine) and [GNU make for Windows](http://www.equation.com/servlet/equation.cmd?fa=make), ensuring it is available in the path
 * in `syncscribble/Makefile`, set `DEPENDBASE` to the parent folder containing all dependencies that `make` should track
 * build SDL: `cd SDL && git checkout write-win && make -f ../scribbleres/SDL-Makefile.msvc`
 * open a Visual Studio command prompt (from the Start Menu) and run `cd syncscribble && make`



### macOS on Apple Silicon (M1 and later)

Build on an Apple Silicon Mac with Apple's Command Line Tools installed
(`xcode-select --install`). The following commands build SDL and Write explicitly
for ARM64, so the resulting app does not require Rosetta.

Starting from the repository root after cloning with `--recurse-submodules`:

```sh
# Build the macOS SDL dependency.
cd SDL
git checkout write-mac
make -f ../scribbleres/SDL-Makefile.mac -j6 \
  CC='clang -arch arm64' \
  MC='clang -arch arm64 -x objective-c'

# Build Write and its app bundle.
cd ../syncscribble
make MACOS=1 -j6 \
  CC='clang -arch arm64' \
  CXX='clang++ -arch arm64' \
  MC='clang -arch arm64 -x objective-c' \
  LD='clang++ -arch arm64'

# Apply a local ad-hoc signature and verify the app.
codesign --force --sign - MacRelease/Write.app
codesign --verify --verbose MacRelease/Write.app
file MacRelease/Write.app/Contents/MacOS/Write

# Launch the app.
open -n MacRelease/Write.app
```

The `file` command should report `Mach-O 64-bit executable arm64`. The app is
created at `syncscribble/MacRelease/Write.app`. Ad-hoc signing is for local use;
it does not provide Developer ID signing or notarization for distribution.

If reusing a checkout previously built for another architecture, run
`make -f ../scribbleres/SDL-Makefile.mac clean` from `SDL/` and
`make MACOS=1 clean` from `syncscribble/` before rebuilding. Switching SDL to
`write-mac` changes the submodule checkout, which will appear in `git status`.

These commands were verified with Write commit `401b65d` and SDL `write-mac`
commit `2ef38d0dc` on an ARM64 Mac running macOS 27.2. Compilation, launch, and
interface rendering were checked; this does not establish full feature coverage
or compatibility with future macOS versions.

## Contributing ##

Contributions are welcome, but please open an issue or discussion before starting on major changes.
