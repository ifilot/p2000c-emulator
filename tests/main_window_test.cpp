#include "app/main_window.h"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QImage>
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
#include "core/imd_image.h"

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

/** Converts a QString to a host filesystem path for ImageDisk validation. */
std::filesystem::path filesystem_path(const QString& path) {
  const QByteArray utf8 = path.toUtf8();
  const auto* begin = reinterpret_cast<const char8_t*>(utf8.constData());
  return std::filesystem::path(std::u8string(begin, begin + utf8.size()));
}

/** Opens an expected working image and checks its physical geometry. */
bool validate_image(const QString& path) {
  std::string error;
  const std::optional<p2000c::ImdImage> image =
      p2000c::ImdImage::open(filesystem_path(path), &error);
  if (!image.has_value() || image->tracks().size() != 160) {
    std::cerr << "Invalid bundled working image: " << error << '\n';
    return false;
  }
  return true;
}

/** Checks inverse cells and the four documented intensity levels in pixels. */
bool validate_attribute_rendering() {
  p2000c::DisplayWidget display;
  display.setFixedSize(560, 288);
  display.set_cursor(0, 0, false);

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
    for (int y = center_y - 3; y <= center_y + 3; ++y) {
      for (int x = center_x - 3; x <= center_x + 3; ++x) {
        energy += qGreen(image.pixel(x, y));
      }
    }
    return energy;
  };
  auto render = [](const p2000c::Terminal& terminal) {
    p2000c::DisplayWidget display;
    display.setFixedSize(560, 288);
    display.set_cursor(0, 0, false);
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

  if (!validate_attribute_rendering() || !validate_graphics_rendering()) {
    return 1;
  }

  QSettings settings;
  settings.clear();
  const QColor saved_color(74, 122, 255);
  settings.setValue("display/baseColor", saved_color);

  p2000c::MainWindow window;
  auto* display = window.findChild<p2000c::DisplayWidget*>();
  QAction* resolution = find_action(&window, "840 x 432");
  QAction* screen_color = find_action(&window, "Screen &Color...");
  if (display == nullptr || resolution == nullptr || screen_color == nullptr) {
    return 1;
  }
  if (display->base_color() != saved_color) {
    std::cerr << "Saved screen color was not restored.\n";
    return 1;
  }

  QTimer::singleShot(0, []() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    auto* buttons =
        dialog != nullptr
            ? dialog->findChild<QDialogButtonBox*>("screenColorButtons")
            : nullptr;
    if (buttons == nullptr) {
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
  if (display->base_color() != default_color ||
      settings.value("display/baseColor").value<QColor>() != default_color) {
    std::cerr << "Accepted screen color was not applied and persisted.\n";
    return 1;
  }

  p2000c::MainWindow restored_window;
  auto* restored_display = restored_window.findChild<p2000c::DisplayWidget*>();
  if (restored_display == nullptr ||
      restored_display->base_color() != default_color) {
    std::cerr << "Screen color did not survive a new window session.\n";
    return 1;
  }

  QTimer::singleShot(0, []() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    auto* slider = dialog != nullptr
                       ? dialog->findChild<QSlider*>("screenBrightnessSlider")
                       : nullptr;
    auto* buttons =
        dialog != nullptr
            ? dialog->findChild<QDialogButtonBox*>("screenColorButtons")
            : nullptr;
    if (slider != nullptr) {
      slider->setValue(35);
    }
    if (buttons != nullptr) {
      buttons->button(QDialogButtonBox::Cancel)->click();
    } else if (dialog != nullptr) {
      dialog->reject();
    }
  });
  screen_color->trigger();
  if (display->base_color() != default_color ||
      settings.value("display/baseColor").value<QColor>() != default_color) {
    std::cerr << "Cancel did not restore the prior screen color.\n";
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

  const QString media_directory =
      QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
          .filePath("media");
  if (!validate_image(QDir(media_directory).filePath("p2kc_sys_drive_a.imd"))) {
    return 1;
  }
  auto* drive_a_status = window.findChild<QLabel*>("floppyDriveAStatus");
  auto* drive_a_menu = window.findChild<QMenu*>("floppyDriveAMenu");
  auto* drive_a_current = window.findChild<QAction*>("currentFloppyAAction");
  if (drive_a_status == nullptr || drive_a_menu == nullptr ||
      drive_a_current == nullptr ||
      !drive_a_status->text().contains("p2kc_sys_drive_a.imd") ||
      !drive_a_menu->title().contains("p2kc_sys_drive_a.imd") ||
      !drive_a_current->isChecked() || !system->isChecked()) {
    std::cerr << "Drive A media indicators did not show the mounted image.\n";
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
      !drive_b_status->text().contains("blank_640k_drive_b.imd") ||
      !drive_b_current->isChecked() || !blank->isChecked()) {
    std::cerr << "Drive B media indicators did not show the mounted image.\n";
    return 1;
  }
  return validate_image(
             QDir(media_directory).filePath("blank_640k_drive_b.imd"))
             ? 0
             : 1;
}
