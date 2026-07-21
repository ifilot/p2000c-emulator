#include "core/imd_image.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <utility>

namespace p2000c {
namespace {

bool read_byte(std::istream& input, std::uint8_t* value) {
  char byte = 0;
  if (!input.get(byte)) {
    return false;
  }
  *value = static_cast<std::uint8_t>(byte);
  return true;
}

bool read_bytes(std::istream& input, std::span<std::uint8_t> bytes) {
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return input.good() ||
         input.gcount() == static_cast<std::streamsize>(bytes.size());
}

void set_error(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

std::size_t sector_size(std::uint8_t size_code) {
  if (size_code > 6) {
    return 0;
  }
  return static_cast<std::size_t>(128) << size_code;
}

}  // namespace

std::optional<ImdImage> ImdImage::open(const std::filesystem::path& path,
                                       std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    set_error(error, "Could not open ImageDisk file: " + path.string());
    return std::nullopt;
  }

  ImdImage image;
  image.path_ = path;
  char character = 0;
  while (input.get(character) &&
         static_cast<std::uint8_t>(character) != kHeaderTerminator) {
    image.comment_.push_back(character);
  }
  if (!input || image.comment_.rfind("IMD ", 0) != 0) {
    set_error(error, "The file does not contain a valid ImageDisk header.");
    return std::nullopt;
  }

  while (input.peek() != std::char_traits<char>::eof()) {
    ImdTrack track;
    std::uint8_t encoded_head = 0;
    std::uint8_t sector_count = 0;
    if (!read_byte(input, &track.mode) || !read_byte(input, &track.cylinder) ||
        !read_byte(input, &encoded_head) || !read_byte(input, &sector_count) ||
        !read_byte(input, &track.size_code)) {
      set_error(error, "Truncated ImageDisk track header.");
      return std::nullopt;
    }
    if (track.mode > 5 || sector_count == 0 || track.size_code > 6) {
      set_error(error, "Unsupported or invalid ImageDisk track encoding.");
      return std::nullopt;
    }
    track.head = encoded_head & 0x3f;

    std::vector<std::uint8_t> sector_ids(sector_count);
    std::vector<std::uint8_t> cylinder_map(sector_count, track.cylinder);
    std::vector<std::uint8_t> head_map(sector_count, track.head);
    if (!read_bytes(input, sector_ids)) {
      set_error(error, "Truncated ImageDisk sector map.");
      return std::nullopt;
    }
    if ((encoded_head & 0x80) != 0 && !read_bytes(input, cylinder_map)) {
      set_error(error, "Truncated ImageDisk cylinder map.");
      return std::nullopt;
    }
    if ((encoded_head & 0x40) != 0 && !read_bytes(input, head_map)) {
      set_error(error, "Truncated ImageDisk head map.");
      return std::nullopt;
    }

    const std::size_t bytes_per_sector = sector_size(track.size_code);
    for (std::size_t index = 0; index < sector_count; ++index) {
      ImdSector sector;
      sector.id = sector_ids[index];
      sector.cylinder = cylinder_map[index];
      sector.head = head_map[index];
      if (!read_byte(input, &sector.type) || sector.type > 8) {
        set_error(error, "Invalid ImageDisk sector data type.");
        return std::nullopt;
      }
      if (sector.type != 0) {
        sector.data.resize(bytes_per_sector);
        if ((sector.type & 1) != 0) {
          if (!read_bytes(input, sector.data)) {
            set_error(error, "Truncated ImageDisk sector data.");
            return std::nullopt;
          }
        } else {
          std::uint8_t fill = 0;
          if (!read_byte(input, &fill)) {
            set_error(error, "Truncated compressed ImageDisk sector.");
            return std::nullopt;
          }
          std::fill(sector.data.begin(), sector.data.end(), fill);
        }
      }
      track.sectors.push_back(std::move(sector));
    }
    image.tracks_.push_back(std::move(track));
  }

