#include "core/p2000c_machine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

void print_screen(const p2000c::P2000cMachine& machine) {
  const auto& screen = machine.terminal().screen();
  const std::string screen_text(screen.begin(), screen.end());
  for (std::size_t row = 0; row < 24; ++row) {
    std::cerr.write(screen_text.data() + row * 80, 80);
    std::cerr << '\n';
  }
}

/** Converts raw physical floppy sectors back to the CP/M logical byte order. */
std::vector<std::uint8_t> logical_floppy(
    const p2000c::RawDiskImage& image) {
  constexpr std::array<std::uint8_t, 16> kInterleave = {
      0, 2, 4, 6, 8, 10, 12, 14, 1, 3, 5, 7, 9, 11, 13, 15};
  std::vector<std::uint8_t> logical(p2000c::RawDiskImage::kFloppySize);
  constexpr std::size_t kTrackSize = 16 * 256;
  for (std::size_t track = 0; track < 160; ++track) {
    for (std::size_t sector = 0; sector < kInterleave.size(); ++sector) {
      const std::span<const std::uint8_t> physical = image.floppy_sector(
          static_cast<std::uint8_t>(track / 2),
          static_cast<std::uint8_t>(track % 2), kInterleave[sector] + 1);
      std::copy(physical.begin(), physical.end(),
                logical.begin() + track * kTrackSize + sector * 256);
    }
  }
  return logical;
}

