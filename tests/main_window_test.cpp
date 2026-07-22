#include "app/main_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDial>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QFontDatabase>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>

#include "app/display_widget.h"
#include "app/hardware_audio.h"
#include "core/raw_disk_image.h"

namespace {

/** Finds a window action by its exact visible text. */
QAction* find_action(p2000c::MainWindow* window, const QString& text) {
  const QList<QAction*> actions = window->findChildren<QAction*>();
  const auto match = std::find_if(
      actions.begin(), actions.end(),
      [&](const QAction* action) { return action->text() == text; });
  return match == actions.end() ? nullptr : *match;
}

/** Finds a named action without depending on its dynamic visible label. */
QAction* find_named_action(p2000c::MainWindow* window,
                           const char* object_name) {
  return window->findChild<QAction*>(object_name);
}

/** Converts a QString to a host filesystem path for raw-image validation. */
std::filesystem::path filesystem_path(const QString& path) {
  const QByteArray utf8 = path.toUtf8();
  const auto* begin = reinterpret_cast<const char8_t*>(utf8.constData());
  return std::filesystem::path(std::u8string(begin, begin + utf8.size()));
}

/** Opens an expected working image and checks its physical geometry. */
bool validate_image(const QString& path, p2000c::RawDiskImage::Kind kind) {
  std::string error;
  const std::optional<p2000c::RawDiskImage> image =
      p2000c::RawDiskImage::open(filesystem_path(path), kind, &error);
  if (!image.has_value()) {
    std::cerr << "Invalid bundled working image: " << error << '\n';
    return false;
  }
  return true;
}

/** Mirrors the unaddressable CRT margin used by the presentation renderer. */
QRectF active_crt_area(const QSize& size) {
  constexpr qreal kOverscanFraction = 0.035;
  const QRectF glass(QPointF(0.0, 0.0), QSizeF(size));
  return glass.adjusted(glass.width() * kOverscanFraction,
                        glass.height() * kOverscanFraction,
                        -glass.width() * kOverscanFraction,
                        -glass.height() * kOverscanFraction);
}

/** Maps a coordinate from the former edge-to-edge raster into active glass. */
QPoint active_crt_point(const QImage& image, qreal x, qreal y) {
  const QRectF active = active_crt_area(image.size());
  return QPoint(qRound(active.left() + x / image.width() * active.width()),
                qRound(active.top() + y / image.height() * active.height()));
}

/** Checks inverse cells and the four documented intensity levels in pixels. */
bool validate_attribute_rendering() {
  p2000c::DisplayWidget display;
  display.setFixedSize(384, 288);
  display.set_cursor(0, 0, false);
  p2000c::CrtEffects effects = p2000c::DisplayWidget::default_crt_effects();
  effects.persistence = false;
  effects.curvature = false;
  effects.vignette = false;
  display.set_crt_effects(effects);

  p2000c::Terminal::Screen screen;
  screen.fill(0x20);
  p2000c::Terminal::AttributeScreen attributes;
  attributes.fill(p2000c::Terminal::kDefaultAttribute);
  constexpr std::array<int, 4> kColumns = {8, 28, 48, 68};
  attributes[kColumns[0]] = p2000c::Terminal::kAttributeInverse;
  attributes[kColumns[1]] = p2000c::Terminal::kAttributeInverse |
                            p2000c::Terminal::kAttributeIntensityHigh |
                            p2000c::Terminal::kAttributeIntensityLow;
  attributes[kColumns[2]] = p2000c::Terminal::kAttributeInverse |
                            p2000c::Terminal::kAttributeIntensityHigh;
  attributes[kColumns[3]] = p2000c::Terminal::kAttributeInverse |
                            p2000c::Terminal::kAttributeIntensityLow;
  display.set_screen(screen, attributes);

  QImage image(display.size(), QImage::Format_ARGB32);
  image.fill(Qt::black);
  display.render(&image);
  auto cell_energy = [&](int column) {
    int energy = 0;
    const QRectF active = active_crt_area(image.size());
    const QRectF cell(active.left() + column * active.width() / 80.0,
                      active.top(), active.width() / 80.0,
                      active.height() / 24.0);
    for (int y = qCeil(cell.top()); y < qFloor(cell.bottom()); ++y) {
      for (int x = qCeil(cell.left()); x < qFloor(cell.right()); ++x) {
        energy += qGreen(image.pixel(x, y));
      }
    }
    return energy;
  };

  const int quarter = cell_energy(kColumns[0]);
  const int half = cell_energy(kColumns[1]);
  const int normal = cell_energy(kColumns[2]);
  const int bold = cell_energy(kColumns[3]);
  if (!(quarter < half && half < normal && normal < bold)) {
    std::cerr << "Attribute intensity rendering is out of order: " << quarter
              << ", " << half << ", " << normal << ", " << bold << '\n';
    return false;
  }
  return true;
}

/** Checks that both graphics RAM layouts reach their physical raster edges. */
bool validate_graphics_rendering() {
  auto send = [](p2000c::Terminal* terminal,
                 std::initializer_list<std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes) {
      terminal->receive(byte);
    }
  };
  auto area_energy = [](const QImage& image, int center_x, int center_y) {
    int energy = 0;
    for (int y = center_y - 1; y <= center_y + 1; ++y) {
      for (int x = center_x - 1; x <= center_x + 1; ++x) {
        energy += qGreen(image.pixel(x, y));
      }
    }
    return energy;
  };
  auto render = [](const p2000c::Terminal& terminal) {
    p2000c::DisplayWidget display;
    display.setFixedSize(384, 288);
    display.set_cursor(0, 0, false);
    p2000c::CrtEffects effects = p2000c::DisplayWidget::default_crt_effects();
    effects.persistence = false;
    effects.curvature = false;
    effects.vignette = false;
    display.set_crt_effects(effects);
    display.set_screen(terminal.screen(), terminal.attributes(),
                       terminal.graphics_mode(), terminal.graphic_screen());
    QImage image(display.size(), QImage::Format_ARGB32);
    image.fill(Qt::black);
    display.render(&image);
    return image;
  };

