#include "app/screen_color_dialog.h"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "app/display_widget.h"

namespace p2000c {
namespace {

constexpr qreal kPi = 3.14159265358979323846;

}  // namespace

ScreenColorWheel::ScreenColorWheel(QWidget* parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::CrossCursor);
  setAccessibleName("Screen color wheel");
}

void ScreenColorWheel::set_color(const QColor& color) {
  if (!color.isValid()) {
    return;
  }
  int value = 0;
  color.getHsv(&hue_, &saturation_, &value);
  if (hue_ < 0) {
    hue_ = 0;
  }
  update();
}

QColor ScreenColorWheel::color() const {
  return QColor::fromHsv(hue_, saturation_, 255);
}

void ScreenColorWheel::set_color_changed_handler(
    std::function<void(const QColor&)> handler) {
  color_changed_handler_ = std::move(handler);
}

QSize ScreenColorWheel::sizeHint() const { return {270, 270}; }

QSize ScreenColorWheel::minimumSizeHint() const { return {180, 180}; }

void ScreenColorWheel::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Left:
      hue_ = (hue_ + 358) % 360;
      break;
    case Qt::Key_Right:
      hue_ = (hue_ + 2) % 360;
      break;
    case Qt::Key_Up:
      saturation_ = std::min(saturation_ + 5, 255);
      break;
    case Qt::Key_Down:
      saturation_ = std::max(saturation_ - 5, 0);
      break;
    default:
      QWidget::keyPressEvent(event);
      return;
  }
  update();
  if (color_changed_handler_) {
    color_changed_handler_(color());
  }
  event->accept();
}

void ScreenColorWheel::mouseMoveEvent(QMouseEvent* event) {
  if (event->buttons().testFlag(Qt::LeftButton)) {
    choose_position(event->position());
  }
}

void ScreenColorWheel::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    setFocus(Qt::MouseFocusReason);
    choose_position(event->position());
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void ScreenColorWheel::choose_position(const QPointF& position) {
  const QPointF center(width() / 2.0, height() / 2.0);
  const qreal radius = std::max(1.0, std::min(width(), height()) / 2.0 - 8.0);
  const QPointF offset = position - center;
  const qreal distance = std::hypot(offset.x(), offset.y());
  qreal angle = std::atan2(-offset.y(), offset.x()) * 180.0 / kPi;
  if (angle < 0.0) {
    angle += 360.0;
  }
  hue_ = qRound(angle) % 360;
  saturation_ = std::clamp(qRound(distance / radius * 255.0), 0, 255);
  update();
  if (color_changed_handler_) {
    color_changed_handler_(color());
  }
}

void ScreenColorWheel::rebuild_wheel(int diameter) {
  wheel_image_ =
      QImage(diameter, diameter, QImage::Format_ARGB32_Premultiplied);
  wheel_image_.fill(Qt::transparent);
  const qreal radius = diameter / 2.0;
  const QPointF center(radius, radius);
  for (int y = 0; y < diameter; ++y) {
    auto* pixels = reinterpret_cast<QRgb*>(wheel_image_.scanLine(y));
    for (int x = 0; x < diameter; ++x) {
      const qreal dx = (x + 0.5) - center.x();
      const qreal dy = (y + 0.5) - center.y();
      const qreal distance = std::hypot(dx, dy);
      if (distance > radius) {
        continue;
      }
      qreal angle = std::atan2(-dy, dx) * 180.0 / kPi;
      if (angle < 0.0) {
        angle += 360.0;
      }
      const int hue = qRound(angle) % 360;
      const int saturation =
          std::clamp(qRound(distance / radius * 255.0), 0, 255);
      pixels[x] = QColor::fromHsv(hue, saturation, 255).rgba();
    }
  }
}

