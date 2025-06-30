# Write #

Cross-platform (Windows, Mac, Linux, iOS, Android) application for handwritten notes.

[styluslabs.com](http://styluslabs.com) | [Help](http://styluslabs.com/write/Help.html) | [FAQ](http://styluslabs.com/faq)

<img alt="Screenshot" src="https://github.com/user-attachments/assets/cc3f1690-073b-4c0e-81ce-f1b4e5ff9fff" width="768">


## Building ##

Checkout: `git clone --recurse-submodules https://github.com/styluslabs/Write`

To build executable `syncscribble/Release/Write`:
* Linux: `cd syncscribble && make USE_SYSTEM_PUGIXML=1 USE_SYSTEM_STB=1 USE_SYSTEM_SDL=1`; On Debian/Ubuntu, `apt install build-essential libstb-dev libpugixml-dev libsdl2-dev`.  Copy fonts from scribbleres/fonts to syncscribble/Release before running Write.
* Android (on Linux): `cd syncscribble/android && ./gww installRelease`; to install Android SDK and NDK, run `gww --install-sdk`
* iOS:
 * build SDL: `cd SDL && git checkout write-mac && make -f ../scribbleres/SDL-Makefile.ios`
 * see [nanovgXC readme](https://github.com/styluslabs/nanovgXC?tab=readme-ov-file#example-app) for setup and then run `cd syncscribble && make` or use the [Xcode project](xcode/Write).
* macOS: `cd syncscribble && make MACOS=1`
* Windows:
 * install Visual Studio (free Community Edition is fine) and [GNU make for Windows](http://www.equation.com/servlet/equation.cmd?fa=make), ensuring it is available in the path
 * in `syncscribble/Makefile`, set `DEPENDBASE` to the parent folder containing all dependencies that `make` should track
 * build SDL: `cd SDL && git checkout write-win && make -f ../scribbleres/SDL-Makefile.msvc`
 * open a Visual Studio command prompt (from the Start Menu) and run `cd syncscribble && make`


## Contributing ##

Contributions are welcome, but please open an issue or discussion before starting on major changes.
