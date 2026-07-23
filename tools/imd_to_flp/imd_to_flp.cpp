#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/raw_disk_image.h"

namespace {

class ImdReader {
 public:
  explicit ImdReader(std::vector<std::uint8_t> bytes)
      : bytes_(std::move(bytes)) {}

  bool empty() const { return position_ == bytes_.size(); }

  std::uint8_t byte() {
    if (empty()) {
      throw std::runtime_error("Unexpected end of IMD image.");
    }
    return bytes_[position_++];
  }

  std::vector<std::uint8_t> bytes(std::size_t count) {
    if (count > bytes_.size() - position_) {
      throw std::runtime_error("Truncated IMD sector data.");
    }
    std::vector<std::uint8_t> result(
        bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
        bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + count));
    position_ += count;
    return result;
  }

  void skip_comment() {
    while (byte() != 0x1a) {
    }
  }

 private:
  std::vector<std::uint8_t> bytes_;
  std::size_t position_ = 0;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("Could not open IMD image: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void write_file(const std::filesystem::path& path,
                const std::vector<std::uint8_t>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("Could not write FLP image: " + path.string());
  }
}

std::vector<std::uint8_t> convert_imd(std::vector<std::uint8_t> bytes) {
  using p2000c::RawDiskImage;
  ImdReader input(std::move(bytes));
  input.skip_comment();

  std::vector<std::uint8_t> output;
  std::vector<bool> present;
  std::size_t geometry_sector_size = 0;
  std::size_t geometry_sectors_per_track = 0;

  while (!input.empty()) {
    const std::uint8_t mode = input.byte();
    if (mode > 5) {
      throw std::runtime_error("Unsupported IMD recording mode.");
    }
    const std::uint8_t track_cylinder = input.byte();
    const std::uint8_t head_flags = input.byte();
    const std::uint8_t sector_count = input.byte();
    const std::uint8_t size_code = input.byte();
    if (sector_count == 0) {
      throw std::runtime_error("IMD track contains no sectors.");
    }

    const std::vector<std::uint8_t> sector_numbers =
        input.bytes(sector_count);
    std::vector<std::uint8_t> cylinder_map(sector_count, track_cylinder);
    std::vector<std::uint8_t> head_map(sector_count, head_flags & 0x3f);
    if ((head_flags & 0x80) != 0) {
      cylinder_map = input.bytes(sector_count);
    }
    if ((head_flags & 0x40) != 0) {
      head_map = input.bytes(sector_count);
    }

    std::vector<std::size_t> sector_sizes(sector_count);
    if (size_code == 0xff) {
      for (std::size_t index = 0; index < sector_count; ++index) {
        const std::uint16_t low = input.byte();
        sector_sizes[index] = low | (input.byte() << 8);
      }
    } else {
      if (size_code > 6) {
        throw std::runtime_error("Invalid IMD sector size code.");
      }
      std::fill(sector_sizes.begin(), sector_sizes.end(),
                std::size_t{128} << size_code);
    }

    if (output.empty()) {
      const bool cpm_geometry =
          sector_count == RawDiskImage::kFloppySectorsPerTrack &&
          std::all_of(sector_sizes.begin(), sector_sizes.end(),
                      [](std::size_t size) {
                        return size == RawDiskImage::kSectorSize;
                      });
      const bool dos_geometry =
          sector_count == RawDiskImage::kDosFloppySectorsPerTrack &&
          std::all_of(sector_sizes.begin(), sector_sizes.end(),
                      [](std::size_t size) {
                        return size == RawDiskImage::kDosFloppySectorSize;
                      });
      if (!cpm_geometry && !dos_geometry) {
        throw std::runtime_error(
            "IMD geometry is not a supported P2000C floppy format.");
      }
      geometry_sector_size =
          cpm_geometry ? RawDiskImage::kSectorSize
                       : RawDiskImage::kDosFloppySectorSize;
      geometry_sectors_per_track =
          cpm_geometry ? RawDiskImage::kFloppySectorsPerTrack
                       : RawDiskImage::kDosFloppySectorsPerTrack;
      const std::size_t sector_total =
          RawDiskImage::kFloppyCylinders * RawDiskImage::kFloppyHeads *
          geometry_sectors_per_track;
      output.assign(sector_total * geometry_sector_size, 0);
      present.assign(sector_total, false);
    }
    if (sector_count != geometry_sectors_per_track ||
        std::any_of(sector_sizes.begin(), sector_sizes.end(),
                    [&](std::size_t size) {
                      return size != geometry_sector_size;
                    })) {
      throw std::runtime_error("IMD image changes geometry between tracks.");
    }

    for (std::size_t index = 0; index < sector_count; ++index) {
      const std::uint8_t data_type = input.byte();
      if (data_type > 8) {
        throw std::runtime_error("Invalid IMD sector data type.");
      }
      std::vector<std::uint8_t> sector(sector_sizes[index], 0);
      if (data_type != 0) {
        if ((data_type & 1) != 0) {
          sector = input.bytes(sector_sizes[index]);
        } else {
          std::fill(sector.begin(), sector.end(), input.byte());
        }
      }

      const std::uint8_t cylinder = cylinder_map[index];
      const std::uint8_t head = head_map[index] & 0x3f;
      const std::uint8_t sector_number = sector_numbers[index];
      if (cylinder >= RawDiskImage::kFloppyCylinders ||
          head >= RawDiskImage::kFloppyHeads || sector_number == 0 ||
          sector_number > geometry_sectors_per_track ||
          sector.size() != geometry_sector_size) {
        throw std::runtime_error(
            "IMD sector lies outside the selected P2000C geometry.");
      }

      const std::size_t logical_sector =
          (static_cast<std::size_t>(cylinder) *
               RawDiskImage::kFloppyHeads +
           head) *
              geometry_sectors_per_track +
          sector_number - 1;
      if (present[logical_sector]) {
        throw std::runtime_error("IMD image contains a duplicate sector.");
      }
      present[logical_sector] = true;
      std::copy(sector.begin(), sector.end(),
                output.begin() +
                    static_cast<std::ptrdiff_t>(
                        logical_sector * geometry_sector_size));
    }
  }

  if (!std::all_of(present.begin(), present.end(),
                   [](bool value) { return value; })) {
    throw std::runtime_error(
        "IMD image is missing sectors from its P2000C geometry.");
  }
  return output;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: p2000c_imd_to_flp INPUT.IMD OUTPUT.FLP\n";
    return 2;
  }
  try {
    write_file(argv[2], convert_imd(read_file(argv[1])));
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "p2000c_imd_to_flp: " << exception.what() << '\n';
    return 1;
  }
}
