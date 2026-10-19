# Tow Tycoon

Build a towing empire in Tow Tycoon! Master 16-bit retro dispatching, dynamic competitor AI, and robust local file management in a high-performance C++ simulation.

**Version 0.2.0 (pre-release).** This is an early Windows test build. Expect rough edges, and please report bugs as GitHub issues.

## Install and play (Windows)

1. Open the [Releases](../../releases) page and download `TowTycoon-<version>-windows-x64.zip`.
2. Unzip it anywhere, for example to your Desktop. Keep every file in the folder together: the game needs the `.dll` files and the `assets` folder beside `TowTycoon.exe`.
3. Double-click `TowTycoon.exe`. If Windows SmartScreen warns about an unrecognized app, choose **More info**, then **Run anyway** (the pre-release build is not code-signed).

Your progress is saved in `%APPDATA%\TowTycoon\TowTycoon\savegame.tt`. The game autosaves every in-game week. If something goes wrong, `towtycoon.log` in the game folder (or the save folder) shows what happened.

## How to play

Drive your tow truck to dispatched breakdowns, hook them up, and haul them back to your yard for cash. Grow from one used wheel-lift into a fleet, win contract lots from your rival, borrow, go public, and buy out the competition.

**Main menu:** Continue resumes your save. New Game starts over (it asks you to confirm when a save exists). Settings toggles fullscreen (F11 also works).

| Key | Action |
| --- | --- |
| W A S D / arrow keys | Drive |
| E | Hook up a car, do a roadside job, or deliver at the yard |
| C | Bid on the open lot you're parked next to |
| M | Vehicle marketplace (unlocks after 6 tows) |
| O | Fleet and drivers |
| F | Bank, finances and loans |
| T | Stock market, IPO and rival buyout |
| [ and ] | Switch which of your trucks you drive |
| Mouse wheel, + and - | Zoom |
| F5 / F9 | Save / load |
| G or Esc | Settings and pause (difficulty, weather, hints, autosave, save and quit) |

## Build from source (Windows, MSYS2)

1. Install [MSYS2](https://www.msys2.org/) and open the **UCRT64** shell.
2. Install the toolchain and libraries:

   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
     mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
     mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_image mingw-w64-ucrt-x86_64-SDL2_ttf
   ```

3. Build and run the tests:

   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

4. Run `build/TowTycoon.exe`. The build copies the needed DLLs and the `assets` folder next to it.

On Linux, install the SDL2, SDL2_image and SDL2_ttf development packages and use the same CMake commands.

## Releasing

Bump the version in `CMakeLists.txt` (`project(TowTycoon VERSION ...)`) and `src/Version.h`, then publish a release from the [New release](../../releases/new) page with a new tag such as `v0.2.0` (tick **Set as a pre-release** for test builds). Creating the tag runs the **Windows build** workflow, which builds, tests and packages the game, then attaches the zip to that release a few minutes later. Every push and pull request also builds the zip as a downloadable workflow artifact.

## License

Copyright Jaron Reis / Dos Reis Kings Properties. All rights reserved; see [LICENSE.txt](LICENSE.txt). The Windows download bundles third-party libraries under their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the `licenses` folder in the zip.
