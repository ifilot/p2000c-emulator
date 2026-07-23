# P2000C hardware notes

These notes capture the subset of the service manual that drives the current
implementation. Page references use the manual's printed section/page numbers,
not PDF page indices.

## Architecture

The P2000C contains two independent Z80 systems:

- The mainboard uses a 4 MHz Z80A, 64 KiB RAM, a 4 KiB IPL ROM, an Intel 8257
  DMA controller, a uPD765 floppy controller, SASI, two Z80 CTCs, and a Z80 SIO.
- The terminal board uses a 3.072 MHz Z80A, an MC6845, two 16 KiB RAM banks,
  firmware ROM, an 8251A USART, keyboard logic, and a 4 KiB character ROM.
- The boards communicate through a serial link. The mainboard BIOS performs a
  two-character handshake before proceeding with IPL.

Sources: service manual sections 2.1/3-1 through 3-3, 3.2/1-1, and 3.3/1-1.

The prototype currently emulates the serial boundary of the terminal board,
not its second Z80. Mainboard bytes sent through SIO-B/DMA channel 3 are
interpreted by a high-level terminal, and keyboard bytes are returned through
the receive side of the same SIO channel. This preserves the mainboard firmware
interface—including its character and graphics commands—while leaving
cycle-accurate terminal-board emulation for a later milestone.

## Mainboard memory manager

After reset, reads from `0000`-`0fff` select the IPL ROM while writes still go
to underlying RAM. Output port `1e` controls the mapping:

- value `00`: IPL ROM at `0000`-`0fff`, internal RAM elsewhere;
- value `02`: internal RAM throughout the address space.

Source: service manual section 3.2/7-1 through 7-3.

## Mainboard I/O map

| Ports | Device |
|---|---|
| `00`-`08` | Intel 8257 DMA |
| `14`-`15` | Printer 8251A USART |
| `16`-`17` | SASI data |
| `18`-`19` | SASI control |
| `1a` | uPD765 main status register |
| `1b` | uPD765 data register |
| `1e` | Memory manager, write-only |
| `1f` | Floppy/SASI output port, write-only |
| `20`-`23` | CTC I |
| `24`-`27` | CTC II |
| `28`-`29` | Communications SIO channel |
| `2a`-`2b` | Terminal SIO channel |
| `30` | CoPower 8088 interrupt vector, write-only |
| `31` | CoPower reset/run/common-memory control, write-only |

Port `1f` bit 4 resets the uPD765 when low, bit 5 controls the motor, bit 6
enables drive 4 selection, and bit 7 selects SASI instead of the floppy
controller. The floppy controller and SASI share DMA channel 0 and CTC II
interrupt channel 3.

Sources: service manual sections 3.2/6-3 through 6-4 and 3.2/11-4 through 11-5.

## P2093 CoPower board

The optional Philips P2093 8088 CoPower board adds a 5 MHz 8088 and up to
512 KiB of local RAM. The implemented configuration installs the full 512 KiB.
Physical 8088 addresses `00000`-`7ffff` select that private RAM;
`f0000`-`fffff` alias the mainboard Z80's complete 64 KiB RAM. The Z80
continues to own the P2000C peripherals and services requests made by the
MS-DOS-side BIOS.

The CP/M utilities control the board through two output ports:

- port `30h` supplies an interrupt vector and asserts the 8088 interrupt;
- port `31h`, value `1`, resets and holds the 8088;
- port `31h`, value `2`, releases or resumes the 8088;
- port `31h`, value `3`, locks the 8088 out of common memory until value `2`.

An 8088 `OUT` instruction asserts the reciprocal Z80 interrupt expected at IM2
vector `dch`. The CPUs run concurrently at instruction boundaries. Scheduling
uses their documented 5:4 clock ratio with a nominal 8088
clocks-per-instruction estimate; individual bus cycles are not modeled.

The board currently behaves as if no 8087 is installed. Its 8088 consumes 8087
escape instructions without performing a coprocessor operation, which is the
hardware-visible behavior needed by the original diagnostic's absent-8087
path. The original `TEST88.COM` short-memory, all-vector bidirectional
interrupt, DRAM refresh, and common-memory lock loops pass.

