#ifndef P2000C_APP_DISPLAY_WIDGET_H_
#define P2000C_APP_DISPLAY_WIDGET_H_

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QRectF>
#include <QWidget>
#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "core/terminal.h"

class QTimer;

namespace p2000c {

/** Independently selectable optical effects applied after raster generation. */
struct CrtEffects {
    bool scanlines = true;
    bool bloom = true;
    bool persistence = true;
    bool curvature = true;
    bool vignette = true;
    bool noise = false;

    bool operator==(const CrtEffects&) const = default;
};

/** Renders the P2000C terminal's character and mixed graphics modes. */
class DisplayWidget : public QWidget {
  private:
    static constexpr int kColumns = 80;
    static constexpr int kRows = 24;
    static constexpr int kCharacterWidth = 8;
    static constexpr int kCharacterHeight = 12;
    static constexpr int kCharacterSheetPitch = 12;
    static constexpr int kTextRasterWidth = kColumns * kCharacterWidth;
    static constexpr int kTextRasterHeight = kRows * kCharacterHeight;
    static constexpr int kDisplayWidth = kTextRasterWidth * 7 / 8;
    static constexpr int kDisplayHeight = kTextRasterHeight;

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

    /** Returns the recommended authentic CRT-effect selection. */
    static CrtEffects default_crt_effects() { return {}; }

    /** Sets the base phosphor color used to derive all screen tones. */
    void set_base_color(const QColor& color);

    /** Returns the current base phosphor color. */
    const QColor& base_color() const { return base_color_; }

    /** Selects the optical effects applied to the generated video raster. */
    void set_crt_effects(const CrtEffects& effects);

    /** Returns the current optical-effect selection. */
    const CrtEffects& crt_effects() const { return crt_effects_; }

    /** Clears the emulated screen to spaces. */
    void clear();

    /** Writes Latin-1 text at an emulated character position. */
    void write_text(int column, int row, std::string_view text);

    /** Replaces the display with an emulated terminal screen and attributes. */
    void set_screen(const Terminal::Screen& screen,
                    const Terminal::AttributeScreen& attributes);

    /** Replaces both video planes and selects the terminal's active mode. */
    void set_screen(const Terminal::Screen& screen,
                    const Terminal::AttributeScreen& attributes,
                    Terminal::GraphicsMode graphics_mode,
                    const Terminal::GraphicScreen& graphic_screen);

    /** Returns the character cells currently presented by the widget. */
    const Terminal::Screen& screen() const { return characters_; }

    /** Returns the per-cell terminal attributes currently being presented. */
    const Terminal::AttributeScreen& attributes() const { return attributes_; }

    /** Returns the graphics mode currently being presented. */
    Terminal::GraphicsMode graphics_mode() const { return graphics_mode_; }

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

    /** Invalidates the cached current phosphor-emission image. */
    void invalidate_emission(bool clear_persistence = false);

    /** Starts or stops periodic repaints needed by temporal effects. */
    void update_effect_timer();

    /** Renders current lit-dot runs into a transparent emission image. */
    QImage build_emission(const QRectF& display, const QRectF& raster) const;

    /** Applies a restrained two-axis barrel warp to an emission image. */
    QImage curve_emission(const QImage& source, const QRectF& display) const;

    /** Fades and updates the retained phosphor-emission layer. */
    const QImage& update_persistence(const QImage& current_emission);

    /** Paints the character raster as phosphor scanline strokes. */
    void paintEvent(QPaintEvent* event) override;

    /** Converts a Qt key event into a terminal input byte. */
    void keyPressEvent(QKeyEvent* event) override;

    QImage character_sheet_;
    std::array<std::uint8_t, kColumns * kRows> characters_{};
    Terminal::AttributeScreen attributes_{};
    Terminal::GraphicScreen graphic_screen_{};
    std::vector<RasterRun> raster_runs_;
    std::function<void(std::uint8_t)> key_handler_;
    QImage emission_cache_;
    QImage persistence_layer_;
    QElapsedTimer persistence_clock_;
    QTimer* effect_timer_ = nullptr;
    int cursor_column_ = 0;
    int cursor_row_ = 0;
    bool cursor_enabled_ = true;
    bool cursor_phase_ = true;
    bool attribute_blink_phase_ = true;
    std::uint32_t noise_phase_ = 0;
    Terminal::GraphicsMode graphics_mode_ =
        Terminal::GraphicsMode::kCharacter;
    QColor base_color_ = default_base_color();
    CrtEffects crt_effects_ = default_crt_effects();
};

}  // namespace p2000c

#endif  // P2000C_APP_DISPLAY_WIDGET_H_
