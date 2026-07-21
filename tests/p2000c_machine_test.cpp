#include "core/p2000c_machine.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 3) {
    return 2;
  }
  p2000c::P2000cMachine machine;

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
  if (machine.read_memory(0x0000) != 0x00 ||
      machine.read_memory(0x1234) != 0xa5) {
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
  if (!boot_machine.load_ipl_rom(argv[1], &error) ||
      !boot_machine.mount_floppy_a(argv[2], &error) ||
      !boot_machine.mount_floppy_b(argv[2], &error)) {
    std::cerr << error << '\n';
    return 1;
  }
  boot_machine.run_for(20'000'000);
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

  for (const char key : std::string("DIR\r")) {
    boot_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  boot_machine.run_for(20'000'000);
  const auto& command_screen = boot_machine.terminal().screen();
  const std::string command_text(command_screen.begin(), command_screen.end());
  if (command_text.find("CPM61") == std::string::npos ||
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

  for (const char key : std::string("A:\rUTIL\r")) {
    boot_machine.queue_key(static_cast<std::uint8_t>(key));
  }
  boot_machine.run_for(60'000'000);
  const auto& util_screen = boot_machine.terminal().screen();
  const auto& util_attributes = boot_machine.terminal().attributes();
  const std::string util_first_line(util_screen.begin(),
                                    util_screen.begin() + 80);
  const auto inverted_on_first_line = std::count_if(
      util_attributes.begin(), util_attributes.begin() + 80,
      [](std::uint8_t attribute) {
        return (attribute & p2000c::Terminal::kAttributeInverse) != 0;
      });
  if (util_first_line.find("UTIL") == std::string::npos ||
      inverted_on_first_line < 40) {
    std::cerr << "UTIL did not produce its inverted heading; "
              << "inverted first-line cells=" << inverted_on_first_line << '\n';
    print_screen(boot_machine);
    return 1;
  }
  return 0;
}
