#include "app/display_widget.h"

#include <QKeyEvent>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QSizePolicy>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace p2000c {
namespace {

/** Unaddressable glass margin visible around the P2000C's active raster. */
constexpr qreal kCrtOverscanFraction = 0.035;

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
    invalidate_emission();
    update();
  });
  cursor_timer->start(500);
  auto* attribute_blink_timer = new QTimer(this);
  connect(attribute_blink_timer, &QTimer::timeout, this, [this]() {
    attribute_blink_phase_ = !attribute_blink_phase_;
    invalidate_emission();
    update();
  });
  attribute_blink_timer->start(667);
  effect_timer_ = new QTimer(this);
  effect_timer_->setTimerType(Qt::PreciseTimer);
  connect(effect_timer_, &QTimer::timeout, this, [this]() {
    ++temporal_phase_;
    update();
  });
  update_effect_timer();
  clear();
}

QColor DisplayWidget::default_base_color() { return QColor(97, 255, 199); }

void DisplayWidget::set_base_color(const QColor& color) {
  if (!color.isValid()) {
    return;
  }
  base_color_ = color.toRgb();
  base_color_.setAlpha(255);
  invalidate_emission(true);
  update();
}

void DisplayWidget::set_crt_effects(const CrtEffects& effects) {
  CrtEffects normalized = effects;
  normalized.persistence_half_life_ms = std::clamp(
      normalized.persistence_half_life_ms,
      CrtEffects::kMinimumPersistenceHalfLifeMs,
      CrtEffects::kMaximumPersistenceHalfLifeMs);
  normalized.brightness_percent = std::clamp(
      normalized.brightness_percent, CrtEffects::kMinimumBrightnessPercent,
      CrtEffects::kMaximumBrightnessPercent);
  if (crt_effects_ == normalized) {
    return;
  }
  crt_effects_ = normalized;
  invalidate_emission(true);
  update_effect_timer();
  update();
}

QImage DisplayWidget::capture_screenshot() {
  QImage screenshot(size(), QImage::Format_ARGB32_Premultiplied);
  screenshot.fill(Qt::black);
  render(&screenshot);
  return screenshot;
}

void DisplayWidget::clear() {
  characters_.fill(0x20);
  attributes_.fill(Terminal::kDefaultAttribute);
  graphic_screen_.fill(0);
  graphics_mode_ = Terminal::GraphicsMode::kCharacter;
  rebuild_raster();
  invalidate_emission();
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
  invalidate_emission();
  update();
}

void DisplayWidget::set_screen(const Terminal::Screen& screen,
                               const Terminal::AttributeScreen& attributes) {
  graphic_screen_.fill(0);
  graphics_mode_ = Terminal::GraphicsMode::kCharacter;
  characters_ = screen;
  attributes_ = attributes;
  rebuild_raster();
  invalidate_emission();
  update();
}

