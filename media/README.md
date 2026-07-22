# Bundled CP/M media sources

This directory contains the inputs used by `tools/build_media.py`; the runtime
images are generated under `images/`.

`files/core` and `files/zork` come from
[`ifilot/p2000c-cpm-disk-tool`](https://github.com/ifilot/p2000c-cpm-disk-tool),
tree `0fc92485faba14490d8ea5a1a6d58f0f37272ade`. The upstream project extracted
the files from P2000C CP/M HDA images while retaining complete 128-byte CP/M
records.

`files/chess/CHESS.COM` was extracted from the former `p2kc_sys.imd` image. The
IMD sectors were first flattened by cylinder/head/sector ID and then decoded
through the P2000C 640 KiB odd/even sector translation. Its SHA-256 is
`c3ca83da047c406cd586e76e801ddd28a9be178db730bca6435f3f423f9ed9d6`.

The system area started from upstream `system/hdboot-split.trk`. Three changes
produce `system/p2000c-ab-cdef.trk`:

- boot byte 3 selects cylinder 0/head 1 as the second floppy system track;
- the logical device table and matching 16-byte descriptors are reordered from
  hard/floppy/floppy/hard/hard/hard to floppy/floppy/hard/hard/hard/hard;
- the fixed environment-display text is reordered to match A through F.

The resulting system-track SHA-256 is
`878ffd34cd8a94c404ae402744ed6fde8d2de2607455e853081c411bbc427820`.

`images/ipldump.flp` is a non-bootable development disk containing the archived
`ASM.COM` and `LOAD.COM` together with the repository's `tools/IPLDUMP.ASM`.

Generated image hashes:

- `images/system.flp` (655,360 bytes):
  `269cba112f764c45ee056c96f2dbb5dbb42ae8cd808dcb40d7d4ac70e579ad01`
- `images/zork.flp` (655,360 bytes):
  `a7c76be53dd45c4c0cee3972e15d966d22f6f6c955a2d00eee657eb35e710cd0`
- `images/chess.flp` (655,360 bytes):
  `ddc322f87aac4fe90234e73902ede8ee2d30ade4baafe8c50423a9a380fbd56b`
- `images/ipldump.flp` (655,360 bytes):
  `7e0db1e195721c663864e25731e6ed552bb4449322843b8e00bf3eaee4f45770`
- `images/blank.hda` (10,485,760 bytes):
  `723d9bfe581069161b9b96396a3e990c93325ce6e277280be9abec27731f5d8a`

The CP/M programs and games are machine assets, not relicensed by the
emulator's GPL declaration. Their redistribution remains subject to the rights
applicable to each program.
