#include "app/fast_boot_controller.h"

#include <iostream>
#include <string_view>

namespace {

using Action = p2000c::FastBootController::Action;
using State = p2000c::FastBootController::State;

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  p2000c::FastBootController controller;
  if (!expect(!controller.active(), "Controller did not start idle.") ||
      !expect(controller.advance("A>") == Action::kNone,
              "Idle controller reacted to a prompt.")) {
    return 1;
  }

  controller.start();
  if (!expect(controller.state() == State::kWaitForCpmPrompt,
              "Controller did not enter the CP/M stage.") ||
      !expect(controller.advance("booting") == Action::kNone,
              "Controller advanced without the CP/M prompt.") ||
      !expect(controller.advance("CP/M 2.2\r\nA>") == Action::kSendMsBoot,
              "Controller did not request MSBOOT.") ||
      !expect(controller.state() == State::kWaitForMsDosDiskPrompt,
              "Controller did not enter the disk-prompt stage.") ||
      !expect(controller.advance("A>") == Action::kNone,
              "A stale CP/M prompt advanced the disk stage.") ||
      !expect(controller.advance("Insert MS-DOS Disk") ==
                  Action::kMountMsDosDisk,
              "Controller did not request the MS-DOS disk.") ||
      !expect(controller.state() == State::kWaitForMsDosDiskPrompt,
              "Disk request advanced before mount confirmation.") ||
      !expect(controller.confirm_ms_dos_disk_mounted(),
              "Controller rejected a valid disk-mount confirmation.") ||
      !expect(controller.state() == State::kWaitForDatePrompt,
              "Controller did not enter the date stage.") ||
      !expect(!controller.confirm_ms_dos_disk_mounted(),
              "Controller accepted duplicate disk-mount confirmation.") ||
      !expect(controller.advance("Enter new date:") == Action::kAcceptDate,
              "Controller did not accept the default date.") ||
      !expect(controller.advance("Enter new time:") == Action::kAcceptTime,
              "Controller did not accept the default time.") ||
      !expect(controller.advance("old CP/M screen\r\nA>") == Action::kNone,
              "A stale CP/M prompt completed the MS-DOS stage.") ||
      !expect(controller.advance("Microsoft MS-DOS version 2.11\r\nA>") ==
                  Action::kComplete,
              "Controller did not complete at the MS-DOS prompt.") ||
      !expect(!controller.active(), "Controller remained active at completion.")) {
    return 1;
  }

  controller.start();
  controller.cancel();
  if (!expect(controller.state() == State::kIdle,
              "Cancellation did not restore the idle state.") ||
      !expect(
          p2000c::FastBootController::state_name(
              State::kWaitForMsDosDiskPrompt) ==
              "waiting for MS-DOS disk prompt",
          "Controller diagnostic state name changed unexpectedly.")) {
    return 1;
  }

  return 0;
}