void DisplayWidget::set_screen(
    const Terminal::Screen& screen,
    const Terminal::AttributeScreen& attributes,
    Terminal::GraphicsMode graphics_mode,
    const Terminal::GraphicScreen& graphic_screen) {
  characters_ = screen;
  attributes_ = attributes;
  graphics_mode_ = graphics_mode;
  graphic_screen_ = graphic_screen;
  rebuild_raster();
  invalidate_emission();
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
  invalidate_emission();
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

  const bool graphics =
      graphics_mode_ != Terminal::GraphicsMode::kCharacter;
  const int raster_width = graphics ? Terminal::kGraphicWidth
                                    : kTextRasterWidth;
  const int raster_height = graphics ? Terminal::kGraphicHeight
                                     : kTextRasterHeight;
  const int text_columns = graphics ? 64 : kColumns;

  auto graphic_attribute = [&](int column, int scanline) {
    const std::size_t address =
        static_cast<std::size_t>(scanline * Terminal::kGraphicBytesPerLine);
    if (graphics_mode_ == Terminal::GraphicsMode::kHigh512) {
      const std::uint8_t byte = graphic_screen_[address + column / 8];
      return (byte & (0x80 >> (column & 7))) != 0
                 ? Terminal::kDefaultAttribute
                 : std::uint8_t{0};
    }

    const int logical_column = column / 2;
    const std::uint8_t byte = graphic_screen_[address + logical_column / 4];
    const int pixel = logical_column & 3;
    const bool high = (byte & (0x80 >> pixel)) != 0;
    const bool low = (byte & (0x08 >> pixel)) != 0;
    if (!high && !low) {
      return std::uint8_t{0};
    }
    if (high && low) {
      return Terminal::kAttributeIntensityLow;  // bold
    }
    if (high) {
      return Terminal::kDefaultAttribute;  // normal
    }
    return static_cast<std::uint8_t>(Terminal::kAttributeIntensityHigh |
                                     Terminal::kAttributeIntensityLow);
  };

  for (int scanline = 0; scanline < raster_height; ++scanline) {
    const int character_row = scanline / kCharacterHeight;
    const int glyph_row = scanline % kCharacterHeight;
    int run_start = -1;
    std::uint8_t run_attribute = Terminal::kDefaultAttribute;
    for (int column = 0; column <= raster_width; ++column) {
      bool dot = false;
      std::uint8_t attribute = Terminal::kDefaultAttribute;
      if (column < raster_width) {
        const int character_column = column / kCharacterWidth;
        const int glyph_column = column % kCharacterWidth;
        const int position = character_row * text_columns + character_column;
        const std::uint8_t character = characters_[position];
        const int source_x =
            (character & 0x0f) * kCharacterSheetPitch + glyph_column;
        const int source_y =
            (character >> 4) * kCharacterSheetPitch + glyph_row;
        bool character_dot =
            qGreen(character_sheet_.pixel(source_x, source_y)) != 0;
        if (!graphics) {
          attribute = attributes_[position];
          if ((attribute & Terminal::kAttributeUnderline) != 0 &&
              glyph_row == 9) {
            character_dot = true;
          }
          if ((attribute & Terminal::kAttributeInverse) != 0) {
            character_dot = !character_dot;
          }
          dot = character_dot;
        } else {
          attribute = graphic_attribute(column, scanline);
          if (character_dot) {
            if (attribute == 0) {
              attribute = graphics_mode_ == Terminal::GraphicsMode::kHigh512
                              ? Terminal::kDefaultAttribute
                              : Terminal::kAttributeIntensityLow;
            } else if (graphics_mode_ == Terminal::GraphicsMode::kHigh512) {
              attribute = 0;
            } else {
              // Character dots invert both medium-resolution bit planes.
              const int intensity =
                  ((attribute & Terminal::kAttributeIntensityHigh) != 0 ? 2
                                                                        : 0) |
                  ((attribute & Terminal::kAttributeIntensityLow) != 0 ? 1
                                                                       : 0);
              constexpr std::array<std::uint8_t, 4> kInvertedIntensity = {
                  Terminal::kAttributeIntensityLow,
                  std::uint8_t{0},
                  Terminal::kAttributeIntensityHigh |
                      Terminal::kAttributeIntensityLow,
                  Terminal::kDefaultAttribute,
              };
              attribute = kInvertedIntensity[intensity];
            }
          }
          dot = attribute != 0;
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

void DisplayWidget::invalidate_emission(bool clear_persistence) {
  emission_cache_ = {};
  if (clear_persistence) {
    persistence_layer_ = {};
    persistence_clock_.invalidate();
  }
}

void DisplayWidget::update_effect_timer() {
  if (effect_timer_ == nullptr) {
    return;
  }
  if (crt_effects_.persistence || crt_effects_.noise ||
      crt_effects_.flicker) {
    effect_timer_->start(16);
  } else {
    effect_timer_->stop();
  }
}

QImage DisplayWidget::build_emission(const QRectF& display,
                                     const QRectF& raster) const {
  QImage emission(size(), QImage::Format_ARGB32_Premultiplied);
  emission.fill(Qt::transparent);
  QPainter painter(&emission);
  painter.setClipRect(display);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);

  const bool graphics =
      graphics_mode_ != Terminal::GraphicsMode::kCharacter;
  const int raster_width = graphics ? Terminal::kGraphicWidth
                                    : kTextRasterWidth;
  const int raster_height = graphics ? Terminal::kGraphicHeight
                                     : kTextRasterHeight;
  const qreal dot_width = raster.width() / raster_width;
  const qreal scanline_height = raster.height() / raster_height;
  const qreal base_beam_height =
      scanline_height * (crt_effects_.scanlines ? 0.58 : 0.94);

  auto beam_scale = [](std::uint8_t attribute) {
    const int intensity =
        ((attribute & Terminal::kAttributeIntensityHigh) != 0 ? 2 : 0) |
        ((attribute & Terminal::kAttributeIntensityLow) != 0 ? 1 : 0);
    constexpr std::array<qreal, 4> kScales = {0.82, 1.10, 1.0, 0.92};
    return kScales[intensity];
  };
  auto run_rect = [&](const RasterRun& run) {
    const qreal beam_height = base_beam_height * beam_scale(run.attribute);
    return QRectF(raster.left() + run.column * dot_width,
                  raster.top() + run.scanline * scanline_height +
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

  if (crt_effects_.bloom) {
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    for_each_visible_run([&](const RasterRun& run) {
      painter.setBrush(
          attributed_tone(base_color_, 1.55, 0.92, 22, run.attribute));
      const QRectF stroke =
          run_rect(run).adjusted(-dot_width * 0.38,
                                 -scanline_height * 0.16,
                                 dot_width * 0.38,
                                 scanline_height * 0.16);
      painter.drawRoundedRect(stroke, scanline_height * 0.38,
                              scanline_height * 0.38);
    });
    for_each_visible_run([&](const RasterRun& run) {
      painter.setBrush(
          attributed_tone(base_color_, 1.42, 0.88, 82, run.attribute));
      const QRectF stroke =
          run_rect(run).adjusted(-dot_width * 0.12,
                                 -scanline_height * 0.035,
                                 dot_width * 0.12,
                                 scanline_height * 0.035);
      painter.drawRoundedRect(stroke, scanline_height * 0.26,
                              scanline_height * 0.26);
    });
  }

  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  for_each_visible_run([&](const RasterRun& run) {
    painter.setBrush(
        attributed_tone(base_color_, 1.0, 1.0, 238, run.attribute));
    const QRectF stroke = run_rect(run);
    painter.drawRoundedRect(stroke, scanline_height * 0.23,
                            scanline_height * 0.23);
  });
  for_each_visible_run([&](const RasterRun& run) {
    painter.setBrush(
        attributed_tone(base_color_, 0.22, 1.0, 68, run.attribute));
    const QRectF stroke =
        run_rect(run).adjusted(dot_width * 0.05, scanline_height * 0.08,
                               -dot_width * 0.05,
                               -scanline_height * 0.17);
    painter.drawRoundedRect(stroke, scanline_height * 0.13,
                            scanline_height * 0.13);
  });
  if (crt_effects_.scanlines) {
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.setBrush(QColor(0, 0, 0, 190));
    const qreal gap_height = std::max<qreal>(0.34, scanline_height * 0.26);
    for (int scanline = 1; scanline < raster_height; ++scanline) {
      const qreal boundary = raster.top() + scanline * scanline_height;
      painter.drawRect(QRectF(raster.left(), boundary - gap_height / 2.0,
                              raster.width(), gap_height));
    }
  }
  return emission;
}

QImage DisplayWidget::curve_emission(const QImage& source,
                                     const QRectF& display) const {
  QImage horizontal(source.size(), QImage::Format_ARGB32_Premultiplied);
  horizontal.fill(Qt::transparent);
  QPainter horizontal_painter(&horizontal);
  const int top = std::max(0, qFloor(display.top()));
  const int bottom = std::min(source.height(), qCeil(display.bottom()));
  for (int y = top; y < bottom; ++y) {
    const qreal normalized =
        (y + 0.5 - display.center().y()) / (display.height() / 2.0);
    const qreal inset = display.width() * 0.010 * normalized * normalized;
    horizontal_painter.drawImage(
        QRectF(display.left() + inset, y, display.width() - 2.0 * inset, 1.0),
        source, QRectF(display.left(), y, display.width(), 1.0));
  }
  horizontal_painter.end();

  QImage curved(source.size(), QImage::Format_ARGB32_Premultiplied);
  curved.fill(Qt::transparent);
  QPainter vertical_painter(&curved);
  const int left = std::max(0, qFloor(display.left()));
  const int right = std::min(source.width(), qCeil(display.right()));
  for (int x = left; x < right; ++x) {
    const qreal normalized =
        (x + 0.5 - display.center().x()) / (display.width() / 2.0);
    const qreal inset = display.height() * 0.010 * normalized * normalized;
    vertical_painter.drawImage(
        QRectF(x, display.top() + inset, 1.0,
               display.height() - 2.0 * inset),
        horizontal, QRectF(x, display.top(), 1.0, display.height()));
  }
  return curved;
}

const QImage& DisplayWidget::update_persistence(
    const QImage& current_emission) {
  if (persistence_layer_.size() != current_emission.size()) {
    persistence_layer_ = QImage(current_emission.size(),
                                QImage::Format_ARGB32_Premultiplied);
    persistence_layer_.fill(Qt::transparent);
    persistence_clock_.start();
  }
  const qint64 elapsed = persistence_clock_.isValid()
                             ? persistence_clock_.restart()
                             : 0;
  const qreal retained =
      std::pow(0.5, elapsed /
                        static_cast<qreal>(
                            crt_effects_.persistence_half_life_ms));
  // Decay the retained premultiplied channels, then take an exact component
  // maximum with current emission. Unlike a generic painter blend mode, this
  // makes a stationary image bit-for-bit stable and only changes afterglow
  // where the electron beam has actually moved or switched off.
  for (int y = 0; y < persistence_layer_.height(); ++y) {
    auto* retained_pixels = reinterpret_cast<QRgb*>(
        persistence_layer_.scanLine(y));
    const auto* current_pixels = reinterpret_cast<const QRgb*>(
        current_emission.constScanLine(y));
    for (int x = 0; x < persistence_layer_.width(); ++x) {
      const QRgb previous = retained_pixels[x];
      const QRgb current = current_pixels[x];
      const int red = std::max(qRed(current), qRound(qRed(previous) * retained));
      const int green =
          std::max(qGreen(current), qRound(qGreen(previous) * retained));
      const int blue =
          std::max(qBlue(current), qRound(qBlue(previous) * retained));
      const int alpha =
          std::max(qAlpha(current), qRound(qAlpha(previous) * retained));
      retained_pixels[x] = qRgba(red, green, blue, alpha);
    }
  }
  return persistence_layer_;
}

void DisplayWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const qreal brightness = crt_effects_.brightness_percent / 100.0;
  painter.fillRect(rect(), palette().color(QPalette::Window));

  const qreal scale = std::min(static_cast<qreal>(width()) / kDisplayWidth,
                               static_cast<qreal>(height()) / kDisplayHeight);
  const QSizeF display_size(kDisplayWidth * scale, kDisplayHeight * scale);
  const QRectF display((width() - display_size.width()) / 2.0,
                       (height() - display_size.height()) / 2.0,
                       display_size.width(), display_size.height());
  const qreal corner_radius =
      std::clamp(display.height() * 0.035, 6.0, 18.0);
  QPainterPath glass_shape;
  glass_shape.addRoundedRect(display, corner_radius, corner_radius);
  const QRectF active_area = display.adjusted(
      display.width() * kCrtOverscanFraction,
      display.height() * kCrtOverscanFraction,
      -display.width() * kCrtOverscanFraction,
      -display.height() * kCrtOverscanFraction);
  painter.save();
  painter.setClipPath(glass_shape);
  painter.fillRect(display,
                   phosphor_tone(base_color_, 0.9, 0.012 * brightness));

  QRadialGradient screen_tone(
      display.center(), display.width() * 0.72,
      display.center() -
          QPointF(display.width() * 0.08, display.height() * 0.10));
  screen_tone.setColorAt(
      0.0, phosphor_tone(base_color_, 0.95, 0.067 * brightness));
  screen_tone.setColorAt(
      0.72, phosphor_tone(base_color_, 1.0, 0.043 * brightness));
  screen_tone.setColorAt(
      1.0, phosphor_tone(base_color_, 1.0, 0.020 * brightness));
  painter.fillRect(display, screen_tone);

  const bool graphics =
      graphics_mode_ != Terminal::GraphicsMode::kCharacter;
  const int raster_width = graphics ? Terminal::kGraphicWidth
                                    : kTextRasterWidth;
  const int raster_height = graphics ? Terminal::kGraphicHeight
                                     : kTextRasterHeight;
  const QSizeF raster_size(active_area.width() * raster_width /
                               kTextRasterWidth,
                           active_area.height() * raster_height /
                               kTextRasterHeight);
  const QRectF raster(active_area.center() -
                          QPointF(raster_size.width() / 2.0,
                                  raster_size.height() / 2.0),
                      raster_size);

  if (emission_cache_.size() != size()) {
    emission_cache_ = build_emission(display, raster);
    if (crt_effects_.curvature) {
      emission_cache_ = curve_emission(emission_cache_, display);
    }
  }
  const QImage& visible_emission =
      crt_effects_.persistence ? update_persistence(emission_cache_)
                               : emission_cache_;
  qreal emission_intensity = brightness;
  if (crt_effects_.flicker) {
    constexpr qreal kTwoPi = 6.2831853071795864769;
    const qreal phase = static_cast<qreal>(temporal_phase_ % 60) / 60.0;
    emission_intensity *=
        0.955 + 0.045 * std::sin(kTwoPi * phase * 10.0);
  }
  painter.save();
  if (emission_intensity <= 1.0) {
    painter.setOpacity(emission_intensity);
    painter.drawImage(0, 0, visible_emission);
  } else {
    painter.drawImage(0, 0, visible_emission);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.setOpacity(std::min<qreal>(0.5, emission_intensity - 1.0));
    painter.drawImage(0, 0, visible_emission);
  }
  painter.restore();

  painter.save();
  painter.setClipRect(display);
  if (crt_effects_.noise) {
    std::uint32_t state = temporal_phase_ * 747796405U + 2891336453U;
    const int points = std::max(1, qRound(display.width() * display.height() /
                                         1350.0));
    for (int index = 0; index < points; ++index) {
      state = state * 1664525U + 1013904223U;
      const int x = qFloor(display.left()) +
                    static_cast<int>(state %
                                     std::max(1, qFloor(display.width())));
      state = state * 1664525U + 1013904223U;
      const int y = qFloor(display.top()) +
                    static_cast<int>(state %
                                     std::max(1, qFloor(display.height())));
      painter.setPen((state & 0x100U) != 0
                         ? phosphor_tone(base_color_, 0.5, 0.55, 9)
                         : QColor(0, 0, 0, 8));
      painter.drawPoint(x, y);
    }
  }
  if (crt_effects_.vignette) {
    QRadialGradient vignette(display.center(), display.width() * 0.68);
    vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
    vignette.setColorAt(0.62, QColor(0, 0, 0, 3));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 72));
    painter.fillRect(display, vignette);
    QLinearGradient glass(display.topLeft(), display.bottomRight());
    glass.setColorAt(0.0, QColor(255, 255, 255, 7));
    glass.setColorAt(0.34, QColor(255, 255, 255, 0));
    glass.setColorAt(1.0, QColor(0, 0, 0, 8));
    painter.fillRect(display, glass);
  }
  painter.restore();

  painter.restore();
  painter.setPen(QPen(phosphor_tone(base_color_, 0.55, 0.23, 120), 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawRoundedRect(display.adjusted(0.5, 0.5, -0.5, -0.5),
                          corner_radius, corner_radius);
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
