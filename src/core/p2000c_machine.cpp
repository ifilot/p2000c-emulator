#include "core/p2000c_machine.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <numeric>
#include <utility>
#include <vector>

extern "C" {
#include "z80.h"
}

namespace p2000c {
namespace {

void set_error(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

P2000cMachine::P2000cMachine() : cpu_(std::make_unique<z80>()) {
  z80_init(cpu_.get());
  cpu_->read_byte = &P2000cMachine::cpu_read;
  cpu_->write_byte = &P2000cMachine::cpu_write;
  cpu_->port_in = &P2000cMachine::cpu_port_in;
  cpu_->port_out = &P2000cMachine::cpu_port_out;
  cpu_->userdata = this;
  reset();
}

P2000cMachine::~P2000cMachine() = default;

bool P2000cMachine::load_ipl_rom(const std::filesystem::path& path,
                                 std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    set_error(error, "Could not open mainboard IPL ROM: " + path.string());
    return false;
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
  return load_ipl_rom(bytes, error);
}

bool P2000cMachine::load_ipl_rom(std::span<const std::uint8_t> bytes,
                                 std::string* error) {
  if (bytes.size() != kIplRomSize) {
    set_error(error, "The mainboard IPL ROM must be exactly 4096 bytes.");
    return false;
  }
  std::uint16_t checksum = 0;
  for (std::size_t index = 0; index < kIplRomSize - 2; ++index) {
    checksum = static_cast<std::uint16_t>(checksum + bytes[index]);
  }
  const std::uint16_t stored_checksum =
      static_cast<std::uint16_t>(bytes[kIplRomSize - 2]) |
      static_cast<std::uint16_t>(bytes[kIplRomSize - 1] << 8);
  if (checksum != stored_checksum) {
    set_error(error, "The mainboard IPL ROM checksum is invalid.");
    return false;
  }
  std::copy(bytes.begin(), bytes.end(), ipl_rom_.begin());
  has_ipl_rom_ = true;
  reset();
  return true;
}

bool P2000cMachine::mount_floppy_a(const std::filesystem::path& path,
                                   std::string* error) {
  return mount_floppy(0, path, error);
}

bool P2000cMachine::mount_floppy_b(const std::filesystem::path& path,
                                   std::string* error) {
  return mount_floppy(1, path, error);
}

bool P2000cMachine::mount_floppy(std::size_t drive,
                                 const std::filesystem::path& path,
                                 std::string* error) {
  std::optional<ImdImage> image = ImdImage::open(path, error);
  if (!image.has_value()) {
    return false;
  }
  floppy_drives_[drive] = std::move(*image);
  return true;
}

const ImdImage* P2000cMachine::floppy_drive(std::uint8_t drive) const {
  if (drive >= floppy_drives_.size() || !floppy_drives_[drive].has_value()) {
    return nullptr;
  }
  return &*floppy_drives_[drive];
}

void P2000cMachine::reset() {
  z80_init(cpu_.get());
  cpu_->read_byte = &P2000cMachine::cpu_read;
  cpu_->write_byte = &P2000cMachine::cpu_write;
  cpu_->port_in = &P2000cMachine::cpu_port_in;
  cpu_->port_out = &P2000cMachine::cpu_port_out;
  cpu_->userdata = this;
  rom_overlay_enabled_ = true;
  terminal_.reset();
  dma_channels_.fill({});
  interrupt_queue_.clear();
  fdc_command_.clear();
  fdc_result_.clear();
  dma_high_byte_ = false;
  dma_read_high_byte_ = false;
  handshake_started_ = false;
  terminal_interrupt_requested_ = false;
  sio_b_dtr_ = false;
  fdc_reset_released_ = false;
  fdc_sense_pending_ = false;
  dma_mode_ = 0;
  dma_status_ = 0;
  fdc_output_ = 0;
  fdc_sense_status_ = 0xc0;
  fdc_tracks_.fill(0);
  fdc_sense_track_ = 0;
  sio_b_register_ = 0;
  sio_b_receive_byte_ = 0;
  sio_b_receive_ready_ = false;
  total_cycles_ = 0;
  next_timer_cycle_ = kTimerPeriod;
}

void P2000cMachine::run_for(std::uint64_t t_states) {
  if (!has_ipl_rom_) {
    return;
  }
  while (t_states > 0) {
    const unsigned long previous_cycles = cpu_->cyc;
    update_devices();
    z80_step(cpu_.get());
    const unsigned long instruction_cycles = cpu_->cyc - previous_cycles;
    total_cycles_ += instruction_cycles;
    if (instruction_cycles >= t_states) {
      break;
    }
    t_states -= instruction_cycles;
  }
}

std::uint16_t P2000cMachine::program_counter() const { return cpu_->pc; }

std::uint64_t P2000cMachine::cycles() const { return total_cycles_; }

std::uint8_t P2000cMachine::read_memory(std::uint16_t address) const {
  if (rom_overlay_enabled_ && address < kIplRomSize) {
    return ipl_rom_[address];
  }
  return ram_[address];
}

void P2000cMachine::write_memory(std::uint16_t address, std::uint8_t value) {
  ram_[address] = value;
}

std::uint8_t P2000cMachine::cpu_read(void* context, std::uint16_t address) {
  return static_cast<P2000cMachine*>(context)->read_memory(address);
}

void P2000cMachine::cpu_write(void* context, std::uint16_t address,
                              std::uint8_t value) {
  static_cast<P2000cMachine*>(context)->write_memory(address, value);
}

std::uint8_t P2000cMachine::cpu_port_in(z80* cpu, std::uint8_t port) {
  auto* machine = static_cast<P2000cMachine*>(cpu->userdata);
  if (port <= 0x08) {
    return machine->read_dma(port);
  }
  switch (port) {
    case 0x14:
      return 0;
    case 0x15:
      return 0x85;
    case 0x16:
      return 0xff;
    case 0x18:
      return 0x00;
    case 0x1a:
      return machine->fdc_status();
    case 0x1b:
      if (machine->fdc_result_.empty()) {
        return 0xff;
      } else {
        const std::uint8_t value = machine->fdc_result_.front();
        machine->fdc_result_.pop_front();
        return value;
      }
    case 0x28:
      return 0;
    case 0x29:
      return 0x04;
    case 0x2a:
    case 0x2b:
      return machine->read_terminal_sio(port);
    default:
      return 0xff;
  }
}

void P2000cMachine::cpu_port_out(z80* cpu, std::uint8_t port,
                                 std::uint8_t value) {
  auto* machine = static_cast<P2000cMachine*>(cpu->userdata);
  if (port <= 0x08) {
    machine->write_dma(port, value);
    return;
  }
  switch (port) {
    case 0x18:
      break;
    case 0x1b:
      machine->write_fdc(value);
      break;
    case 0x1e:
      machine->rom_overlay_enabled_ = (value & 0x03) != 0x02;
      break;
    case 0x1f: {
      const bool was_released = (machine->fdc_output_ & 0x10) != 0;
      machine->fdc_output_ = value;
      const bool is_released = (value & 0x10) != 0;
      if (!was_released && is_released) {
        machine->release_fdc_reset();
      }
      if (!is_released) {
        machine->fdc_reset_released_ = false;
      }
      break;
    }
    case 0x2a:
    case 0x2b:
      machine->write_terminal_sio(port, value);
      break;
    default:
      break;
  }
}

void P2000cMachine::update_devices() {
  if (!handshake_started_ && total_cycles_ >= kHandshakeDelay) {
    terminal_.begin_handshake();
    handshake_started_ = true;
  }
  if (terminal_.has_input() && sio_b_dtr_ && !sio_b_receive_ready_ &&
      !terminal_interrupt_requested_) {
    sio_b_receive_byte_ = terminal_.take_input();
    sio_b_receive_ready_ = true;
    request_interrupt(0xe4);
    terminal_interrupt_requested_ = true;
  }
  while (total_cycles_ >= next_timer_cycle_) {
    request_interrupt(0xd8);
    next_timer_cycle_ += kTimerPeriod;
  }
  if (cpu_->iff1 && !cpu_->int_pending && !interrupt_queue_.empty()) {
    z80_gen_int(cpu_.get(), interrupt_queue_.front());
    interrupt_queue_.pop_front();
  }
}

void P2000cMachine::request_interrupt(std::uint8_t vector) {
  if (interrupt_queue_.size() < 32) {
    interrupt_queue_.push_back(vector);
  }
}

std::uint8_t P2000cMachine::read_dma(std::uint8_t port) {
  if (port == 0x08) {
    const std::uint8_t status = dma_status_;
    dma_status_ = 0;
    dma_read_high_byte_ = false;
    return status;
  }
  const DmaChannel& channel = dma_channels_[port / 2];
  const std::uint16_t value = (port & 1) == 0 ? channel.address : channel.count;
  const std::uint8_t result = dma_read_high_byte_
                                  ? static_cast<std::uint8_t>(value >> 8)
                                  : static_cast<std::uint8_t>(value);
  dma_read_high_byte_ = !dma_read_high_byte_;
  return result;
}

void P2000cMachine::write_dma(std::uint8_t port, std::uint8_t value) {
  if (port == 0x08) {
    dma_mode_ = value;
    dma_high_byte_ = false;
    dma_read_high_byte_ = false;
    if ((dma_mode_ & 0x08) != 0) {
      run_terminal_dma();
    }
    return;
  }
  DmaChannel& channel = dma_channels_[port / 2];
  std::uint16_t& target = (port & 1) == 0 ? channel.address : channel.count;
  if (dma_high_byte_) {
    target = static_cast<std::uint16_t>((target & 0x00ff) | (value << 8));
  } else {
    target = static_cast<std::uint16_t>((target & 0xff00) | value);
  }
  dma_high_byte_ = !dma_high_byte_;
}

void P2000cMachine::run_terminal_dma() {
  DmaChannel& channel = dma_channels_[3];
  const std::size_t length = (channel.count & 0x3fff) + 1;
  for (std::size_t index = 0; index < length; ++index) {
    terminal_.receive(read_memory(channel.address++));
  }
  channel.count = 0x3fff;
  dma_mode_ &= ~0x08;
  dma_status_ |= 0x08;
  request_interrupt(0xd6);
}

bool P2000cMachine::run_floppy_dma(std::span<const std::uint8_t> data) {
  if ((dma_mode_ & 0x01) == 0) {
    return false;
  }
  DmaChannel& channel = dma_channels_[0];
  const std::size_t length = (channel.count & 0x3fff) + 1;
  if (data.size() < length) {
    return false;
  }
  for (std::size_t index = 0; index < length; ++index) {
    write_memory(channel.address++, data[index]);
  }
  channel.count = 0x3fff;
  dma_mode_ &= ~0x01;
  dma_status_ |= 0x01;
  request_interrupt(0xd6);
  return true;
}

std::uint8_t P2000cMachine::fdc_status() const {
  if (!fdc_reset_released_) {
    return 0;
  }
  return fdc_result_.empty() ? 0x80 : 0xc0;
}

void P2000cMachine::write_fdc(std::uint8_t value) {
  if (!fdc_reset_released_) {
    return;
  }
  fdc_command_.push_back(value);
  if (fdc_command_.size() == fdc_command_size(fdc_command_.front())) {
    complete_fdc_command();
    fdc_command_.clear();
  }
}

void P2000cMachine::complete_fdc_command() {
  const std::uint8_t command = fdc_command_.front() & 0x1f;
  if (command == 0x03) {
    return;
  }
  if (command == 0x08) {
    fdc_result_.push_back(fdc_sense_pending_ ? fdc_sense_status_ : 0x80);
    fdc_result_.push_back(fdc_sense_track_);
    fdc_sense_pending_ = false;
    return;
  }
  if (command == 0x07 || command == 0x0f) {
    const std::uint8_t drive = fdc_command_[1] & 0x03;
    const ImdImage* image = floppy_drive(drive);
    if (drive < fdc_tracks_.size()) {
      fdc_tracks_[drive] = command == 0x0f ? fdc_command_[2] : 0;
      fdc_sense_track_ = fdc_tracks_[drive];
    } else {
      fdc_sense_track_ = 0;
    }
    const bool drive_ready = image != nullptr;
    fdc_sense_status_ =
        static_cast<std::uint8_t>((drive_ready ? 0x20 : 0x48) | drive);
    fdc_sense_pending_ = true;
    request_interrupt(0xde);
    return;
  }

  if (command == 0x06 && fdc_command_.size() == 9) {
    const std::uint8_t drive = fdc_command_[1] & 0x03;
    const std::uint8_t cylinder = fdc_command_[2];
    const std::uint8_t head = fdc_command_[3];
    const std::uint8_t first_sector = fdc_command_[4];
    const std::uint8_t size_code = fdc_command_[5];
    const std::uint8_t last_sector = fdc_command_[6];
    std::vector<std::uint8_t> track_data;
    const ImdImage* image = floppy_drive(drive);
    bool media_ok = image != nullptr;
    for (std::uint16_t id = first_sector; media_ok && id <= last_sector; ++id) {
      const ImdSector* sector =
          image->find_sector(cylinder, head, static_cast<std::uint8_t>(id));
      if (sector == nullptr || sector->data.empty()) {
        media_ok = false;
      } else {
        track_data.insert(track_data.end(), sector->data.begin(),
                          sector->data.end());
      }
    }
    media_ok = media_ok && run_floppy_dma(track_data);
    if (media_ok) {
      fdc_result_.insert(fdc_result_.end(),
                         {static_cast<std::uint8_t>((head << 2) | drive), 0, 0,
                          cylinder, head, last_sector, size_code});
    } else {
      fdc_result_.insert(fdc_result_.end(),
                         {static_cast<std::uint8_t>(0x48 | (head << 2) | drive),
                          0x04, 0, cylinder, head, first_sector, size_code});
    }
    request_interrupt(0xde);
    return;
  }

  fdc_result_.push_back(0x80);
}

std::size_t P2000cMachine::fdc_command_size(std::uint8_t command) {
  switch (command & 0x1f) {
    case 0x03:
      return 3;
    case 0x04:
    case 0x07:
    case 0x0a:
      return 2;
    case 0x08:
      return 1;
    case 0x0f:
      return 3;
    case 0x05:
    case 0x06:
    case 0x09:
    case 0x0c:
      return 9;
    case 0x0d:
      return 6;
    default:
      return 1;
  }
}

void P2000cMachine::release_fdc_reset() {
  fdc_reset_released_ = true;
  fdc_command_.clear();
  fdc_result_.clear();
  fdc_sense_status_ = 0xc0;
  fdc_sense_track_ = 0;
  fdc_sense_pending_ = true;
  request_interrupt(0xde);
}

std::uint8_t P2000cMachine::read_terminal_sio(std::uint8_t port) {
  if (port == 0x2b) {
    return static_cast<std::uint8_t>(0x04 | (sio_b_receive_ready_ ? 0x01 : 0));
  }
  sio_b_receive_ready_ = false;
  return sio_b_receive_byte_;
}

void P2000cMachine::write_terminal_sio(std::uint8_t port, std::uint8_t value) {
  if (port == 0x2a) {
    terminal_.receive(value);
    return;
  }
  if (sio_b_register_ != 0) {
    if (sio_b_register_ == 5) {
      sio_b_dtr_ = (value & 0x80) != 0;
      if (!sio_b_dtr_) {
        terminal_interrupt_requested_ = false;
      }
    }
    sio_b_register_ = 0;
  } else if ((value & 0x07) != 0) {
    sio_b_register_ = value & 0x07;
  }
}

}  // namespace p2000c
