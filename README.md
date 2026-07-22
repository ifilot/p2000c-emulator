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
partial, and printer, communications, sound, and the terminal-board Z80 are not
implemented yet.

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

The verified IPL dump and default media are embedded in the application. At
startup, drive A receives a persistent writable copy of `images/system.flp`;
drive B remains available for another 640 KiB FLP. Two persistent 10 MiB HDA
working images back C/D and E/F, each pair representing the low and high 5 MiB
CP/M volumes of one physical SASI disk.

The drive A and B menus provide the bootable core system, non-bootable ZORK,
CHESS, and IPL dump toolchain disks, a blank data floppy, and an action for
opening external `.flp` images. The hard-disk menus expose physical disk 1
(C/D) and disk 2 (E/F) and accept exact 10 MiB `.hda` images. Bundled masters
remain unchanged; all emulated writes go directly to per-user working copies.
Bundled floppy copies are kept in content-versioned directories, so an updated
master is mounted without overwriting a writable copy made by an older emulator
version.

The Media menu shows each mounted filename directly in its submenu title.
Matching bundled images are checked, and status-bar indicators cover A, B,
C/D, and E/F; hover an indicator or mounted-image entry for the full path.

The **View > Display Resolution** menu offers fixed, aspect-correct display
sizes from 560x288 through 1680x864. The selected size is remembered between
runs; 1120x576 is the default. **View > Save Screenshot...** (Ctrl+Shift+S)
saves the complete rendered CRT, including the selected optical effects, as a
PNG image.

The **Settings > Screen Appearance...** panel provides a color wheel and
brightness control for tuning the CRT phosphor color. It also independently
controls scanline separation, phosphor bloom and persistence, tube curvature,
glass/edge shading, and subtle analogue noise. The persistence half-life is
adjustable from 20 to 250 ms and defaults to a relatively crisp 60 ms. Changes
are previewed live, restored when canceled, and remembered between runs.

The **Machine > Emulation Speed** menu paces emulated time against a monotonic
host clock. Authentic speed is four million Z80 T-states per second; selectable
scales range from 1 MHz/25% through 16 MHz/400%. Scaling accelerates or slows
the complete emulated machine, including its timer interrupt cadence.

Emulated floppy and SASI writes are applied directly to the selected working
image, so keep a backup of valuable external media.

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
