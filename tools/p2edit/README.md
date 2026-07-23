# P2EDIT

P2EDIT is a small full-screen text editor for CP/M 2.2 on the Philips P2000C.
It is written entirely in Intel 8080 assembly and uses documented BDOS calls
and P2000C terminal controls. Its compact header, status line, two-row shortcut
footer, direct text entry, and modified-buffer prompts are inspired by
[X16 Edit](https://github.com/stefan-b-jakobsson/x16-edit) and GNU nano.

Build the development floppy with:

```sh
./tools/p2edit/build.sh
```

Boot CP/M from `images/cpm/system.flp`, mount `images/cpm/p2edit.flp` as B:,
and run:

```text
A>B:
B>P2EDIT
B>P2EDIT NOTES.TXT
B>P2EDIT A:README.TXT
```

The graphical emulator exposes the image under **Media > Drive A/B > CP/M
Images > P2EDIT Development Floppy**. It mounts a writable session copy and
leaves the bundled template unchanged. `TEST.TXT` is the short file created by
the build script's write/read smoke test.

Ordinary typing and deletion update only the affected text row and the changing
header. Text is repainted in full only when an edit changes line structure or
the viewport scrolls; the static shortcut footer is left untouched. The header
shows the current and total logical line count as `Ln current/total`.

The text area is managed as a cursor-centered gap buffer. On the first local
edit, the file suffix is moved once to the top of the 36 KiB text area and all
free memory becomes the gap. Further typing, Backspace, Delete, and horizontal
cursor movement operate at the gap edges without shifting the remainder of the
file. Saving, searching, cutting, and vertical navigation compact the two
occupied segments when they need a contiguous stream.

Logical lines are not limited to the 80-column display and P2EDIT never inserts
an automatic line break. Crossing a horizontal edge moves the viewport in
40-column chunks, keeping the cursor near the middle and avoiding a full
viewport repaint for every subsequent character. During ordinary editing only
the dynamic portion of the header is rewritten; the application name and
filename remain untouched.

The P2000C terminal's explicit scroll commands cannot accelerate vertical
editor scrolling safely: they scroll the whole screen even when areas are
locked, while insert/delete-line operations also shift the status and shortcut
rows below the cursor. P2EDIT therefore repaints the 20-row text viewport and
leaves the footer stable instead of using those commands.

To rebuild on the emulated or physical machine:

```text
B>ASM P2EDIT
B>LOAD P2EDIT
```

## Keys

The dedicated P2000C cursor, Home, and End keys move through the text. The
editor also recognizes:

- `Backspace` / `Delete`: delete left of or under the cursor
- `Ctrl-Left` / `Ctrl-Right`: move to the previous or next word
- `Return`: insert a line break
- `Tab`: insert spaces through the next eight-column tab stop
- `Ctrl-G` or `Escape`: help
- `Ctrl-O`: write the buffer
- `Ctrl-R`: open a file
- `Ctrl-N`: start a new buffer
- `Ctrl-W`: find text forward (case-sensitive)
- `Ctrl-Y` / `Ctrl-V`: previous or next page
- `Ctrl-K` / `Ctrl-P`: cut the current line or paste it
- `Ctrl-X`: exit, prompting when the buffer has changed

On original hardware the cursor keys produce the documented control bytes
`15h`, `06h`, `1Ah`, and `0Ah`. The headless emulator tests use those same
bytes. The graphical emulator maps host cursor, Home, End, Page Up/Down,
Delete, and `Ctrl`+letter keys to the corresponding editor input.

## Limits and file format

The text buffer holds 36 KiB. A larger file is opened as a read-only truncated
prefix so it cannot accidentally replace the original. Clipboard lines are
limited to 255 bytes.

The current program image ends at `194Fh`, the stack starts at `2FF0h`, and the
text area occupies `3000h` through `BFFFh`. This leaves `16A0h` bytes (5,792
bytes) between the program and initial stack pointer for stack use and future
program growth.

P2EDIT accepts CP/M text files with CR/LF or LF line endings and stops at the
usual `1Ah` text end marker. It writes CR/LF records followed by `1Ah`. Saving
first writes `$P2EDIT$.$$$` on the destination drive and only then replaces the
named file.