void ScreenColorWheel::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const int diameter = std::max(1, std::min(width(), height()) - 16);
  if (wheel_image_.width() != diameter) {
    rebuild_wheel(diameter);
  }
  const QRectF wheel_rect((width() - diameter) / 2.0,
                          (height() - diameter) / 2.0, diameter, diameter);

  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0, 0, 0, 55));
  painter.drawEllipse(wheel_rect.translated(0.0, 3.0).adjusted(-3, -3, 3, 3));
  painter.drawImage(wheel_rect, wheel_image_);

  const qreal radius = diameter / 2.0;
  const qreal radians = hue_ * kPi / 180.0;
  const qreal selection_radius = radius * saturation_ / 255.0;
  const QPointF marker(
      wheel_rect.center().x() + std::cos(radians) * selection_radius,
      wheel_rect.center().y() - std::sin(radians) * selection_radius);
  painter.setBrush(color());
  painter.setPen(QPen(QColor(255, 255, 255, 235), 2.0));
  painter.drawEllipse(marker, 7.0, 7.0);
  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(0, 0, 0, 180), 1.0));
  painter.drawEllipse(marker, 9.0, 9.0);

  if (hasFocus()) {
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2.0));
    painter.drawEllipse(wheel_rect.adjusted(-4, -4, 4, 4));
  }
}

ScreenColorDialog::ScreenColorDialog(const QColor& initial_color,
                                     QWidget* parent)
    : QDialog(parent),
      selected_color_(initial_color.isValid()
                          ? initial_color
                          : DisplayWidget::default_base_color()) {
  setWindowTitle("Screen Color");
  setModal(true);
  setMinimumWidth(340);

  auto* layout = new QVBoxLayout(this);
  auto* introduction = new QLabel(
      "Choose the phosphor hue and saturation. Changes are previewed on the "
      "emulated display.",
      this);
  introduction->setWordWrap(true);
  layout->addWidget(introduction);

  wheel_ = new ScreenColorWheel(this);
  wheel_->setObjectName("screenColorWheel");
  wheel_->set_color(selected_color_);
  wheel_->set_color_changed_handler([this](const QColor& color) {
    int hue = 0;
    int saturation = 0;
    int ignored_value = 0;
    color.getHsv(&hue, &saturation, &ignored_value);
    selected_color_ = QColor::fromHsv(
        hue, saturation, qRound(brightness_->value() * 255.0 / 100.0));
    update_preview();
  });
  layout->addWidget(wheel_, 1, Qt::AlignHCenter);

  auto* brightness_label = new QLabel("Brightness", this);
  layout->addWidget(brightness_label);
  brightness_ = new QSlider(Qt::Horizontal, this);
  brightness_->setObjectName("screenBrightnessSlider");
  brightness_->setRange(35, 100);
  brightness_->setValue(qRound(selected_color_.valueF() * 100.0));
  brightness_->setAccessibleName("Screen color brightness");
  connect(brightness_, &QSlider::valueChanged, this, [this](int value) {
    const QColor wheel_color = wheel_->color();
    selected_color_ =
        QColor::fromHsv(wheel_color.hue(), wheel_color.saturation(),
                        qRound(value * 255.0 / 100.0));
    update_preview();
  });
  layout->addWidget(brightness_);

  swatch_ = new QLabel(this);
  swatch_->setObjectName("screenColorSwatch");
  swatch_->setMinimumHeight(42);
  swatch_->setAlignment(Qt::AlignCenter);
  layout->addWidget(swatch_);
  value_label_ = new QLabel(this);
  value_label_->setAlignment(Qt::AlignCenter);
  layout->addWidget(value_label_);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                               QDialogButtonBox::RestoreDefaults,
                           this);
  buttons->setObjectName("screenColorButtons");
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons->button(QDialogButtonBox::RestoreDefaults),
          &QPushButton::clicked, this,
          [this]() { select_color(DisplayWidget::default_base_color()); });
  layout->addWidget(buttons);
  update_preview();
}

void ScreenColorDialog::set_preview_handler(
    std::function<void(const QColor&)> handler) {
  preview_handler_ = std::move(handler);
}

void ScreenColorDialog::select_color(const QColor& color) {
  selected_color_ = color;
  wheel_->set_color(color);
  brightness_->setValue(qRound(color.valueF() * 100.0));
  update_preview();
}

void ScreenColorDialog::update_preview() {
  const QString color_name = selected_color_.name(QColor::HexRgb).toUpper();
  swatch_->setStyleSheet(
      QString("background-color: %1; border: 1px solid palette(mid); "
              "border-radius: 5px;")
          .arg(color_name));
  value_label_->setText(color_name);
  if (preview_handler_) {
    preview_handler_(selected_color_);
  }
}

}  // namespace p2000c
