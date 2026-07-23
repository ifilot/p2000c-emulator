# P2UTIL development floppy

`build.sh` creates `images/cpm/p2util.flp`, a combined development floppy for
P2FILE and P2EDIT. It downloads the canonical CP/M assembly sources from the
`ifilot/p2000c-file` and `ifilot/p2000c-editor` GitHub repositories, adds
`ASM.COM` and `LOAD.COM`, and assembles both programs inside the emulator.

Run:

```sh
./tools/p2util/build.sh
```

The source revisions default to each repository's `master` branch. Set
`P2FILE_REF` or `P2EDIT_REF` to a tag or commit for a pinned build. The complete
raw source URLs can instead be overridden with `P2FILE_SOURCE_URL` and
`P2EDIT_SOURCE_URL`.

The resulting floppy contains the assembler, loader, downloaded `.ASM` files,
generated `.HEX` and `.PRN` files, and runnable `P2FILE.COM` and `P2EDIT.COM`
executables.
