# Tekken Tag 2 Ultimate Edition

<img width="1593" height="888" alt="image" src="https://github.com/user-attachments/assets/3bd58409-1442-4ce7-b9a6-8f036f38f4f8" />


An unofficial native Linux and Windows port of **Tekken Tag Tournament 2 Wii U Edition**,
made by static recompilation: the game's PowerPC code is translated to C++
ahead of time and runs as native x86-64 code on a reimplementation of the
Wii U's system libraries. There is no CPU emulation; the game's GPU commands
and shaders are translated to Vulkan as it runs.

> [!IMPORTANT]
> **No game files are included.** This repository holds only original tools
> and runtime code; you need your own copy of the game
> ([see below](#the-game)). The C++ generated from the game, programs built
> from it, an install folder, and captures or dumps made with the debugging
> options all contain the game's code or assets: do not share or upload
> them, and do not attach them to issues.
>
> This project is not affiliated with or endorsed by Bandai Namco
> Entertainment or Nintendo. TEKKEN, Tekken Tag Tournament and Wii U are
> trademarks of their respective owners.

## Features

- **Plays natively**: intro, menus, character select, arcade mode and versus
  matches, with sound and saves. Arcade mode has been played through by
  hand.
- **120 frames per second** on displays of 100 Hz and more: between each
  two frames the game renders, the port shows one more, drawn from the same
  frame with the camera, objects and character skeletons blended halfway.
  The game's logic, physics and hitboxes stay at 60 Hz. When other programs
  keep the GPU busy, frames go out without their extra frame rather than
  slowing the game down. The launcher's Settings can turn it off.
- **Higher resolutions**: up to 4x the original 720p; by default the
  smallest scale that covers your main display (2x, 2560x1440, on 1080p and
  1440p displays; 3x on 4K), or the one chosen in the launcher's Settings
  (1280x720, 1920x1080, 2560x1440, 3840x2160 or 5120x2880).
- **Even frame pacing** on 120 Hz and 240 Hz displays (with
  `VK_KHR_present_wait2`): the game's clock follows the display's
  refreshes, so every image stays on screen equally long.
- **Controls and icons as on a computer**: menus confirm with the bottom face
  button and go back with the right one, as on the other consoles (Nintendo
  controllers keep the Wii U's way). Button icons follow the controller in
  use: PlayStation controllers get PlayStation-style icons, any other
  neutral ones that show each face button by its place on the pad. The
  game's texts show the icons where they named the Wii U's buttons, and the
  title screen's logo says "Ultimate Edition" where it said "Wii U
  Edition", in the same brush lettering. All of it is made by the port
  from the Wii U game's own files.
- **Position reset in Practice**, as in Tekken 8: 3 and + (Start) pressed
  together put both fighters back where the stage put them.
- **Installs like any other program**: `ttt2 --install` copies the game into
  a folder you choose and adds it to the application menu.
- **Made to be modified**: every function of the game is a C++ function that
  can be replaced ([see below](#changing-the-game)).

## Status

Playable from boot through arcade mode. Not done yet: the Wii U GamePad's
screen and speaker, online play, the software keyboard and error viewer
screens, and audio effects the game does not seem to use. Only the European
version with update 16 is supported (other versions have different code).
Tested on Linux with an AMD GPU (RADV) under KDE Plasma (Wayland). The
Windows build (MSYS2 CLANG64) compiles, installs and passes the tests that
need no game files, but has not been played through on Windows yet. Design
notes, measurements and milestones are in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## The game

You need **TEKKEN TAG TOURNAMENT 2 Wii U EDITION**, European release, with
update v16, as a `.wua` archive (Cemu can create one from an installed title;
the update may be merged into the game's folder or stored beside it), or as
a folder with the game's `code/`, `content/` and `meta/` and the update's
files over them.

| Property | Value |
| --- | --- |
| Product code | WUP-P-AKNP |
| Title ID | `000500001010F800` (update `0005000E1010F800`) |
| Version | 16 |
| `code/Tekken.rpx` SHA-256 | `fc0270465d384386f67804e48716e30bfbe85a5a1004e3a6bfe3ac75f9de5019` |

`cafe-recomp` refuses any other `Tekken.rpx`, and the port and its installer
refuse a game whose executable is not the one they were built from.

## Requirements

- Linux or Windows 10/11 on x86-64 with AVX2 and FMA (x86-64-v3: Intel
  Haswell, AMD Excavator or newer).
- A Vulkan 1.3 GPU and driver with `VK_EXT_external_memory_host` and push
  descriptors (current AMD, Intel and NVIDIA drivers).
- To build: Git, CMake 3.20+, Ninja, Clang (or GCC 15+), Python 3, and the
  development files of SDL 3.2 or newer, shaderc, the Vulkan loader, Vulkan
  headers recent enough to define `VK_KHR_present_wait2` (mid-2025 or
  newer), OpenSSL 3, zstd and zlib; for the launcher also GTK 4.10 or newer
  (without it the launcher is left out) and MinGW (optional: the Tekken 7
  helper is then built in).

On Arch Linux:

```bash
sudo pacman -S --needed base-devel git cmake ninja clang python zstd zlib openssl vulkan-headers vulkan-icd-loader sdl3 shaderc gtk4 mingw-w64-gcc
```

Other distributions package the same libraries under similar names; older
releases such as Ubuntu 24.04 and Debian 12 ship SDL and Vulkan headers that
are too old.

On Windows, build in [MSYS2](https://www.msys2.org/)'s **CLANG64**
environment (MSVC is not supported: the generated code relies on Clang's weak
symbols, aliases and `musttail`). In the "MSYS2 CLANG64" terminal:

```bash
pacman -S --needed git mingw-w64-clang-x86_64-{toolchain,cmake,ninja,python,sdl3,shaderc,vulkan-headers,vulkan-loader,openssl,zstd,zlib}
```

The Vulkan driver itself comes with the GPU's driver (AMD, NVIDIA, Intel).
The launcher (below) is Linux's alone: on Windows the game is built with the
commands of the next section and installed with `ttt2.exe --install`.

## Windows installer

On Windows the easiest way is the installer from the
[Releases](../../releases) page, `TTT2-Setup-Windows-<version>.exe`: it asks
for your `.wua` and an install folder, builds the game program for your PC
from the archive with the compiler it carries (the game's code cannot be
shipped, only translated on your PC), and installs the game with a Start
menu shortcut. It needs no MSYS2 and no administrator rights; building
takes some minutes, about 1.5 GB of free space and 4 GB of memory or more.
`windows/` holds its sources and `.github/workflows/windows-installer.yml`
builds it; a tag `windows-v<version>` publishes a release.

`TTT2-Launcher-Windows-<version>.exe` (a tag `launcher-v<version>`,
`windows/launcher/`) installs **TTT2 Ultimate Launcher**: Play; Update all,
which installs the newest game installer from the releases and rebuilds the
game program from the install folder (no `.wua` needed again); Install the
game; and Tekken 7's fighters, from any folder holding your Tekken 7 (with
Steam signed in to the account that bought it).

## Building

```bash
git clone https://github.com/Petexy/Tekken-Tag-2-Ultimate-Edition.git && cd Tekken-Tag-2-Ultimate-Edition
python3 tools/setup_deps.py                      # ZArchive and AMD's addrlib, at pinned revisions

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --target wua_extract cafe-recomp

# The game's executables and metadata (about 70 MB per title folder) into local/wua
build/wua_extract extract-analysis "/path/to/TEKKEN TAG 2 Wii U EDITION (EU).wua" local/wua
# The v16 executable's code as C++ in generated/ttt2 (in the update's
# folder, or in the game's when the update is merged into it)
build/recomp/cafe-recomp local/wua/*_v16/code/Tekken.rpx generated/ttt2

cmake -B build -DTTT2_GENERATED_DIR="$PWD/generated/ttt2"
cmake --build build --target ttt2
```

On Windows the commands are the same in the MSYS2 CLANG64 terminal (the
programs are `build/wua_extract.exe`, `build/recomp/cafe-recomp.exe` and
`build/ttt2.exe`; paths can be written `/c/Users/...` or `C:/Users/...`).
After building, `ttt2.exe` has the libraries it needs (SDL3, shaderc,
OpenSSL, the C++ runtime) copied beside it, so it also starts from Windows'
Explorer or a `cmd` prompt; `-DTTT2_DLL_DIRS=<folders>` adds folders to take
them from.

Recompiling takes a few seconds; compiling the generated code takes a few
minutes. `local/`, `generated/`, `third_party/` and `build*/` are ignored by
Git, so nothing derived from the game ends up in a commit; keep captures and
dumps from the debugging options under `local/` too.

## The launcher

`ttt2-launcher` (**Ultimate Launcher**) does the installing and
the adding of fighters in a window, without a terminal. **Install…** asks for
your `.wua` and a folder, checks the PC, builds the game program for it from
the archive and installs the game with its menu entry, as `ttt2 --install`
does; then **Play**, **Update** (when the launcher's version differs from
the program's) and **Uninstall…** (saves and added characters are kept unless you
tick them; of a save folder you chose with `TTT2_SAVE_DIR`, only the game's
own saves go). While the game runs, however it was started, Play, Update and
Uninstall wait for it to end. The gear next to Play opens its **Settings**:
whether the game starts fullscreen, the resolution it draws at, its 120
frames per second, and its keyboard keys ([Playing](#playing)). Its
Characters section shows where Steam has Tekken 7, or why it cannot be used, with **Add** per fighter and **Add All**, and Tekken 7's DLC
fighters in a list that opens, with those your Steam account owns and **Add
All Owned**; **Remove** takes one out again.

```bash
cmake --build build --target ttt2-launcher ttt2_kit
build/launcher/ttt2-launcher
```

Nothing of the game is in the launcher: the game program is built on your PC
by the launcher's kit (`build/launcher/kit`: `ttt2-build`, `cafe-recomp` and
the runtime) with your C++ compiler, in a few minutes. It needs Clang (or
GCC 15+) and the SDL 3, shaderc, Vulkan, OpenSSL 3, zstd and zlib libraries
(not their development files); the launcher checks the processor, the
compiler and free space first and shows the command that installs what is
missing. On image-based systems, whose package manager cannot add to the
system (SteamOS, Fedora Atomic and Bazzite, openSUSE MicroOS and Aeon,
NixOS), it says instead that the compiler and the importer's tools must come
from a toolbox or distrobox container, or from the system's own way of
adding software. `build/launcher/kit/bin/ttt2-build install GAME FOLDER`
does the same in a terminal; `ttt2-build check` alone only checks that this
PC can build the game program (the compiler, the libraries, and that the
program links with them and starts).

To make an AppImage of the launcher (the launcher, its kit and the character
importer; nothing of the game):

```bash
launcher/appimage/make_appimage.sh Ultimate_Launcher-x86_64.AppImage build-appimage
```

It builds in a folder of its own (here `build-appimage`; Git ignores it and
the AppImage), needs LLD and `mksquashfs`,
and downloads the AppImage runtime once (`--runtime-cache DIR` says where).
The AppImage uses the system's GTK 4 (4.10 or newer) and needs glibc 2.38
and GCC 13's libstdc++ or newer (Ubuntu 24.04, Debian 13, Fedora 39 and
newer releases, and rolling distributions), on any x86-64 processor. The
game program it builds needs SDL 3.2 or newer from the system, which
Ubuntu 24.04 does not have yet.

The AppImage updates itself. When it starts it asks GitHub for this
repository's newest release; when that is newer, a notice at the top of the
window says so, with **What's New** (the release's page) and **Update**,
and the button left of Check Again is highlighted (that button also looks
for an update any time). Update downloads the new AppImage (with curl or
wget), checks it, puts it in place of the AppImage you started and opens it. A release is tagged `v<version>` (`v0.1.1`) with
the AppImage attached, built with that version as `TTT2_LAUNCHER_VERSION` in
`launcher/builder/CMakeLists.txt`: before it replaces anything the launcher
checks that the new one says it is that version (`ttt2-launcher --version`).

## Playing

Run the game straight from its archive (or from a game folder):

```bash
build/ttt2 "/path/to/TEKKEN TAG 2 Wii U EDITION (EU).wua"
```

Or install it: choose the archive and the install folder in file dialogs, or
give them as `--install GAME FOLDER`. This copies the game (16 GB) and the
program into that folder (a folder that is not empty gets a
`Tekken Tag Tournament 2` folder inside it) and adds **Tekken Tag Tournament
2** to the application menu (`--no-launcher` leaves that out). Then start it
from the menu, or run `ttt2` without arguments; the `.wua` is no longer
needed.

```bash
build/ttt2 --install
build/ttt2 --update      # after a rebuild: copies the new program into the install
```

Saves go to `~/.local/share/ttt2/save` and compiled shaders to
`~/.cache/ttt2`. F11 toggles fullscreen while the game runs (whether it
starts fullscreen is a setting, below).

On Windows, `ttt2.exe --install` copies `Tekken Tag Tournament 2.exe` with
its libraries into the install folder and adds a **Tekken Tag Tournament 2**
shortcut to the Start menu. Saves go to `%APPDATA%\ttt2\save`, character
packs are read from `%APPDATA%\ttt2\characters` and compiled shaders go to
`%LOCALAPPDATA%\ttt2`.

| Wii U GamePad | Keyboard |
| --- | --- |
| D-pad up / left / down / right | W / A / S / D |
| Top face button (the Wii U's X) | I |
| Left face button (the Wii U's Y) | J |
| Bottom face button (the Wii U's B) | K |
| Right face button (the Wii U's A) | L |
| L / R | H / ; |
| ZL / ZR | U / O |
| + (Start) / − (Select) | Enter / Backspace |
| HOME | Home |
| Left stick up / left / down / right | ↑ / ← / ↓ / → |

The face buttons go by their place on the pad, not by their letters, which
differ between controllers (an Xbox-style pad's bottom button is its A):
I / J K L on the keyboard make the same diamond as the pad's buttons, so
with the game's own button setup J is left punch, I right punch, K left
kick and L right kick. Keys go by position, as on a US QWERTY keyboard.

These are the defaults: the launcher's **Settings** (the gear next to Play)
change them, at once, also while the game runs, and **Restore Defaults**
there brings them back. They are kept in `~/.config/ttt2/keyboard.ini`
(`$XDG_CONFIG_HOME/ttt2/`; on Windows `%APPDATA%\ttt2\keyboard.ini`), one `input = key` per line, which can be edited
by hand too ([its format](docs/ARCHITECTURE.md#running-the-port)); F11,
Escape and the Windows keys cannot be given to a button, and the game prints
what it read when it starts.

Gamepads work through SDL by button position, as on the Wii U: the right
face button is A, the bottom one B, the top one X and the left one Y, and
fights use them that way. In menus the bottom button (B, by default K on the
keyboard) confirms and the right one goes back, except with a Nintendo
controller, which confirms with its A as on the Wii U. The icons follow the
last controller used (the keyboard counts as a neutral one).

In Practice, 3 (left kick: by default K, or a gamepad's bottom button) and
+ (Start: Enter) pressed together, in either order, put both fighters back
where the stage put them, standing, as Tekken 8's position reset does; the
pause menu's POSITION SETTINGS apply as at the start of training. 3 is
whichever button the game's Controller Setup gives left kick. + alone
still opens the pause menu, a few frames later than before.

The launcher's **Settings** also choose how the game starts: fullscreen or
in a window, the resolution it draws at (Automatic, the default, is enough
to cover your main display) and whether it shows 120 frames per second on
displays of 100 Hz and more (on by default). They are kept in
`~/.config/ttt2/display.ini` (`$XDG_CONFIG_HOME/ttt2/`; on Windows
`%APPDATA%\ttt2\display.ini`, or `<install>\data\display.ini` for an
install made by the Windows installer), which can be edited by hand too (on
Windows, TTT2 Ultimate Launcher's Graphics tab writes it):

```ini
fullscreen = no             # yes or no
resolution = auto           # auto, 1280x720, 1920x1080, 2560x1440, 3840x2160 or 5120x2880
interpolation = yes         # yes or no (no: 60 frames per second)
frame_rate = auto           # auto, 60, 120, 180 or 240 (over interpolation; optional)
anisotropic = game          # game, 2, 4, 8 or 16: textures' anisotropic filtering at least this (optional)
```

The game reads it when it starts, so a change applies the next time. The
window opens at the chosen resolution when that fits on the desktop,
otherwise smaller (the picture is scaled to the window or the screen).
Uninstalling keeps it, as it keeps `keyboard.ini`. The environment
variables below win over it.

| Environment variable | Effect |
| --- | --- |
| `TTT2_SCALE=1/1.5/2/3/4` | render scale: 1280x720 times this (default from `display.ini`, else from the display) |
| `TTT2_FPS=60/120/180/240` | frames shown per second (default 120 on displays of 100 Hz and more, unless `display.ini` turns it off) |
| `TTT2_FULLSCREEN=1` | start fullscreen (another value: in a window) |
| `TTT2_AUDIO=0` | no sound |
| `TTT2_AUDIO_VOLUME=0..100` | volume in percent (default 100) |
| `TTT2_SAVE_DIR=<folder>` | where saves go |
| `TTT2_CONTROLLER=playstation/nintendo/xbox` | icons and menu buttons for this kind of controller, whichever is used |
| `TTT2_CHARACTERS=<folder>` | where character packs are (default `$XDG_DATA_HOME/ttt2/characters`, else `~/.local/share/ttt2/characters`; on Windows `%APPDATA%\ttt2\characters`) |

Debugging variables are listed in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#running-the-port).

## Adding fighters

The fighters of Tekken 7 that Tekken Tag Tournament 2 does not have can join
the roster beside its own (no fighter is replaced): Kazumi Mishima, Lucky
Chloe, Eliza, Claudio Serafino, Akuma, Gigas, Josie Rizal, Katarina Alves,
Master Raven, Shaheen, Fahkumram, Leroy Smith, Lidia Sobieska, Kunimitsu II,
Negan, Geese Howard and Noctis. Each gets a cell of its own on the select
screen: the free ends of the top row first, then new columns beyond the rows'
ends (19 cells; the order follows the order of import).

Nothing of them is in this repository: an importer makes them from **your own
copies of both games** and writes character packs on your computer. The
importer runs on Linux only (it starts Tekken 7 through Steam's Proton); the
packs it makes are plain files, read by the Windows build from
`%APPDATA%\ttt2\characters`. You need:

- Tekken 7 for PC (Steam), installed and started from Steam at least once
  (the importer finds it, its Proton and its prefix through Steam), and Steam
  running: the Steam of your distribution's packages or Valve's installer
  (Steam from Flatpak or Snap is found, but not supported yet);
- the game installed with the launcher or `ttt2 --install` (the importer
  reads its folder);
- gamescope, `pactl` (PipeWire or PulseAudio), MinGW
  (`x86_64-w64-mingw32-gcc`, to build a small helper the first time) and
  Python 3 with numpy and Pillow.

```bash
python3 tools/characters/import_character.py --all --game /path/to/your/install
```

A fighter's moveset exists only inside the running Tekken 7, so the importer
first saves the movesets it does not have yet: Tekken 7 starts in the
background, with no window and no sound (gamescope's headless mode and a
silent audio output of its own), the game's own loader loads the fighters
while it sits on its start-up screens, and it is closed again. Nothing is
pressed and no menu is used, so your key bindings and settings do not matter.
Stopping the importer (Ctrl+C, or Cancel in the launcher) closes the hidden
Tekken 7 too and leaves no half-written pack; a Tekken 7 you start yourself
is left alone.
Only fighters your Steam account owns are imported (Steam must be running; it
tells which DLC you own): a fighter whose DLC you have not bought is skipped,
whichever movesets are already saved. Tekken 7 also keeps Geese's and
Noctis's costumes (and Noctis's voice) encrypted in its files: in the same
run the game reads those files with its own file reader, the one its loading
uses, which hands them over decoded, and they are saved to
`~/.local/share/ttt2/tekken7/files/` (about 670 MB for both) for the
importer. `tools/characters/dump_tekken7.py` does that step alone (the
movesets go to `~/.local/share/ttt2/tekken7/movesets/`); movesets saved with
[TKMovesets2](https://github.com/Kiloutre/TKMovesets) work too
(`--movesets FOLDER`).

The fighters' names for the importer: `kazumi`, `chloe`, `eliza`, `claudio`,
`akuma`, `gigas`, `josie`, `katarina`, `master_raven`, `shaheen`,
`fahkumram`, `leroy`, `lidia`, `kunimitsu2`, `negan`, `geese`, `noctis` (one or several; each
takes about a minute, and what they share is made once at the end). The packs
go to `~/.local/share/ttt2/characters/` (or `$XDG_DATA_HOME/ttt2/characters/`:
a folder per fighter, and `shared/`, the title's pictures, colours and name
calls the added fighters share; `$TTT2_CHARACTERS` picks another folder).
`--remove NAME...` removes fighters again; the others keep their cells.
Each pack names the pack format it was made in. The game loads only packs of
its own format and skips older and newer ones (it says so on its console):
add an older pack's fighter again (the launcher offers it), and it comes back
in its own cell. A pack with no format line counts as format 1, unless it has a
`keys/` folder of its own: those are the earliest packs (made before
`shared/` existed), which must be added again.
**The packs hold Namco's assets: keep them to yourself and never upload or
share them.** The importer refuses to write them inside this repository, and
`.gitignore` keeps the games' file types out of commits.

Each has their own model with both players' colours (skin tones matched to
Tekken Tag Tournament 2's own fighters), moves, voice, command list, and their
pictures and name on the menus' screens and in fights; Kazumi's tiger appears
in the moves that summon it, long hair and tails hang and swing, their moves'
effects (Eliza's and Akuma's fireballs, auras, smoke) are Tekken 7's own,
converted to Tekken Tag Tournament 2's effect files (close, not the same:
Tekken 7's glow and some of its shading are left out), Negan swings his bat
and hits with it, Noctis summons his swords, daggers, spear and shield for the
moves that use them, Leroy's cane and Master Raven's sheathed katana are where
they carry them, and the moves that hit with a weapon (Kunimitsu II's blades
too) reach as far as the weapon. Left out for now: Rage Arts and Rage Drives,
power crush armour (those moves are there, without it), Screw becomes Bound,
the weapons they take in hand only for some moves (Kunimitsu II's blades,
Master Raven's drawn sword, Leroy's folding staff, Negan's knife), the weapons
of Noctis's Rage Art, Katarina's and Leroy's sunglasses, and Gigas's back
tubes.

### Bound moves

Tekken 7 has no tag, so the importer adds what Tekken Tag Tournament 2's tag
system needs: they tag out (and have tag throws) while walking, dashing,
stepping back, crouching or crouch walking, as Tekken Tag Tournament 2's own
fighters do; press Tag during a bound move's startup (before it hits) for a
Tag Assault, and their juggles give the Tag Crash input on landing (2+Tag
with the partner in Rage). Many of Tekken Tag Tournament 2's bounds are
slams in Tekken 7 that bounce on every air hit; those bound once per juggle,
as Tekken Tag Tournament 2's own do (right column). Inputs in Tekken 7's
notation (WR while rising, SS sidestep, BT back turned, FC full crouch):

| Fighter | Bound moves | Slams in Tekken 7, bounds here |
|---|---|---|
| Kazumi | f+4; b+2; 3,1; u/f+3,4,3; b,f+2,1,4; WR 4,4 | d/f+1,2; d/b+1+2; f+3+4,3; u/f+3,4,1 |
| Lucky Chloe | 1+2; 1,2,1+2; f+2,1+2; BT 1+2; d/f+3,3; b+3; FC d/f+2; d/f+2,4,3,1; 10-hit string | 1,2,2; f+2,2; BT 3,3+4; d/f+3,2; f,f,f+3; b+2 |
| Eliza | 4; f,f,f+1; b+3,1; d,d/b,b+1; Moon Glide 1,4 / 3,4 | u/f+4,2; b+1,2 |
| Claudio | f+2,2; 4,3; 2,1,2; f+3; b+1; b+4,1; WR 1,2 | b+1+2; b+4,3,2; b+3,3; f,F+2,2; jump 2 |
| Akuma | f+4; 4,3; Hyakkishu (f,d,d/f+3 or 4) 3 | Hyakkishu 2; f,f,f+3+4 |
| Gigas | f+2,f+1; d/f+2,1; d/b+3,2; Golem Set Up 4,1; SS 2 | f+2,1,1; d+2; b+1+2; WR 4,2; d/f+4,1+2 |
| Josie | 3,3; b+1,1; f+2,3,4; f,F+4; 1,2,d+4,4; 2,d+4,4; 3+4; f,n,d,d/f+1,3 | jump f+3; d+2; d/f+4,1; b+3+4,f,1+2 |
| Katarina | 2,2; 3,4 (also WR); d/f+2,4; f,F+4; Harrier d/f+2,3; f,F+2,4 | f+4,2; f+4,u/f+3; f,F+3; d+1; u/f+3+4; b+4,3+4; f,f,f+3+4; 1+2,1,3; 3,3,3,3,3 |
| Master Raven | b+4,4; d/f+2,3; b+2,4,2; BT f+2,4,2; 2,4; f+1+2; 4,3,3; WR 3+4; BT b+3; BT f+4,3 | b+1; b+4,b+4; BT 3,4,4; jump f+3+4 |
| Shaheen | 4,3; f+2,4,4; f+4; d/f+3; Stealth Step 4,1 | 4,4; b+1; f,f,f+3+4; Stealth Step 3 |
| Fahkumram | 3,4,4; f+3,2,4,4; WR 2,4,4; 4,3,1; 1,2,4,3,1; 2,4,3,1; d+3,4,3,1; f,F+4; b+3; d/f+1,4 | b,f+3+4; d/f+2,1; d/f+4,3,3 |
| Leroy | 1,2,3; f+4,4; b+3; b+4; Hermit 2,1; Hermit f+1 | Hermit b+1; d+1+2 |
| Lidia | 1,2,3; 1,2,4,4; 4,3; f+4,3,4; d/f+4,3; d/f+4,4; d/f+3+4; d+3,4; b+4,4,4; b+3+4,3; u/f+4; SS 4; Cat Foot 3; 10-hit string | f+3+4,4; f+4,3,1; d+1+2 |
| Kunimitsu II | 1,1,1; 3,4; Katon 4,2; f+2,3; Setsunagake 1,2,1; f,n,d,d/f+2; f+3,2; f+3,1,2; f+3+4; b+2; WR 2,4 | d,d/f,f+2; f,F+4,2; d/f+1,2,2; d+1+2; 1,1,4; Setsunagake 1,2,4; f+3,1,2,4 |
| Negan | d/b+1+2 (also held); d/f+1,1,2; b+1,2; SS 2; Intimidation 1,2 / 1+2 | f+1,2,1+2; 2,2,2,2; b+2,2; b+1+2; d/f+3+4,1,2; Intimidation 2,3 |
| Geese Howard | 4; d/f+2; b+4,4; WR 3; f,d,d/f+1 / +2 | d,d/b,b+2,3 (and its EX version) |
| Noctis | 1,2,1+2; 2,2,f+2; 1+2; f+4,2; d/f+1,4; d/f+4,2,4; d/f+1+2; d+2,2; u/f+4; u+4; f,F+1+2; WR 2,2 | u+2; u/f+2; jump 1+2; b+2+3; b+1+4; d+4,1,1+2; d/f+4,2; f,f,f+1+2 |

## How it works

- `recomp/`: `cafe-recomp` loads the RPX, finds all 205,200 function entry
  points and writes the 79,564 reachable ones as C++ functions (`--all`
  writes every one). Guest calls are C++ calls; indirect calls go through an
  address table.
- `runtime/src/os/`: the Cafe OS libraries the game imports (threads,
  synchronisation, memory, file system, saves, input, sound mixing), written
  natively.
- `runtime/src/gx2/` and `runtime/src/gpu/`: GX2 writes real GPU command
  buffers, which a command processor executes; the Vulkan renderer
  (`runtime/src/gpu/vulkan/`) translates the GPU's shaders to SPIR-V
  (`runtime/src/latte/`), interpolates frames and paces presentation.
- `runtime/src/title/`: what this game shows differently on a computer: the
  menu buttons, the icon sets, texts and the title screen, made at start-up
  from the game's archives (their format is described in
  [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#presentation-on-a-computer)),
  and the added fighters from their character packs.
- `tools/characters/`: the importer that makes a character pack from the
  player's own Tekken 7 and Tekken Tag Tournament 2
  ([`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#added-fighters)).
- `tests/`: a memory-model test, tests of reading split archives and of the
  game's file formats and the icons and texts made from them, and
  differential tests that run every instruction form the game uses against
  Dolphin's PowerPC interpreter.

### Changing the game

A generated function `sub_<address>` can be replaced by defining it in a
`.cpp` file added to the `ttt2` target (`target_sources(ttt2 PRIVATE ...)` in
`CMakeLists.txt`); every call, direct or through a function pointer, then
reaches the replacement, and `sub_<address>_orig` is still the original:

```cpp
#include "cafe/ppc_ops.h"

PPC_FUNC(sub_0123ABCD_orig);
PPC_FUNC(sub_0123ABCD) {
    // ... before
    sub_0123ABCD_orig(ctx, base);
    // ... after
}
```

## Tests

```bash
cmake --build build      # also builds the tests
ctest --test-dir build
```

runs `memory_model_test`, `wua_layers_test`, `title_test` and, with Python 3,
numpy and Pillow, `character_import_test` (the importer's formats). The
differential tests (`semantics_test`, `control_flow_test`) also need `fmt`,
the game's `Tekken.rpx` (by default
`local/wua/000500001010f800_v16/code/Tekken.rpx`; otherwise configure with
`-DTTT2_RPX=<path>`), and a partial checkout of Dolphin:

```bash
git clone --filter=blob:none --no-checkout https://github.com/dolphin-emu/dolphin.git third_party/ref/dolphin
git -C third_party/ref/dolphin sparse-checkout set --no-cone \
  /Source/Core/Core/PowerPC/Interpreter/ /Source/Core/Core/PowerPC/Gekko.h \
  /Source/Core/Core/PowerPC/PowerPC.h '/Source/Core/Core/PowerPC/ConditionRegister.*' \
  /Source/Core/Common/BitField.h /Source/Core/Common/BitUtils.h /Source/Core/Common/CommonTypes.h \
  '/Source/Core/Common/FloatUtils.*' /Source/Core/Common/Inline.h /Source/Core/Common/Swap.h
git -C third_party/ref/dolphin checkout 771fb154059c5812d6a715a23226249cc42877a2
cmake -B build && cmake --build build && ctest --test-dir build
```

They build Dolphin's interpreter (GPL-2.0 or later) into the test programs;
nothing of Dolphin is stored in this repository.

## License

This repository's code is under the [MIT License](LICENSE). It does not cover
the game, or anything generated from it.

Fetched by `tools/setup_deps.py`, under their own licenses:
[ZArchive](https://github.com/Exzap/ZArchive) (MIT-0) and AMD's address
library ([decaf-emu/addrlib](https://github.com/decaf-emu/addrlib), MIT,
built into the `ttt2` program). The port also links SDL3 (zlib), shaderc
(Apache-2.0), the Vulkan loader (Apache-2.0), OpenSSL (Apache-2.0), zstd
(BSD) and zlib.

## Acknowledgements

- [Cemu](https://github.com/cemu-project/Cemu) and
  [decaf-emu](https://github.com/decaf-emu/decaf-emu), whose reverse
  engineering of the Wii U's system libraries and GPU documented much of the
  behaviour reimplemented here (structure layouts, register values, formats,
  GX2's surface layout rules). This repository's code is its own.
- [Dolphin](https://github.com/dolphin-emu/dolphin), whose hardware-tested
  PowerPC interpreter is the reference the instruction tests compare
  against, and whose hardware research measured the processor's estimate
  tables.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and
  [N64Recomp](https://github.com/N64Recomp/N64Recomp), for showing how far
  function-level static recompilation can go.
- [TKMovesets2](https://github.com/Kiloutre/TKMovesets) and
  [TekkenMovesetExtractor](https://github.com/Kiloutre/TekkenMovesetExtractor),
  whose research documented the Tekken 7 and TTT2 moveset layouts, their
  animations and what their requirement and property ids mean, which the
  character importer builds on (its movesets are in TKMovesets2's .tkmvst
  layout, which it can also read);
  [UE Viewer](https://github.com/gildor2/UEViewer) and
  [CUE4Parse](https://github.com/FabianFG/CUE4Parse), for Tekken 7's Unreal
  Engine 4.14 packages. The importer's code is its own.
