# Philips P2000C emulator

An early C++/Qt 6 emulator for the Philips P2000C portable computer.

The current prototype boots the supplied CP/M system floppy with the authentic
4 KiB mainboard IPL. It integrates a complete Z80 interpreter and the IPL ROM
overlay, an initial Intel 8257/uPD765 floppy path, vectored interrupts, and a
high-level replacement for the serial terminal board. The Qt application
renders the terminal's exact 80x24 character buffer with the supplied
256-character P2000C font and accepts keyboard input. Per-cell terminal
attributes provide inverse video, four phosphor intensity levels, underline,
and blink. The display renderer preserves the 640x288 timing raster, calibrated
non-square dot proportions, visible phosphor scanline gaps, restrained bloom,
and a blinking block cursor.

This is deliberately still a minimal prototype. The floppy controller covers
the commands needed during startup, terminal control-code support is partial,
and SASI hard-disk boot, printer, communications, sound, and the terminal-board
Z80 are not implemented yet.

## Requirements

- CMake 3.24 or newer
- A C++20 compiler
- Qt 6.4 or newer (`Core`, `Gui`, and `Widgets`)

On Debian/Ubuntu, the Qt development package is commonly named `qt6-base-dev`.
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

The verified IPL dump is embedded in the application. Use **Media > Drive A >
Use CP/M 2.2 System Floppy** to mount and boot the included system disk without
locating it on the host. A persistent writable copy is created in the user
application-data directory, leaving the bundled master intact. Each drive menu
also provides a blank 640 KiB data floppy and an **Open Image...** action for
external ImageDisk files.

The Media menu is organized by drive and shows each mounted filename directly
in the Drive A and Drive B submenu titles. Matching bundled images are checked,
and persistent status-bar indicators show both drives at a glance; hover an
indicator or the mounted-image menu entry to see the full path.

The **View > Display Resolution** menu offers fixed, aspect-correct display
sizes from 560x288 through 1680x864. The selected size is remembered between
runs; 1120x576 is the default.

The **Settings > Screen Color...** panel provides a color wheel and brightness
control for tuning the CRT phosphor color. It previews changes live, restores
the previous color when canceled, and remembers accepted colors between runs.

The **Machine > Emulation Speed** menu paces emulated time against a monotonic
host clock. Authentic speed is four million Z80 T-states per second; selectable
scales range from 1 MHz/25% through 16 MHz/400%. Scaling accelerates or slows
the complete emulated machine, including its timer interrupt cadence.

Changes made by future emulated write commands will be applied directly to the
selected working image, so keep a backup of valuable external media.

Inspect an IMD image without starting Qt:

```sh
./build/p2000c_media_info images/p2kc_sys.imd
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