/** Extracts one single-extent file from the confirmed 640 KiB CP/M layout. */
std::optional<std::vector<std::uint8_t>> extract_small_cpm_file(
    const p2000c::RawDiskImage& image, const std::string& name,
    const std::string& extension) {
  constexpr std::size_t kDirectoryOffset = 8192;
  constexpr std::size_t kDirectoryEntries = 128;
  constexpr std::size_t kBlockSize = 4096;
  const std::vector<std::uint8_t> logical = logical_floppy(image);
  for (std::size_t index = 0; index < kDirectoryEntries; ++index) {
    const std::size_t offset = kDirectoryOffset + index * 32;
    const std::span<const std::uint8_t> entry(logical.data() + offset, 32);
    if (entry[0] != 0 ||
        std::string(entry.begin() + 1, entry.begin() + 9) != name ||
        std::string(entry.begin() + 9, entry.begin() + 12) != extension ||
        entry[12] != 0 || entry[14] != 0) {
      continue;
    }
    const std::size_t byte_count = entry[15] * 128;
    const std::size_t block_count =
        (byte_count + kBlockSize - 1) / kBlockSize;
    std::vector<std::uint8_t> data;
    data.reserve(block_count * kBlockSize);
    for (std::size_t block = 0; block < block_count; ++block) {
      const std::size_t source =
          kDirectoryOffset + entry[16 + block] * kBlockSize;
      data.insert(data.end(), logical.begin() + source,
                  logical.begin() + source + kBlockSize);
    }
    data.resize(byte_count);
    return data;
  }
  return std::nullopt;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 7) {
    return 2;
  }
  p2000c::P2000cMachine machine;
  machine.set_storage_delays_enabled(false);
  if (machine.storage_delays_enabled()) {
    std::cerr << "Storage latency could not be disabled.\n";
    return 1;
  }
  machine.set_storage_delays_enabled(true);

  // A SASI read must present status immediately even while realistic floppy
  // delays are enabled. The tiny IPL performs a one-block DMA read and stores
  // the control-bus state observed directly after the command at 0x1000.
  std::array<std::uint8_t, 4096> sasi_test_ipl{};
  constexpr std::array<std::uint8_t, 54> kSasiReadProgram = {
      0x3e, 0x00, 0xd3, 0x00,  // DMA address low
      0x3e, 0x20, 0xd3, 0x00,  // DMA address high
      0x3e, 0xff, 0xd3, 0x01,  // DMA count low (512 bytes)
      0x3e, 0x00, 0xd3, 0x01,  // DMA count high
      0x3e, 0x01, 0xd3, 0x08,  // Enable DMA channel zero
      0x3e, 0x04, 0xd3, 0x18,  // Select the SASI target
      0x3e, 0x08, 0xd3, 0x16,  // READ(6)
      0x3e, 0x00, 0xd3, 0x16,  // Unit zero and LBA high
      0x3e, 0x00, 0xd3, 0x16,  // LBA middle
      0x3e, 0x07, 0xd3, 0x16,  // LBA low
      0x3e, 0x01, 0xd3, 0x16,  // One block
      0x3e, 0x00, 0xd3, 0x16,  // Control byte
      0xdb, 0x18,              // Read SASI control immediately
      0x32, 0x00, 0x10,        // Store it outside the ROM overlay
      0x76};                    // HALT
  std::copy(kSasiReadProgram.begin(), kSasiReadProgram.end(),
            sasi_test_ipl.begin());
  std::uint16_t sasi_checksum = 0;
  for (std::size_t index = 0; index < sasi_test_ipl.size() - 2; ++index) {
    sasi_checksum =
        static_cast<std::uint16_t>(sasi_checksum + sasi_test_ipl[index]);
  }
  sasi_test_ipl[sasi_test_ipl.size() - 2] =
      static_cast<std::uint8_t>(sasi_checksum);
  sasi_test_ipl[sasi_test_ipl.size() - 1] =
      static_cast<std::uint8_t>(sasi_checksum >> 8);
  p2000c::P2000cMachine immediate_sasi_machine;
  std::string sasi_error;
  bool saw_sasi_read = false;
  immediate_sasi_machine.set_storage_activity_handler(
      [&saw_sasi_read](const auto& activity) {
        saw_sasi_read =
            saw_sasi_read ||
            (activity.device ==
                 p2000c::P2000cMachine::StorageDevice::kHardDisk &&
             activity.operation ==
                 p2000c::P2000cMachine::StorageOperation::kRead);
      });
  immediate_sasi_machine.set_storage_delays_enabled(true);
  if (!immediate_sasi_machine.load_ipl_rom(sasi_test_ipl, &sasi_error) ||
      !immediate_sasi_machine.mount_hard_disk(0, argv[6], &sasi_error)) {
    std::cerr << sasi_error << '\n';
    return 1;
  }
  immediate_sasi_machine.run_for(20'000);
  if (!saw_sasi_read || immediate_sasi_machine.read_memory(0x1000) != 0x9b ||
      immediate_sasi_machine.hard_disk_block(0) != 7) {
    std::cerr << "SASI read was not completed without hardware latency.\n";
    return 1;
  }
  if (!immediate_sasi_machine.unmount_hard_disk(0) ||
      immediate_sasi_machine.hard_disk(0).has_value() ||
      immediate_sasi_machine.unmount_hard_disk(2)) {
    std::cerr << "SASI hard disk could not be removed safely.\n";
    return 1;
  }

  p2000c::Terminal graphics_terminal;
  const std::uint64_t initial_bell = graphics_terminal.bell_revision();
  graphics_terminal.receive(0x07);
  if (graphics_terminal.bell_revision() != initial_bell + 1) {
    std::cerr << "BEL did not activate the terminal beeper event.\n";
    return 1;
  }
  auto send_terminal = [&](std::initializer_list<std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes) {
      graphics_terminal.receive(byte);
    }
  };
  send_terminal({0x1b, '5', 0x1b, 'D', 0x00, 0x00});
  const std::size_t bottom_line =
      (p2000c::Terminal::kGraphicHeight - 1) *
      p2000c::Terminal::kGraphicBytesPerLine;
  if (graphics_terminal.graphics_mode() !=
          p2000c::Terminal::GraphicsMode::kMedium256 ||
      graphics_terminal.graphic_screen()[bottom_line] != 0x80) {
    std::cerr << "Medium-resolution pixel command was not decoded.\n";
    return 1;
  }
  send_terminal({0x1b, '0', 0x01, 0x1b, 'D', 0x01, 0xfb});
  if (graphics_terminal.graphic_screen().front() != 0x44) {
    std::cerr << "Medium-resolution bold pixel planes are incorrect.\n";
    return 1;
  }

  send_terminal({0x1b, '3', 0x1b, 'D', 0xff, 0x01, 0xfb,
                 0x1b, 'm', 0x00, 0x00, 0x00,
                 0x1b, 'M', 0x07, 0x00, 0x00});
  if (graphics_terminal.graphics_mode() !=
          p2000c::Terminal::GraphicsMode::kHigh512 ||
      graphics_terminal.graphic_screen()[63] != 0x01 ||
      graphics_terminal.graphic_screen()[bottom_line] != 0xff) {
    std::cerr << "High-resolution pixel/vector commands were not decoded.\n";
    return 1;
  }

  send_terminal(
      {0x1b, 'r', 0x00, 0x00, 0xfb, 0x02, 0x00, 0xaa, 0x55});
  if (graphics_terminal.graphic_screen()[0] != 0xaa ||
      graphics_terminal.graphic_screen()[1] != 0x55) {
    std::cerr << "Raw picture upload did not reach graphics RAM.\n";
    return 1;
  }
  send_terminal({0x1b, 't', 0x00, 0x00, 0xfb, 0x02, 0x00});
  if (graphics_terminal.take_input() != 0xaa ||
      graphics_terminal.take_input() != 0x55) {
    std::cerr << "Raw picture download did not return graphics RAM.\n";
    return 1;
  }

  send_terminal({0x1b, '?'});
  std::array<std::uint8_t, 12> status{};
  for (std::uint8_t& byte : status) {
    byte = graphics_terminal.take_input();
  }
  if ((status[3] & 0x03) != 0x03 || status[4] != 7 || status[5] != 0 ||
      status[6] != 0) {
    std::cerr << "Graphics fields in terminal status are incorrect.\n";
    return 1;
  }
  send_terminal({0x1b, '4'});
  if (graphics_terminal.graphics_mode() !=
          p2000c::Terminal::GraphicsMode::kCharacter ||
      std::any_of(graphics_terminal.graphic_screen().begin(),
                  graphics_terminal.graphic_screen().end(),
                  [](std::uint8_t byte) { return byte != 0; })) {
    std::cerr << "Returning to character mode did not clear graphics RAM.\n";
    return 1;
  }

  p2000c::Terminal attribute_terminal;
  if (attribute_terminal.attributes().front() !=
      p2000c::Terminal::kDefaultAttribute) {
    std::cerr << "Terminal did not reset to normal intensity.\n";
    return 1;
  }
  for (const std::uint8_t value : {std::uint8_t{0x1b}, std::uint8_t{'0'},
                                   std::uint8_t{0x63}, std::uint8_t{'X'}}) {
    attribute_terminal.receive(value);
  }
  if (attribute_terminal.screen().front() != 'X' ||
      attribute_terminal.attributes().front() != 0x63) {
    std::cerr << "ESC 0 attribute was not stored with its character.\n";
    return 1;
  }
  machine.write_memory(0x0000, 0x5a);
  machine.write_memory(0x1234, 0xa5);
  machine.write_memory(0x1234, 0xa5);
  if (machine.read_memory(0x0000) != 0x00 ||
      machine.read_memory(0x1234) != 0xa5 ||
      machine.memory_page_write_counts()[0x00] != 1 ||
      machine.memory_page_write_counts()[0x12] != 1 ||
      !machine.ipl_rom_mapped()) {
    return 1;
  }

  std::vector<std::uint8_t> rom(4096, 0x00);
  const std::vector<std::uint8_t> program = {
      0x3e, 0x02,        // LD A,02
      0xd3, 0x1e,        // OUT (1e),A: select all-RAM mapping
      0x3e, 0x5a,        // LD A,5a
      0x32, 0x00, 0x00,  // LD (0000),A
      0x76,              // HALT
  };
  std::copy(program.begin(), program.end(), rom.begin());
  std::uint16_t checksum = 0;
  for (std::size_t index = 0; index < rom.size() - 2; ++index) {
    checksum = static_cast<std::uint16_t>(checksum + rom[index]);
  }
  rom[rom.size() - 2] = static_cast<std::uint8_t>(checksum);
  rom[rom.size() - 1] = static_cast<std::uint8_t>(checksum >> 8);
  for (std::size_t index = 0; index < program.size(); ++index) {
    machine.write_memory(static_cast<std::uint16_t>(index), program[index]);
  }
  const auto unique_suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path rom_path =
      std::filesystem::temp_directory_path() /
      ("p2000c-rom-test-" + std::to_string(unique_suffix) + ".rom");
  {
    std::ofstream output(rom_path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(rom.data()), rom.size());
  }
  std::string error;
  if (!machine.load_ipl_rom(rom_path, &error)) {
    std::cerr << error << '\n';
    std::filesystem::remove(rom_path);
    return 1;
  }
  machine.run_for(100);
  std::filesystem::remove(rom_path);
  if (machine.read_memory(0x0000) != 0x5a) {
    std::cerr << "ROM overlay did not switch to writable RAM; read "
              << static_cast<int>(machine.read_memory(0x0000)) << '\n';
    return 1;
  }

  p2000c::P2000cMachine authentic_machine;
  if (!authentic_machine.load_ipl_rom(argv[1], &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  authentic_machine.run_for(8'000'000);
  const auto& screen = authentic_machine.terminal().screen();
  const std::string screen_text(screen.begin(), screen.end());
  if (screen_text.find("IPL-1.1") == std::string::npos ||
      screen_text.find("SYSTEM DISK?") == std::string::npos) {
    std::cerr << "Authentic IPL did not reach the no-media prompt; PC="
              << std::hex << authentic_machine.program_counter()
              << " cycles=" << std::dec << authentic_machine.cycles() << '\n';
    std::cerr
        << std::hex
        << "FFBE=" << static_cast<int>(authentic_machine.read_memory(0xffbe))
        << " FFBF=" << static_cast<int>(authentic_machine.read_memory(0xffbf))
        << " FF92=" << static_cast<int>(authentic_machine.read_memory(0xff92))
        << " FFB2=" << static_cast<int>(authentic_machine.read_memory(0xffb2))
        << '\n';
    print_screen(authentic_machine);
    return 1;
  }

  p2000c::P2000cMachine boot_machine;
  std::vector<p2000c::P2000cMachine::StorageActivity> storage_activity;
  boot_machine.set_storage_activity_handler(
      [&](const p2000c::P2000cMachine::StorageActivity& activity) {
        storage_activity.push_back(activity);
      });
  if (!boot_machine.load_ipl_rom(argv[1], &error) ||
      !boot_machine.mount_floppy_a(argv[2], &error) ||
      !boot_machine.mount_floppy_b(argv[2], &error) ||
      !boot_machine.mount_hard_disk(0, argv[6], &error) ||
      !boot_machine.mount_hard_disk(1, argv[6], &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  boot_machine.run_for(60'000'000);
  const auto& boot_screen = boot_machine.terminal().screen();
  const std::string boot_text(boot_screen.begin(), boot_screen.end());
  if (boot_text.find("CP/M 2.2") == std::string::npos ||
      boot_text.find("A>") == std::string::npos) {
    std::cerr << "Authentic IPL did not boot the system floppy; PC=" << std::hex
              << boot_machine.program_counter() << " cycles=" << std::dec
              << boot_machine.cycles() << '\n';
    print_screen(boot_machine);
    return 1;
  }
  const bool realistic_floppy_read =
      std::any_of(storage_activity.begin(), storage_activity.end(),
                  [](const auto& activity) {
                    return activity.device ==
                               p2000c::P2000cMachine::StorageDevice::kFloppy &&
                           activity.operation ==
                               p2000c::P2000cMachine::StorageOperation::kRead &&
                           activity.duration_ms >= 118;
                  });
  if (!realistic_floppy_read) {
    std::cerr << "Floppy access did not expose its physical operation timing.\n";
    return 1;
  }

  for (const char key : std::string("DIR\r")) {
    boot_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  boot_machine.run_for(20'000'000);
  const auto& command_screen = boot_machine.terminal().screen();
  const std::string command_text(command_screen.begin(), command_screen.end());
  constexpr std::array<const char*, 16> kCoreDirectory = {
      "CPM61    COM", "CPM62    COM", "CPM63    COM", "CBIOS61  COM",
      "CBIOS62  COM", "CBIOS63  COM", "CONFIG   COM", "CONFIG   DAT",
      "CONFIG   MSG", "SYSGEN   COM", "STAT     COM", "PIP      COM",
      "DDT      COM", "ED       COM", "ASM      COM", "LOAD     COM"};
  const bool complete_directory =
      std::all_of(kCoreDirectory.begin(), kCoreDirectory.end(),
                  [&](const char* filename) {
                    return command_text.find(filename) != std::string::npos;
                  });
  if (!complete_directory || command_text.find(":   :   :") != std::string::npos ||
      command_text.find("A>") == std::string::npos) {
    std::cerr << "CP/M did not execute a keyboard-driven DIR command; PC="
              << std::hex << boot_machine.program_counter()
              << " cycles=" << std::dec << boot_machine.cycles() << '\n';
    print_screen(boot_machine);
    return 1;
  }

  for (const char key : std::string("B:\rDIR\r")) {
    boot_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  boot_machine.run_for(40'000'000);
  const auto& drive_b_screen = boot_machine.terminal().screen();
  const std::string drive_b_text(drive_b_screen.begin(), drive_b_screen.end());
  if (drive_b_text.find("B>") == std::string::npos) {
    std::cerr << "CP/M did not access mounted floppy drive B; PC=" << std::hex
              << boot_machine.program_counter() << " cycles=" << std::dec
              << boot_machine.cycles() << '\n';
    print_screen(boot_machine);
    return 1;
  }

  for (const char key : std::string("C:\rDIR\rD:\rDIR\rE:\rDIR\rF:\rDIR\r")) {
    boot_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  boot_machine.run_for(100'000'000);
  const auto& hard_drive_screen = boot_machine.terminal().screen();
  const std::string hard_drive_text(hard_drive_screen.begin(),
                                    hard_drive_screen.end());
  if (hard_drive_text.find("F>") == std::string::npos ||
      hard_drive_text.find("Bdos Err") != std::string::npos) {
    std::cerr << "CP/M did not access all four hard-disk volumes.\n";
    print_screen(boot_machine);
    return 1;
  }
  const bool visible_hard_disk_read =
      std::any_of(storage_activity.begin(), storage_activity.end(),
                  [](const auto& activity) {
                    return activity.device == p2000c::P2000cMachine::
                                                  StorageDevice::kHardDisk &&
                           activity.operation == p2000c::P2000cMachine::
                                                     StorageOperation::kRead &&
                           activity.duration_ms >= 60;
                  });
  if (!visible_hard_disk_read) {
    std::cerr << "SASI access did not emit a visible activity event.\n";
    return 1;
  }

  p2000c::P2000cMachine ddt_machine;
  if (!ddt_machine.load_ipl_rom(argv[1], &error) ||
      !ddt_machine.mount_floppy_a(argv[2], &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  ddt_machine.run_for(60'000'000);
  for (const char key : std::string("DDT\r")) {
    ddt_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  ddt_machine.run_for(40'000'000);
  const auto& ddt_screen = ddt_machine.terminal().screen();
  const std::string ddt_text(ddt_screen.begin(), ddt_screen.end());
  const std::size_t ddt_banner = ddt_text.find("DDT VERS 2.2");
  const std::size_t ddt_prompt =
      ddt_banner == std::string::npos ? std::string::npos
                                      : ddt_text.find('-', ddt_banner);
  if (ddt_banner == std::string::npos || ddt_prompt == std::string::npos ||
      ddt_text.find("DDT VERS 2.2", ddt_banner + 1) != std::string::npos ||
      ddt_text.find('-', ddt_prompt + 1) != std::string::npos ||
      ddt_machine.terminal().cursor_column() != 1) {
    std::cerr << "DDT.COM did not reach its command prompt.\n";
    print_screen(ddt_machine);
    return 1;
  }

  const std::filesystem::path compilation_floppy =
      std::filesystem::temp_directory_path() /
      ("p2000c-ipldump-test-" + std::to_string(unique_suffix) + ".flp");
  std::filesystem::copy_file(argv[5], compilation_floppy,
                             std::filesystem::copy_options::overwrite_existing);
  p2000c::P2000cMachine compilation_machine;
  if (!compilation_machine.load_ipl_rom(argv[1], &error) ||
      !compilation_machine.mount_floppy_a(argv[2], &error) ||
      !compilation_machine.mount_floppy_b(compilation_floppy, &error)) {
    std::cerr << error << '\n';
    std::filesystem::remove(compilation_floppy);
    return 1;
  }
  compilation_machine.run_for(60'000'000);
  for (const char key : std::string("B:\rASM IPLDUMP\r")) {
    compilation_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  compilation_machine.run_for(300'000'000);
  const std::string assembler_text(
      compilation_machine.terminal().screen().begin(),
      compilation_machine.terminal().screen().end());
  if (assembler_text.find("END OF ASSEMBLY") == std::string::npos ||
      assembler_text.find("ERROR") != std::string::npos) {
    std::cerr << "ASM.COM did not assemble IPLDUMP.ASM successfully.\n";
    print_screen(compilation_machine);
    std::filesystem::remove(compilation_floppy);
    return 1;
  }
  for (const char key : std::string("LOAD IPLDUMP\r")) {
    compilation_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  compilation_machine.run_for(100'000'000);
  const std::string loader_text(
      compilation_machine.terminal().screen().begin(),
      compilation_machine.terminal().screen().end());
  if (loader_text.find("FIRST ADDRESS") == std::string::npos ||
      loader_text.find("BYTES READ") == std::string::npos) {
    std::cerr << "LOAD.COM did not create IPLDUMP.COM.\n";
    print_screen(compilation_machine);
    std::filesystem::remove(compilation_floppy);
    return 1;
  }
  for (const char key : std::string("IPLDUMP\r")) {
    compilation_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  compilation_machine.run_for(100'000'000);
  const std::string dumper_text(
      compilation_machine.terminal().screen().begin(),
      compilation_machine.terminal().screen().end());
  if (dumper_text.find("IPLDUMP.BIN CREATED (4096 BYTES).") ==
      std::string::npos) {
    std::cerr << "The assembled IPL dumper did not complete.\n";
    print_screen(compilation_machine);
    std::filesystem::remove(compilation_floppy);
    return 1;
  }
  const auto dumped_rom = extract_small_cpm_file(
      *compilation_machine.floppy_b(), "IPLDUMP ", "BIN");
  std::ifstream expected_rom_file(argv[1], std::ios::binary);
  std::vector<std::uint8_t> expected_rom(4096);
  expected_rom_file.read(reinterpret_cast<char*>(expected_rom.data()),
                         static_cast<std::streamsize>(expected_rom.size()));
  std::filesystem::remove(compilation_floppy);
  if (expected_rom_file.gcount() !=
          static_cast<std::streamsize>(expected_rom.size()) ||
      !dumped_rom.has_value() || *dumped_rom != expected_rom) {
    std::cerr << "IPLDUMP.BIN did not exactly match the mapped IPL ROM.\n";
    return 1;
  }

  p2000c::P2000cMachine zork_machine;
  if (!zork_machine.load_ipl_rom(argv[1], &error) ||
      !zork_machine.mount_floppy_a(argv[2], &error) ||
      !zork_machine.mount_floppy_b(argv[3], &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  zork_machine.run_for(60'000'000);
  for (const char key : std::string("B:\rZORK1\r")) {
    zork_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  zork_machine.run_for(100'000'000);
  const auto& zork_screen = zork_machine.terminal().screen();
  const std::string zork_text(zork_screen.begin(), zork_screen.end());
  if (zork_text.find("West of House") == std::string::npos) {
    std::cerr << "ZORK1 did not load its data and reach the opening scene.\n";
    print_screen(zork_machine);
    return 1;
  }

  p2000c::P2000cMachine chess_machine;
  if (!chess_machine.load_ipl_rom(argv[1], &error) ||
      !chess_machine.mount_floppy_a(argv[2], &error) ||
      !chess_machine.mount_floppy_b(argv[4], &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  chess_machine.run_for(60'000'000);
  for (const char key : std::string("B:\rCHESS\r")) {
    chess_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  chess_machine.run_for(80'000'000);
  const auto lit_graphics_bytes = std::count_if(
      chess_machine.terminal().graphic_screen().begin(),
      chess_machine.terminal().graphic_screen().end(),
      [](std::uint8_t byte) { return byte != 0; });
  if (chess_machine.terminal().graphics_mode() ==
          p2000c::Terminal::GraphicsMode::kCharacter ||
      lit_graphics_bytes < 100) {
    std::cerr << "CHESS.COM did not activate and draw a graphics mode; mode="
              << static_cast<int>(chess_machine.terminal().graphics_mode())
              << " nonzero bytes=" << lit_graphics_bytes << '\n';
    print_screen(chess_machine);
    return 1;
  }

  return 0;
}