`MSBOOT.COM` loads and starts the MS-DOS-side code through common memory and
the reciprocal interrupts. The original Z80-resident CoPower BIOS continues to
service P2000C hardware for the 8088, including floppy requests, so no invented
host BIOS interface is needed. The supplied MS-DOS 2.11 disk boots through
this path and reaches its command prompt.

The desktop's ordinary CP/M startup arrangement includes two unformatted,
all-`e5` SASI templates. They are useful as writable CP/M media but contain no
valid CoPower DOS hard-disk metadata. Selecting a bundled CoPower floppy
therefore installs the board automatically and ejects only those disposable
templates. Persistent user-mounted HDA images are not removed.

Source: P2093 CoPower Board User Manual and the original Philips `TEST88.COM`
diagnostic on the CoPower CP/M boot disk.

## Floppy boot

The IPL reads cylinder 0, head 0 into address `d600`. Byte 3 determines whether
another 4 KiB track is required: `80` means no second track, `81` loads cylinder
1/head 0 at `e600`, and `82` loads cylinder 0/head 1 at `e600`. The supplied
disk uses `82` and contains 80 cylinders, two heads, 16 sectors per track, and
256 bytes per sector (655,360 decoded bytes).

The disk-loaded code is not self-contained. Before transferring control, the
IPL installs firmware driver entry points at `f606` and a driver parameter block
referenced by `ffd0`. The supplied boot track calls these services. Authentic
boot therefore requires the mainboard IPL ROM rather than only the disk image.

The memory manager is selected through bits 0 and 1 of output port `1EH`.
Writing `00H` makes the IPL ROM readable at `0000H`-`0FFFH` while writes still
reach the underlying internal RAM; writing `02H` restores internal RAM over the
whole address space. This permits a routine executing above `0FFFH` to expose,
copy, and then hide the IPL ROM without resetting the machine.

The current uPD765 model implements reset, specify, sense-interrupt,
recalibrate, seek, read-data, and write-data behavior used by the supplied
systems. Raw FLP geometry is inferred from the exact file size:

| Format | Cylinders | Heads | Sectors/track | Sector bytes | Raw size |
|---|---:|---:|---:|---:|---:|
| P2000C CP/M | 80 | 2 | 16 | 256 | 655,360 bytes |
| P2093 CoPower MS-DOS | 80 | 2 | 10 | 512 | 819,200 bytes |

Bytes are stored by cylinder, then head, then ascending sector ID. The CP/M
filesystem applies its documented odd/even sector translation above that
physical ordering. The manual's `STAT` output reports 632 KiB of CP/M record
capacity: its raw medium is still exactly 640 KiB, with two 4 KiB physical
tracks reserved for the boot system. The MS-DOS boot sector's BPB records its
512-byte sector and ten-sector track geometry.

Sources: service manual sections 2.1/3-2 through 3-4, 2.1/3-5 through 3-13,
and 3.2/7-1 through 7-4.

## SASI hard disks

The P2000C SASI data and control registers occupy ports `16`-`19`. The
interface selects up to two physical disks, shares DMA channel 0 with the
uPD765, and uses CTC II channel 3 for completion interrupts. The emulator
implements the six-byte SASI commands used by Philips CP/M for readiness,
recalibration/seek, and 256-byte block reads and writes.

Each HDA is a raw 10,485,760-byte device containing 40,960 blocks. The bundled
system table exposes two approximately 5 MiB filesystems per physical disk:

| CP/M drive | Physical device | Volume |
|---|---|---|
| A | floppy 1 | 640 KiB |
| B | floppy 2 | 640 KiB |
| C | hard disk 1 | low 5 MiB |
| D | hard disk 1 | high 5 MiB |
| E | hard disk 2 | low 5 MiB |
| F | hard disk 2 | high 5 MiB |