  p2000c::Terminal medium;
  send(&medium, {0x1b, '5', 0x1b, 'D', 0x00, 0x00});
  const QImage medium_image = render(medium);
  const QPoint medium_pixel = active_crt_point(medium_image, 39, 269);
  const QPoint medium_blank = active_crt_point(medium_image, 49, 269);
  const int medium_pixel_energy =
      area_energy(medium_image, medium_pixel.x(), medium_pixel.y());
  const int medium_blank_energy =
      area_energy(medium_image, medium_blank.x(), medium_blank.y());
  if (medium.graphics_mode() !=
          p2000c::Terminal::GraphicsMode::kMedium256 ||
      medium_pixel_energy * 4 <= medium_blank_energy * 5) {
    std::cerr << "Medium-resolution graphics were not rendered: "
              << medium_pixel_energy << " vs " << medium_blank_energy
              << ".\n";
    return false;
  }

  p2000c::Terminal high;
  send(&high, {0x1b, '3', 0x1b, 'D', 0xff, 0x01, 0xfb});
  const QImage high_image = render(high);
  const QPoint high_pixel = active_crt_point(high_image, 345, 19);
  const QPoint high_blank = active_crt_point(high_image, 335, 19);
  const int high_pixel_energy =
      area_energy(high_image, high_pixel.x(), high_pixel.y());
  const int high_blank_energy =
      area_energy(high_image, high_blank.x(), high_blank.y());
  if (high.graphics_mode() != p2000c::Terminal::GraphicsMode::kHigh512 ||
      high_pixel_energy * 4 <= high_blank_energy * 5) {
    std::cerr << "High-resolution graphics were not rendered: "
              << high_pixel_energy << " vs " << high_blank_energy << ".\n";
    return false;
  }
  return true;
}

