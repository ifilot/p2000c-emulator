#include "app/display_widget.h"

#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QSizePolicy>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace p2000c {
namespace {

/** Derives a display-palette color while retaining the selected phosphor hue.
 */
QColor phosphor_tone(const QColor& base, qreal saturation_scale,
                     qreal value_scale, int alpha = 255) {
  int hue = 0;
  int saturation = 0;
  int value = 0;
  base.getHsv(&hue, &saturation, &value);
  if (hue < 0) {
    hue = 0;
  }
  return QColor::fromHsv(
      hue, std::clamp(qRound(saturation * saturation_scale), 0, 255),
      std::clamp(qRound(value * value_scale), 0, 255), alpha);
}

/** Returns manual-specified brightness as a phosphor alpha multiplier. */
qreal attribute_alpha_scale(std::uint8_t attribute) {
  const int intensity =
      ((attribute & Terminal::kAttributeIntensityHigh) != 0 ? 2 : 0) |
      ((attribute & Terminal::kAttributeIntensityLow) != 0 ? 1 : 0);
  // Serial values: 00 quarter, 01 bold, 10 normal, 11 half bright.
  constexpr std::array<qreal, 4> kScales = {0.26, 1.35, 1.0, 0.55};
  return kScales[intensity];
}

/** Returns a smaller color-value adjustment supplementing phosphor alpha. */
qreal attribute_value_scale(std::uint8_t attribute) {
  const int intensity =
      ((attribute & Terminal::kAttributeIntensityHigh) != 0 ? 2 : 0) |
      ((attribute & Terminal::kAttributeIntensityLow) != 0 ? 1 : 0);
  // Serial values: 00 quarter, 01 bold, 10 normal, 11 half bright.
  constexpr std::array<qreal, 4> kScales = {0.72, 1.12, 1.0, 0.86};
  return kScales[intensity];
}

/** Derives one phosphor layer at the intensity stored for a character cell. */
QColor attributed_tone(const QColor& base, qreal saturation_scale,
                       qreal value_scale, int alpha, std::uint8_t attribute) {
  return phosphor_tone(
      base, saturation_scale, value_scale * attribute_value_scale(attribute),
      std::clamp(qRound(alpha * attribute_alpha_scale(attribute)), 0, 255));
}

}  // namespace

DisplayWidget::DisplayWidget(QWidget* parent)
    : QWidget(parent), character_sheet_(":/font/P2000C font mini.png") {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent);
  QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  policy.setHeightForWidth(true);
  setSizePolicy(policy);
  character_sheet_ = character_sheet_.convertToFormat(QImage::Format_ARGB32);
  auto* cursor_timer = new QTimer(this);
  connect(cursor_timer, &QTimer::timeout, this, [this]() {
    cursor_phase_ = !cursor_phase_;
    update();
  });
  cursor_timer->start(500);
  auto* attribute_blink_timer = new QTimer(this);
  connect(attribute_blink_timer, &QTimer::timeout, this, [this]() {
    attribute_blink_phase_ = !attribute_blink_phase_;
    update();
  });
  attribute_blink_timer->start(667);
  clear();
}

QColor DisplayWidget::default_base_color() { return QColor(97, 255, 199); }

void DisplayWidget::set_base_color(const QColor& color) {
  if (!color.isValid()) {
    return;
  }
  base_color_ = color.toRgb();
  base_color_.setAlpha(255);
  update();
}

void DisplayWidget::clear() {
  characters_.fill(0x20);
  attributes_.fill(Terminal::kDefaultAttribute);
  rebuild_raster();
  update();
}

void DisplayWidget::write_text(int column, int row, std::string_view text) {
  if (row < 0 || row >= kRows || column >= kColumns) {
    return;
  }
  int destination = std::max(column, 0) + row * kColumns;
  std::size_t source = column < 0 ? static_cast<std::size_t>(-column) : 0;
  while (source < text.size() && destination < (row + 1) * kColumns) {
    characters_[destination++] = static_cast<std::uint8_t>(text[source++]);
    attributes_[destination - 1] = Terminal::kDefaultAttribute;
  }
  rebuild_raster();
  update();
}

