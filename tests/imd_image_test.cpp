#include "core/imd_image.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
  if (argc != 2) {
    return 2;
  }
  std::string error;
  const auto image =
      p2000c::ImdImage::open(std::filesystem::path(argv[1]), &error);
  if (!image.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }
  if (image->tracks().size() != 160) {
    std::cerr << "Expected 160 physical track records, got "
              << image->tracks().size() << '\n';
    return 1;
  }
  const p2000c::ImdSector* boot = image->find_sector(0, 0, 1);
  if (boot == nullptr || boot->data.size() != 256 || boot->data[0] != 0xc3 ||
      boot->data[1] != 0x39 || boot->data[2] != 0xd6 || boot->data[3] != 0x82) {
    std::cerr << "Unexpected P2000C boot sector.\n";
    return 1;
  }

  const auto unique_suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path temporary_path =
      std::filesystem::temp_directory_path() /
      ("p2000c-imd-test-" + std::to_string(unique_suffix) + ".imd");
  std::filesystem::copy_file(argv[1], temporary_path);
  auto writable = p2000c::ImdImage::open(temporary_path, &error);
  if (!writable.has_value()) {
    std::cerr << error << '\n';
    return 1;
  }
  const p2000c::ImdSector* writable_sector = nullptr;
  for (const p2000c::ImdTrack& track : writable->tracks()) {
    for (const p2000c::ImdSector& sector : track.sectors) {
      if (sector.type != 0 && !sector.data.empty()) {
        writable_sector = &sector;
        break;
      }
    }
    if (writable_sector != nullptr) {
      break;
    }
  }
  if (writable_sector == nullptr) {
    std::cerr << "No writable sector found.\n";
    return 1;
  }
  const std::uint8_t cylinder = writable_sector->cylinder;
  const std::uint8_t head = writable_sector->head;
  const std::uint8_t id = writable_sector->id;
  std::vector<std::uint8_t> changed = writable_sector->data;
  changed[0] ^= 0xff;
  if (!writable->write_sector(cylinder, head, id, changed, &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  const auto reopened = p2000c::ImdImage::open(temporary_path, &error);
  const p2000c::ImdSector* changed_sector =
      reopened.has_value() ? reopened->find_sector(cylinder, head, id)
                           : nullptr;
  const bool write_verified =
      changed_sector != nullptr && changed_sector->data == changed;
  std::filesystem::remove(temporary_path);
  if (!write_verified) {
    std::cerr << "The direct sector write did not survive reopening.\n";
    return 1;
  }
  return 0;
}
