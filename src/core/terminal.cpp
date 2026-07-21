#include "core/terminal.h"

#include <algorithm>

namespace p2000c {

Terminal::Terminal() { reset(); }

void Terminal::reset() {
  screen_.fill(0x20);
  attributes_.fill(kDefaultAttribute);
  input_.clear();
  escape_state_ = EscapeState::kNone;
  cursor_column_ = 0;
  cursor_row_ = 0;
  pending_row_ = 0;
  skip_bytes_ = 0;
  handshake_echoes_ = 0;
  attribute_ = kDefaultAttribute;
  keyboard_locked_ = false;
  cursor_visible_ = true;
  ++revision_;
}

void Terminal::begin_handshake() {
  input_.push_back(0x55);
  handshake_echoes_ = 2;
}

void Terminal::receive(std::uint8_t value) {
  if (handshake_echoes_ > 0) {
    --handshake_echoes_;
    if (handshake_echoes_ == 1) {
      input_.push_back(0xaa);
    }
    return;
  }

  switch (escape_state_) {
    case EscapeState::kCommand:
      escape_command(value);
      return;
    case EscapeState::kCursorRow:
      pending_row_ = std::clamp(static_cast<int>(value) - 0x20, 0, kRows - 1);
      escape_state_ = EscapeState::kCursorColumn;
      return;
    case EscapeState::kCursorColumn:
      cursor_row_ = pending_row_;
      cursor_column_ =
          std::clamp(static_cast<int>(value) - 0x20, 0, kColumns - 1);
      escape_state_ = EscapeState::kNone;
      return;
    case EscapeState::kAttribute:
      attribute_ = value;
      escape_state_ = EscapeState::kNone;
      return;
    case EscapeState::kLockLines:
      escape_state_ = EscapeState::kNone;
      return;
    case EscapeState::kSkipBytes:
      if (--skip_bytes_ == 0) {
        escape_state_ = EscapeState::kNone;
      }
      return;
    case EscapeState::kNone:
      break;
  }

  switch (value) {
    case 0x01:
      cursor_column_ = 0;
      cursor_row_ = 0;
      break;
    case 0x02:
      keyboard_locked_ = false;
      break;
    case 0x04:
      cursor_column_ = kColumns - 1;
      cursor_row_ = kRows - 1;
      break;
    case 0x06:
      if (++cursor_column_ == kColumns) {
        cursor_column_ = 0;
        cursor_down();
      }
      break;
    case 0x07:
      break;
    case 0x08:
    case 0x15:
      cursor_column_ = std::max(cursor_column_ - 1, 0);
      break;
    case 0x09:
      cursor_column_ = std::min((cursor_column_ + 8) & ~7, kColumns - 1);
      break;
    case 0x0a:
      cursor_down();
      break;
    case 0x0c:
      screen_.fill(0x20);
      attributes_.fill(attribute_);
      cursor_column_ = 0;
      cursor_row_ = 0;
      ++revision_;
      break;
    case 0x0d:
      cursor_column_ = 0;
      break;
    case 0x18:
      reset();
      break;
    case 0x19:
      keyboard_locked_ = true;
      break;
    case 0x1a:
      cursor_row_ = std::max(cursor_row_ - 1, 0);
      break;
    case 0x1b:
      escape_state_ = EscapeState::kCommand;
      break;
    default:
      if (value >= 0x20 && value != 0x7f) {
        put_character(value);
      }
      break;
  }
}

void Terminal::queue_key(std::uint8_t value) {
  if (!keyboard_locked_) {
    input_.push_back(value);
  }
}

std::uint8_t Terminal::take_input() {
  if (input_.empty()) {
    return 0;
  }
  const std::uint8_t value = input_.front();
  input_.pop_front();
  return value;
}

void Terminal::put_character(std::uint8_t value) {
  const std::size_t position = cursor_row_ * kColumns + cursor_column_;
  screen_[position] = value;
  attributes_[position] = attribute_;
  ++revision_;
  if (++cursor_column_ == kColumns) {
    cursor_column_ = 0;
    cursor_down();
  }
}

void Terminal::cursor_down() {
  if (++cursor_row_ == kRows) {
    cursor_row_ = kRows - 1;
    scroll_up();
  }
}

void Terminal::scroll_up() {
  std::move(screen_.begin() + kColumns, screen_.end(), screen_.begin());
  std::fill(screen_.end() - kColumns, screen_.end(), 0x20);
  std::move(attributes_.begin() + kColumns, attributes_.end(),
            attributes_.begin());
  std::fill(attributes_.end() - kColumns, attributes_.end(), attribute_);
  ++revision_;
}

void Terminal::scroll_down() {
  std::move_backward(screen_.begin(), screen_.end() - kColumns, screen_.end());
  std::fill(screen_.begin(), screen_.begin() + kColumns, 0x20);
  std::move_backward(attributes_.begin(), attributes_.end() - kColumns,
                     attributes_.end());
  std::fill(attributes_.begin(), attributes_.begin() + kColumns, attribute_);
  ++revision_;
}

void Terminal::escape_command(std::uint8_t value) {
  escape_state_ = EscapeState::kNone;
  switch (value) {
    case 'Y':
      escape_state_ = EscapeState::kCursorRow;
      break;
    case 'K': {
      const auto begin =
          screen_.begin() + cursor_row_ * kColumns + cursor_column_;
      const auto end = screen_.begin() + (cursor_row_ + 1) * kColumns;
      const auto attribute_begin =
          attributes_.begin() + cursor_row_ * kColumns + cursor_column_;
      std::fill(begin, end, 0x20);
      std::fill(attribute_begin,
                attributes_.begin() + (cursor_row_ + 1) * kColumns, attribute_);
    }
      ++revision_;
      break;
    case 'k': {
      const auto begin =
          screen_.begin() + cursor_row_ * kColumns + cursor_column_;
      const auto attribute_begin =
          attributes_.begin() + cursor_row_ * kColumns + cursor_column_;
      std::fill(begin, screen_.end(), 0x20);
      std::fill(attribute_begin, attributes_.end(), attribute_);
    }
      ++revision_;
      break;
    case 'S':
      scroll_up();
      break;
    case 'T':
      scroll_down();
      break;
    case 'C':
      cursor_visible_ = true;
      ++revision_;
      break;
    case 'c':
      cursor_visible_ = false;
      ++revision_;
      break;
    case '0':
      escape_state_ = EscapeState::kAttribute;
      break;
    case 'A':
      escape_state_ = EscapeState::kLockLines;
      break;
    case '@':
      skip_bytes_ = 360;
      escape_state_ = EscapeState::kSkipBytes;
      break;
    case '!':
      skip_bytes_ = 256;
      escape_state_ = EscapeState::kSkipBytes;
      break;
    case '+':
      skip_bytes_ = 2;
      escape_state_ = EscapeState::kSkipBytes;
      break;
    case '?':
      queue_status();
      break;
    default:
      break;
  }
}

void Terminal::queue_status() {
  input_.push_back(static_cast<std::uint8_t>(cursor_column_ + 1));
  input_.push_back(static_cast<std::uint8_t>(cursor_row_ + 1));
  input_.push_back(screen_[cursor_row_ * kColumns + cursor_column_]);
  input_.push_back(keyboard_locked_ ? 0x20 : 0x00);
  input_.insert(input_.end(), 8, 0x00);
}

}  // namespace p2000c