/** Checks that scanline separation and temporal persistence are effective. */
bool validate_crt_effect_rendering() {
  if (p2000c::DisplayWidget::default_crt_effects()
          .persistence_half_life_ms >= 170) {
    std::cerr << "The default phosphor persistence was not shortened.\n";
    return false;
  }
  p2000c::Terminal::Screen lit_screen;
  lit_screen.fill(0x20);
  p2000c::Terminal::AttributeScreen lit_attributes;
  lit_attributes.fill(p2000c::Terminal::kAttributeInverse |
                      p2000c::Terminal::kAttributeIntensityHigh);
  auto render = [&](const p2000c::CrtEffects& effects) {
    p2000c::DisplayWidget display;
    display.setFixedSize(768, 576);
    display.set_cursor(0, 0, false);
    display.set_crt_effects(effects);
    display.set_screen(lit_screen, lit_attributes);
    QImage image(display.size(), QImage::Format_ARGB32);
    image.fill(Qt::black);
    display.render(&image);
    return image;
  };
  auto image_energy = [](const QImage& image) {
    std::uint64_t energy = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        energy += qGreen(image.pixel(x, y));
      }
    }
    return energy;
  };

  const p2000c::CrtEffects plain = {false, false, false,
                                    false, false, false};
  p2000c::CrtEffects scanlines = plain;
  scanlines.scanlines = true;
  const QImage plain_image = render(plain);
  const std::uint64_t plain_energy = image_energy(plain_image);
  const QImage scanline_image = render(scanlines);
  const std::uint64_t scanline_energy = image_energy(scanline_image);
  if (scanline_image.pixelColor(0, 0) ==
      scanline_image.pixelColor(scanline_image.width() / 2, 0)) {
    std::cerr << "CRT presentation did not retain its rounded corners.\n";
    return false;
  }
  const QColor overscan = plain_image.pixelColor(
      plain_image.width() / 100, plain_image.height() / 2);
  constexpr std::array<QPointF, 4> kSafeRasterCorners = {
      QPointF(0.05, 0.05), QPointF(0.95, 0.05),
      QPointF(0.05, 0.95), QPointF(0.95, 0.95)};
  for (const QPointF& corner : kSafeRasterCorners) {
    const QColor active = plain_image.pixelColor(
        qRound(corner.x() * (plain_image.width() - 1)),
        qRound(corner.y() * (plain_image.height() - 1)));
    if (qGreen(active.rgb()) <= qGreen(overscan.rgb()) + 15) {
      std::cerr << "Active CRT raster reached a clipped glass corner.\n";
      return false;
    }
  }
  if (scanline_energy * 100 >= plain_energy * 94) {
    std::cerr << "Scanline separation did not materially shape the raster: "
              << scanline_energy << " vs " << plain_energy << ".\n";
    return false;
  }

  p2000c::CrtEffects dim = plain;
  dim.brightness_percent = 30;
  p2000c::CrtEffects bright = plain;
  bright.brightness_percent = 150;
  const std::uint64_t dim_energy = image_energy(render(dim));
  const std::uint64_t bright_energy = image_energy(render(bright));
  if (bright_energy * 10 <= dim_energy * 14) {
    std::cerr << "CRT brightness control did not materially alter output: "
              << dim_energy << " vs " << bright_energy << ".\n";
    return false;
  }

  p2000c::DisplayWidget persistent_display;
  persistent_display.setFixedSize(384, 288);
  persistent_display.set_cursor(0, 0, false);
  p2000c::CrtEffects persistence = plain;
  persistence.persistence = true;
  persistent_display.set_crt_effects(persistence);
  p2000c::Terminal::Screen sparse_screen;
  sparse_screen.fill(0x20);
  p2000c::Terminal::AttributeScreen sparse_attributes;
  sparse_attributes.fill(p2000c::Terminal::kDefaultAttribute);
  constexpr int kCell = 10 * 80 + 40;
  sparse_attributes[kCell] = p2000c::Terminal::kAttributeInverse |
                             p2000c::Terminal::kAttributeIntensityHigh;
  persistent_display.set_screen(sparse_screen, sparse_attributes);
  QImage first(persistent_display.size(), QImage::Format_ARGB32);
  first.fill(Qt::black);
  persistent_display.render(&first);
  sparse_attributes[kCell] = p2000c::Terminal::kDefaultAttribute;
  persistent_display.set_screen(sparse_screen, sparse_attributes);
  QImage afterglow(persistent_display.size(), QImage::Format_ARGB32);
  afterglow.fill(Qt::black);
  persistent_display.render(&afterglow);
  persistent_display.set_crt_effects(plain);
  QImage cleared(persistent_display.size(), QImage::Format_ARGB32);
  cleared.fill(Qt::black);
  persistent_display.render(&cleared);
  auto cell_energy = [](const QImage& image) {
    std::uint64_t energy = 0;
    const QRectF active = active_crt_area(image.size());
    const QRectF cell(active.left() + 40.0 * active.width() / 80.0,
                      active.top() + 10.0 * active.height() / 24.0,
                      active.width() / 80.0, active.height() / 24.0);
    for (int y = qCeil(cell.top()); y < qFloor(cell.bottom()); ++y) {
      for (int x = qCeil(cell.left()); x < qFloor(cell.right()); ++x) {
        energy += qGreen(image.pixel(x, y));
      }
    }
    return energy;
  };
  if (cell_energy(afterglow) <= cell_energy(cleared) * 2) {
    std::cerr << "Phosphor persistence did not retain extinguished pixels.\n";
    return false;
  }

  p2000c::DisplayWidget stable_display;
  stable_display.setFixedSize(384, 288);
  stable_display.set_cursor(0, 0, false);
  stable_display.set_crt_effects(persistence);
  stable_display.set_screen(lit_screen, lit_attributes);
  QImage stable_first(stable_display.size(), QImage::Format_ARGB32);
  stable_first.fill(Qt::black);
  stable_display.render(&stable_first);
  QEventLoop persistence_wait;
  QTimer::singleShot(50, &persistence_wait, &QEventLoop::quit);
  persistence_wait.exec();
  QImage stable_second(stable_display.size(), QImage::Format_ARGB32);
  stable_second.fill(Qt::black);
  stable_display.render(&stable_second);
  if (stable_first != stable_second) {
    std::cerr << "Persistence changed a stationary phosphor image.\n";
    return false;
  }

  p2000c::CrtEffects flicker = plain;
  flicker.flicker = true;
  stable_display.set_crt_effects(flicker);
  QImage flicker_first(stable_display.size(), QImage::Format_ARGB32);
  flicker_first.fill(Qt::black);
  stable_display.render(&flicker_first);
  QEventLoop flicker_wait;
  QTimer::singleShot(35, &flicker_wait, &QEventLoop::quit);
  flicker_wait.exec();
  QImage flicker_second(stable_display.size(), QImage::Format_ARGB32);
  flicker_second.fill(Qt::black);
  stable_display.render(&flicker_second);
  if (flicker_first == flicker_second) {
    std::cerr << "Refresh flicker did not modulate screen brightness.\n";
    return false;
  }
  const std::uint64_t flicker_first_energy = image_energy(flicker_first);
  const std::uint64_t flicker_second_energy = image_energy(flicker_second);
  const std::uint64_t flicker_delta =
      flicker_first_energy > flicker_second_energy
          ? flicker_first_energy - flicker_second_energy
          : flicker_second_energy - flicker_first_energy;
  if (flicker_delta * 100 <
      std::max(flicker_first_energy, flicker_second_energy)) {
    std::cerr << "Refresh flicker modulation was still imperceptibly small.\n";
    return false;
  }
  return true;
}

