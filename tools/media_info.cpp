#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "core/raw_disk_image.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: p2000c_media_info IMAGE.flp|IMAGE.hda\n";
    return 2;
  }

  std::string error;
  const std::filesystem::path path(argv[1]);
  std::error_code size_error;
  const std::uintmax_t size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    std::cerr << "Could not inspect raw image: " << size_error.message()
              << '\n';
    return 1;
  }
  const auto kind = size == p2000c::RawDiskImage::kFloppySize
                        ? p2000c::RawDiskImage::Kind::kFloppy
                        : p2000c::RawDiskImage::Kind::kHardDisk;
  const auto image = p2000c::RawDiskImage::open(path, kind, &error);
  if (!image.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }

  std::cout << "Type:          "
            << (kind == p2000c::RawDiskImage::Kind::kFloppy
                    ? "640 KiB FLP floppy"
                    : "10 MiB HDA hard disk")
            << '\n'
            << "Bytes:         " << image->data().size() << '\n'
            << "Sectors:       "
            << image->data().size() / p2000c::RawDiskImage::kSectorSize
            << '\n';

  const std::span<const std::uint8_t> boot = image->blocks(0, 1);
  if (boot.size() >= 4) {
    std::cout << "Boot prefix:   ";
    constexpr char kHex[] = "0123456789ABCDEF";
    for (std::size_t index = 0; index < 4; ++index) {
      const std::uint8_t value = boot[index];
      std::cout << kHex[value >> 4] << kHex[value & 0x0f]
                << (index == 3 ? '\n' : ' ');
    }
  }
  return 0;
}
