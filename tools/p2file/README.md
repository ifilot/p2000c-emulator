# P2FILE

P2FILE is a two-panel file manager for CP/M 2.2 on the Philips P2000C. It is
written entirely in Intel 8080 assembly and uses only documented BDOS calls and
P2000C terminal controls.

The bundled `images/p2file.flp` development disk contains `ASM.COM`,
`LOAD.COM`, `P2FILE.ASM`, and the generated `P2FILE.PRN`, `P2FILE.HEX`, and
`P2FILE.COM`. Boot CP/M from `images/system.flp`, mount the development disk as
B:, and rebuild or run it on the emulated machine:

```text
A>B:
B>ASM P2FILE
B>LOAD P2FILE
B>P2FILE
```

For a minimal compiled floppy, run:

```sh
./tools/p2file/build.sh
```

The default output is `images/p2file.flp`. An alternative output path can be
passed as the first argument. The script creates a clean disk with only
`ASM.COM`, `LOAD.COM`, and `P2FILE.ASM`, compiles and links inside the emulator,
launches `P2FILE.COM` as a check, and writes a final disk containing those three
inputs plus `P2FILE.PRN`, `P2FILE.HEX`, and `P2FILE.COM`.

The graphical emulator exposes the bundled image under **Media > Drive A/B >
Use P2FILE Development Floppy**. Selecting it mounts a writable session copy,
leaving the bundled template unchanged.

The left and right panels initially show A: and B:. Available keys are:

- `Tab`: activate the other panel
- `W` / `S`: move the cursor up or down
- `Space`: mark or unmark a file; multiple files may be marked
- `C`: copy marked files to the drive in the other panel; if nothing is marked,
  copy the current file
- `D`: delete marked files after confirmation; if nothing is marked, delete the
  current file
- `R`: rename the current file (CP/M 8.3 names)
- `V`: open the drive menu for the active panel; press `A` through `F` to
  select a drive, or Escape to cancel
- `Q`: return to CP/M

Copying never silently replaces a file: P2FILE asks `Overwrite ...? (Y/N)` for
each destination name that already exists. While copying, a centered
inverse-video modal shows the current filename and its position in the batch.
The completion message uses the same modal, leaving the bottom key table
visible; the next key dismisses the message and performs its normal command.
Messages and input prompts that use the bottom row are also temporary. Once
input is complete, the next command key restores the inverted key table before
performing its normal action.

Each panel header shows the drive's total file count, the current file number,
and `^`/`v` indicators when more files exist above or below the visible window.
Moving with `W` or `S` scrolls that panel automatically. Each file row shows its
rounded-up size in KiB and the CP/M `R` (read-only), `S` (system), and `A`
(archive) attributes; unset attributes appear as `-`.

Panel headers and the active panel's current `>` row use the P2000C terminal's
inverse-video attribute. The inactive panel has no cursor marker or highlighted
row. Ordinary cursor movement updates only the old row, new row, and changing
header fields. Crossing a window boundary redraws only that panel's file rows;
it does not clear and repaint the complete screen.

The compact 24-row layout puts both drive headers at the top, provides 22 file
rows per panel, and keeps the inverted key table on the bottom row. It has no
separators, program-title row, or filename-column legend.