void DisplayWidget::set_screen(const Terminal::Screen& screen,
                               const Terminal::AttributeScreen& attributes) {
  characters_ = screen;
  attributes_ = attributes;
  rebuild_raster();
  update();
}

void DisplayWidget::set_key_handler(std::function<void(std::uint8_t)> handler) {
  key_handler_ = std::move(handler);
}

void DisplayWidget::set_cursor(int column, int row, bool visible) {
  cursor_column_ = std::clamp(column, 0, kColumns - 1);
  cursor_row_ = std::clamp(row, 0, kRows - 1);
  cursor_enabled_ = visible;
  cursor_phase_ = true;
  update();
}

QSize DisplayWidget::sizeHint() const { return {980, 504}; }

QSize DisplayWidget::minimumSizeHint() const {
  return {kDisplayWidth, kDisplayHeight};
}

int DisplayWidget::heightForWidth(int width) const {
  return width * kDisplayHeight / kDisplayWidth;
}

void DisplayWidget::rebuild_raster() {
  raster_runs_.clear();
  if (character_sheet_.isNull()) {
    return;
  }

  for (int scanline = 0; scanline < kRasterHeight; ++scanline) {
    const int character_row = scanline / kCharacterHeight;
    const int glyph_row = scanline % kCharacterHeight;
    int run_start = -1;
    std::uint8_t run_attribute = Terminal::kDefaultAttribute;
    for (int column = 0; column <= kRasterWidth; ++column) {
      bool dot = false;
      std::uint8_t attribute = Terminal::kDefaultAttribute;
      if (column < kRasterWidth) {
        const int character_column = column / kCharacterWidth;
        const int glyph_column = column % kCharacterWidth;
        const int position = character_row * kColumns + character_column;
        const std::uint8_t character = characters_[position];
        attribute = attributes_[position];
        const int source_x =
            (character & 0x0f) * kCharacterSheetPitch + glyph_column;
        const int source_y =
            (character >> 4) * kCharacterSheetPitch + glyph_row;
        dot = qGreen(character_sheet_.pixel(source_x, source_y)) != 0;
        if ((attribute & Terminal::kAttributeUnderline) != 0 &&
            glyph_row == 9) {
          dot = true;
        }
        if ((attribute & Terminal::kAttributeInverse) != 0) {
          dot = !dot;
        }
      }
      if (dot && run_start >= 0 && attribute != run_attribute) {
        raster_runs_.push_back(
            {run_start, scanline, column - run_start, run_attribute});
        run_start = column;
        run_attribute = attribute;
      } else if (dot && run_start < 0) {
        run_start = column;
        run_attribute = attribute;
      } else if (!dot && run_start >= 0) {
        raster_runs_.push_back(
            {run_start, scanline, column - run_start, run_attribute});
        run_start = -1;
      }
    }
  }
}

void DisplayWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), phosphor_tone(base_color_, 0.9, 0.012));

  const qreal scale = std::min(static_cast<qreal>(width()) / kDisplayWidth,
                               static_cast<qreal>(height()) / kDisplayHeight);
  const QSizeF display_size(kDisplayWidth * scale, kDisplayHeight * scale);
  const QRectF display((width() - display_size.width()) / 2.0,
                       (height() - display_size.height()) / 2.0,
                       display_size.width(), display_size.height());

  QRadialGradient screen_tone(
      display.center(), display.width() * 0.72,
      display.center() -
          QPointF(display.width() * 0.08, display.height() * 0.10));
  screen_tone.setColorAt(0.0, phosphor_tone(base_color_, 0.95, 0.067));
  screen_tone.setColorAt(0.72, phosphor_tone(base_color_, 1.0, 0.043));
  screen_tone.setColorAt(1.0, phosphor_tone(base_color_, 1.0, 0.020));
  painter.fillRect(display, screen_tone);

  painter.save();
  painter.setClipRect(display);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const qreal dot_width = display.width() / kRasterWidth;
  const qreal scanline_height = display.height() / kRasterHeight;
  const qreal beam_height = scanline_height * 0.74;

  auto run_rect = [&](const RasterRun& run) {
    return QRectF(display.left() + run.column * dot_width,
                  display.top() + run.scanline * scanline_height +
                      (scanline_height - beam_height) / 2.0,
                  run.length * dot_width, beam_height);
  };
  auto for_each_visible_run = [&](const auto& draw_run) {
    for (const RasterRun& run : raster_runs_) {
      if ((run.attribute & Terminal::kAttributeBlink) == 0 ||
          attribute_blink_phase_) {
        draw_run(run);
      }
    }
    if (cursor_enabled_ && cursor_phase_) {
      for (int row = 0; row < kCharacterHeight; ++row) {
        draw_run(RasterRun{cursor_column_ * kCharacterWidth,
                           cursor_row_ * kCharacterHeight + row,
                           kCharacterWidth, Terminal::kDefaultAttribute});
      }
    }
  };

  painter.setPen(Qt::NoPen);
  for_each_visible_run([&](const RasterRun& run) {
    painter.setBrush(attributed_tone(base_color_, 1.6, 1.0, 45, run.attribute));
    const QRectF stroke =
        run_rect(run).adjusted(-dot_width * 0.34, -scanline_height * 0.26,
                               dot_width * 0.34, scanline_height * 0.26);
    painter.drawRoundedRect(stroke, scanline_height * 0.42,
                            scanline_height * 0.42);
  });

  for_each_visible_run([&](const RasterRun& run) {
    painter.setBrush(
        attributed_tone(base_color_, 1.5, 0.83, 220, run.attribute));
    const QRectF stroke =
        run_rect(run).adjusted(-dot_width * 0.10, -scanline_height * 0.08,
                               dot_width * 0.10, scanline_height * 0.08);
    painter.drawRoundedRect(stroke, scanline_height * 0.34,
                            scanline_height * 0.34);
  });

  for_each_visible_run([&](const RasterRun& run) {
    painter.setBrush(
        attributed_tone(base_color_, 1.0, 1.0, 245, run.attribute));
    const QRectF stroke = run_rect(run);
    painter.drawRoundedRect(stroke, scanline_height * 0.28,
                            scanline_height * 0.28);
  });

  for_each_visible_run([&](const RasterRun& run) {
    painter.setBrush(
        attributed_tone(base_color_, 0.25, 1.0, 78, run.attribute));
    const QRectF stroke =
        run_rect(run).adjusted(dot_width * 0.04, scanline_height * 0.10,
                               -dot_width * 0.04, -scanline_height * 0.22);
    painter.drawRoundedRect(stroke, scanline_height * 0.18,
                            scanline_height * 0.18);
  });

  QRadialGradient vignette(display.center(), display.width() * 0.68);
  vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
  vignette.setColorAt(0.62, QColor(0, 0, 0, 3));
  vignette.setColorAt(1.0, QColor(0, 0, 0, 72));
  painter.fillRect(display, vignette);
  painter.restore();

  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(phosphor_tone(base_color_, 0.55, 0.23, 120), 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(display.adjusted(0.5, 0.5, -0.5, -0.5));
}

void DisplayWidget::keyPressEvent(QKeyEvent* event) {
  if (!key_handler_) {
    QWidget::keyPressEvent(event);
    return;
  }
  std::uint8_t value = 0;
  switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
      value = 0x0d;
      break;
    case Qt::Key_Backspace:
      value = 0x08;
      break;
    case Qt::Key_Tab:
      value = 0x09;
      break;
    case Qt::Key_Escape:
      value = 0x1b;
      break;
    default: {
      const QByteArray bytes = event->text().toLatin1();
      if (!bytes.isEmpty()) {
        value = static_cast<std::uint8_t>(bytes.front());
      }
      break;
    }
  }
  if (value != 0) {
    key_handler_(value);
    event->accept();
  } else {
    QWidget::keyPressEvent(event);
  }
}

}  // namespace p2000c
