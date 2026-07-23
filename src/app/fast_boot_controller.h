#ifndef P2000C_APP_FAST_BOOT_CONTROLLER_H_
#define P2000C_APP_FAST_BOOT_CONTROLLER_H_

#include <string_view>

namespace p2000c {

/**
 * Deterministic prompt-driven state machine for one-click CoPower MS-DOS boot.
 *
 * The controller contains no timers, Qt objects, or emulation pacing. Callers
 * supply terminal snapshots and perform the returned side effects.
 */
class FastBootController {
  public:
    enum class State {
      kIdle,
      kWaitForCpmPrompt,
      kWaitForMsDosDiskPrompt,
      kWaitForDatePrompt,
      kWaitForTimePrompt,
      kWaitForDosPrompt,
    };

    enum class Action {
      kNone,
      kSendMsBoot,
      kMountMsDosDisk,
      kAcceptDate,
      kAcceptTime,
      kComplete,
    };

    /** Starts a fresh boot sequence at the CP/M command prompt stage. */
    void start();

    /** Cancels any active sequence. */
    void cancel();

    /** Returns the next side effect implied by the supplied terminal screen. */
    Action advance(std::string_view screen);

    /** Confirms that the requested MS-DOS disk was mounted successfully. */
    bool confirm_ms_dos_disk_mounted();

    bool active() const { return state_ != State::kIdle; }
    State state() const { return state_; }

    /** Stable diagnostic name for tests and error reporting. */
    static std::string_view state_name(State state);

  private:
    State state_ = State::kIdle;
};

}  // namespace p2000c

#endif  // P2000C_APP_FAST_BOOT_CONTROLLER_H_
