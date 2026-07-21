#ifndef P2000C_APP_DISPLAY_WIDGET_H_
#define P2000C_APP_DISPLAY_WIDGET_H_

#include <QColor>
#include <QImage>
#include <QWidget>
#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "core/terminal.h"

namespace p2000c {

/** Renders an 80x24 P2000C text display from the original character sheet. */
class DisplayWidget : public QWidget {
  private:
    static constexpr int kColumns = 80;
    static constexpr int kRows = 24;
    static constexpr int kCharacterWidth = 8;
    static constexpr int kCharacterHeight = 12;
    static constexpr int kCharacterSheetPitch = 12;
    static constexpr int kRasterWidth = kColumns * kCharacterWidth;
    static constexpr int kRasterHeight = kRows * kCharacterHeight;
    static constexpr int kDisplayWidth = kRasterWidth * 7 / 8;
    static constexpr int kDisplayHeight = kRasterHeight;

    struct RasterRun {
        int column = 0;
        int scanline = 0;
        int length = 0;
        std::uint8_t attribute = Terminal::kDefaultAttribute;
    };

  public:
    /** Creates an empty display backed by the embedded character sheet. */
    explicit DisplayWidget(QWidget* parent = nullptr);

    /** Returns the authentic green phosphor color used by default. */
    static QColor default_base_color();

    /** Sets the base phosphor color used to derive all screen tones. */
    void set_base_color(const QColor& color);

    /** Returns the current base phosphor color. */
    const QColor& base_color() const { return base_color_; }

    /** Clears the emulated screen to spaces. */
    void clear();

    /** Writes Latin-1 text at an emulated character position. */
    void write_text(int column, int row, std::string_view text);

    /** Replaces the display with an emulated terminal screen and attributes. */
    void set_screen(const Terminal::Screen& screen,
                    const Terminal::AttributeScreen& attributes);

    /** Returns the character cells currently presented by the widget. */
    const Terminal::Screen& screen() const { return characters_; }

    /** Returns the per-cell terminal attributes currently being presented. */
    const Terminal::AttributeScreen& attributes() const { return attributes_; }

    /** Installs the callback receiving translated host key presses. */
    void set_key_handler(std::function<void(std::uint8_t)> handler);

    /** Updates the terminal cursor position and hardware visibility state. */
    void set_cursor(int column, int row, bool visible);

    /** Returns the native 80x24 character-cell presentation size. */
    QSize sizeHint() const override;

    /** Returns a usable minimum size with calibrated CRT dot proportions. */
    QSize minimumSizeHint() const override;

    /** Reports that the preferred height follows the display width. */
    bool hasHeightForWidth() const override { return true; }

    /** Calculates the calibrated display height for a proposed width. */
    int heightForWidth(int width) const override;

  private:
    /** Rebuilds lit-dot runs from the exact 8x12 character generator. */
    void rebuild_raster();

    /** Paints the character raster as phosphor scanline strokes. */
    void paintEvent(QPaintEvent* event) override;

    /** Converts a Qt key event into a terminal input byte. */
    void keyPressEvent(QKeyEvent* event) override;

    QImage character_sheet_;
    std::array<std::uint8_t, kColumns * kRows> characters_{};
    Terminal::AttributeScreen attributes_{};
    std::vector<RasterRun> raster_runs_;
    std::function<void(std::uint8_t)> key_handler_;
    int cursor_column_ = 0;
    int cursor_row_ = 0;
    bool cursor_enabled_ = true;
    bool cursor_phase_ = true;
    bool attribute_blink_phase_ = true;
    QColor base_color_ = default_base_color();
};

}  // namespace p2000c

#endif  // P2000C_APP_DISPLAY_WIDGET_H_
