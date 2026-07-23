# Utilities

Each utility is kept in its own directory:

- `build_media/`: deterministic CP/M disk-image builder
- `ipldump/`: P2000C IPL ROM dumping utility and reference ROM image
- `media_info/`: raw FLP/HDA image inspector
- `p2000c_cli/`: headless emulator command-line frontend
- `p2util/`: combined P2EDIT/P2FILE development-floppy builder

See the utility-specific README files for controls and on-machine build
instructions.

P2EDIT and P2FILE are maintained in the standalone `p2000c-editor` and
`p2000c-file` repositories. This repository retrieves their canonical sources
when building the shared `images/cpm/p2util.flp` artifact.