The split is described by the system-track DPBs rather than a byte boundary.
On each HDA, the low directory begins at LBA 32 and the high directory at LBA
19,584. The low filesystem has `DSM=1221, OFF=2`; the high filesystem has
`DSM=1223, OFF=1224`.

Sources: service manual sections 2.1/3-8 through 3-10 and 3.2/10-1 through
10-4; P2519 CP/M Reference Manual section 4.7.

## Execution timing

The mainboard CPU clock is 4 MHz. The integrated Z80 core accounts for
documented T-states at instruction boundaries rather than modeling each bus
phase. The Qt scheduler uses monotonic elapsed host time to budget four million
T-states per second at authentic speed, retaining fractional T-states between
ticks. Long host stalls are capped at 100 ms of catch-up to keep the interface
responsive. User-selected speed multipliers scale the whole machine timeline;
the emulated 60 Hz interrupt therefore also scales in wall-clock time.

## Character generator

The terminal character ROM contains 256 glyphs. Each character occupies 16
bytes, of which the first 12 describe an 8x12 bitmap and the final four are
unused. Bits are shifted least-significant first. The supplied 192x192 PNG is a
16x16 character grid with 12x12 presentation cells and is embedded unchanged in
the Qt application.

The active alphanumeric raster is fixed at 80 columns by 24 rows, or 640 dots
by 288 scanlines. Only the first eight columns of each 12-pixel-wide PNG sheet
cell contain character-generator data; the other four columns are sheet
spacing and are not part of the emulated display.

Timing coordinates do not imply square physical dots on the 9-inch CRT. The Qt
presentation maps the 640x288 timing raster onto the tube's 4:3 display area,
giving its dots a 3:5 horizontal-to-vertical pitch. This preserves all 80
columns without presenting the physical screen as a modern widescreen.
Contiguous dots on one scanline are rendered as a single rounded phosphor
stroke. A subtractive gap mask keeps scanlines visible at common host
resolutions, while low-alpha additive layers provide bloom without filling the
gap. Brighter attributes produce a slightly wider beam.

The current emission image is cached separately from the glass/background.
Optional after-effects apply a two-axis barrel warp, exponentially decaying
phosphor persistence (60 ms default half-life, adjustable from 20 to 250 ms),
edge shading and a faint glass highlight, animated monochrome noise, or subtle
whole-screen refresh flicker. Persistence uses an explicit per-pixel maximum
with current emission, so a stationary trace remains bit-for-bit stable while
only extinguished phosphor decays; it never introduces flicker itself. A
separate 30–150% intensity dial models the physical monitor brightness control
independently of the selected phosphor hue. Each effect can be previewed and
persisted independently through **Settings > Screen Appearance...**. The
complete rendered CRT can also be captured as a PNG via
**View > Save Screenshot...**. The
monitor service instructions specify a 15.67 kHz horizontal frequency, but the
emulator does not simulate individual beam sweeps; temporal effects are updated
at approximately 60 Hz and presented at the host compositor's refresh rate.

Source: service manual sections 3.3/10-1 through 10-2 and Appendix A.

## Character attributes

The terminal firmware's `ESC,0,b` sequence selects an attribute byte that is
stored alongside every subsequently written character until another attribute
is selected. Its serial representation is different from the lower-five-bit
layout used internally by the terminal board's attribute RAM:

| Serial bit | Attribute |
|---|---|
| 6 | Intensity high bit |
| 5 | Underline |
| 4 | Inverse video |
| 1 | Blink |
| 0 | Intensity low bit |

The intensity values `00`, `01`, `10`, and `11` mean quarter bright, bold,
normal, and half bright respectively. Reset selects normal intensity (`40h`).
The emulator keeps the raw attribute byte per cell, moves it with characters
during scrolling, and applies inversion and the four brightness levels to the
phosphor raster. Underline and blink are also interpreted; underline occupies
the tenth character scanline and blink changes state approximately every 667
milliseconds.

Sources: service manual terminal-firmware section 2.3.2 (page 4-7) and sections
3.3/5-3 and 3.3/9-2 through 9-3.

## Graphics modes

