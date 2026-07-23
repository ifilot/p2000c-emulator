#include "core/raw_disk_image.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
  if (argc != 4) {
    return 2;
  }
  std::string error;
  const auto floppy = p2000c::RawDiskImage::open(
      argv[1], p2000c::RawDiskImage::Kind::kFloppy, &error);
  const auto hard_disk = p2000c::RawDiskImage::open(
      argv[2], p2000c::RawDiskImage::Kind::kHardDisk, &error);
  const auto dos_floppy = p2000c::RawDiskImage::open(
      argv[3], p2000c::RawDiskImage::Kind::kFloppy, &error);
  if (!floppy.has_value() || !hard_disk.has_value() ||
      !dos_floppy.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }
  const auto boot = floppy->floppy_sector(0, 0, 1);
  if (floppy->data().size() != p2000c::RawDiskImage::kFloppySize ||
      hard_disk->data().size() != p2000c::RawDiskImage::kHardDiskSize ||
      floppy->floppy_sector_size() != 256 ||
      floppy->floppy_sectors_per_track() != 16 ||
      boot.size() != 256 || boot[0] != 0xc3 || boot[1] != 0x39 ||
      boot[2] != 0xd6 || boot[3] != 0x82) {
    std::cerr << "Raw image geometry or system-track prefix is invalid.\n";
    return 1;
  }
  const auto dos_boot = dos_floppy->floppy_sector(0, 0, 1);
  if (dos_floppy->data().size() !=
          p2000c::RawDiskImage::kDosFloppySize ||
      dos_floppy->floppy_sector_size() != 512 ||
      dos_floppy->floppy_sectors_per_track() != 10 ||
      dos_boot.size() != 512 || dos_boot[0] != 0xeb ||
      dos_boot[1] != 0x50 || dos_boot[0x0b] != 0x00 ||
      dos_boot[0x0c] != 0x02 || dos_boot[0x18] != 0x0a ||
      dos_boot[0x19] != 0x00 ||
      !dos_floppy->floppy_sector(0, 0, 11).empty()) {
    std::cerr << "Raw MS-DOS floppy geometry or BPB is invalid.\n";
    return 1;
  }
  if (p2000c::RawDiskImage::open(
          argv[1], p2000c::RawDiskImage::Kind::kHardDisk, &error)
          .has_value()) {
    std::cerr << "A FLP was accepted as a 10 MiB HDA.\n";
    return 1;
  }

  const auto unique_suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path temporary_path =
      std::filesystem::temp_directory_path() /
      ("p2000c-raw-test-" + std::to_string(unique_suffix) + ".flp");
  std::filesystem::copy_file(argv[1], temporary_path);
  auto writable = p2000c::RawDiskImage::open(
      temporary_path, p2000c::RawDiskImage::Kind::kFloppy, &error);
  if (!writable.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }
  std::vector<std::uint8_t> changed(
      writable->floppy_sector(2, 1, 16).begin(),
      writable->floppy_sector(2, 1, 16).end());
  changed[0] ^= 0xff;
  if (!writable->write_floppy_sector(2, 1, 16, changed, &error)) {
    std::cerr << error << '\n';
    std::filesystem::remove(temporary_path);
    return 1;
  }
  const auto reopened = p2000c::RawDiskImage::open(
      temporary_path, p2000c::RawDiskImage::Kind::kFloppy, &error);
  const bool persisted = reopened.has_value() &&
                         reopened->floppy_sector(2, 1, 16)[0] == changed[0];
  std::filesystem::remove(temporary_path);
  if (!persisted) {
    std::cerr << "Raw sector write did not survive reopening.\n";
    return 1;
  }

  const std::filesystem::path temporary_dos_path =
      std::filesystem::temp_directory_path() /
      ("p2000c-raw-dos-test-" + std::to_string(unique_suffix) + ".flp");
  std::filesystem::copy_file(argv[3], temporary_dos_path);
  auto writable_dos = p2000c::RawDiskImage::open(
      temporary_dos_path, p2000c::RawDiskImage::Kind::kFloppy, &error);
  if (!writable_dos.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }
  std::vector<std::uint8_t> changed_dos(
      writable_dos->floppy_sector(79, 1, 10).begin(),
      writable_dos->floppy_sector(79, 1, 10).end());
  changed_dos[0] ^= 0xff;
  if (!writable_dos->write_floppy_sector(79, 1, 10, changed_dos, &error)) {
    std::cerr << error << '\n';
    std::filesystem::remove(temporary_dos_path);
    return 1;
  }
  const auto reopened_dos = p2000c::RawDiskImage::open(
      temporary_dos_path, p2000c::RawDiskImage::Kind::kFloppy, &error);
  const bool dos_persisted =
      reopened_dos.has_value() &&
      reopened_dos->floppy_sector(79, 1, 10)[0] == changed_dos[0];
  std::filesystem::remove(temporary_dos_path);
  if (!dos_persisted) {
    std::cerr << "Raw 512-byte sector write did not survive reopening.\n";
    return 1;
  }
  return 0;
}
