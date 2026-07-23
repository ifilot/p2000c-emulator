#!/usr/bin/env python3
"""Build the emulator's deterministic raw CP/M floppy and hard-disk images."""

from __future__ import annotations

import argparse
from pathlib import Path


SECTOR_SIZE = 256
FLOPPY_SIZE = 640 * 1024
HARD_DISK_SIZE = 10 * 1024 * 1024
SYSTEM_SIZE = 8 * 1024
DIRECTORY_OFFSET = SYSTEM_SIZE
DIRECTORY_ENTRIES = 128
DIRECTORY_ENTRY_SIZE = 32
BLOCK_SIZE = 4096
MAX_BLOCK = 157
RECORD_SIZE = 128
RECORDS_PER_DIRECTORY_EXTENT = 512
FILL = 0xE5
RECORD_FILL = 0x1A
# The 640 KiB CBIOS sector-translation table visits odd physical sector IDs
# first, followed by even IDs, for one logical 4 KiB CP/M track.
SECTOR_INTERLEAVE = tuple(range(0, 16, 2)) + tuple(range(1, 16, 2))


class MediaBuildError(ValueError):
    """Raised when a source cannot be represented by the floppy layout."""


def _cpm_name(path: Path) -> tuple[bytes, bytes]:
    parts = path.name.upper().split(".")
    if len(parts) > 2 or not parts[0] or len(parts[0]) > 8:
        raise MediaBuildError(f"{path.name!r} is not a CP/M 8.3 filename")
    extension = parts[1] if len(parts) == 2 else ""
    if len(extension) > 3:
        raise MediaBuildError(f"{path.name!r} is not a CP/M 8.3 filename")
    try:
        return parts[0].encode("ascii").ljust(8), extension.encode("ascii").ljust(3)
    except UnicodeEncodeError as exc:
        raise MediaBuildError(f"{path.name!r} is not an ASCII filename") from exc


def build_floppy(output: Path, files: list[Path], system: bytes | None) -> None:
    """Build one P2000C 640 KiB CP/M floppy using the confirmed DPB."""
    if system is not None and len(system) != SYSTEM_SIZE:
        raise MediaBuildError("a floppy system area must be exactly 8192 bytes")
    image = bytearray([FILL]) * FLOPPY_SIZE
    if system is not None:
        image[:SYSTEM_SIZE] = system

    directory_index = 0
    next_block = 1  # Block zero contains the 4 KiB directory.
    names: set[tuple[bytes, bytes]] = set()
    for source in files:
        name, extension = _cpm_name(source)
        if (name, extension) in names:
            raise MediaBuildError(f"duplicate CP/M filename {source.name}")
        names.add((name, extension))
        data = source.read_bytes()
        records = max(1, (len(data) + RECORD_SIZE - 1) // RECORD_SIZE)
        record_data = data.ljust(records * RECORD_SIZE, bytes([RECORD_FILL]))
        source_offset = 0
        physical_extent = 0
        records_left = records
        while records_left:
            extent_records = min(records_left, RECORDS_PER_DIRECTORY_EXTENT)
            block_count = (extent_records * RECORD_SIZE + BLOCK_SIZE - 1) // BLOCK_SIZE
            if directory_index >= DIRECTORY_ENTRIES:
                raise MediaBuildError("the floppy directory has no free entries")
            if next_block + block_count - 1 > MAX_BLOCK:
                raise MediaBuildError("the floppy has no free allocation blocks")

            logical_extent = physical_extent * 4 + (extent_records - 1) // 128
            record_count = extent_records - (logical_extent & 3) * 128
            entry = bytearray(32)
            entry[0] = 0
            entry[1:9] = name
            entry[9:12] = extension
            entry[12] = logical_extent & 0x1F
            entry[14] = logical_extent >> 5
            entry[15] = record_count
            for index in range(block_count):
                entry[16 + index] = next_block + index
            entry_offset = DIRECTORY_OFFSET + directory_index * DIRECTORY_ENTRY_SIZE
            image[entry_offset : entry_offset + DIRECTORY_ENTRY_SIZE] = entry

            byte_count = extent_records * RECORD_SIZE
            allocation_start = DIRECTORY_OFFSET + next_block * BLOCK_SIZE
            image[allocation_start : allocation_start + byte_count] = record_data[
                source_offset : source_offset + byte_count
            ]
            directory_index += 1
            next_block += block_count
            source_offset += byte_count
            records_left -= extent_records
            physical_extent += 1

    raw = bytearray([FILL]) * FLOPPY_SIZE
    physical_track_size = 16 * SECTOR_SIZE
    for track in range(FLOPPY_SIZE // physical_track_size):
        track_start = track * physical_track_size
        if track < 2 and system is not None:
            raw[track_start : track_start + physical_track_size] = image[
                track_start : track_start + physical_track_size
            ]
            continue
        for logical_sector, physical_sector in enumerate(SECTOR_INTERLEAVE):
            logical_start = track_start + logical_sector * SECTOR_SIZE
            physical_start = track_start + physical_sector * SECTOR_SIZE
            raw[physical_start : physical_start + SECTOR_SIZE] = image[
                logical_start : logical_start + SECTOR_SIZE
            ]
    output.write_bytes(raw)


def _ordered_core(directory: Path) -> list[Path]:
    patterns = (
        "CPM*.COM",
        "CBIOS*.COM",
        "CONFIG*",
        "SYSGEN.COM",
        "STAT.COM",
        "PIP.COM",
        "DDT.COM",
        "ED.COM",
        "ASM.COM",
        "LOAD.COM",
    )
    files: list[Path] = []
    for pattern in patterns:
        matches = sorted(directory.glob(pattern))
        if not matches:
            raise MediaBuildError(f"core pattern {pattern!r} matched no files")
        files.extend(matches)
    unlisted = set(directory.iterdir()) - set(files)
    if any(path.is_file() for path in unlisted):
        raise MediaBuildError("one or more core files have no defined ordering")
    return files


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--system", required=True, type=Path)
    parser.add_argument("--core", required=True, type=Path)
    parser.add_argument("--zork", required=True, type=Path)
    parser.add_argument("--chess", required=True, type=Path)
    parser.add_argument("--ipldump", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()

    cpm_output = arguments.output / "cpm"
    hard_disk_output = arguments.output / "hard-disks"
    cpm_output.mkdir(parents=True, exist_ok=True)
    hard_disk_output.mkdir(parents=True, exist_ok=True)
    system = arguments.system.read_bytes()
    build_floppy(
        cpm_output / "system.flp", _ordered_core(arguments.core), system
    )
    build_floppy(
        cpm_output / "zork.flp",
        [arguments.zork / "ZORK1.COM", arguments.zork / "ZORK1.DAT"],
        None,
    )
    build_floppy(cpm_output / "chess.flp", [arguments.chess], None)
    build_floppy(
        cpm_output / "ipldump.flp",
        [
            arguments.core / "ASM.COM",
            arguments.core / "LOAD.COM",
            arguments.ipldump,
        ],
        None,
    )
    (hard_disk_output / "blank.hda").write_bytes(
        bytes([FILL]) * HARD_DISK_SIZE
    )


if __name__ == "__main__":
    main()
