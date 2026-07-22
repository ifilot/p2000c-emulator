# Philips P2000C emulator

An early C++/Qt 6 emulator for the Philips P2000C portable computer.

The current prototype boots the supplied CP/M system floppy with the authentic
4 KiB mainboard IPL. It integrates a complete Z80 interpreter and the IPL ROM
overlay, Intel 8257/uPD765 floppy and SASI hard-disk paths, vectored interrupts,
and a high-level replacement for the serial terminal board. The Qt application
renders the terminal's 80x24 character mode and its mixed 256x252 and 512x252
graphics modes, using the supplied 256-character P2000C font, and accepts
keyboard input. The documented serial graphics protocol includes pixels,
Cartesian and polar vectors, and raw picture transfer; the bundled
`CHESS.COM` exercises it directly. Per-cell text attributes provide inverse
video, four phosphor intensity levels, underline, and blink. The display
renderer preserves the hardware timing rasters, calibrated non-square dot
proportions, visible phosphor scanline gaps, restrained bloom, and a blinking
block cursor.

This is deliberately still a minimal prototype. The disk controllers cover the
commands used by the bundled CP/M system, terminal control-code support is
partial, and printer, communications, the programmable optional CTC sound
connector, and the terminal-board Z80 are not implemented yet. The standard
terminal BEL beeper and mechanical drive audio are implemented.

## Requirements

- CMake 3.24 or newer
- A C++20 compiler
- Qt 6.4 or newer (`Core`, `Gui`, and `Widgets`)
- OpenAL development files

On Debian/Ubuntu, the required packages are commonly named `qt6-base-dev` and
`libopenal-dev`.
MSYS2 MinGW users can install the corresponding `mingw-w64-*-qt6-base` and
`mingw-w64-*-cmake` packages.

## Build

The default preset selects GCC and an optimized CMake `Release` build. With
GCC, this currently produces `-O3 -DNDEBUG` for C and C++ sources.

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
```

For a GCC debug build, use a separate directory:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
cmake --build build-debug
```

Run the graphical shell with:

```sh
./build/p2000c
```

## Packages and releases

Release packages are built with Qt 6.8.3 for Windows 10 version 1809 or newer,
macOS 12 or newer, and x86-64 Linux systems compatible with the Ubuntu 22.04
build baseline. Four separate native graphical offline installers are produced:

- Windows x86-64 (`.exe`)
- Linux x86-64 (`.run`), plus a portable AppImage
- macOS Intel (`.dmg`)
- macOS Apple Silicon (`.dmg`)

The graphical installers use Qt Installer Framework and include an uninstaller
or maintenance tool. Each package contains the deployed Qt and OpenAL runtime,
all emulator resources, pristine FLP and HDA templates, the IPL dump utility,
the three PDF manuals, the README, and license and third-party notices. Linux
packages also install a desktop entry and scalable application icon.

Packages are intentionally unsigned. Windows SmartScreen and macOS Gatekeeper
can therefore show an unknown-publisher warning. On macOS, use Finder's
**Open** context-menu command to approve an unsigned download. No package
claims to carry publisher verification or Apple notarization.

The GitHub Actions workflow always builds and runs the complete test suite
before it stages or packages any platform. Pull requests, branch pushes, and
manual runs exercise the complete packaging pipeline but do not retain the
installers. Pushing a version tag that exactly matches the CMake project
version—for example `v0.1.0`—retains all five packages and publishes them in a
new GitHub Release. A version mismatch or any failed test, deployment check,
or package-content check prevents publication.

For a local graphical installer build, install Qt Installer Framework, make
its tools available on `PATH`, then run:

```sh
cmake -S . -B build-package -DCMAKE_BUILD_TYPE=Release
cmake --build build-package --parallel
ctest --test-dir build-package --output-on-failure
cpack --config build-package/CPackConfig.cmake -C Release -G IFW
```

The verified IPL dump and default media are embedded in the application. At
startup, drive A receives a disposable writable copy of `images/system.flp`;
drive B remains available for another 640 KiB FLP. Two disposable 10 MiB HDA
copies back C/D and E/F, each pair representing the low and high 5 MiB CP/M
volumes of one physical SASI disk. Selecting a bundled image always recreates
it from its pristine template.

The drive A and B menus provide the bootable core system, non-bootable ZORK,
CHESS, and IPL dump toolchain disks, a blank data floppy, and an action for
opening external `.flp` images. **Save Current Image As...** promotes the
current temporary state to a persistent image and mounts it immediately. Each
physical drive remembers its five most recent external or saved images. The
hard-disk menus expose physical disk 1 (C/D) and disk 2 (E/F) and accept exact
10 MiB `.hda` images. Bundled masters remain unchanged and session copies are
automatically removed when the emulator closes.

The Media menu shows each mounted filename directly in its submenu title.
Matching bundled images are checked. A drive panel beside the CRT covers A, B,
C/D, and E/F, identifies temporary media, and uses fading coloured LEDs for
motor, seek, read, and write activity. Borderless entries use attributed Font
Awesome floppy and hard-drive icons; hover an entry for the full path.

