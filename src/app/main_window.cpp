#include "app/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <algorithm>
#include <array>
#include <optional>
#include <string>

#include "app/display_widget.h"
#include "app/screen_color_dialog.h"

namespace p2000c {
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
    {"560 x 288 (Compact)", {560, 288}},
    {"840 x 432", {840, 432}},
    {"1120 x 576 (Recommended)", {1120, 576}},
    {"1400 x 720", {1400, 720}},
    {"1680 x 864", {1680, 864}},
}};

constexpr QSize kDefaultDisplayResolution(1120, 576);

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

/** Returns the expected writable path for one bundled image. */
QString bundled_media_path(const QString& filename) {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath("media/" + filename);
}

/** Returns the per-user writable directory for bundled media copies. */
std::optional<QString> writable_media_directory(QString* error) {
  const QString application_data =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (application_data.isEmpty()) {
    *error = "No writable application-data directory is available.";
    return std::nullopt;
  }
  const QString media_directory = QDir(application_data).filePath("media");
  if (!QDir().mkpath(media_directory)) {
    *error = "Could not create the bundled-media working directory.";
    return std::nullopt;
  }
  return media_directory;
}

/** Copies a resource to a persistent writable working file when absent. */
std::optional<QString> writable_resource_copy(const QString& resource,
                                              const QString& filename,
                                              QString* error) {
  const std::optional<QString> directory = writable_media_directory(error);
  if (!directory.has_value()) {
    return std::nullopt;
  }
  const QString destination = QDir(*directory).filePath(filename);
  if (QFileInfo::exists(destination)) {
    return destination;
  }

  QFile input(resource);
  if (!input.open(QIODevice::ReadOnly)) {
    *error = "Could not open bundled media resource " + resource + ".";
    return std::nullopt;
  }
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) || output.write(input.readAll()) < 0 ||
      !output.commit()) {
    *error = "Could not create writable bundled media " + destination + ".";
    return std::nullopt;
  }
  return destination;
}

/** Copies immutable bundled media into a directory keyed by its contents. */
std::optional<QString> writable_versioned_resource_copy(
    const QString& resource, const QString& filename, QString* error) {
  QFile input(resource);
  if (!input.open(QIODevice::ReadOnly)) {
    *error = "Could not open bundled media resource " + resource + ".";
    return std::nullopt;
  }
  const QByteArray image = input.readAll();
  const QString version = QString::fromLatin1(
      QCryptographicHash::hash(image, QCryptographicHash::Sha256)
          .toHex()
          .left(16));
  const std::optional<QString> media_directory =
      writable_media_directory(error);
  if (!media_directory.has_value()) {
    return std::nullopt;
  }
  const QString directory =
      QDir(*media_directory).filePath("bundled/" + version);
  if (!QDir().mkpath(directory)) {
    *error = "Could not create the versioned bundled-media directory.";
    return std::nullopt;
  }
  const QString destination = QDir(directory).filePath(filename);
  if (QFileInfo::exists(destination)) {
    return destination;
  }

  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(image) != image.size() || !output.commit()) {
    *error = "Could not create writable bundled media " + destination + ".";
    return std::nullopt;
  }
  return destination;
}

/** Creates an unformatted 80x2x16x256 raw floppy. */
std::optional<QString> writable_blank_floppy(const QString& filename,
                                             QString* error) {
  const std::optional<QString> directory = writable_media_directory(error);
  if (!directory.has_value()) {
    return std::nullopt;
  }
  const QString destination = QDir(*directory).filePath(filename);
  if (QFileInfo::exists(destination)) {
    return destination;
  }

  const QByteArray image(static_cast<qsizetype>(RawDiskImage::kFloppySize),
                         static_cast<char>(0xe5));

  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) ||
      output.write(image) != image.size() || !output.commit()) {
    *error = "Could not create blank bundled media " + destination + ".";
    return std::nullopt;
  }
  return destination;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("Philips P2000C Emulator");
  display_ = new DisplayWidget(this);
  setCentralWidget(display_);
  display_->set_key_handler(
      [this](std::uint8_t value) { machine_.queue_key(value); });
  statusBar();
  create_menus();
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
  refresh_media_indicators();
  return true;
}

