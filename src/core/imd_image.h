#ifndef P2000C_CORE_IMD_IMAGE_H_
#define P2000C_CORE_IMD_IMAGE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace p2000c {

/** A sector stored in an ImageDisk track record. */
struct ImdSector {
    std::uint8_t id = 0;
    std::uint8_t cylinder = 0;
    std::uint8_t head = 0;
    std::uint8_t type = 0;
    std::vector<std::uint8_t> data;
};

/** A physical track stored in an ImageDisk image. */
struct ImdTrack {
    std::uint8_t mode = 0;
    std::uint8_t cylinder = 0;
    std::uint8_t head = 0;
    std::uint8_t size_code = 0;
    std::vector<ImdSector> sectors;
};

/** Reads and writes the ImageDisk format without flattening its geometry. */
class ImdImage {
  private:
    static constexpr std::uint8_t kHeaderTerminator = 0x1a;

  public:
    /** Loads and validates an ImageDisk file. */
    static std::optional<ImdImage> open(const std::filesystem::path& path,
                                        std::string* error);

    /** Returns the ImageDisk comment without the terminating byte. */
    const std::string& comment() const { return comment_; }

    /** Returns all physical track records in file order. */
    const std::vector<ImdTrack>& tracks() const { return tracks_; }

    /** Returns the mounted file path. */
    const std::filesystem::path& path() const { return path_; }

    /** Finds a sector by physical cylinder, head, and sector identifier. */
    const ImdSector* find_sector(std::uint8_t cylinder, std::uint8_t head,
                                 std::uint8_t sector_id) const;

    /** Replaces sector contents and immediately rewrites the mounted image. */
    bool write_sector(std::uint8_t cylinder, std::uint8_t head,
                      std::uint8_t sector_id,
                      std::span<const std::uint8_t> data, std::string* error);

    /** Serializes the current image to its mounted path. */
    bool save(std::string* error) const;

  private:
    /** Mutable counterpart used by sector writes. */
    ImdSector* find_sector(std::uint8_t cylinder, std::uint8_t head,
                           std::uint8_t sector_id);

    std::filesystem::path path_;
    std::string comment_;
    std::vector<ImdTrack> tracks_;
};

}  // namespace p2000c

#endif  // P2000C_CORE_IMD_IMAGE_H_
