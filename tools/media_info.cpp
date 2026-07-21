#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <utility>

#include "core/imd_image.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: p2000c_media_info IMAGE.imd\n";
    return 2;
  }

  std::string error;
  const auto image =
      p2000c::ImdImage::open(std::filesystem::path(argv[1]), &error);
  if (!image.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }

  std::set<std::pair<std::uint8_t, std::uint8_t>> surfaces;
  std::size_t sectors = 0;
  std::size_t bytes = 0;
  for (const p2000c::ImdTrack& track : image->tracks()) {
    surfaces.emplace(track.cylinder, track.head);
    sectors += track.sectors.size();
    for (const p2000c::ImdSector& sector : track.sectors) {
      bytes += sector.data.size();
    }
  }

  std::cout << image->comment() << "\n\n"
            << "Track records: " << image->tracks().size() << '\n'
            << "Surfaces:      " << surfaces.size() << '\n'
            << "Sectors:       " << sectors << '\n'
            << "Decoded bytes: " << bytes << '\n';

  const p2000c::ImdSector* boot = image->find_sector(0, 0, 1);
  if (boot != nullptr && boot->data.size() >= 4) {
    std::cout << "Boot prefix:   ";
    constexpr char kHex[] = "0123456789ABCDEF";
    for (std::size_t index = 0; index < 4; ++index) {
      const std::uint8_t value = boot->data[index];
      std::cout << kHex[value >> 4] << kHex[value & 0x0f]
                << (index == 3 ? '\n' : ' ');
    }
  }
  return 0;
}