void MainWindow::create_menus() {
  QMenu* machine_menu = menuBar()->addMenu("&Machine");
  QAction* load_rom = machine_menu->addAction("Load &IPL ROM...");
  connect(load_rom, &QAction::triggered, this, &MainWindow::load_ipl_rom);
  QAction* reset = machine_menu->addAction("&Reset");
  connect(reset, &QAction::triggered, this, [this]() {
    machine_.reset();
    pending_t_states_ = 0.0;
    execution_timer_.restart();
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

    auto* status = new QLabel(this);
    status->setObjectName(QString("floppyDrive%1Status").arg(drive_letter));
    status->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    status->setMargin(4);
    status->setMinimumWidth(110);
    status->setMaximumWidth(240);
    status->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(status, 1);
    media_status_labels_[drive] = status;
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
    QAction* bundled = drive_menu->addAction("Use &Default Blank HDA");
    bundled->setObjectName(
        QString("mountDefaultHardDisk%1Action").arg(drive + 1));
    bundled->setCheckable(true);
    bundled_hard_disk_actions_[drive] = bundled;
    connect(bundled, &QAction::triggered, this,
            [this, drive]() { mount_bundled_hard_disk(drive); });

    auto* status = new QLabel(this);
    status->setObjectName(QString("hardDisk%1Status").arg(drive + 1));
    status->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    status->setMargin(4);
    status->setMinimumWidth(90);
    status->setMaximumWidth(210);
    status->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(status, 1);
    hard_disk_status_labels_[drive] = status;
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

  QMenu* settings_menu = menuBar()->addMenu("&Settings");
  QAction* screen_color = settings_menu->addAction("Screen &Appearance...");
  connect(screen_color, &QAction::triggered, this,
          &MainWindow::open_screen_color_settings);
  const QColor saved_color =
      QSettings()
          .value("display/baseColor", DisplayWidget::default_base_color())
          .value<QColor>();
  display_->set_base_color(saved_color.isValid()
                               ? saved_color
                               : DisplayWidget::default_base_color());
  display_->set_crt_effects(load_crt_effects(QSettings()));
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
      bundled_system_actions_[drive]->setChecked(false);
      bundled_zork_actions_[drive]->setChecked(false);
      bundled_chess_actions_[drive]->setChecked(false);
      bundled_ipldump_actions_[drive]->setChecked(false);
      bundled_blank_actions_[drive]->setChecked(false);
      continue;
    }

    const QString filename = compact_filename(image->path());
    const QString full_path = qstring_path(image->path());
    const QString tooltip = QString("Drive %1 — writable raw FLP image\n%2")
                                .arg(drive_letter)
                                .arg(full_path);
    media_drive_menus_[drive]->setTitle(
        QString("Drive &%1 — %2").arg(drive_letter).arg(menu_safe(filename)));
    current_media_actions_[drive]->setText("Mounted: " + menu_safe(filename));
    current_media_actions_[drive]->setChecked(true);
    current_media_actions_[drive]->setToolTip(tooltip);
    current_media_actions_[drive]->setStatusTip(full_path);
    media_status_labels_[drive]->setText(
        QString("Drive %1: %2").arg(drive_letter).arg(filename));
    media_status_labels_[drive]->setToolTip(tooltip);

    const QString suffix = drive == 0 ? "a" : "b";
    const QString& system_path = bundled_system_paths_[drive];
    const QString& zork_path = bundled_zork_paths_[drive];
    const QString& chess_path = bundled_chess_paths_[drive];
    const QString& ipldump_path = bundled_ipldump_paths_[drive];
    const QString blank_path =
        bundled_media_path("blank_drive_" + suffix + ".flp");
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
      bundled_hard_disk_actions_[drive]->setChecked(false);
      continue;
    }

    const QString filename = compact_filename(image->path());
    const QString full_path = qstring_path(image->path());
    const QString tooltip =
        QString("CP/M volumes %1 — writable 10 MiB raw HDA image\n%2")
            .arg(volumes)
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
    hard_disk_status_labels_[drive]->setText(volumes + ": " + filename);
    hard_disk_status_labels_[drive]->setToolTip(tooltip);
    const QString bundled_path =
        bundled_media_path(QString("hard_disk_%1.hda").arg(drive + 1));
    bundled_hard_disk_actions_[drive]->setChecked(
        QFileInfo(full_path).absoluteFilePath() ==
        QFileInfo(bundled_path).absoluteFilePath());
  }
}

void MainWindow::mount_bundled_system_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = writable_versioned_resource_copy(
      ":/images/system.flp",
      QString("system_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare bundled floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    bundled_system_paths_[drive] = *path;
    refresh_media_indicators();
    if (drive == 0) {
      machine_.reset();
      pending_t_states_ = 0.0;
      execution_timer_.restart();
      refresh_screen();
    }
    statusBar()->showMessage(
        QString(
            "Bundled CP/M 2.2 system floppy mounted in drive %1 from writable "
            "working copy.")
            .arg(drive_letter));
  }
}

void MainWindow::mount_bundled_zork_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = writable_versioned_resource_copy(
      ":/images/zork.flp",
      QString("zork_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare ZORK floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    bundled_zork_paths_[drive] = *path;
    refresh_media_indicators();
    statusBar()->showMessage(
        QString("ZORK data floppy mounted in drive %1.").arg(drive_letter));
  }
}

void MainWindow::mount_bundled_chess_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = writable_versioned_resource_copy(
      ":/images/chess.flp",
      QString("chess_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare CHESS floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    bundled_chess_paths_[drive] = *path;
    refresh_media_indicators();
    statusBar()->showMessage(
        QString("CHESS data floppy mounted in drive %1.").arg(drive_letter));
  }
}

void MainWindow::mount_bundled_ipldump_floppy(std::size_t drive) {
  QString error;
  const QChar drive_letter = drive == 0 ? 'A' : 'B';
  const std::optional<QString> path = writable_versioned_resource_copy(
      ":/images/ipldump.flp",
      QString("ipldump_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare IPL dump floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
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
  const std::optional<QString> path = writable_blank_floppy(
      QString("blank_drive_%1.flp").arg(drive_letter.toLower()), &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare blank floppy", error);
    refresh_media_indicators();
    return;
  }
  if (mount_floppy(filesystem_path(*path), drive)) {
    statusBar()->showMessage(
        QString("Blank 640 KiB floppy mounted in drive %1 from persistent "
                "working copy.")
            .arg(drive_letter));
  }
}

void MainWindow::mount_bundled_hard_disk(std::size_t drive) {
  QString error;
  const std::optional<QString> path = writable_resource_copy(
      ":/images/blank.hda", QString("hard_disk_%1.hda").arg(drive + 1),
      &error);
  if (!path.has_value()) {
    QMessageBox::critical(this, "Cannot prepare default hard disk", error);
    refresh_media_indicators();
    return;
  }
  mount_hard_disk(filesystem_path(*path), drive);
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
