#include "app/main_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <algorithm>
#include <array>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>

#include "app/display_widget.h"
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

/** Finds the writable copy whose bytes match the current bundled master. */
QString current_bundled_copy(const QString& media_directory,
                             const QString& filename,
                             const QString& resource) {
  QFile master(resource);
  if (!master.open(QIODevice::ReadOnly)) {
    return {};
  }
  const QByteArray expected = master.readAll();
  QDirIterator candidates(QDir(media_directory).filePath("bundled"),
                          QStringList{filename}, QDir::Files,
                          QDirIterator::Subdirectories);
  while (candidates.hasNext()) {
    QFile candidate(candidates.next());
    if (candidate.open(QIODevice::ReadOnly) && candidate.readAll() == expected) {
      return candidate.fileName();
    }
  }
  return {};
}

/** Checks inverse cells and the four documented intensity levels in pixels. */
bool validate_attribute_rendering() {
  p2000c::DisplayWidget display;
  display.setFixedSize(560, 288);
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
    const int left = column * 7;
    for (int y = 0; y < 12; ++y) {
      for (int x = left; x < left + 7; ++x) {
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
    display.setFixedSize(560, 288);
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
  const int medium_pixel_energy = area_energy(medium_image, 57, 269);
  const int medium_blank_energy = area_energy(medium_image, 72, 269);
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
  const int high_pixel_energy = area_energy(high_image, 503, 19);
  const int high_blank_energy = area_energy(high_image, 488, 19);
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
    display.setFixedSize(1120, 576);
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
  const std::uint64_t plain_energy = image_energy(render(plain));
  const QImage scanline_image = render(scanlines);
  const std::uint64_t scanline_energy = image_energy(scanline_image);
  if (scanline_energy * 100 >= plain_energy * 94) {
    std::cerr << "Scanline separation did not materially shape the raster: "
              << scanline_energy << " vs " << plain_energy << ".\n";
    return false;
  }

  p2000c::DisplayWidget persistent_display;
  persistent_display.setFixedSize(560, 288);
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
    for (int y = 120; y < 132; ++y) {
      for (int x = 280; x < 287; ++x) {
        energy += qGreen(image.pixel(x, y));
      }
    }
    return energy;
  };
  if (cell_energy(afterglow) <= cell_energy(cleared) * 2) {
    std::cerr << "Phosphor persistence did not retain extinguished pixels.\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 2) {
    return 2;
  }
  qputenv("XDG_DATA_HOME", argv[1]);
  qputenv("XDG_CONFIG_HOME", argv[1]);
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
      !validate_crt_effect_rendering()) {
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
  settings.setValue("display/effects/persistenceHalfLifeMs", 95);

  p2000c::MainWindow window;
  auto* display = window.findChild<p2000c::DisplayWidget*>();
  QAction* resolution = find_action(&window, "840 x 432");
  QAction* screen_color = find_action(&window, "Screen &Appearance...");
  QAction* screenshot = find_named_action(&window, "saveScreenshotAction");
  if (display == nullptr || resolution == nullptr || screen_color == nullptr ||
      screenshot == nullptr ||
      screenshot->shortcut() != QKeySequence(Qt::CTRL | Qt::SHIFT |
                                             Qt::Key_S)) {
    return 1;
  }
  const p2000c::CrtEffects saved_effects = display->crt_effects();
  if (display->base_color() != saved_color || saved_effects.scanlines ||
      saved_effects.bloom || saved_effects.persistence ||
      saved_effects.curvature || saved_effects.vignette ||
      !saved_effects.noise || saved_effects.persistence_half_life_ms != 95) {
    std::cerr << "Saved screen appearance was not restored.\n";
    return 1;
  }

  const QImage captured = display->capture_screenshot();
  const QString screenshot_path = QDir(argv[1]).filePath("crt-screenshot.png");
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
    auto* persistence_half_life =
        dialog != nullptr
            ? dialog->findChild<QSlider*>("screenPersistenceHalfLifeSlider")
            : nullptr;
    auto* buttons =
        dialog != nullptr
            ? dialog->findChild<QDialogButtonBox*>("screenColorButtons")
            : nullptr;
    if (buttons == nullptr || scanlines == nullptr || noise == nullptr ||
        persistence_half_life == nullptr ||
        persistence_half_life->value() != 95) {
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
      settings.value("display/effects/persistenceHalfLifeMs").toInt() !=
          default_effects.persistence_half_life_ms) {
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
  if (display->size() != QSize(840, 432)) {
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
  if (!window.statusBar()->currentMessage().contains("system floppy")) {
    std::cerr << "Bundled system floppy action did not mount media.\n";
    return 1;
  }

  const QString system_path = current_bundled_copy(
      media_directory, "system_drive_a.flp", ":/images/system.flp");
  if (system_path.isEmpty() ||
      !validate_image(system_path,
                      p2000c::RawDiskImage::Kind::kFloppy)) {
    return 1;
  }
  auto* drive_a_status = window.findChild<QLabel*>("floppyDriveAStatus");
  auto* drive_a_menu = window.findChild<QMenu*>("floppyDriveAMenu");
  auto* drive_a_current = window.findChild<QAction*>("currentFloppyAAction");
  if (drive_a_status == nullptr || drive_a_menu == nullptr ||
      drive_a_current == nullptr ||
      !drive_a_status->text().contains("system_drive_a.flp") ||
      !drive_a_status->toolTip().contains(system_path) ||
      !drive_a_menu->title().contains("system_drive_a.flp") ||
      !drive_a_current->isChecked() || !system->isChecked()) {
    std::cerr << "Drive A media indicators did not show the mounted image.\n";
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
  const QString zork_path = current_bundled_copy(
      media_directory, "zork_drive_b.flp", ":/images/zork.flp");
  if (zork_path.isEmpty() ||
      !validate_image(zork_path,
                      p2000c::RawDiskImage::Kind::kFloppy) ||
      !zork->isChecked()) {
    std::cerr << "Bundled ZORK floppy did not mount as raw media.\n";
    return 1;
  }
  chess->trigger();
  const QString chess_path = current_bundled_copy(
      media_directory, "chess_drive_b.flp", ":/images/chess.flp");
  if (chess_path.isEmpty() ||
      !validate_image(chess_path,
                      p2000c::RawDiskImage::Kind::kFloppy) ||
      !chess->isChecked()) {
    std::cerr << "Bundled CHESS floppy did not mount as raw media.\n";
    return 1;
  }
  ipldump->trigger();
  const QString ipldump_path = current_bundled_copy(
      media_directory, "ipldump_drive_b.flp", ":/images/ipldump.flp");
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
  auto* drive_b_current = window.findChild<QAction*>("currentFloppyBAction");
  if (drive_b_status == nullptr || drive_b_current == nullptr ||
      !drive_b_status->text().contains("blank_drive_b.flp") ||
      !drive_b_current->isChecked() || !blank->isChecked()) {
    std::cerr << "Drive B media indicators did not show the mounted image.\n";
    return 1;
  }
  if (!validate_image(QDir(media_directory).filePath("blank_drive_b.flp"),
                      p2000c::RawDiskImage::Kind::kFloppy)) {
    return 1;
  }
  for (int drive = 1; drive <= 2; ++drive) {
    auto* status =
        window.findChild<QLabel*>(QString("hardDisk%1Status").arg(drive));
    auto* current = window.findChild<QAction*>(
        QString("currentHardDisk%1Action").arg(drive));
    auto* bundled = window.findChild<QAction*>(
        QString("mountDefaultHardDisk%1Action").arg(drive));
    const QString filename = QString("hard_disk_%1.hda").arg(drive);
    if (status == nullptr || current == nullptr || bundled == nullptr ||
        !status->text().contains(filename) || !current->isChecked() ||
        !bundled->isChecked() ||
        !validate_image(QDir(media_directory).filePath(filename),
                        p2000c::RawDiskImage::Kind::kHardDisk)) {
      std::cerr << "Default SASI hard-disk working image was not mounted.\n";
      return 1;
    }
  }
  return 0;
}
