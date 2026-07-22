#ifndef P2000C_CORE_RAW_DISK_IMAGE_H_
#define P2000C_CORE_RAW_DISK_IMAGE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace p2000c {

/** A fixed-geometry, directly writable P2000C raw disk image. */
class RawDiskImage {
 public:
  enum class Kind {
    kFloppy,
    kHardDisk,
  };

  static constexpr std::size_t kSectorSize = 256;
  static constexpr std::size_t kFloppyCylinders = 80;
  static constexpr std::size_t kFloppyHeads = 2;
  static constexpr std::size_t kFloppySectorsPerTrack = 16;
  static constexpr std::size_t kFloppySize = 640 * 1024;
  static constexpr std::size_t kHardDiskSize = 10 * 1024 * 1024;
  static_assert(kFloppySize == kFloppyCylinders * kFloppyHeads *
                                    kFloppySectorsPerTrack * kSectorSize);

  /** Opens an image and verifies its exact fixed size. */
  static std::optional<RawDiskImage> open(const std::filesystem::path& path,
                                          Kind kind, std::string* error);

  /** Returns the mounted image kind. */
  Kind kind() const { return kind_; }

  /** Returns the mounted file path. */
  const std::filesystem::path& path() const { return path_; }

  /** Returns the complete raw byte sequence. */
  std::span<const std::uint8_t> data() const { return data_; }

  /** Returns one floppy sector in cylinder/head/sector order. */
  std::span<const std::uint8_t> floppy_sector(
      std::uint8_t cylinder, std::uint8_t head,
      std::uint8_t sector) const;

  /** Returns a contiguous sequence of 256-byte logical blocks. */
  std::span<const std::uint8_t> blocks(std::size_t lba,
                                       std::size_t count) const;

  /** Writes one floppy sector through to the mounted file. */
  bool write_floppy_sector(std::uint8_t cylinder, std::uint8_t head,
                           std::uint8_t sector,
                           std::span<const std::uint8_t> data,
                           std::string* error);

  /** Writes contiguous 256-byte logical blocks through to the file. */
  bool write_blocks(std::size_t lba, std::span<const std::uint8_t> data,
                    std::string* error);

 private:
  /** Returns a raw floppy-sector byte offset, or no value when invalid. */
  static std::optional<std::size_t> floppy_offset(
      std::uint8_t cylinder, std::uint8_t head, std::uint8_t sector);

  /** Replaces a checked byte range in memory and on disk. */
  bool write(std::size_t offset, std::span<const std::uint8_t> data,
             std::string* error);

  std::filesystem::path path_;
  Kind kind_ = Kind::kFloppy;
  std::vector<std::uint8_t> data_;
};

}  // namespace p2000c

#endif  // P2000C_CORE_RAW_DISK_IMAGE_H_
