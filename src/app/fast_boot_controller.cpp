#include "app/fast_boot_controller.h"

namespace p2000c {

void FastBootController::start() { state_ = State::kWaitForCpmPrompt; }

void FastBootController::cancel() { state_ = State::kIdle; }

FastBootController::Action FastBootController::advance(
    std::string_view screen) {
  const auto contains = [&](std::string_view prompt) {
    return screen.find(prompt) != std::string_view::npos;
  };

  switch (state_) {
    case State::kWaitForCpmPrompt:
      if (contains("A>")) {
        state_ = State::kWaitForMsDosDiskPrompt;
        return Action::kSendMsBoot;
      }
      break;
    case State::kWaitForMsDosDiskPrompt:
      if (contains("Insert MS-DOS Disk")) {
        return Action::kMountMsDosDisk;
      }
      break;
    case State::kWaitForDatePrompt:
      if (contains("Enter new date:")) {
        state_ = State::kWaitForTimePrompt;
        return Action::kAcceptDate;
      }
      break;
    case State::kWaitForTimePrompt:
      if (contains("Enter new time:")) {
        state_ = State::kWaitForDosPrompt;
        return Action::kAcceptTime;
      }
      break;
    case State::kWaitForDosPrompt:
      if (contains("Microsoft MS-DOS version 2.11") && contains("A>")) {
        state_ = State::kIdle;
        return Action::kComplete;
      }
      break;
    case State::kIdle:
      break;
  }
  return Action::kNone;
}

bool FastBootController::confirm_ms_dos_disk_mounted() {
  if (state_ != State::kWaitForMsDosDiskPrompt) {
    return false;
  }
  state_ = State::kWaitForDatePrompt;
  return true;
}

std::string_view FastBootController::state_name(State state) {
  switch (state) {
    case State::kIdle:
      return "idle";
    case State::kWaitForCpmPrompt:
      return "waiting for CP/M prompt";
    case State::kWaitForMsDosDiskPrompt:
      return "waiting for MS-DOS disk prompt";
    case State::kWaitForDatePrompt:
      return "waiting for date prompt";
    case State::kWaitForTimePrompt:
      return "waiting for time prompt";
    case State::kWaitForDosPrompt:
      return "waiting for MS-DOS prompt";
  }
  return "unknown";
}

}  // namespace p2000c