/** Checks that a latched MOTON signal cannot leave spindle audio looping. */
bool validate_floppy_audio_timeout() {
  using Device = p2000c::P2000cMachine::StorageDevice;
  using Operation = p2000c::P2000cMachine::StorageOperation;
  p2000c::HardwareAudio audio;
  audio.play_storage_activity(
      {Device::kFloppy, Operation::kMotorStart, 0, 0, 400});
  if (!audio.floppy_motor_active()) {
    std::cerr << "Floppy spindle audio did not start.\n";
    return false;
  }
  QEventLoop wait;
  QTimer::singleShot(p2000c::HardwareAudio::kFloppyIdleTimeoutMs + 100, &wait,
                     &QEventLoop::quit);
  wait.exec();
  if (audio.floppy_motor_active()) {
    std::cerr << "Floppy spindle audio did not stop after inactivity.\n";
    return false;
  }
  audio.play_storage_activity({Device::kFloppy, Operation::kRead, 0, 0, 120});
  if (!audio.floppy_motor_active()) {
    std::cerr << "Floppy activity did not restart an idle spindle.\n";
    return false;
  }
  audio.play_storage_activity(
      {Device::kFloppy, Operation::kMotorStop, 0, 0, 100});
  if (audio.floppy_motor_active()) {
    std::cerr << "Explicit floppy motor-off did not stop the spindle.\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 2) {
    return 2;
  }
  const QString test_root = QString::fromLocal8Bit(argv[1]);
  if (!QDir().mkpath(test_root)) {
    return 1;
  }
  QTemporaryDir scratch_directory(QDir(test_root).filePath("run-XXXXXX"));
  if (!scratch_directory.isValid()) {
    return 1;
  }
  const QByteArray scratch_path = QFile::encodeName(scratch_directory.path());
  qputenv("XDG_DATA_HOME", scratch_path);
  qputenv("XDG_CONFIG_HOME", scratch_path);
  qputenv("ALSOFT_DRIVERS", "null");
  QApplication application(argc, argv);
  QApplication::setApplicationName("P2000C Emulator UI Test");
  QApplication::setOrganizationName("P2000C Emulator Project");

  const QString media_directory =
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
          .filePath("media");
  if (!QDir().mkpath(media_directory)) {
    return 1;
  }
  const QString obsolete_system_path =
      QDir(media_directory).filePath("system_drive_a.flp");
  QFile obsolete_system(obsolete_system_path);
  const QByteArray obsolete_bytes(
      static_cast<qsizetype>(p2000c::RawDiskImage::kFloppySize), '\0');
  if (!obsolete_system.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      obsolete_system.write(obsolete_bytes) != obsolete_bytes.size()) {
    return 1;
  }
  obsolete_system.close();

  if (!validate_attribute_rendering() || !validate_graphics_rendering() ||
      !validate_crt_effect_rendering() || !validate_floppy_audio_timeout()) {
    return 1;
  }

  QSettings settings;
  settings.clear();
  const QColor saved_color(74, 122, 255);
  settings.setValue("display/baseColor", saved_color);
  settings.setValue("display/effects/scanlines", false);
  settings.setValue("display/effects/bloom", false);
  settings.setValue("display/effects/persistence", false);
  settings.setValue("display/effects/curvature", false);
  settings.setValue("display/effects/vignette", false);
  settings.setValue("display/effects/noise", true);
  settings.setValue("display/effects/flicker", true);
  settings.setValue("display/effects/persistenceHalfLifeMs", 95);
  settings.setValue("display/effects/brightnessPercent", 125);
  settings.setValue("audio/enabled", false);
  settings.setValue("audio/volume", 62);
  settings.setValue("machine/storageDelays", false);

  p2000c::MainWindow window;
  auto* display = window.findChild<p2000c::DisplayWidget*>();
  QAction* resolution = find_action(&window, "640 x 480");
  QAction* screen_color = find_action(&window, "Screen &Appearance...");
  QAction* screenshot = find_named_action(&window, "saveScreenshotAction");
  QAction* sound_volume =
      find_named_action(&window, "hardwareSoundVolumeAction");
  QAction* about = find_named_action(&window, "aboutAction");
  QAction* system_manual =
      find_named_action(&window, "systemReferenceManualAction");
  QAction* cpm_user_guide = find_named_action(&window, "cpmUserGuideAction");
  QAction* cpm_reference =
      find_named_action(&window, "cpmReferenceManualAction");
  QAction* hardware_delays =
      find_named_action(&window, "enableHardwareDelaysAction");
  if (display == nullptr || resolution == nullptr || screen_color == nullptr ||
      screenshot == nullptr || sound_volume == nullptr || about == nullptr ||
      system_manual == nullptr || cpm_user_guide == nullptr ||
      cpm_reference == nullptr || !system_manual->isEnabled() ||
      !cpm_user_guide->isEnabled() || !cpm_reference->isEnabled() ||
      !QFileInfo(system_manual->data().toString()).isFile() ||
      !QFileInfo(cpm_user_guide->data().toString()).isFile() ||
      !QFileInfo(cpm_reference->data().toString()).isFile() ||
      hardware_delays == nullptr ||
      window.windowIcon().isNull() ||
      hardware_delays->isChecked() ||
      screenshot->shortcut() != QKeySequence(Qt::CTRL | Qt::SHIFT |
                                             Qt::Key_S)) {
    return 1;
  }
  bool about_valid = false;
  QTimer::singleShot(0, [&about_valid]() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    auto* tabs = dialog != nullptr
                     ? dialog->findChild<QTabWidget*>("aboutTabs")
                     : nullptr;
    auto* overview = dialog != nullptr
                         ? dialog->findChild<QTextBrowser*>("aboutOverview")
                         : nullptr;
    auto* logo =
        dialog != nullptr ? dialog->findChild<QLabel*>("aboutLogo") : nullptr;
    auto* notices =
        dialog != nullptr
            ? dialog->findChild<QTextBrowser*>("aboutThirdPartyNotices")
            : nullptr;
    auto* license = dialog != nullptr
                        ? dialog->findChild<QTextBrowser*>("aboutLicenseText")
                        : nullptr;
    auto* third_party_licenses =
        dialog != nullptr
            ? dialog->findChild<QTextBrowser*>("aboutThirdPartyLicenses")
            : nullptr;
    auto* buttons =
        dialog != nullptr
            ? dialog->findChild<QDialogButtonBox*>("aboutButtons")
            : nullptr;
    about_valid =
        tabs != nullptr && tabs->count() == 4 && overview != nullptr &&
        logo != nullptr && !logo->pixmap().isNull() &&
        overview->toPlainText().contains(P2000C_VERSION) &&
        overview->toPlainText().contains("not affiliated") &&
        notices != nullptr &&
        notices->toPlainText().contains("Drive-panel illustrations") &&
        notices->toPlainText().contains("OpenAI image-generation") &&
        notices->toPlainText().contains("MAME") &&
        notices->toPlainText().contains("redistribution rights") &&
        third_party_licenses != nullptr &&
        third_party_licenses->toPlainText().contains("MIT License") &&
        third_party_licenses->toPlainText().contains("BSD-3-Clause") &&
        license != nullptr &&
        license->toPlainText().contains("GNU GENERAL PUBLIC LICENSE") &&
        buttons != nullptr;
    if (buttons != nullptr) {
      buttons->button(QDialogButtonBox::Close)->click();
    } else if (dialog != nullptr) {
      dialog->reject();
    }
  });
  about->trigger();
  if (!about_valid) {
    std::cerr << "About dialog omitted package or third-party disclosures.\n";
    return 1;
  }
  QTimer::singleShot(0, []() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    auto* slider = dialog != nullptr
                       ? dialog->findChild<QSlider*>(
                             "hardwareSoundVolumeSlider")
                       : nullptr;
    auto* buttons = dialog != nullptr
                        ? dialog->findChild<QDialogButtonBox*>(
                              "hardwareSoundVolumeButtons")
                        : nullptr;
    if (slider == nullptr || buttons == nullptr || slider->value() != 62) {
      if (dialog != nullptr) {
        dialog->reject();
      }
      return;
    }
    slider->setValue(37);
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  sound_volume->trigger();
  if (settings.value("audio/volume").toInt() != 37) {
    std::cerr << "Hardware sound volume was not persisted.\n";
    return 1;
  }
  hardware_delays->setChecked(true);
  if (!settings.value("machine/storageDelays").toBool()) {
    std::cerr << "Hardware-delay preference was not persisted.\n";
    return 1;
  }
  for (const QString& sample : {
           ":/audio/525_spin_start_loaded.wav",
           ":/audio/525_spin_loaded.wav", ":/audio/525_spin_end.wav",
           ":/audio/525_seek_6ms.wav", ":/audio/525_step_1_1.wav"}) {
    QFile file(sample);
    if (!file.open(QIODevice::ReadOnly) || file.read(4) != "RIFF") {
      std::cerr << "A bundled 5.25-inch drive sample is unavailable.\n";
      return 1;
    }
  }
  const p2000c::CrtEffects saved_effects = display->crt_effects();
  if (display->base_color() != saved_color || saved_effects.scanlines ||
      saved_effects.bloom || saved_effects.persistence ||
      saved_effects.curvature || saved_effects.vignette ||
      !saved_effects.noise || !saved_effects.flicker ||
      saved_effects.persistence_half_life_ms != 95 ||
      saved_effects.brightness_percent != 125) {
    std::cerr << "Saved screen appearance was not restored.\n";
    return 1;
  }

  const QImage captured = display->capture_screenshot();
  const QString screenshot_path =
      QDir(scratch_directory.path()).filePath("crt-screenshot.png");
  if (captured.isNull() || captured.size() != display->size() ||
      !captured.save(screenshot_path, "PNG") ||
      QImage(screenshot_path).size() != display->size()) {
    std::cerr << "CRT screenshot capture did not produce a valid PNG.\n";
    return 1;
  }

  QTimer::singleShot(0, []() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    auto* scanlines =
        dialog != nullptr
            ? dialog->findChild<QCheckBox*>("screenScanlinesCheckBox")
            : nullptr;
    auto* noise =
        dialog != nullptr
            ? dialog->findChild<QCheckBox*>("screenNoiseCheckBox")
            : nullptr;
    auto* flicker =
        dialog != nullptr
            ? dialog->findChild<QCheckBox*>("screenFlickerCheckBox")
            : nullptr;
    auto* persistence_half_life =
        dialog != nullptr
            ? dialog->findChild<QSlider*>("screenPersistenceHalfLifeSlider")
            : nullptr;
    auto* crt_brightness =
        dialog != nullptr
            ? dialog->findChild<QDial*>("screenCrtBrightnessDial")
            : nullptr;
    auto* buttons =
        dialog != nullptr
            ? dialog->findChild<QDialogButtonBox*>("screenColorButtons")
            : nullptr;
    if (buttons == nullptr || scanlines == nullptr || noise == nullptr ||
        flicker == nullptr || !flicker->isChecked() ||
        persistence_half_life == nullptr ||
        persistence_half_life->value() != 95 || crt_brightness == nullptr ||
        crt_brightness->value() != 125) {
      if (dialog != nullptr) {
        dialog->reject();
      }
      return;
    }
    buttons->button(QDialogButtonBox::RestoreDefaults)->click();
    buttons->button(QDialogButtonBox::Ok)->click();
  });
  screen_color->trigger();
  const QColor default_color = p2000c::DisplayWidget::default_base_color();
  const p2000c::CrtEffects default_effects =
      p2000c::DisplayWidget::default_crt_effects();
  if (display->base_color() != default_color ||
      display->crt_effects() != default_effects ||
      settings.value("display/baseColor").value<QColor>() != default_color ||
      settings.value("display/effects/scanlines").toBool() !=
          default_effects.scanlines ||
      settings.value("display/effects/noise").toBool() !=
          default_effects.noise ||
      settings.value("display/effects/flicker").toBool() !=
          default_effects.flicker ||
      settings.value("display/effects/persistenceHalfLifeMs").toInt() !=
          default_effects.persistence_half_life_ms ||
      settings.value("display/effects/brightnessPercent").toInt() !=
          default_effects.brightness_percent) {
    std::cerr << "Accepted screen appearance was not applied and persisted.\n";
    return 1;
  }

  p2000c::MainWindow restored_window;
  auto* restored_display = restored_window.findChild<p2000c::DisplayWidget*>();
  if (restored_display == nullptr ||
      restored_display->base_color() != default_color ||
      restored_display->crt_effects() != default_effects) {
    std::cerr << "Screen appearance did not survive a new window session.\n";
    return 1;
  }

  QTimer::singleShot(0, []() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    auto* slider = dialog != nullptr
                       ? dialog->findChild<QSlider*>("screenBrightnessSlider")
                       : nullptr;
    auto* scanlines =
        dialog != nullptr
            ? dialog->findChild<QCheckBox*>("screenScanlinesCheckBox")
            : nullptr;
    auto* persistence_half_life =
        dialog != nullptr
            ? dialog->findChild<QSlider*>("screenPersistenceHalfLifeSlider")
            : nullptr;
    auto* buttons =
        dialog != nullptr
            ? dialog->findChild<QDialogButtonBox*>("screenColorButtons")
            : nullptr;
    if (slider != nullptr) {
      slider->setValue(35);
    }
    if (scanlines != nullptr) {
      scanlines->setChecked(false);
    }
    if (persistence_half_life != nullptr) {
      persistence_half_life->setValue(
          p2000c::CrtEffects::kMaximumPersistenceHalfLifeMs);
    }
    if (buttons != nullptr) {
      buttons->button(QDialogButtonBox::Cancel)->click();
    } else if (dialog != nullptr) {
      dialog->reject();
    }
  });
  screen_color->trigger();
  if (display->base_color() != default_color ||
      display->crt_effects() != default_effects ||
      settings.value("display/baseColor").value<QColor>() != default_color ||
      settings.value("display/effects/scanlines").toBool() !=
          default_effects.scanlines ||
      settings.value("display/effects/persistenceHalfLifeMs").toInt() !=
          default_effects.persistence_half_life_ms) {
    std::cerr << "Cancel did not restore the prior screen appearance.\n";
    return 1;
  }

  QEventLoop boot_wait;
  QTimer::singleShot(500, &boot_wait, &QEventLoop::quit);
  boot_wait.exec();
  const auto& initial_screen = display->screen();
  const std::string initial_text(initial_screen.begin(), initial_screen.end());
  if (initial_text.find("IPL-1.1") == std::string::npos) {
    std::cerr << "Wall-clock scheduler did not execute the IPL.\n";
    return 1;
  }

  resolution->trigger();
  if (display->size() != QSize(640, 480)) {
    std::cerr << "Fixed display resolution was not applied.\n";
    return 1;
  }

  QAction* speed = find_action(&window, "8 MHz (200%)");
  if (speed == nullptr) {
    return 1;
  }
  speed->trigger();

  QAction* system = find_named_action(&window, "mountSystemFloppyAAction");
  if (system == nullptr) {
    return 1;
  }
  system->trigger();
  if (!window.statusBar()->currentMessage().contains("CP/M 2.2 template")) {
    std::cerr << "Bundled system floppy action did not mount media.\n";
    return 1;
  }

  auto* drive_a_current = window.findChild<QAction*>("currentFloppyAAction");
  const QString system_path =
      drive_a_current != nullptr ? drive_a_current->statusTip() : QString();
  if (system_path.isEmpty() ||
      !validate_image(system_path,
                      p2000c::RawDiskImage::Kind::kFloppy)) {
    return 1;
  }
  auto* drive_a_status = window.findChild<QLabel*>("floppyDriveAStatus");
  auto* drive_a_position =
      window.findChild<QWidget*>("floppyDriveAPosition");
  auto* drive_a_menu = window.findChild<QMenu*>("floppyDriveAMenu");
  auto* drive_panel = window.findChild<QWidget*>("driveActivityPanel");
  auto* drive_a_led = window.findChild<QWidget*>("floppyDriveAActivityLed");
  auto* drive_a_icon = window.findChild<QLabel*>("floppyDriveATypeIcon");
  auto* drive_a_card = window.findChild<QFrame*>("floppyDriveACard");
  QImage idle_led(drive_a_led != nullptr ? drive_a_led->size() : QSize(),
                  QImage::Format_ARGB32_Premultiplied);
  idle_led.fill(Qt::transparent);
  if (drive_a_led != nullptr) {
    drive_a_led->render(&idle_led);
  }
  QLayoutItem* drive_a_text_item =
      drive_a_card != nullptr && drive_a_card->layout() != nullptr
          ? drive_a_card->layout()->itemAt(2)
          : nullptr;
  const bool drive_a_widgets_valid =
      drive_a_status != nullptr && drive_a_position != nullptr &&
      drive_a_menu != nullptr && drive_a_current != nullptr &&
      drive_panel != nullptr && drive_a_led != nullptr &&
      drive_a_icon != nullptr && drive_a_card != nullptr &&
      drive_a_text_item != nullptr && drive_a_text_item->layout() != nullptr;
  const bool drive_a_structure_valid =
      drive_a_widgets_valid && !drive_a_icon->pixmap().isNull() &&
      drive_a_card->frameShape() == QFrame::NoFrame &&
      drive_a_card->property("driveCard").toBool() &&
      drive_a_icon->property("driveTypeIcon").toBool() &&
      drive_a_icon->size() == QSize(60, 60) &&
      drive_a_icon->pixmap().size() == QSize(58, 58) &&
      drive_a_icon->accessibleName().contains("hardware illustration") &&
      drive_a_status->property("mediaFilename").toBool() &&
      window.findChild<QWidget*>("floppyDriveASource") == nullptr;
  const bool drive_a_layout_valid =
      drive_a_widgets_valid &&
      std::abs(drive_a_led->geometry().center().y() -
               drive_a_icon->geometry().center().y()) <= 1 &&
      std::abs(drive_a_text_item->geometry().center().y() -
               drive_a_icon->geometry().center().y()) <= 1;
  const bool system_fixed_font_valid =
      drive_a_widgets_valid &&
      drive_a_status->font().family() ==
          QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
  const bool idle_led_highlight_valid =
      !idle_led.isNull() &&
      qGray(idle_led.pixel(9, 8)) > qGray(idle_led.pixel(12, 12));
  const bool idle_led_fill_valid =
      !idle_led.isNull() &&
      idle_led.pixelColor(12, 12) != idle_led.pixelColor(0, 0);
  const bool drive_a_style_valid =
      system_fixed_font_valid &&
      window.palette().color(QPalette::Window) == QColor("#e8dcb3") &&
      qApp->styleSheet().contains(
          "QFrame#driveActivityPanel {\n      background: #f5eac6;") &&
      idle_led_highlight_valid && idle_led_fill_valid;
  const bool drive_a_media_valid =
      drive_a_widgets_valid &&
      drive_a_status->text() == "system_drive_a.flp *" &&
      drive_a_position->accessibleName().contains("Track") &&
      drive_a_position->accessibleName().contains("side") &&
      drive_a_status->toolTip().contains("bundled template") &&
      drive_a_status->toolTip().contains(system_path) &&
      drive_a_menu->title().contains("system_drive_a.flp") &&
      drive_a_current->isChecked() && system->isChecked();
  if (!drive_a_structure_valid || !drive_a_layout_valid ||
      !drive_a_style_valid || !drive_a_media_valid) {
    std::cerr << "Drive A validation failed: widgets=" << drive_a_widgets_valid
              << ", structure=" << drive_a_structure_valid
              << ", layout=" << drive_a_layout_valid
              << ", style=" << drive_a_style_valid
              << ", media=" << drive_a_media_valid
              << ", system-fixed-font=" << system_fixed_font_valid
              << ", palette="
              << (window.palette().color(QPalette::Window) ==
                  QColor("#e8dcb3"))
              << ", stylesheet="
              << qApp->styleSheet().contains(
                     "QFrame#driveActivityPanel {\n      background: #f5eac6;")
              << ", led-highlight=" << idle_led_highlight_valid
              << ", led-fill=" << idle_led_fill_valid
              << ".\n";
    return 1;
  }
  drive_panel->grab().save(QDir(test_root).filePath("drive-panel-preview.png"));
  QFile system_master(":/images/system.flp");
  QFile system_session(system_path);
  if (!system_master.open(QIODevice::ReadOnly) ||
      !system_session.open(QIODevice::ReadOnly) ||
      system_master.readAll() != system_session.readAll()) {
    std::cerr << "Bundled system template was not copied exactly.\n";
    return 1;
  }
  system_session.close();
  if (!system_session.open(QIODevice::ReadWrite) ||
      !system_session.seek(1234) || system_session.write("X", 1) != 1) {
    return 1;
  }
  system_session.close();
  system->trigger();
  system_master.seek(0);
  if (!system_session.open(QIODevice::ReadOnly) ||
      system_session.readAll() != system_master.readAll()) {
    std::cerr << "Reopening a bundled template did not discard changes.\n";
    return 1;
  }
  QFile preserved_obsolete(obsolete_system_path);
  if (!preserved_obsolete.open(QIODevice::ReadOnly) ||
      preserved_obsolete.readAll() != obsolete_bytes) {
    std::cerr << "Obsolete writable media was overwritten during migration.\n";
    return 1;
  }

  QAction* zork = find_named_action(&window, "mountZorkFloppyBAction");
  QAction* chess = find_named_action(&window, "mountChessFloppyBAction");
  QAction* ipldump =
      find_named_action(&window, "mountIplDumpFloppyBAction");
  if (zork == nullptr || chess == nullptr || ipldump == nullptr) {
    return 1;
  }
  zork->trigger();
  auto* drive_b_current = window.findChild<QAction*>("currentFloppyBAction");
  const QString zork_path =
      drive_b_current != nullptr ? drive_b_current->statusTip() : QString();
  if (zork_path.isEmpty() ||
      !validate_image(zork_path,
                      p2000c::RawDiskImage::Kind::kFloppy) ||
      !zork->isChecked()) {
    std::cerr << "Bundled ZORK floppy did not mount as raw media.\n";
    return 1;
  }
  chess->trigger();
  const QString chess_path = drive_b_current->statusTip();
  if (chess_path.isEmpty() ||
      !validate_image(chess_path,
                      p2000c::RawDiskImage::Kind::kFloppy) ||
      !chess->isChecked()) {
    std::cerr << "Bundled CHESS floppy did not mount as raw media.\n";
    return 1;
  }
  ipldump->trigger();
  const QString ipldump_path = drive_b_current->statusTip();
  if (ipldump_path.isEmpty() ||
      !validate_image(ipldump_path,
                      p2000c::RawDiskImage::Kind::kFloppy) ||
      !ipldump->isChecked()) {
    std::cerr << "Bundled IPL dump toolchain did not mount as raw media.\n";
    return 1;
  }

  QAction* blank = find_named_action(&window, "mountBlankFloppyBAction");
  if (blank == nullptr) {
    return 1;
  }
  blank->trigger();
  if (!window.statusBar()->currentMessage().contains("Blank 640 KiB")) {
    std::cerr << "Bundled blank floppy action did not mount media.\n";
    return 1;
  }
  auto* drive_b_status = window.findChild<QLabel*>("floppyDriveBStatus");
  if (drive_b_status == nullptr || drive_b_current == nullptr ||
      drive_b_status->text() != "blank_drive_b.flp *" ||
      !drive_b_current->isChecked() || !blank->isChecked()) {
    std::cerr << "Drive B media indicators did not show the mounted image.\n";
    return 1;
  }
  if (!validate_image(drive_b_current->statusTip(),
                      p2000c::RawDiskImage::Kind::kFloppy)) {
    return 1;
  }
  for (int drive = 1; drive <= 2; ++drive) {
    auto* status =
        window.findChild<QLabel*>(QString("hardDisk%1Status").arg(drive));
    auto* position =
        window.findChild<QWidget*>(QString("hardDisk%1Position").arg(drive));
    auto* current = window.findChild<QAction*>(
        QString("currentHardDisk%1Action").arg(drive));
    auto* bundled = window.findChild<QAction*>(
        QString("mountDefaultHardDisk%1Action").arg(drive));
    const QString filename = QString("hard_disk_%1.hda").arg(drive);
    if (status == nullptr || position == nullptr ||
        current == nullptr || bundled == nullptr ||
        status->text() != filename + " *" || !current->isChecked() ||
        window.findChild<QWidget*>(QString("hardDisk%1Source").arg(drive)) !=
            nullptr ||
        !position->accessibleName().startsWith("SASI block ") ||
        !bundled->isChecked() ||
        !validate_image(current->statusTip(),
                        p2000c::RawDiskImage::Kind::kHardDisk)) {
      std::cerr << "Default SASI hard-disk working image was not mounted.\n";
      return 1;
    }
  }

  auto* save_floppy =
      window.findChild<QAction*>("saveFloppyBAsAction");
  auto* save_hard_disk =
      window.findChild<QAction*>("saveHardDisk1AsAction");
  auto* recent_menu = window.findChild<QMenu*>("recentFloppyBMenu");
  auto* hard_led = window.findChild<QWidget*>("hardDisk1ActivityLed");
  auto* hard_icon = window.findChild<QLabel*>("hardDisk1TypeIcon");
  if (save_floppy == nullptr || save_hard_disk == nullptr ||
      recent_menu == nullptr || hard_led == nullptr || hard_icon == nullptr ||
      hard_icon->pixmap().isNull() ||
      !save_floppy->isEnabled() || !save_hard_disk->isEnabled()) {
    std::cerr << "Media retention or side-panel controls are missing.\n";
    return 1;
  }
  QStringList recent_paths;
  for (int index = 0; index < 6; ++index) {
    const QString path =
        QDir(scratch_directory.path())
            .filePath(QString("recent-%1.flp").arg(index));
    QFile::remove(path);
    if (!QFile::copy(":/images/system.flp", path) ||
        !window.mount_floppy(filesystem_path(path), 1)) {
      return 1;
    }
    recent_paths.append(QFileInfo(path).absoluteFilePath());
  }
  const QStringList stored_recent =
      settings.value("media/recent/floppy1").toStringList();
  if (stored_recent.size() != 5 || stored_recent.front() != recent_paths.back() ||
      stored_recent.contains(recent_paths.front()) ||
      recent_menu->actions().size() != 5 ||
      drive_b_status->text().endsWith(" *")) {
    std::cerr << "Per-drive recent images were not limited to the latest five.\n";
    return 1;
  }
  return 0;
}
