#ifndef P2000C_APP_HARDWARE_AUDIO_H_
#define P2000C_APP_HARDWARE_AUDIO_H_

#include <memory>

#include "core/p2000c_machine.h"

namespace p2000c {

/** Reproduces the terminal beeper and recorded floppy mechanism sounds. */
class HardwareAudio {
 public:
  static constexpr int kFloppyIdleTimeoutMs = 1200;

  HardwareAudio();
  ~HardwareAudio();
  HardwareAudio(const HardwareAudio&) = delete;
  HardwareAudio& operator=(const HardwareAudio&) = delete;

  /** Sets the master output level in the inclusive range 0.0 to 1.0. */
  void set_volume(double volume);

  /** Returns the current normalized master output level. */
  double volume() const;

  /** Stops all currently playing hardware audio immediately. */
  void stop_all();

  /** Returns whether the floppy spindle loop is logically active. */
  bool floppy_motor_active() const;

  /** Sounds the documented terminal-board 1.3 kHz one-bit beeper. */
  void play_bell();

  /** Reproduces genuine floppy mechanics; SASI activity remains silent. */
  void play_storage_activity(const P2000cMachine::StorageActivity& activity);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace p2000c

#endif  // P2000C_APP_HARDWARE_AUDIO_H_
