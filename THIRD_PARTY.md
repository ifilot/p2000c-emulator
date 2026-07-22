# Third-party software

## superzazu/z80

- Project: <https://github.com/superzazu/z80>
- Commit: `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`
- Vendored at: `third_party/superzazu_z80`
- License: MIT/Expat; see `third_party/superzazu_z80/LICENSE`
- Purpose: complete portable Z80 CPU instruction interpreter

The vendored source and its license notice are retained unchanged. The MIT
terms continue to apply to those files even though the combined emulator is
distributed under GPLv3.

## MAME 5.25-inch floppy samples

- Project: <https://www.mamedev.org/>
- Source package: MAME 0.264 (`mame-data` 0.264+dfsg.1-1)
- Vendored at: `audio/525_*.wav`
- License: BSD-3-Clause; see `audio/LICENSE-MAME-SAMPLES.txt`
- Purpose: authentic spindle start/run/stop, seek, and single-step sounds

The unmodified 44.1 kHz mono PCM recordings are the five loaded-media samples
used by MAME's 5.25-inch floppy sound device. Debian's machine-readable
copyright declaration applies BSD-3-Clause to these files.

## Font Awesome drive icons

- Project: <https://fontawesome.com/>
- Version: 7.3.1
- Vendored at: `icons/floppy-disk.svg`, `icons/hard-drive.svg`
- License: CC BY 4.0; see `icons/LICENSE-FONT-AWESOME.txt`
- Copyright: 2026 Fonticons, Inc.
- Purpose: floppy and hard-drive symbols in the drive-status panel

The SVG path geometry and embedded attribution comments are retained. Only the
fill color was changed to remain legible in the drive-status panel.

## Build and runtime libraries

- Qt 6.4 or newer (`Core`, `Gui`, and `Widgets`) provides the desktop UI. Qt is
  not vendored by this repository. Open-source Qt builds are offered under the
  module-specific LGPL/GPL terms; commercial Qt builds have separate terms.
  See <https://www.qt.io/licensing/open-source-obligations>.
- An OpenAL implementation provides audio output. It is discovered from the
  build/host system and is not vendored, so its implementation, version, and
  license depend on the distributor (OpenAL Soft is commonly used under
  LGPL-2.0-or-later).

Official release packages deploy the Qt 6.8.3 and OpenAL Soft shared libraries
needed by the application. They remain dynamically linked and retain their own
licenses; corresponding project and license information is available at
<https://www.qt.io/licensing/open-source-obligations> and
<https://github.com/kcat/openal-soft>. The package does not restrict users from
replacing those shared libraries with compatible modified builds.

## Historical and machine assets

The manuals, P2000C character image, IPL firmware dump, CP/M disk images, and
the software contained in those images—including Philips/CP/M material, ZORK,
and CHESS—are not original emulator source and are not relicensed under GPLv3.
Their provenance and redistribution rights are separate and, for some archive
contents, not established in this repository. They are included for historical
preservation and interoperability; downstream distributors must make their own
rights assessment. This project is independent and is not affiliated with or
endorsed by Philips, Digital Research, or the owners of the bundled programs.