  if (image.tracks_.empty()) {
    set_error(error, "The ImageDisk file contains no tracks.");
    return std::nullopt;
  }
  return image;
}

const ImdSector* ImdImage::find_sector(std::uint8_t cylinder, std::uint8_t head,
                                       std::uint8_t sector_id) const {
  for (const ImdTrack& track : tracks_) {
    for (const ImdSector& sector : track.sectors) {
      if (sector.cylinder == cylinder && sector.head == head &&
          sector.id == sector_id) {
        return &sector;
      }
    }
  }
  return nullptr;
}

ImdSector* ImdImage::find_sector(std::uint8_t cylinder, std::uint8_t head,
                                 std::uint8_t sector_id) {
  return const_cast<ImdSector*>(
      std::as_const(*this).find_sector(cylinder, head, sector_id));
}

bool ImdImage::write_sector(std::uint8_t cylinder, std::uint8_t head,
                            std::uint8_t sector_id,
                            std::span<const std::uint8_t> data,
                            std::string* error) {
  ImdSector* sector = find_sector(cylinder, head, sector_id);
  if (sector == nullptr) {
    set_error(error, "The requested sector does not exist.");
    return false;
  }
  if (sector->type == 0 || data.size() != sector->data.size()) {
    set_error(error, "The write size does not match the physical sector.");
    return false;
  }

  const std::vector<std::uint8_t> previous_data = sector->data;
  const std::uint8_t previous_type = sector->type;
  sector->data.assign(data.begin(), data.end());
  if ((sector->type & 1) == 0) {
    --sector->type;
  }
  if (!save(error)) {
    sector->data = previous_data;
    sector->type = previous_type;
    return false;
  }
  return true;
}

bool ImdImage::save(std::string* error) const {
  const std::filesystem::path temporary_path = path_.string() + ".tmp";
  const std::filesystem::path backup_path = path_.string() + ".bak";
  std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    set_error(error, "Could not create temporary ImageDisk file.");
    return false;
  }

  output.write(comment_.data(), static_cast<std::streamsize>(comment_.size()));
  output.put(static_cast<char>(kHeaderTerminator));
  for (const ImdTrack& track : tracks_) {
    const bool has_cylinder_map =
        std::any_of(track.sectors.begin(), track.sectors.end(),
                    [&](const ImdSector& sector) {
                      return sector.cylinder != track.cylinder;
                    });
    const bool has_head_map = std::any_of(
        track.sectors.begin(), track.sectors.end(),
        [&](const ImdSector& sector) { return sector.head != track.head; });
    std::uint8_t encoded_head = track.head;
    encoded_head |= has_cylinder_map ? 0x80 : 0;
    encoded_head |= has_head_map ? 0x40 : 0;
    const std::array<std::uint8_t, 5> header = {
        track.mode, track.cylinder, encoded_head,
        static_cast<std::uint8_t>(track.sectors.size()), track.size_code};
    output.write(reinterpret_cast<const char*>(header.data()), header.size());
    for (const ImdSector& sector : track.sectors) {
      output.put(static_cast<char>(sector.id));
    }
    if (has_cylinder_map) {
      for (const ImdSector& sector : track.sectors) {
        output.put(static_cast<char>(sector.cylinder));
      }
    }
    if (has_head_map) {
      for (const ImdSector& sector : track.sectors) {
        output.put(static_cast<char>(sector.head));
      }
    }
    for (const ImdSector& sector : track.sectors) {
      output.put(static_cast<char>(sector.type));
      if (sector.type == 0) {
        continue;
      }
      if ((sector.type & 1) == 0) {
        output.put(static_cast<char>(sector.data.front()));
      } else {
        output.write(reinterpret_cast<const char*>(sector.data.data()),
                     static_cast<std::streamsize>(sector.data.size()));
      }
    }
  }
  output.close();
  if (!output) {
    std::filesystem::remove(temporary_path);
    set_error(error, "Failed while writing the ImageDisk file.");
    return false;
  }

  std::error_code file_error;
  std::filesystem::remove(backup_path, file_error);
  file_error.clear();
  std::filesystem::rename(path_, backup_path, file_error);
  if (file_error) {
    std::filesystem::remove(temporary_path);
    set_error(error, "Could not preserve the original ImageDisk file: " +
                         file_error.message());
    return false;
  }
  std::filesystem::rename(temporary_path, path_, file_error);
  if (file_error) {
    std::error_code restore_error;
    std::filesystem::rename(backup_path, path_, restore_error);
    std::filesystem::remove(temporary_path);
    set_error(error,
              "Could not replace ImageDisk file: " + file_error.message());
    return false;
  }
  std::filesystem::remove(backup_path, file_error);
  return true;
}

}  // namespace p2000c