The terminal firmware selects its three display arrangements over the same
serial command stream as text output:

| Sequence | Mode | Graphics representation |
|---|---|---|
| `ESC 5` | Medium resolution | 256×252, three intensities plus background |
| `ESC 3` | High resolution | 512×252, monochrome |
| `ESC 4` | Character mode | 80×24 text, graphics disabled |

Both graphics modes mix a 64×21 character plane into the 512×252 physical-dot
raster. Entering either graphics mode preserves and reflows the linear text
buffer, clears graphics RAM, and resets the graphics cursor and polar origin.
Returning to character mode clears both planes, as specified by the CP/M
reference manual.

The visible graphics plane occupies 252 rows of 64 bytes (16,128 bytes). Its
first 64 bytes are the top scanline. In high resolution, a byte contains eight
left-to-right pixels, most-significant bit first. In medium resolution, it
contains four double-width pixels in two parallel bit planes: bit pairs
`7/3`, `6/2`, `5/1`, and `4/0` select background, half, normal, or bold
brightness. Firmware drawing coordinates put `(0,0)` at the bottom left, so
the command decoder reverses the y coordinate when addressing graphics RAM.

The high-level terminal implements the documented Cartesian pixel/move/line
commands (`ESC D/d/m/M/v`), polar origin/pixel/move/line commands
(`ESC z/F/f/y/U/w`), and raw picture upload/download commands (`ESC r/t`).
`ESC 0 b` supplies the current intensity when a medium-resolution pixel is
set. `ESC ?` reports graphics enable/mode and the two-byte graphics cursor in
the normal 12-byte terminal status reply.

Sources: service manual terminal-firmware sections 2.3.5 through 2.3.8 (pages
4-14 through 4-20), terminal-board sections 3.3/5-3 through 5-4 and 3.3/8-2
through 8-3; CP/M reference manual sections 10.2.5 through 10.3.

## Storage mechanics and sound

Bundled FLP and HDA resources are immutable templates. The application copies
one into an automatically removed session directory every time it is selected;
the controller writes only to that copy. Saving the mounted image creates and
mounts an ordinary persistent image at the user's chosen path.

The uPD765 model reports physical activity and delays DMA/FDC completion
signals without stopping the Z80. The FD-55 timing model uses the service
manual's 300 RPM spindle, 100 ms average latency, 5 ms head load, and
BIOS-selected 6 ms step rate. SASI status and completion are similarly withheld
during the modeled hard-disk interval. These events drive the side-panel LEDs;
floppy events also drive the recorded mechanical audio. Timing can be disabled
independently; controller operations then complete immediately while their
activity events remain visible and available floppy sounds remain audible.

Floppy audio uses MAME's BSD-licensed 5.25-inch loaded-disk recordings for
spindle start, the 300 RPM running loop, spindle stop, a single head step, and a
6 ms seek train. The seek train is cropped or repeated to the requested track
distance. Because the source recordings retain between roughly 19 and 36 dB of
peak headroom, they are independently normalized to −3 dBFS when loaded before
the motor/head balance and user master level are applied. Read and write
commands do not add broadband noise: on real drives the audible components are
overwhelmingly the rotating medium and mechanical head positioning. This avoids
the conspicuously synthetic hiss produced by the earlier procedural model. MAME
currently supplies no corresponding hard-disk
mechanics sample set, so SASI activity is intentionally silent instead of using
an invented effect. A persisted master level controls floppy and beeper volume
together. The manual exposes the spindle as a direct software-controlled
`MOTON` line and specifies no hardware timeout. To prevent software that leaves
this line asserted from producing an endless loop, the audible spindle coasts
down 1.2 seconds after the last floppy operation; subsequent activity starts it
again. An explicit motor-off signal still stops it immediately.

BEL (07H) activates the terminal board's one-bit beeper. As documented in
sections 3.3/8-3 and 3.3/9-3, OUT2 gates the decoded underline-row signal to
produce approximately 1,300 Hz. The audio model synthesizes that square-wave
source with a small analogue roll-off rather than substituting the host's
generic alert sound.
