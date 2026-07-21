#ifndef P2000C_CORE_TERMINAL_H_
#define P2000C_CORE_TERMINAL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace p2000c {

/** Functional replacement for the serial P2000C terminal board. */
class Terminal {
  private:
    static constexpr int kColumns = 80;
    static constexpr int kRows = 24;

    enum class EscapeState {
      kNone,
      kCommand,
      kCursorRow,
      kCursorColumn,
      kAttribute,
      kLockLines,
      kSkipBytes,
    };

  public:
    using Screen = std::array<std::uint8_t, kColumns * kRows>;
    using AttributeScreen = std::array<std::uint8_t, kColumns * kRows>;

    // ESC 0 attribute-byte layout documented in the terminal firmware manual.
    static constexpr std::uint8_t kAttributeIntensityHigh = 0x40;
    static constexpr std::uint8_t kAttributeUnderline = 0x20;
    static constexpr std::uint8_t kAttributeInverse = 0x10;
    static constexpr std::uint8_t kAttributeBlink = 0x02;
    static constexpr std::uint8_t kAttributeIntensityLow = 0x01;
    static constexpr std::uint8_t kDefaultAttribute = kAttributeIntensityHigh;

    /** Creates a reset 80x24 terminal. */
    Terminal();

    /** Clears terminal state, queues, and screen contents. */
    void reset();

    /** Queues the two bytes expected by the mainboard IPL handshake. */
    void begin_handshake();

    /** Accepts a serial byte transmitted by the mainboard. */
    void receive(std::uint8_t value);

    /** Queues a translated keyboard byte for the mainboard receiver. */
    void queue_key(std::uint8_t value);

    /** Returns whether a byte is waiting for the mainboard. */
    bool has_input() const { return !input_.empty(); }

    /** Removes and returns the next byte for the mainboard. */
    std::uint8_t take_input();

    /** Returns the current 80x24 character buffer. */
    const Screen& screen() const { return screen_; }

    /** Returns the terminal attribute byte stored for every character cell. */
    const AttributeScreen& attributes() const { return attributes_; }

    /** Returns a counter changed after every visible screen update. */
    std::uint64_t revision() const { return revision_; }

    /** Returns the zero-based cursor column. */
    int cursor_column() const { return cursor_column_; }

    /** Returns the zero-based cursor row. */
    int cursor_row() const { return cursor_row_; }

    /** Returns whether the terminal firmware has enabled the cursor. */
    bool cursor_visible() const { return cursor_visible_; }

  private:
    /** Places a printable character at the cursor and advances it. */
    void put_character(std::uint8_t value);

    /** Moves down, scrolling at the bottom of the display. */
    void cursor_down();

    /** Scrolls the complete display upward by one row. */
    void scroll_up();

    /** Scrolls the complete display downward by one row. */
    void scroll_down();

    /** Interprets the byte following ESC. */
    void escape_command(std::uint8_t value);

    /** Queues the documented 12-byte terminal status reply. */
    void queue_status();

    Screen screen_{};
    AttributeScreen attributes_{};
    std::deque<std::uint8_t> input_;
    EscapeState escape_state_ = EscapeState::kNone;
    int cursor_column_ = 0;
    int cursor_row_ = 0;
    int pending_row_ = 0;
    int skip_bytes_ = 0;
    int handshake_echoes_ = 0;
    std::uint8_t attribute_ = kDefaultAttribute;
    bool keyboard_locked_ = false;
    bool cursor_visible_ = true;
    std::uint64_t revision_ = 0;
};

}  // namespace p2000c

#endif  // P2000C_CORE_TERMINAL_H_
