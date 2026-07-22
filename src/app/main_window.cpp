#include "app/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeySequence>
#include <QLinearGradient>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPushButton>
#include <QRadialGradient>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSlider>
#include <QTimer>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <optional>
#include <string>

#include "app/display_widget.h"
#include "app/hardware_audio.h"
#include "app/screen_color_dialog.h"

namespace p2000c {

namespace {

void apply_p2000c_theme() {
  QPalette palette;
  palette.setColor(QPalette::Window, QColor("#e8dcb3"));
  palette.setColor(QPalette::WindowText, QColor("#262626"));
  palette.setColor(QPalette::Base, QColor("#f5eac6"));
  palette.setColor(QPalette::AlternateBase, QColor("#e5d6ae"));
  palette.setColor(QPalette::ToolTipBase, QColor("#f5eac6"));
  palette.setColor(QPalette::ToolTipText, QColor("#262626"));
  palette.setColor(QPalette::Text, QColor("#262626"));
  palette.setColor(QPalette::Button, QColor("#e5d6ae"));
  palette.setColor(QPalette::ButtonText, QColor("#262626"));
  palette.setColor(QPalette::BrightText, QColor("#f5eac6"));
  palette.setColor(QPalette::Highlight, QColor("#b5a065"));
  palette.setColor(QPalette::HighlightedText, QColor("#262626"));
  palette.setColor(QPalette::Link, QColor("#913939"));
  palette.setColor(QPalette::LinkVisited, QColor("#70705c"));
  QApplication::setPalette(palette);
  qApp->setStyleSheet(QStringLiteral(R"(
    QMainWindow, QDialog { background: #e8dcb3; color: #262626; }
    QWidget#emulatorCentral { background: #d6c89a; }
    QMenuBar {
      background: #e5d6ae;
      color: #262626;
      border-bottom: 1px solid #a38a45;
      padding: 2px;
    }
    QMenuBar::item { background: transparent; padding: 5px 9px; }
    QMenuBar::item:selected, QMenuBar::item:pressed {
      background: #f5eac6;
      border-radius: 3px;
    }
    QMenu {
      background: #f5eac6;
      color: #262626;
      border: 1px solid #a38a45;
      padding: 4px;
    }
    QMenu::item { padding: 5px 28px 5px 24px; }
    QMenu::item:selected { background: #b5a065; color: #262626; }
    QMenu::item:disabled { color: #70705c; }
    QMenu::separator { height: 1px; background: #d6c89a; margin: 4px 8px; }
    QStatusBar {
      background: #e5d6ae;
      color: #262626;
      border-top: 1px solid #a38a45;
    }
    QFrame#driveActivityPanel {
      background: #f5eac6;
      color: #262626;
      border: 1px solid #b5a065;
      border-radius: 8px;
    }
    QFrame#driveActivityPanel QLabel { color: #262626; background: transparent; }
    QLabel#drivePanelTitle {
      color: #594a2e;
      border: none;
      border-bottom: 1px solid #c7b77e;
      padding: 0 0 5px 0;
    }
    QFrame[driveCard="true"] {
      background: transparent;
      border: none;
      border-bottom: 1px solid #d1c18d;
    }
    QLabel[driveTypeIcon="true"] { background: transparent; border: none; }
    QLabel[driveHeading="true"] { color: #4b3f2a; border: none; }
    QLabel[mediaFilename="true"] { color: #625a42; border: none; }
    QTabWidget::pane { border: 1px solid #b5a065; background: #f5eac6; }
    QTabBar::tab {
      background: #d6c89a;
      color: #262626;
      border: 1px solid #b5a065;
      padding: 6px 10px;
    }
    QTabBar::tab:selected { background: #f5eac6; border-bottom-color: #f5eac6; }
    QTextEdit, QTextBrowser, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
      background: #f5eac6;
      color: #262626;
      border: 1px solid #b5a065;
      border-radius: 3px;
      selection-background-color: #b5a065;
      selection-color: #262626;
    }
    QPushButton {
      background: #e5d6ae;
      color: #262626;
      border: 1px solid #a38a45;
      border-radius: 4px;
      padding: 5px 12px;
    }
    QPushButton:hover { background: #f5eac6; }
    QPushButton:pressed { background: #b5a065; }
    QPushButton:disabled { color: #70705c; border-color: #d6c89a; }
    QGroupBox { border: 1px solid #b5a065; border-radius: 5px; margin-top: 8px; }
    QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
    QToolTip { background: #f5eac6; color: #262626; border: 1px solid #a38a45; }
  )"));
}

}  // namespace

/** A glassy, smoothly fading drive LED driven by real controller activity. */
class DriveLed : public QWidget {
 public:
  explicit DriveLed(QWidget* parent = nullptr) : QWidget(parent) {
    setFixedSize(24, 24);
    fade_timer_.setInterval(16);
    connect(&fade_timer_, &QTimer::timeout, this, [this]() {
      update();
      if (pulse_clock_.isValid() && pulse_clock_.elapsed() > hold_ms_ + 420) {
        fade_timer_.stop();
      }
    });
  }

  void pulse(P2000cMachine::StorageOperation operation, int duration_ms) {
    using Operation = P2000cMachine::StorageOperation;
    color_ = operation == Operation::kWrite
                 ? QColor(255, 96, 38)
                 : operation == Operation::kSeek
                       ? QColor(255, 184, 45)
                       : operation == Operation::kMotorStart ||
                                 operation == Operation::kMotorStop
                             ? QColor(80, 205, 255)
                             : QColor(70, 255, 138);
    hold_ms_ = std::clamp(duration_ms, 35, 450);
    pulse_clock_.restart();
    fade_timer_.start();
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    const qreal elapsed = pulse_clock_.isValid() ? pulse_clock_.elapsed() : 9999;
    const qreal intensity =
        elapsed <= hold_ms_
            ? 1.0
            : std::exp(-(elapsed - hold_ms_) / 115.0);
    // Even unlit indicator lamps retain the colored, glossy appearance of
    // their moulded plastic lens. Activity adds emitted light on top of it.
    const qreal lens_intensity = 0.13 + intensity * 0.87;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (intensity > 0.015) {
      QRadialGradient glow(rect().center(), width() * 0.52);
      QColor glow_color = color_;
      glow_color.setAlphaF(std::min(0.55, intensity * 0.5));
      glow.setColorAt(0.0, glow_color);
      glow_color.setAlpha(0);
      glow.setColorAt(1.0, glow_color);
      painter.fillRect(rect(), glow);
    }
    const QRectF bezel(4.5, 4.5, 15.0, 15.0);
    QLinearGradient metal(bezel.topLeft(), bezel.bottomRight());
    metal.setColorAt(0.0, QColor(105, 102, 94));
    metal.setColorAt(0.30, QColor(181, 175, 160));
    metal.setColorAt(0.58, QColor(91, 89, 83));
    metal.setColorAt(1.0, QColor(145, 140, 129));
    painter.setPen(QPen(QColor(67, 65, 60), 0.7));
    painter.setBrush(metal);
    painter.drawEllipse(bezel);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(8, 13, 12));
    painter.drawEllipse(bezel.adjusted(1.25, 1.25, -1.25, -1.25));
    QRadialGradient lens(QPointF(10.0, 9.0), 10.0);
    const QColor active = QColor::fromRgbF(
        color_.redF() * lens_intensity,
        color_.greenF() * lens_intensity,
        color_.blueF() * lens_intensity);
    lens.setColorAt(0.0, active.lighter(155));
    lens.setColorAt(0.38, active);
    lens.setColorAt(1.0, QColor(4, 17, 10));
    painter.setBrush(lens);
    painter.drawEllipse(bezel.adjusted(2.0, 2.0, -2.0, -2.0));
    painter.setBrush(
        QColor(255, 255, 245, qRound(52 + 86 * intensity)));
    painter.drawEllipse(QRectF(8.0, 7.5, 3.2, 2.2));
    painter.setBrush(QColor(255, 255, 255, qRound(18 + 22 * intensity)));
    painter.drawEllipse(QRectF(10.8, 8.1, 5.0, 3.0));
  }

 private:
  QElapsedTimer pulse_clock_;
  QTimer fade_timer_{this};
  QColor color_{70, 255, 138};
  int hold_ms_ = 0;
};

/** A mechanical tape-counter-style display for live drive position. */
class DrivePositionDisplay : public QWidget {
 public:
  enum class Kind { kFloppy, kHardDisk };

  explicit DrivePositionDisplay(Kind kind, QWidget* parent = nullptr)
      : QWidget(parent), kind_(kind) {
    setMinimumSize(108, 32);
    setMaximumHeight(32);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }

  void set_floppy_position(unsigned track, unsigned side, bool mounted) {
    mounted_ = mounted;
    primary_ = track;
    secondary_ = side;
    setAccessibleName(mounted ? QString("Track %1, side %2").arg(track).arg(side)
                              : "No floppy position");
    update();
  }

  void set_hard_disk_position(std::size_t block, bool mounted) {
    mounted_ = mounted;
    primary_ = block;
    setAccessibleName(mounted ? QString("SASI block %1").arg(block)
                              : "No hard-disk position");
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF housing = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient bezel(housing.topLeft(), housing.bottomLeft());
    bezel.setColorAt(0.0, QColor("#777368"));
    bezel.setColorAt(0.18, QColor("#34332d"));
    bezel.setColorAt(0.82, QColor("#292822"));
    bezel.setColorAt(1.0, QColor("#878174"));
    painter.setPen(QPen(QColor("#5e594f"), 0.8));
    painter.setBrush(bezel);
    painter.drawRoundedRect(housing, 4.0, 4.0);

    const QString primary = mounted_
                                ? QString::number(primary_).rightJustified(
                                      kind_ == Kind::kHardDisk ? 5 : 2, '0')
                                : QString(kind_ == Kind::kHardDisk ? 5 : 2,
                                          '-');
    const QString secondary = mounted_ ? QString::number(secondary_) : "-";
    constexpr qreal kDigitWidth = 14.0;
    constexpr qreal kDigitGap = 1.5;
    const qreal primary_label_width = 20.0;
    const qreal secondary_label_width = 22.0;
    const qreal group_gap = kind_ == Kind::kFloppy ? 5.0 : 0.0;
    const qreal content_width =
        primary_label_width + primary.size() * kDigitWidth +
        static_cast<qreal>(primary.size() - 1) * kDigitGap + group_gap +
        (kind_ == Kind::kFloppy
             ? secondary_label_width + secondary.size() * kDigitWidth +
                   static_cast<qreal>(secondary.size() - 1) * kDigitGap
             : 0.0);
    qreal x = (width() - content_width) / 2.0;
    QFont legend = font();
    legend.setBold(true);
    legend.setPointSizeF(6.0);
    painter.setFont(legend);
    painter.setPen(QColor("#d8cda8"));

    const auto draw_legend = [&painter, &x](const QString& text, qreal width) {
      painter.drawText(QRectF(x, 5.0, width, 22.0), Qt::AlignCenter, text);
      x += width;
    };
    const auto draw_digits = [&](const QString& digits) {
      for (qsizetype index = 0; index < digits.size(); ++index) {
        draw_digit_drum(&painter, QRectF(x, 4.0, kDigitWidth, 24.0),
                        digits[index]);
        x += kDigitWidth;
        if (index + 1 < digits.size()) {
          x += kDigitGap;
        }
      }
    };

    draw_legend(kind_ == Kind::kHardDisk ? "BLK" : "TRK",
                primary_label_width);
    draw_digits(primary);
    if (kind_ == Kind::kFloppy) {
      x += group_gap;
      draw_legend("SIDE", secondary_label_width);
      draw_digits(secondary);
    }
  }

 private:
  static void draw_digit_drum(QPainter* painter, const QRectF& drum,
                              QChar digit) {
    painter->save();
    painter->setClipRect(drum);
    QLinearGradient paper(drum.topLeft(), drum.bottomLeft());
    paper.setColorAt(0.0, QColor("#8f876d"));
    paper.setColorAt(0.16, QColor("#d8cfaa"));
    paper.setColorAt(0.50, QColor("#f2e9c8"));
    paper.setColorAt(0.84, QColor("#c8be99"));
    paper.setColorAt(1.0, QColor("#766f5a"));
    painter->setPen(QPen(QColor("#171713"), 0.7));
    painter->setBrush(paper);
    painter->drawRect(drum);
    QFont digits = painter->font();
    digits.setStyleHint(QFont::Monospace);
    digits.setFixedPitch(true);
    digits.setBold(true);
    digits.setPointSizeF(12.0);
    painter->setFont(digits);
    painter->setPen(QColor("#191914"));
    painter->drawText(drum.adjusted(0.0, -0.5, 0.0, 0.0), Qt::AlignCenter,
                      QString(digit));
    painter->setPen(QPen(QColor(255, 255, 235, 80), 0.7));
    painter->drawLine(drum.left(), drum.center().y() - 5.5, drum.right(),
                      drum.center().y() - 5.5);
    painter->setPen(QPen(QColor(50, 46, 37, 90), 0.7));
    painter->drawLine(drum.left(), drum.center().y() + 6.0, drum.right(),
                      drum.center().y() + 6.0);
    painter->restore();
  }

  Kind kind_;
  bool mounted_ = false;
  std::size_t primary_ = 0;
  unsigned secondary_ = 0;
};

namespace {

struct DisplayResolution {
    const char* label;
    QSize size;
};

struct EmulationSpeed {
    const char* label;
    double multiplier;
};

constexpr std::array<DisplayResolution, 5> kDisplayResolutions = {{
    {"512 x 384 (Compact)", {512, 384}},
    {"640 x 480", {640, 480}},
    {"768 x 576 (Recommended)", {768, 576}},
    {"960 x 720", {960, 720}},
    {"1152 x 864", {1152, 864}},
}};

constexpr QSize kDefaultDisplayResolution(768, 576);

constexpr std::array<EmulationSpeed, 5> kEmulationSpeeds = {{
    {"1 MHz (25%)", 0.25},
    {"2 MHz (50%)", 0.5},
    {"4 MHz (Authentic)", 1.0},
    {"8 MHz (200%)", 2.0},
    {"16 MHz (400%)", 4.0},
}};

/** Restores independently persisted CRT-effect switches. */
CrtEffects load_crt_effects(const QSettings& settings) {
  const CrtEffects defaults = DisplayWidget::default_crt_effects();
  return {
      settings.value("display/effects/scanlines", defaults.scanlines).toBool(),
      settings.value("display/effects/bloom", defaults.bloom).toBool(),
      settings.value("display/effects/persistence", defaults.persistence)
          .toBool(),
      settings.value("display/effects/curvature", defaults.curvature).toBool(),
      settings.value("display/effects/vignette", defaults.vignette).toBool(),
      settings.value("display/effects/noise", defaults.noise).toBool(),
      settings.value("display/effects/flicker", defaults.flicker).toBool(),
      settings
          .value("display/effects/persistenceHalfLifeMs",
                 defaults.persistence_half_life_ms)
          .toInt(),
      settings
          .value("display/effects/brightnessPercent",
                 defaults.brightness_percent)
          .toInt(),
  };
}

/** Persists every CRT effect separately for future sessions. */
void save_crt_effects(QSettings* settings, const CrtEffects& effects) {
  settings->setValue("display/effects/scanlines", effects.scanlines);
  settings->setValue("display/effects/bloom", effects.bloom);
  settings->setValue("display/effects/persistence", effects.persistence);
  settings->setValue("display/effects/curvature", effects.curvature);
  settings->setValue("display/effects/vignette", effects.vignette);
  settings->setValue("display/effects/noise", effects.noise);
  settings->setValue("display/effects/flicker", effects.flicker);
  settings->setValue("display/effects/persistenceHalfLifeMs",
                     effects.persistence_half_life_ms);
  settings->setValue("display/effects/brightnessPercent",
                     effects.brightness_percent);
}

/** Converts a QString to a Unicode-aware host filesystem path. */
std::filesystem::path filesystem_path(const QString& path) {
  const QByteArray utf8 = path.toUtf8();
  const auto* begin = reinterpret_cast<const char8_t*>(utf8.constData());
  return std::filesystem::path(std::u8string(begin, begin + utf8.size()));
}

/** Converts a host filesystem path to a Unicode-aware QString. */
QString qstring_path(const std::filesystem::path& path) {
  const std::u8string utf8 = path.u8string();
  return QString::fromUtf8(reinterpret_cast<const char*>(utf8.data()),
                           static_cast<qsizetype>(utf8.size()));
}

/** Shortens a filename for compact menu and status-bar presentation. */
QString compact_filename(const std::filesystem::path& path) {
  const QString filename = qstring_path(path.filename());
  constexpr qsizetype kMaximumLength = 34;
  if (filename.size() <= kMaximumLength) {
    return filename;
  }
  return filename.left(16) + QChar(0x2026) + filename.right(17);
}

/** Escapes menu mnemonic markers present in a mounted filename. */
QString menu_safe(QString text) { return text.replace('&', "&&"); }

/** Finds one installed manual, with the source tree as a developer fallback. */
QString manual_path(const QString& filename) {
  const QDir executable_directory(QCoreApplication::applicationDirPath());
  const std::array<QString, 4> directories = {
      executable_directory.filePath("../Resources/manuals"),
      executable_directory.filePath("../share/p2000c-emulator/manuals"),
      executable_directory.filePath("manuals"),
      QStringLiteral(P2000C_SOURCE_MANUALS_DIR)};
  for (const QString& directory : directories) {
    const QFileInfo candidate(QDir(directory).filePath(filename));
    if (candidate.isFile()) {
      return candidate.canonicalFilePath();
    }
  }
  return {};
}

QString recent_images_key(bool hard_disk, std::size_t drive) {
  return QString("media/recent/%1%2")
      .arg(hard_disk ? "hardDisk" : "floppy")
      .arg(drive);
}

bool copy_image_file(const QString& source, const QString& destination,
                     QString* error) {
  QFile input(source);
  if (!input.open(QIODevice::ReadOnly)) {
    *error = "Could not read the mounted image " + source + ".";
    return false;
  }
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(input.readAll()) != input.size() || !output.commit()) {
    *error = "Could not save the image to " + destination + ".";
    return false;
  }
  return true;
}

/** Recreates a disposable session copy from an immutable bundled resource. */
std::optional<QString> temporary_resource_copy(const QString& resource,
                                               const QString& directory,
                                               const QString& filename,
                                               QString* error) {
  if (directory.isEmpty() || !QDir().mkpath(directory)) {
    *error = "Could not create the temporary media session directory.";
    return std::nullopt;
  }
  QFile input(resource);
  if (!input.open(QIODevice::ReadOnly)) {
    *error = "Could not open bundled media resource " + resource + ".";
    return std::nullopt;
  }
  const QByteArray bytes = input.readAll();
  const QString destination = QDir(directory).filePath(filename);
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(bytes) != bytes.size() || !output.commit()) {
    *error = "Could not create temporary media " + destination + ".";
    return std::nullopt;
  }
  return destination;
}

/** Recreates a disposable unformatted floppy for the current session. */
std::optional<QString> temporary_blank_floppy(const QString& directory,
                                              const QString& filename,
                                              QString* error) {
  if (directory.isEmpty() || !QDir().mkpath(directory)) {
    *error = "Could not create the temporary media session directory.";
    return std::nullopt;
  }
  const QString destination = QDir(directory).filePath(filename);
  const QByteArray image(static_cast<qsizetype>(RawDiskImage::kFloppySize),
                         static_cast<char>(0xe5));
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(image) != image.size() || !output.commit()) {
    *error = "Could not create temporary blank floppy " + destination + ".";
    return std::nullopt;
  }
  return destination;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  apply_p2000c_theme();
  setWindowTitle("Philips P2000C Emulator");
  setWindowIcon(QIcon(":/logo/logo-p2000c.svg"));
  auto* central = new QWidget(this);
  central->setObjectName("emulatorCentral");
  auto* central_layout = new QHBoxLayout(central);
  central_layout->setContentsMargins(8, 8, 8, 8);
  central_layout->setSpacing(10);
  display_ = new DisplayWidget(central);
  central_layout->addWidget(display_, 0, Qt::AlignCenter);
  auto* drive_panel = new QFrame(central);
  drive_panel->setObjectName("driveActivityPanel");
  drive_panel->setFrameShape(QFrame::StyledPanel);
  drive_panel->setMinimumWidth(260);
  drive_panel->setMaximumWidth(290);
  drive_panel_layout_ = new QVBoxLayout(drive_panel);
  drive_panel_layout_->setContentsMargins(12, 12, 12, 12);
  drive_panel_layout_->setSpacing(8);
  auto* panel_title = new QLabel("DRIVE STATUS", drive_panel);
  panel_title->setObjectName("drivePanelTitle");
  QFont panel_font = panel_title->font();
  panel_font.setBold(true);
  panel_font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
  panel_title->setFont(panel_font);
  panel_title->setAlignment(Qt::AlignCenter);
  drive_panel_layout_->addWidget(panel_title);
  central_layout->addWidget(drive_panel, 0, Qt::AlignTop);
  setCentralWidget(central);
  display_->set_key_handler(
      [this](std::uint8_t value) { machine_.queue_key(value); });
  statusBar();
  create_menus();
  drive_panel_layout_->addStretch(1);
  hardware_audio_ = std::make_unique<HardwareAudio>();
  QSettings settings;
  audio_enabled_ = settings.value("audio/enabled", true).toBool();
  const int audio_volume =
      std::clamp(settings.value("audio/volume", 70).toInt(), 0, 100);
  hardware_audio_->set_volume(audio_volume / 100.0);
  machine_.set_storage_activity_handler(
      [this](const P2000cMachine::StorageActivity& activity) {
        DriveLed* led = nullptr;
        if (activity.drive < 2) {
          led = activity.device == P2000cMachine::StorageDevice::kFloppy
                    ? floppy_activity_leds_[activity.drive]
                    : hard_disk_activity_leds_[activity.drive];
        }
        if (led != nullptr) {
          led->pulse(activity.operation, activity.duration_ms);
        }
        if (activity.drive < 2) {
          refresh_drive_position(
              activity.device == P2000cMachine::StorageDevice::kHardDisk,
              activity.drive);
        }
        if (audio_enabled_) {
          hardware_audio_->play_storage_activity(activity);
        }
      });
  refresh_media_indicators();

  QFile bundled_rom(":/tools/IPLDUMP.BIN");
  if (bundled_rom.open(QIODevice::ReadOnly)) {
    const QByteArray bytes = bundled_rom.readAll();
    std::string error;
    if (!machine_.load_ipl_rom(
            std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                static_cast<std::size_t>(bytes.size())),
            &error)) {
      statusBar()->showMessage(QString::fromStdString(error));
    }
  }
  mount_default_media();

  timer_ = new QTimer(this);
  timer_->setTimerType(Qt::PreciseTimer);
  connect(timer_, &QTimer::timeout, this, &MainWindow::run_emulation_slice);
  execution_timer_.start();
  timer_->start(10);
  refresh_screen();
}

MainWindow::~MainWindow() = default;

bool MainWindow::mount_floppy(const std::filesystem::path& path,
                              std::size_t drive) {
  if (drive >= 2) {
    return false;
  }
  std::string error;
  const bool mounted = drive == 0 ? machine_.mount_floppy_a(path, &error)
                                  : machine_.mount_floppy_b(path, &error);
  if (!mounted) {
    QMessageBox::critical(this, "Cannot mount floppy",
                          QString::fromStdString(error));
    refresh_media_indicators();
    return false;
  }
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  statusBar()->showMessage(
      QString("Floppy %1 mounted read/write: ").arg(drive_letter) +
      qstring_path(path));
  save_floppy_actions_[drive]->setEnabled(true);
  const QString mounted_path = qstring_path(path);
  if (!mounted_path.startsWith(media_session_.path() + QDir::separator())) {
    temporary_floppy_paths_[drive].clear();
    remember_recent_image(mounted_path, false, drive);
  }
  refresh_media_indicators();
  refresh_screen();
  return true;
}

bool MainWindow::mount_hard_disk(const std::filesystem::path& path,
                                 std::size_t drive) {
  std::string error;
  if (!machine_.mount_hard_disk(drive, path, &error)) {
    QMessageBox::critical(this, "Cannot mount hard disk",
                          QString::fromStdString(error));
    refresh_media_indicators();
    return false;
  }
  statusBar()->showMessage(
      QString("SASI hard disk %1 mounted read/write: ").arg(drive + 1) +
      qstring_path(path));
  save_hard_disk_actions_[drive]->setEnabled(true);
  const QString mounted_path = qstring_path(path);
  if (!mounted_path.startsWith(media_session_.path() + QDir::separator())) {
    temporary_hard_disk_paths_[drive].clear();
    remember_recent_image(mounted_path, true, drive);
  }
  refresh_media_indicators();
  return true;
}

void MainWindow::remember_recent_image(const QString& path, bool hard_disk,
                                       std::size_t drive) {
  QSettings settings;
  const QString key = recent_images_key(hard_disk, drive);
  QStringList recent = settings.value(key).toStringList();
  const QString absolute = QFileInfo(path).absoluteFilePath();
  recent.removeAll(absolute);
  recent.prepend(absolute);
  while (recent.size() > 5) {
    recent.removeLast();
  }
  settings.setValue(key, recent);
  refresh_recent_image_menus();
}

void MainWindow::refresh_recent_image_menus() {
  QSettings settings;
  for (std::size_t drive = 0; drive < 2; ++drive) {
    for (const bool hard_disk : {false, true}) {
      QMenu* menu = hard_disk ? recent_hard_disk_menus_[drive]
                              : recent_floppy_menus_[drive];
      if (menu == nullptr) {
        continue;
      }
      menu->clear();
      const QStringList recent =
          settings.value(recent_images_key(hard_disk, drive)).toStringList();
      for (const QString& path : recent) {
        QAction* action = menu->addAction(menu_safe(QFileInfo(path).fileName()));
        action->setToolTip(path);
        action->setEnabled(QFileInfo::exists(path));
        connect(action, &QAction::triggered, this,
                [this, path, hard_disk, drive]() {
                  if (hard_disk) {
                    mount_hard_disk(filesystem_path(path), drive);
                  } else {
                    mount_floppy(filesystem_path(path), drive);
                  }
                });
      }
      if (recent.isEmpty()) {
        QAction* empty = menu->addAction("No recent images");
        empty->setEnabled(false);
      }
    }
  }
}

void MainWindow::create_menus() {
  auto add_drive_card = [this](const QString& title,
                               const QString& status_object_name,
                               const QString& icon_resource,
                               QLabel** status,
                               DrivePositionDisplay** position,
                               DriveLed** led, bool hard_disk) {
    auto* card = new QFrame(this);
    card->setObjectName(QString(status_object_name).replace("Status", "Card"));
    card->setFrameShape(QFrame::NoFrame);
    card->setProperty("driveCard", true);
    auto* card_layout = new QHBoxLayout(card);
    card_layout->setContentsMargins(2, 6, 2, 7);
    card_layout->setSpacing(6);
    *led = new DriveLed(card);
    (*led)->setObjectName(
        QString(status_object_name).replace("Status", "ActivityLed"));
    card_layout->addWidget(*led, 0, Qt::AlignVCenter);
    auto* icon = new QLabel(card);
    icon->setObjectName(
        QString(status_object_name).replace("Status", "TypeIcon"));
    icon->setProperty("driveTypeIcon", true);
    icon->setPixmap(QPixmap(icon_resource).scaled(
        QSize(58, 58), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    icon->setFixedSize(60, 60);
    icon->setAlignment(Qt::AlignCenter);
    icon->setToolTip(title);
    icon->setAccessibleName(title + " hardware illustration");
    card_layout->addWidget(icon, 0, Qt::AlignVCenter);
    auto* text_layout = new QVBoxLayout();
    text_layout->setSpacing(2);
    auto* heading_layout = new QHBoxLayout();
    heading_layout->setContentsMargins(0, 0, 0, 0);
    heading_layout->setSpacing(5);
    auto* heading = new QLabel(title, card);
    heading->setProperty("driveHeading", true);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading_font.setLetterSpacing(QFont::AbsoluteSpacing, 0.35);
    heading->setFont(heading_font);
    heading_layout->addWidget(heading);
    heading_layout->addStretch(1);
    text_layout->addLayout(heading_layout);
    *status = new QLabel("Empty", card);
    (*status)->setObjectName(status_object_name);
    (*status)->setProperty("mediaFilename", true);
    (*status)->setWordWrap(false);
    QFont filename_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    filename_font.setPointSizeF(
        std::max(7.0, filename_font.pointSizeF() - 1.0));
    (*status)->setFont(filename_font);
    text_layout->addWidget(*status);
    *position = new DrivePositionDisplay(
        hard_disk ? DrivePositionDisplay::Kind::kHardDisk
                  : DrivePositionDisplay::Kind::kFloppy,
        card);
    (*position)->setObjectName(
        QString(status_object_name).replace("Status", "Position"));
    text_layout->addWidget(*position);
    card_layout->addLayout(text_layout, 1);
    card_layout->setAlignment(text_layout, Qt::AlignVCenter);
    drive_panel_layout_->addWidget(card);
  };

  QMenu* machine_menu = menuBar()->addMenu("&Machine");
  QAction* load_rom = machine_menu->addAction("Load &IPL ROM...");
  connect(load_rom, &QAction::triggered, this, &MainWindow::load_ipl_rom);
  QAction* reset = machine_menu->addAction("&Reset");
  connect(reset, &QAction::triggered, this, [this]() {
    machine_.reset();
    pending_t_states_ = 0.0;
    execution_timer_.restart();
    refresh_media_indicators();
    refresh_screen();
  });
  QMenu* speed_menu = machine_menu->addMenu("Emulation &Speed");
  auto* speed_group = new QActionGroup(this);
  speed_group->setExclusive(true);
  double selected_speed = QSettings().value("machine/speed", 1.0).toDouble();
  if (std::none_of(kEmulationSpeeds.begin(), kEmulationSpeeds.end(),
                   [&](const EmulationSpeed& speed) {
                     return speed.multiplier == selected_speed;
                   })) {
    selected_speed = 1.0;
  }
  for (const EmulationSpeed& speed : kEmulationSpeeds) {
    QAction* action = speed_menu->addAction(speed.label);
    action->setActionGroup(speed_group);
    action->setCheckable(true);
    action->setChecked(speed.multiplier == selected_speed);
    connect(action, &QAction::triggered, this,
            [this, speed]() { set_emulation_speed(speed.multiplier); });
  }
  set_emulation_speed(selected_speed);
  machine_menu->addSeparator();
  QAction* quit = machine_menu->addAction("E&xit");
  connect(quit, &QAction::triggered, this, &QWidget::close);

  QMenu* media_menu = menuBar()->addMenu("&Media");
  for (std::size_t drive = 0; drive < 2; ++drive) {
    const QChar drive_letter = drive == 0 ? 'A' : 'B';
    QMenu* drive_menu =
        media_menu->addMenu(QString("Drive &%1 — empty").arg(drive_letter));
    drive_menu->setObjectName(QString("floppyDrive%1Menu").arg(drive_letter));
    drive_menu->setToolTipsVisible(true);
    media_drive_menus_[drive] = drive_menu;

    QAction* current = drive_menu->addAction("Mounted: none");
    current->setObjectName(QString("currentFloppy%1Action").arg(drive_letter));
    current->setEnabled(false);
    current->setCheckable(true);
    QFont current_font = current->font();
    current_font.setBold(true);
    current->setFont(current_font);
    current_media_actions_[drive] = current;

    drive_menu->addSeparator();
    QAction* open = drive_menu->addAction("&Open Image...");
    open->setObjectName(QString("openFloppy%1Action").arg(drive_letter));
    connect(open, &QAction::triggered, this,
            [this, drive]() { open_floppy(drive); });
    QAction* save = drive_menu->addAction("Save Current Image &As...");
    save->setObjectName(
        QString("saveFloppy%1AsAction").arg(drive_letter));
    save->setEnabled(false);
    save_floppy_actions_[drive] = save;
    connect(save, &QAction::triggered, this,
            [this, drive]() { save_floppy_as(drive); });
    recent_floppy_menus_[drive] = drive_menu->addMenu("Open &Recent");
    recent_floppy_menus_[drive]->setObjectName(
        QString("recentFloppy%1Menu").arg(drive_letter));
    drive_menu->addSeparator();

    QAction* system_floppy =
        drive_menu->addAction("Use CP/M 2.2 &System Floppy");
    system_floppy->setObjectName(
        QString("mountSystemFloppy%1Action").arg(drive_letter));
    system_floppy->setCheckable(true);
    bundled_system_actions_[drive] = system_floppy;
    connect(system_floppy, &QAction::triggered, this,
            [this, drive]() { mount_bundled_system_floppy(drive); });

    QAction* zork_floppy = drive_menu->addAction("Use &ZORK Data Floppy");
    zork_floppy->setObjectName(
        QString("mountZorkFloppy%1Action").arg(drive_letter));
    zork_floppy->setCheckable(true);
    bundled_zork_actions_[drive] = zork_floppy;
    connect(zork_floppy, &QAction::triggered, this,
            [this, drive]() { mount_bundled_zork_floppy(drive); });

    QAction* chess_floppy = drive_menu->addAction("Use &CHESS Data Floppy");
    chess_floppy->setObjectName(
        QString("mountChessFloppy%1Action").arg(drive_letter));
    chess_floppy->setCheckable(true);
    bundled_chess_actions_[drive] = chess_floppy;
    connect(chess_floppy, &QAction::triggered, this,
            [this, drive]() { mount_bundled_chess_floppy(drive); });

    QAction* ipldump_floppy =
        drive_menu->addAction("Use IPL &Dump Toolchain Floppy");
    ipldump_floppy->setObjectName(
        QString("mountIplDumpFloppy%1Action").arg(drive_letter));
    ipldump_floppy->setCheckable(true);
    bundled_ipldump_actions_[drive] = ipldump_floppy;
    connect(ipldump_floppy, &QAction::triggered, this,
            [this, drive]() { mount_bundled_ipldump_floppy(drive); });

    QAction* blank_floppy =
        drive_menu->addAction("Use &Blank 640 KiB Data Floppy");
    blank_floppy->setObjectName(
        QString("mountBlankFloppy%1Action").arg(drive_letter));
    blank_floppy->setCheckable(true);
    bundled_blank_actions_[drive] = blank_floppy;
    connect(blank_floppy, &QAction::triggered, this,
            [this, drive]() { mount_bundled_blank_floppy(drive); });

    add_drive_card(QString("FLOPPY %1").arg(drive_letter),
                   QString("floppyDrive%1Status").arg(drive_letter),
                   ":/icons/drive-floppy-525.png",
                   &media_status_labels_[drive],
                   &floppy_position_displays_[drive],
                   &floppy_activity_leds_[drive], false);
  }

  media_menu->addSeparator();
  for (std::size_t drive = 0; drive < 2; ++drive) {
    const QString volumes = drive == 0 ? "C/D" : "E/F";
    QMenu* drive_menu = media_menu->addMenu(
        QString("Hard disk &%1 (%2) — empty").arg(drive + 1).arg(volumes));
    drive_menu->setObjectName(QString("hardDisk%1Menu").arg(drive + 1));
    drive_menu->setToolTipsVisible(true);
    hard_disk_menus_[drive] = drive_menu;

    QAction* current = drive_menu->addAction("Mounted: none");
    current->setObjectName(
        QString("currentHardDisk%1Action").arg(drive + 1));
    current->setEnabled(false);
    current->setCheckable(true);
    QFont current_font = current->font();
    current_font.setBold(true);
    current->setFont(current_font);
    current_hard_disk_actions_[drive] = current;

    drive_menu->addSeparator();
    QAction* open = drive_menu->addAction("&Open HDA Image...");
    open->setObjectName(QString("openHardDisk%1Action").arg(drive + 1));
    connect(open, &QAction::triggered, this,
            [this, drive]() { open_hard_disk(drive); });
    QAction* save = drive_menu->addAction("Save Current Image &As...");
    save->setObjectName(QString("saveHardDisk%1AsAction").arg(drive + 1));
    save->setEnabled(false);
    save_hard_disk_actions_[drive] = save;
    connect(save, &QAction::triggered, this,
            [this, drive]() { save_hard_disk_as(drive); });
    recent_hard_disk_menus_[drive] = drive_menu->addMenu("Open &Recent");
    recent_hard_disk_menus_[drive]->setObjectName(
        QString("recentHardDisk%1Menu").arg(drive + 1));
    QAction* bundled = drive_menu->addAction("Use &Default Blank HDA");
    bundled->setObjectName(
        QString("mountDefaultHardDisk%1Action").arg(drive + 1));
    bundled->setCheckable(true);
    bundled_hard_disk_actions_[drive] = bundled;
    connect(bundled, &QAction::triggered, this,
            [this, drive]() { mount_bundled_hard_disk(drive); });

    add_drive_card(drive == 0 ? "HARD DISK C/D" : "HARD DISK E/F",
                   QString("hardDisk%1Status").arg(drive + 1),
                   ":/icons/drive-hard-disk-sasi.png",
                   &hard_disk_status_labels_[drive],
                   &hard_disk_position_displays_[drive],
                   &hard_disk_activity_leds_[drive], true);
  }

  QMenu* view_menu = menuBar()->addMenu("&View");
  QMenu* resolution_menu = view_menu->addMenu("Display &Resolution");
  auto* resolution_group = new QActionGroup(this);
  resolution_group->setExclusive(true);
  QSize selected_resolution =
      QSettings()
          .value("display/resolution", kDefaultDisplayResolution)
          .toSize();
  if (std::none_of(kDisplayResolutions.begin(), kDisplayResolutions.end(),
                   [&](const DisplayResolution& resolution) {
                     return resolution.size == selected_resolution;
                   })) {
    selected_resolution = kDefaultDisplayResolution;
  }
  for (const DisplayResolution& resolution : kDisplayResolutions) {
    QAction* action = resolution_menu->addAction(resolution.label);
    action->setActionGroup(resolution_group);
    action->setCheckable(true);
    action->setChecked(resolution.size == selected_resolution);
    connect(action, &QAction::triggered, this,
            [this, size = resolution.size]() { set_display_resolution(size); });
  }
  set_display_resolution(selected_resolution);
  view_menu->addSeparator();
  QAction* screenshot = view_menu->addAction("Save &Screenshot...");
  screenshot->setObjectName("saveScreenshotAction");
  screenshot->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  connect(screenshot, &QAction::triggered, this, &MainWindow::save_screenshot);

  QMenu* settings_menu = menuBar()->addMenu("&Settings");
  QAction* screen_color = settings_menu->addAction("Screen &Appearance...");
  connect(screen_color, &QAction::triggered, this,
          &MainWindow::open_screen_color_settings);
  QAction* sound = settings_menu->addAction("Enable Hardware &Sounds");
  sound->setObjectName("enableHardwareSoundsAction");
  sound->setCheckable(true);
  sound->setChecked(QSettings().value("audio/enabled", true).toBool());
  connect(sound, &QAction::toggled, this, [this](bool enabled) {
    audio_enabled_ = enabled;
    if (!enabled && hardware_audio_ != nullptr) {
      hardware_audio_->stop_all();
    }
    QSettings().setValue("audio/enabled", enabled);
  });
  QAction* volume = settings_menu->addAction("Hardware Sound &Volume...");
  volume->setObjectName("hardwareSoundVolumeAction");
  connect(volume, &QAction::triggered, this,
          &MainWindow::open_audio_volume_settings);
  QAction* delays =
      settings_menu->addAction("Enable Floppy Drive &Delays");
  delays->setObjectName("enableHardwareDelaysAction");
  delays->setCheckable(true);
  delays->setChecked(
      QSettings().value("machine/storageDelays", true).toBool());
  machine_.set_storage_delays_enabled(delays->isChecked());
  connect(delays, &QAction::toggled, this, [this](bool enabled) {
    machine_.set_storage_delays_enabled(enabled);
    QSettings().setValue("machine/storageDelays", enabled);
  });

  QMenu* help_menu = menuBar()->addMenu("&Help");
  QMenu* documentation_menu = help_menu->addMenu("&Documentation");
  documentation_menu->setObjectName("documentationMenu");
  struct ManualEntry {
      const char* title;
      const char* filename;
      const char* object_name;
  };
  constexpr std::array<ManualEntry, 3> kManuals = {{
      {"P2000C System Reference and Service Manual",
       "P2000C-SystemRefServiceManual.pdf", "systemReferenceManualAction"},
      {"P2519 CP/M User Guide", "P2519CPM_UserGuide.pdf",
       "cpmUserGuideAction"},
      {"P2519 CP/M Reference Manual", "P2519_CPM_Reference.pdf",
       "cpmReferenceManualAction"},
  }};
  for (const ManualEntry& manual : kManuals) {
    QAction* action = documentation_menu->addAction(manual.title);
    action->setObjectName(manual.object_name);
    const QString path = manual_path(manual.filename);
    action->setData(path);
    action->setToolTip(path.isEmpty() ? "Manual was not found." : path);
    action->setEnabled(!path.isEmpty());
    connect(action, &QAction::triggered, this, [this, path]() {
      if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(this, "Cannot Open Manual",
                             "No application is available to open:\n" + path);
      }
      display_->setFocus();
    });
  }
  help_menu->addSeparator();
  QAction* about = help_menu->addAction("&About P2000C Emulator...");
  about->setObjectName("aboutAction");
  connect(about, &QAction::triggered, this, &MainWindow::open_about);
  const QColor saved_color =
      QSettings()
          .value("display/baseColor", DisplayWidget::default_base_color())
          .value<QColor>();
  display_->set_base_color(saved_color.isValid()
                               ? saved_color
                               : DisplayWidget::default_base_color());
  display_->set_crt_effects(load_crt_effects(QSettings()));
  refresh_recent_image_menus();
}

void MainWindow::refresh_screen() {
  if (machine_.has_ipl_rom()) {
    terminal_revision_ = machine_.terminal().revision();
    display_->set_screen(machine_.terminal().screen(),
                         machine_.terminal().attributes(),
                         machine_.terminal().graphics_mode(),
                         machine_.terminal().graphic_screen());
    display_->set_cursor(machine_.terminal().cursor_column(),
                         machine_.terminal().cursor_row(),
                         machine_.terminal().cursor_visible());
    return;
  }
  display_->clear();
  display_->write_text(25, 2, "PHILIPS P2000C EMULATOR");
  display_->write_text(2, 5, "MAINBOARD: Z80A 4 MHZ / 64 KBYTE RAM");
  display_->write_text(2, 7,
                       machine_.has_ipl_rom()
                           ? "IPL ROM: LOADED (4096 BYTES)"
                           : "IPL ROM: MISSING - LOAD VIA MACHINE MENU");
  if (machine_.floppy_a().has_value()) {
    display_->write_text(2, 9, "FLOPPY A: MOUNTED READ/WRITE");
    display_->write_text(2, 10, "RAW CAPACITY: 640 KIB");
  } else {
    display_->write_text(2, 9, "FLOPPY A: NOT MOUNTED");
  }
  display_->write_text(2, 12,
                       machine_.floppy_b().has_value()
                           ? "FLOPPY B: MOUNTED READ/WRITE"
                           : "FLOPPY B: NOT MOUNTED");
}

void MainWindow::load_ipl_rom() {
  const QString path =
      QFileDialog::getOpenFileName(this, "Load 4 KiB mainboard IPL ROM", {},
                                   "ROM images (*.rom *.bin);;All files (*)");
  if (path.isEmpty()) {
    return;
  }
  std::string error;
  if (!machine_.load_ipl_rom(path.toStdString(), &error)) {
    QMessageBox::critical(this, "Cannot load IPL ROM",
                          QString::fromStdString(error));
    return;
  }
  statusBar()->showMessage("IPL ROM loaded: " + path);
  refresh_media_indicators();
  refresh_screen();
}

void MainWindow::open_floppy(std::size_t drive) {
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const QString path = QFileDialog::getOpenFileName(
      this, QString("Mount floppy %1 read/write").arg(drive_letter), {},
      "Raw P2000C floppies (*.flp);;All files (*)");
  if (!path.isEmpty()) {
    mount_floppy(filesystem_path(path), drive);
  }
}

void MainWindow::open_hard_disk(std::size_t drive) {
  const QString path = QFileDialog::getOpenFileName(
      this, QString("Mount SASI hard disk %1 read/write").arg(drive + 1), {},
      "Raw P2000C hard disks (*.hda);;All files (*)");
  if (!path.isEmpty()) {
    mount_hard_disk(filesystem_path(path), drive);
  }
}

void MainWindow::save_floppy_as(std::size_t drive) {
  const std::optional<RawDiskImage>& image =
      drive == 0 ? machine_.floppy_a() : machine_.floppy_b();
  if (!image.has_value()) {
    return;
  }
  QString path = QFileDialog::getSaveFileName(
      this, QString("Save floppy %1 as").arg(drive == 0 ? 'A' : 'B'),
      QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
          .filePath(QFileInfo(qstring_path(image->path())).fileName()),
      "Raw P2000C floppy images (*.flp)");
  if (path.isEmpty()) {
    return;
  }
  if (QFileInfo(path).suffix().isEmpty()) {
    path += ".flp";
  }
  QString error;
  if (!copy_image_file(qstring_path(image->path()), path, &error)) {
    QMessageBox::critical(this, "Cannot Save Floppy", error);
    return;
  }
  if (mount_floppy(filesystem_path(path), drive)) {
    statusBar()->showMessage("Persistent floppy image saved and mounted: " +
                                 path,
                             5000);
  }
}

void MainWindow::save_hard_disk_as(std::size_t drive) {
  const std::optional<RawDiskImage>& image = machine_.hard_disk(drive);
  if (!image.has_value()) {
    return;
  }
  QString path = QFileDialog::getSaveFileName(
      this, QString("Save hard disk %1 as").arg(drive + 1),
      QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
          .filePath(QFileInfo(qstring_path(image->path())).fileName()),
      "Raw P2000C hard-disk images (*.hda)");
  if (path.isEmpty()) {
    return;
  }
  if (QFileInfo(path).suffix().isEmpty()) {
    path += ".hda";
  }
  QString error;
  if (!copy_image_file(qstring_path(image->path()), path, &error)) {
    QMessageBox::critical(this, "Cannot Save Hard Disk", error);
    return;
  }
  if (mount_hard_disk(filesystem_path(path), drive)) {
    statusBar()->showMessage("Persistent hard-disk image saved and mounted: " +
                                 path,
                             5000);
  }
}

void MainWindow::refresh_drive_position(bool hard_disk, std::size_t drive) {
  if (drive >= 2) {
    return;
  }
  DrivePositionDisplay* display =
      hard_disk ? hard_disk_position_displays_[drive]
                : floppy_position_displays_[drive];
  if (display == nullptr) {
    return;
  }
  const bool mounted = hard_disk
                           ? machine_.hard_disk(drive).has_value()
                           : (drive == 0 ? machine_.floppy_a().has_value()
                                         : machine_.floppy_b().has_value());
  if (!mounted) {
    if (hard_disk) {
      display->set_hard_disk_position(0, false);
    } else {
      display->set_floppy_position(0, 0, false);
    }
    display->setToolTip("No media position is available.");
    return;
  }
  if (hard_disk) {
    const std::size_t block = machine_.hard_disk_block(drive);
    const std::size_t final_block =
        RawDiskImage::kHardDiskSize / RawDiskImage::kSectorSize - 1;
    display->set_hard_disk_position(block, true);
    display->setToolTip(
        QString("Last SASI block accessed: %1 of %2")
            .arg(block)
            .arg(final_block));
  } else {
    const unsigned track = machine_.floppy_track(drive);
    const unsigned side = machine_.floppy_side(drive);
    display->set_floppy_position(track, side, true);
    display->setToolTip(
        QString("Current floppy position: track %1 of %2, side %3 of %4")
            .arg(track)
            .arg(RawDiskImage::kFloppyCylinders - 1)
            .arg(side)
            .arg(RawDiskImage::kFloppyHeads - 1));
  }
}

void MainWindow::refresh_media_indicators() {
  for (std::size_t drive = 0; drive < 2; ++drive) {
    const QChar drive_letter = drive == 0 ? 'A' : 'B';
    const std::optional<RawDiskImage>& image =
        drive == 0 ? machine_.floppy_a() : machine_.floppy_b();
    if (!image.has_value()) {
      media_drive_menus_[drive]->setTitle(
          QString("Drive &%1 — empty").arg(drive_letter));
      current_media_actions_[drive]->setText("Mounted: none");
      current_media_actions_[drive]->setChecked(false);
      current_media_actions_[drive]->setToolTip({});
      current_media_actions_[drive]->setStatusTip({});
      media_status_labels_[drive]->setText(
          QString("Drive %1: empty").arg(drive_letter));
      media_status_labels_[drive]->setToolTip(
          QString("No image is mounted in floppy drive %1.").arg(drive_letter));
      refresh_drive_position(false, drive);
      bundled_system_actions_[drive]->setChecked(false);
      bundled_zork_actions_[drive]->setChecked(false);
      bundled_chess_actions_[drive]->setChecked(false);
      bundled_ipldump_actions_[drive]->setChecked(false);
      bundled_blank_actions_[drive]->setChecked(false);
      continue;
    }

    const QString filename = compact_filename(image->path());
    const QString full_path = qstring_path(image->path());
    const bool temporary =
        !temporary_floppy_paths_[drive].isEmpty() &&
        QFileInfo(full_path).absoluteFilePath() ==
            QFileInfo(temporary_floppy_paths_[drive]).absoluteFilePath();
    const QString tooltip =
        QString("Drive %1 — %2 raw FLP image\n%3")
            .arg(drive_letter)
            .arg(temporary ? "writable session copy of bundled template"
                           : "persistent writable image")
            .arg(full_path);
    media_drive_menus_[drive]->setTitle(
        QString("Drive &%1 — %2").arg(drive_letter).arg(menu_safe(filename)));
    current_media_actions_[drive]->setText("Mounted: " + menu_safe(filename));
    current_media_actions_[drive]->setChecked(true);
    current_media_actions_[drive]->setToolTip(tooltip);
    current_media_actions_[drive]->setStatusTip(full_path);
    media_status_labels_[drive]->setText(filename + (temporary ? " *" : ""));
    media_status_labels_[drive]->setToolTip(tooltip);
    refresh_drive_position(false, drive);

    const QString& system_path = bundled_system_paths_[drive];
    const QString& zork_path = bundled_zork_paths_[drive];
    const QString& chess_path = bundled_chess_paths_[drive];
    const QString& ipldump_path = bundled_ipldump_paths_[drive];
    const QString& blank_path = bundled_blank_paths_[drive];
    bundled_system_actions_[drive]->setChecked(
        !system_path.isEmpty() && QFileInfo(full_path).absoluteFilePath() ==
        QFileInfo(system_path).absoluteFilePath());
    bundled_zork_actions_[drive]->setChecked(
        !zork_path.isEmpty() && QFileInfo(full_path).absoluteFilePath() ==
        QFileInfo(zork_path).absoluteFilePath());
    bundled_chess_actions_[drive]->setChecked(
        !chess_path.isEmpty() && QFileInfo(full_path).absoluteFilePath() ==
        QFileInfo(chess_path).absoluteFilePath());
    bundled_ipldump_actions_[drive]->setChecked(
        !ipldump_path.isEmpty() && QFileInfo(full_path).absoluteFilePath() ==
        QFileInfo(ipldump_path).absoluteFilePath());
    bundled_blank_actions_[drive]->setChecked(
        QFileInfo(full_path).absoluteFilePath() ==
        QFileInfo(blank_path).absoluteFilePath());
  }

  for (std::size_t drive = 0; drive < 2; ++drive) {
    const QString volumes = drive == 0 ? "C/D" : "E/F";
    const std::optional<RawDiskImage>& image = machine_.hard_disk(drive);
    if (!image.has_value()) {
      hard_disk_menus_[drive]->setTitle(
          QString("Hard disk &%1 (%2) — empty")
              .arg(drive + 1)
              .arg(volumes));
      current_hard_disk_actions_[drive]->setText("Mounted: none");
      current_hard_disk_actions_[drive]->setChecked(false);
      hard_disk_status_labels_[drive]->setText(volumes + ": empty");
      hard_disk_status_labels_[drive]->setToolTip(
          QString("No image is mounted for CP/M volumes %1.").arg(volumes));
      refresh_drive_position(true, drive);
      bundled_hard_disk_actions_[drive]->setChecked(false);
      continue;
    }

    const QString filename = compact_filename(image->path());
    const QString full_path = qstring_path(image->path());
    const bool temporary =
        !temporary_hard_disk_paths_[drive].isEmpty() &&
        QFileInfo(full_path).absoluteFilePath() ==
            QFileInfo(temporary_hard_disk_paths_[drive]).absoluteFilePath();
    const QString tooltip =
        QString("CP/M volumes %1 — %2 10 MiB raw HDA image\n%3")
            .arg(volumes)
            .arg(temporary ? "writable session copy of bundled template"
                           : "persistent writable image")
            .arg(full_path);
    hard_disk_menus_[drive]->setTitle(
        QString("Hard disk &%1 (%2) — %3")
            .arg(drive + 1)
            .arg(volumes)
            .arg(menu_safe(filename)));
    current_hard_disk_actions_[drive]->setText("Mounted: " +
                                                menu_safe(filename));
    current_hard_disk_actions_[drive]->setChecked(true);
    current_hard_disk_actions_[drive]->setToolTip(tooltip);
    current_hard_disk_actions_[drive]->setStatusTip(full_path);
    hard_disk_status_labels_[drive]->setText(filename +
                                             (temporary ? " *" : ""));
    hard_disk_status_labels_[drive]->setToolTip(tooltip);
    refresh_drive_position(true, drive);
    bundled_hard_disk_actions_[drive]->setChecked(
        temporary && QFileInfo(full_path).absoluteFilePath() ==
                         QFileInfo(temporary_hard_disk_paths_[drive])
                             .absoluteFilePath());
  }
}

void MainWindow::mount_bundled_system_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = temporary_resource_copy(
      ":/images/system.flp", media_session_.path(),
      QString("system_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare bundled floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    temporary_floppy_paths_[drive] = *path;
    bundled_system_paths_[drive] = *path;
    refresh_media_indicators();
    if (drive == 0) {
      machine_.reset();
      pending_t_states_ = 0.0;
      execution_timer_.restart();
      refresh_media_indicators();
      refresh_screen();
    }
    statusBar()->showMessage(
        QString(
            "Pristine CP/M 2.2 template mounted in drive %1 as a writable "
            "session copy.")
            .arg(drive_letter));
  }
}

void MainWindow::mount_bundled_zork_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = temporary_resource_copy(
      ":/images/zork.flp", media_session_.path(),
      QString("zork_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare ZORK floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    temporary_floppy_paths_[drive] = *path;
    bundled_zork_paths_[drive] = *path;
    refresh_media_indicators();
    statusBar()->showMessage(
        QString("ZORK data floppy mounted in drive %1.").arg(drive_letter));
  }
}

void MainWindow::mount_bundled_chess_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = temporary_resource_copy(
      ":/images/chess.flp", media_session_.path(),
      QString("chess_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare CHESS floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    temporary_floppy_paths_[drive] = *path;
    bundled_chess_paths_[drive] = *path;
    refresh_media_indicators();
    statusBar()->showMessage(
        QString("CHESS data floppy mounted in drive %1.").arg(drive_letter));
  }
}

void MainWindow::mount_bundled_ipldump_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = temporary_resource_copy(
      ":/images/ipldump.flp", media_session_.path(),
      QString("ipldump_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare IPL dump floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    temporary_floppy_paths_[drive] = *path;
    bundled_ipldump_paths_[drive] = *path;
    refresh_media_indicators();
    statusBar()->showMessage(
        QString("IPL dump toolchain floppy mounted in drive %1.")
            .arg(drive_letter));
  }
}

void MainWindow::mount_bundled_blank_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = temporary_blank_floppy(
      media_session_.path(),
      QString("blank_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare blank floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    temporary_floppy_paths_[drive] = *path;
    bundled_blank_paths_[drive] = *path;
    refresh_media_indicators();
    statusBar()->showMessage(
        QString("Blank 640 KiB template mounted in drive %1 as a writable "
                "session copy.")
            .arg(drive_letter));
  }
}

void MainWindow::mount_bundled_hard_disk(std::size_t drive) {
  QString error;
  const std::optional<QString> path = temporary_resource_copy(
      ":/images/blank.hda", media_session_.path(),
      QString("hard_disk_%1.hda").arg(drive + 1), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare default hard disk", error);
    refresh_media_indicators();
    return;
  }
  if (mount_hard_disk(filesystem_path(*path), drive)) {
    temporary_hard_disk_paths_[drive] = *path;
    refresh_media_indicators();
    statusBar()->showMessage(
        QString("Pristine blank hard-disk template %1 mounted as a writable "
                "session copy.")
            .arg(drive + 1));
  }
}

void MainWindow::mount_default_media() {
  mount_bundled_system_floppy(0);
  mount_bundled_hard_disk(0);
  mount_bundled_hard_disk(1);
  machine_.reset();
  pending_t_states_ = 0.0;
  refresh_media_indicators();
  refresh_screen();
}

void MainWindow::set_display_resolution(const QSize& resolution) {
  QSettings().setValue("display/resolution", resolution);
  setMinimumSize(0, 0);
  setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  display_->setFixedSize(resolution);
  adjustSize();
  setFixedSize(sizeHint());
}

void MainWindow::open_screen_color_settings() {
  const QColor original_color = display_->base_color();
  const CrtEffects original_effects = display_->crt_effects();
  ScreenColorDialog dialog(original_color, original_effects, this);
  dialog.set_preview_handler([this](const QColor& color,
                                    const CrtEffects& effects) {
    display_->set_base_color(color);
    display_->set_crt_effects(effects);
  });
  if (dialog.exec() == QDialog::Accepted) {
    QSettings settings;
    settings.setValue("display/baseColor", dialog.selected_color());
    save_crt_effects(&settings, dialog.selected_effects());
    statusBar()->showMessage("CRT screen settings saved.", 3000);
  } else {
    display_->set_base_color(original_color);
    display_->set_crt_effects(original_effects);
  }
  display_->setFocus();
}

void MainWindow::open_audio_volume_settings() {
  const int original_volume =
      std::clamp(qRound(hardware_audio_->volume() * 100.0), 0, 100);
  QDialog dialog(this);
  dialog.setObjectName("hardwareSoundVolumeDialog");
  dialog.setWindowTitle("Hardware Sound Volume");
  dialog.setModal(true);
  dialog.setMinimumWidth(360);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(new QLabel(
      "Master volume for the P2000C beeper and floppy drives.",
      &dialog));
  auto* value_label = new QLabel(&dialog);
  value_label->setObjectName("hardwareSoundVolumeLabel");
  auto* slider = new QSlider(Qt::Horizontal, &dialog);
  slider->setObjectName("hardwareSoundVolumeSlider");
  slider->setRange(0, 100);
  slider->setSingleStep(1);
  slider->setPageStep(10);
  slider->setValue(original_volume);
  slider->setAccessibleName("Hardware sound master volume");
  layout->addWidget(value_label);
  layout->addWidget(slider);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                            QDialogButtonBox::Cancel,
                                        &dialog);
  buttons->setObjectName("hardwareSoundVolumeButtons");
  auto* test = buttons->addButton("Test Beep", QDialogButtonBox::ActionRole);
  test->setEnabled(audio_enabled_);
  connect(test, &QPushButton::clicked, &dialog,
          [this]() { hardware_audio_->play_bell(); });
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(slider, &QSlider::valueChanged, &dialog,
          [this, value_label](int value) {
            value_label->setText(QString("Volume: %1%").arg(value));
            hardware_audio_->set_volume(value / 100.0);
          });
  value_label->setText(QString("Volume: %1%").arg(original_volume));
  layout->addWidget(buttons);

  if (dialog.exec() == QDialog::Accepted) {
    QSettings().setValue("audio/volume", slider->value());
    statusBar()->showMessage(
        QString("Hardware sound volume set to %1%.").arg(slider->value()),
        3000);
  } else {
    hardware_audio_->set_volume(original_volume / 100.0);
  }
  display_->setFocus();
}

void MainWindow::open_about() {
  QDialog dialog(this);
  dialog.setObjectName("aboutDialog");
  dialog.setWindowTitle("About P2000C Emulator");
  dialog.resize(690, 650);
  auto* layout = new QVBoxLayout(&dialog);

  auto* logo = new QLabel(&dialog);
  logo->setObjectName("aboutLogo");
  logo->setAlignment(Qt::AlignCenter);
  logo->setAccessibleName("Philips P2000C computer illustration");
  logo->setPixmap(QIcon(":/logo/logo-p2000c.svg")
                      .pixmap(QSize(400, 169), 1.0));
  layout->addWidget(logo);

  auto* tabs = new QTabWidget(&dialog);
  tabs->setObjectName("aboutTabs");
  auto* overview = new QTextBrowser(tabs);
  overview->setObjectName("aboutOverview");
  overview->setOpenExternalLinks(true);
  overview->setHtml(
      QString(
          "<h2>P2000C Emulator %1</h2>"
          "<p>An independent, work-in-progress emulator for the Philips "
          "P2000C portable computer. It is not affiliated with or endorsed "
          "by Philips or any owner of the bundled historical software.</p>"
          "<p><b>Emulator source license:</b> GNU General Public License "
          "version 3 only (GPL-3.0-only), without warranty.</p>"
          "<p><b>Runtime:</b> Qt %2. Peripheral models are functional and "
          "timing-aware, but are not transistor- or bus-cycle-exact.</p>"
          "<p>The GPL declaration covers original emulator source. Historical "
          "manuals, firmware, character data, disk images, and programs inside "
          "those images retain separate rights; see <i>Third-party and asset "
          "notices</i> for the full disclosure.</p>")
          .arg(P2000C_VERSION, qVersion()));
  tabs->addTab(overview, "About");

  auto* notices = new QTextBrowser(tabs);
  notices->setObjectName("aboutThirdPartyNotices");
  notices->setOpenExternalLinks(true);
  QFile notices_file(":/THIRD_PARTY.md");
  if (notices_file.open(QIODevice::ReadOnly)) {
    notices->setMarkdown(QString::fromUtf8(notices_file.readAll()));
  }
  tabs->addTab(notices, "Third-party and asset notices");

  auto* third_party_licenses = new QTextBrowser(tabs);
  third_party_licenses->setObjectName("aboutThirdPartyLicenses");
  QString license_notices;
  const std::array<std::pair<QString, QString>, 2> third_party_files = {{
      {"superzazu/z80 — MIT", ":/third_party/superzazu_z80/LICENSE"},
      {"MAME floppy samples — BSD-3-Clause",
       ":/audio/LICENSE-MAME-SAMPLES.txt"},
  }};
  for (const auto& [heading, path] : third_party_files) {
    QFile file(path);
    license_notices += "====================\n" + heading +
                       "\n====================\n\n";
    if (file.open(QIODevice::ReadOnly)) {
      license_notices += QString::fromUtf8(file.readAll());
    } else {
      license_notices += "License notice unavailable.";
    }
    license_notices += "\n\n";
  }
  third_party_licenses->setPlainText(license_notices);
  tabs->addTab(third_party_licenses, "Third-party licenses");

  auto* license = new QTextBrowser(tabs);
  license->setObjectName("aboutLicenseText");
  QFile license_file(":/LICENSE");
  if (license_file.open(QIODevice::ReadOnly)) {
    license->setPlainText(QString::fromUtf8(license_file.readAll()));
  }
  tabs->addTab(license, "GPL-3.0 license");
  layout->addWidget(tabs);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
  buttons->setObjectName("aboutButtons");
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  layout->addWidget(buttons);
  dialog.exec();
  display_->setFocus();
}

void MainWindow::save_screenshot() {
  const QString pictures =
      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  const QString filename =
      "p2000c-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") +
      ".png";
  QString path = QFileDialog::getSaveFileName(
      this, "Save CRT Screenshot", QDir(pictures).filePath(filename),
      "PNG images (*.png)");
  if (path.isEmpty()) {
    display_->setFocus();
    return;
  }
  if (QFileInfo(path).suffix().isEmpty()) {
    path += ".png";
  }
  if (!display_->capture_screenshot().save(path, "PNG")) {
    QMessageBox::critical(this, "Cannot Save Screenshot",
                          "The screenshot could not be written to:\n" + path);
  } else {
    statusBar()->showMessage("Screenshot saved: " + path, 5000);
  }
  display_->setFocus();
}

void MainWindow::set_emulation_speed(double multiplier) {
  speed_multiplier_ = multiplier;
  pending_t_states_ = 0.0;
  if (execution_timer_.isValid()) {
    execution_timer_.restart();
  }
  QSettings().setValue("machine/speed", multiplier);
}

void MainWindow::run_emulation_slice() {
  const qint64 elapsed_nanoseconds =
      std::min(execution_timer_.nsecsElapsed(), kMaximumCatchUpNanoseconds);
  execution_timer_.restart();
  pending_t_states_ += static_cast<double>(elapsed_nanoseconds) * kBaseClockHz *
                       speed_multiplier_ / 1'000'000'000.0;
  const auto t_states = static_cast<std::uint64_t>(pending_t_states_);
  pending_t_states_ -= static_cast<double>(t_states);
  machine_.run_for(t_states);
  const std::uint64_t bell_revision = machine_.terminal().bell_revision();
  if (bell_revision != terminal_bell_revision_) {
    terminal_bell_revision_ = bell_revision;
    if (audio_enabled_) {
      hardware_audio_->play_bell();
    }
  }
  if (terminal_revision_ != machine_.terminal().revision()) {
    terminal_revision_ = machine_.terminal().revision();
    display_->set_screen(machine_.terminal().screen(),
                         machine_.terminal().attributes(),
                         machine_.terminal().graphics_mode(),
                         machine_.terminal().graphic_screen());
    display_->set_cursor(machine_.terminal().cursor_column(),
                         machine_.terminal().cursor_row(),
                         machine_.terminal().cursor_visible());
  }
}

}  // namespace p2000c