Storage timing follows the supplied hardware documentation: the 300 RPM TEAC
FD-55 model includes head-load, average rotational-latency, sector-transfer,
and 6 ms stepping delays, while SASI operations include early hard-disk access
latency. **Settings > Enable Hardware Sounds** controls spindle, stepper, and
the terminal's documented 1,300 Hz one-bit BEL beeper. The 5.25-inch spindle
and head sounds use MAME's authentic recordings; magnetic read/write operations
and SASI disks add no invented effects. The recordings are peak-normalized to
−3 dBFS on load, then balanced so the continuous spindle remains quieter than
head movement. The spindle sound coasts down after 1.2 seconds without floppy
activity, even when system software leaves the motor-control bit asserted.
**Settings > Hardware Sound Volume...** sets and remembers one
master level for floppy and beeper audio. **Settings >
Enable Hardware Delays** independently bypasses physical access latency while
retaining activity lights and available sounds. OpenAL is required for audio
output.

The **View > Display Resolution** menu offers fixed, aspect-correct display
sizes from 560x288 through 1680x864. The selected size is remembered between
runs; 1120x576 is the default. **View > Save Screenshot...** (Ctrl+Shift+S)
saves the complete rendered CRT, including the selected optical effects, as a
PNG image.

The **Settings > Screen Appearance...** panel provides a color wheel and
brightness control for tuning the CRT phosphor color. It also independently
controls scanline separation, phosphor bloom and persistence, tube curvature,
glass/edge shading, subtle analogue noise, and refresh flicker. Persistence is
a pixel-stable exponential afterglow; optional whole-screen flicker is a
separate, clearly perceptible effect. A dedicated 30–150% CRT-brightness dial
models the monitor's physical brightness wheel without changing phosphor hue.
The persistence half-life is adjustable from 20 to 250 ms and defaults to a
relatively crisp 60 ms. Changes are previewed live, restored when canceled, and
remembered between runs.

**Help > About P2000C Emulator...** reports the package version,
GPL-3.0-only license, runtime Qt version, implementation limitations, vendored
and runtime dependencies, and the separate or uncertain rights status of the
historical manuals, firmware, disk images, and bundled programs.

**Help > Documentation** opens the installed P2000C system/service manual and
the CP/M user and reference manuals in the operating system's default PDF
viewer. Development builds fall back to the PDFs in the source tree.

The **Machine > Emulation Speed** menu paces emulated time against a monotonic
host clock. Authentic speed is four million Z80 T-states per second; selectable
scales range from 1 MHz/25% through 16 MHz/400%. Scaling accelerates or slows
the complete emulated machine, including its timer interrupt cadence.

Emulated writes to external and explicitly saved images are applied directly,
so keep a backup of valuable user media.

Inspect a raw image without starting Qt:

```sh
./build/p2000c_media_info images/system.flp
./build/p2000c_media_info images/blank.hda
```

Rebuild all bundled images deterministically from the archived payloads:

```sh
python3 tools/build_media.py \
  --system media/system/p2000c-ab-cdef.trk \
  --core media/files/core --zork media/files/zork \
  --chess media/files/chess/CHESS.COM --ipldump tools/IPLDUMP.ASM \
  --output images
```

## Firmware

The service manual specifies a 4 KiB mainboard IPL ROM. The bootable disk's
first track assumes that this firmware has already installed its driver jump
table and parameter block in high RAM. Consequently the disk image alone is
not a substitute for the IPL ROM. The verified `tools/IPLDUMP.BIN` dump is
bundled by default; **Machine > Load IPL ROM** can replace it with another valid
4096-byte image. ROM loading verifies the Philips 16-bit checksum stored in its
last two bytes.

A CP/M `ASM.COM`-compatible utility for producing that dump on a real P2000C is
provided in [`tools/IPLDUMP.ASM`](tools/IPLDUMP.ASM). See
[`docs/rom_dump.md`](docs/rom_dump.md) for instructions.

## References used

- `manuals/P2000C-SystemRefServiceManual.pdf`
- `manuals/P2519CPM_UserGuide.pdf`
- `manuals/P2519_CPM_Reference.pdf`
- `https://github.com/ifilot/p2000c-cpm-disk-tool`

The Z80 core is vendored from `superzazu/z80`, commit
`d64fe10a2274e5e40019b1086bf7d8990cbc5f23`, under its original MIT/Expat
license. See `THIRD_PARTY.md`. It counts documented T-states at instruction
granularity and passes the Z80 instruction timing exercisers. It is not a
bus-cycle-level model; the current peripheral implementations are likewise
functional rather than electrical/cycle-exact models.

## License

The emulator's original source code is licensed under the GNU General Public
License version 3 only (`GPL-3.0-only`); see `LICENSE`. The vendored Z80 core
retains its MIT/Expat license and copyright notice.

The manuals, firmware dumps, disk images, and supplied font image are reference
or machine assets and are not relicensed by the GPL declaration for our source
code. Their redistribution remains subject to the rights applicable to each
asset.
