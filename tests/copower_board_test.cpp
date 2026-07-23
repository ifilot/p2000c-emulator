#include "core/copower_board.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>

namespace {

using Memory = std::array<std::uint8_t, 65536>;

void copy_bytes(Memory& memory, std::uint16_t address,
                std::initializer_list<std::uint8_t> bytes) {
  std::copy(bytes.begin(), bytes.end(), memory.begin() + address);
}

void install_far_reset(Memory& memory, std::uint16_t offset) {
  copy_bytes(memory, 0xfff0,
             {0xea, static_cast<std::uint8_t>(offset),
              static_cast<std::uint8_t>(offset >> 8), 0x00, 0xf0});
}

}  // namespace

int main() {
  Memory shared{};
  int z80_interrupts = 0;
  p2000c::CoPowerBoard board(
      [&](std::uint16_t address) { return shared[address]; },
      [&](std::uint16_t address, std::uint8_t value) {
        shared[address] = value;
      },
      [&]() { ++z80_interrupts; });

  board.write_control(2);
  board.run_for(10'000);
  if (!board.held_in_reset()) {
    std::cerr << "An absent CoPower board left reset.\n";
    return 1;
  }

  board.set_enabled(true);
  copy_bytes(shared, 0xfff0,
             {0x2e, 0xc6, 0x06, 0x08, 0x00, 0x5a, 0xf4});
  board.write_control(2);
  board.run_for(10'000);
  if (shared[0xfff8] != 0x5a || board.faulted()) {
    std::cerr << "The 8088 reset vector did not reach common memory.\n";
    return 1;
  }

  board.write_control(1);
  install_far_reset(shared, 0x0100);
  copy_bytes(shared, 0x0100,
             {
                 0xb8, 0x00, 0x00,        // MOV AX,0000
                 0x8e, 0xd8,              // MOV DS,AX
                 0xc6, 0x06, 0x34, 0x12, 0xa5,  // MOV BYTE [1234],A5
                 0x80, 0x3e, 0x34, 0x12, 0xa5,  // CMP BYTE [1234],A5
                 0x75, 0x07,              // JNE failure
                 0x2e, 0xc6, 0x06, 0x00, 0x02, 0x5a,  // success
                 0xf4,                    // HLT
                 0x2e, 0xc6, 0x06, 0x00, 0x02, 0xff,  // failure
                 0xf4,                    // HLT
             });
  shared[0x0200] = 0;
  board.write_control(2);
  board.run_for(20'000);
  if (shared[0x0200] != 0x5a || board.faulted()) {
    std::cerr << "The 512 KiB CoPower local RAM path failed.\n";
    return 1;
  }

  board.write_control(1);
  install_far_reset(shared, 0x0100);
  copy_bytes(shared, 0x0100,
             {
                 0xb8, 0x00, 0x00,              // MOV AX,0000
                 0x8e, 0xd8,                    // MOV DS,AX
                 0xc7, 0x06, 0x08, 0x01, 0x00, 0x02,  // vector offset
                 0xc7, 0x06, 0x0a, 0x01, 0x00, 0xf0,  // vector segment
                 0xfb,                          // STI
                 0xeb, 0xfe,                    // wait for interrupt
             });
  copy_bytes(shared, 0x0200,
             {
                 0x2e, 0xc6, 0x06, 0x00, 0x03, 0xa5,  // shared marker
                 0xcf,                                // IRET
             });
  shared[0x0300] = 0;
  board.write_control(2);
  board.run_for(20'000);
  board.write_interrupt_vector(0x42);
  board.run_for(20'000);
  if (shared[0x0300] != 0xa5 || board.faulted()) {
    std::cerr << "The Z80-to-8088 interrupt path failed.\n";
    return 1;
  }

  board.write_control(1);
  install_far_reset(shared, 0x0100);
  copy_bytes(shared, 0x0100,
             {
                 0xba, 0x00, 0x00,  // MOV DX,0000
                 0xb0, 0x42,        // MOV AL,42
                 0xee,              // OUT DX,AL
                 0xf4,              // HLT
             });
  const int interrupts_before = z80_interrupts;
  board.write_control(2);
  board.run_for(20'000);
  if (z80_interrupts != interrupts_before + 1 || board.faulted()) {
    std::cerr << "The 8088-to-Z80 interrupt path failed.\n";
    return 1;
  }

  board.write_control(1);
  install_far_reset(shared, 0x0100);
  copy_bytes(shared, 0x0100,
             {
                 0x2e, 0xfe, 0x06, 0x00, 0x02,  // INC BYTE CS:[0200]
                 0xeb, 0xf9,                    // loop
             });
  shared[0x0200] = 0;
  board.write_control(2);
  board.run_for(2'000);
  const std::uint8_t before_lock = shared[0x0200];
  board.write_control(3);
  board.run_for(20'000);
  if (shared[0x0200] != before_lock || !board.common_memory_locked()) {
    std::cerr << "The common-memory lock did not stop the 8088.\n";
    return 1;
  }
  board.write_control(2);
  board.run_for(2'000);
  if (shared[0x0200] == before_lock || board.faulted()) {
    std::cerr << "The 8088 did not resume after common-memory unlock.\n";
    return 1;
  }

  return 0;
}
