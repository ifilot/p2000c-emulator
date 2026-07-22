#ifndef P2000C_APP_SCREEN_COLOR_DIALOG_H_
#define P2000C_APP_SCREEN_COLOR_DIALOG_H_

#include <QColor>
#include <QDialog>
#include <QImage>
#include <QWidget>
#include <functional>

#include "app/display_widget.h"

class QCheckBox;
class QLabel;
class QSlider;

namespace p2000c {

/** Circular HSV hue/saturation picker used by the screen settings panel. */
class ScreenColorWheel : public QWidget {
  public:
    explicit ScreenColorWheel(QWidget* parent = nullptr);

    void set_color(const QColor& color);
    QColor color() const;
    void set_color_changed_handler(std::function<void(const QColor&)> handler);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

  private:
    void choose_position(const QPointF& position);
    void rebuild_wheel(int diameter);

    int hue_ = 159;
    int saturation_ = 158;
    QImage wheel_image_;
    std::function<void(const QColor&)> color_changed_handler_;
};

/** Modal settings panel for previewing phosphor color and CRT effects. */
class ScreenColorDialog : public QDialog {
  public:
    explicit ScreenColorDialog(const QColor& initial_color,
                               const CrtEffects& initial_effects,
                               QWidget* parent = nullptr);

    QColor selected_color() const { return selected_color_; }
    const CrtEffects& selected_effects() const { return selected_effects_; }
    void set_preview_handler(
        std::function<void(const QColor&, const CrtEffects&)> handler);

  private:
    void select_color(const QColor& color);
    void select_effects(const CrtEffects& effects);
    void update_preview();

    ScreenColorWheel* wheel_ = nullptr;
    QSlider* brightness_ = nullptr;
    QLabel* swatch_ = nullptr;
    QLabel* value_label_ = nullptr;
    QCheckBox* scanlines_ = nullptr;
    QCheckBox* bloom_ = nullptr;
    QCheckBox* persistence_ = nullptr;
    QCheckBox* curvature_ = nullptr;
    QCheckBox* vignette_ = nullptr;
    QCheckBox* noise_ = nullptr;
    QColor selected_color_;
    CrtEffects selected_effects_;
    std::function<void(const QColor&, const CrtEffects&)> preview_handler_;
};

}  // namespace p2000c

#endif  // P2000C_APP_SCREEN_COLOR_DIALOG_H_
