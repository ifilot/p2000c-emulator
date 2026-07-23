#ifndef P2000C_APP_MAIN_WINDOW_H_
#define P2000C_APP_MAIN_WINDOW_H_

#include <QElapsedTimer>
#include <QMainWindow>
#include <QSize>
#include <QString>
#include <QTemporaryDir>
#include <array>
#include <cstddef>
#include <filesystem>

#include "app/fast_boot_controller.h"
#include "core/p2000c_machine.h"

class QAction;
class QLabel;
class QMenu;
class QTimer;
class QVBoxLayout;

namespace p2000c {

class DisplayWidget;
class DriveLed;
class DrivePositionDisplay;
class HardwareAudio;
class MemoryMapDisplay;

/** Main window for the minimal Qt P2000C emulator shell. */
class MainWindow : public QMainWindow {
  private:
    static constexpr double kBaseClockHz = 4'000'000.0;
    static constexpr qint64 kMaximumCatchUpNanoseconds = 100'000'000;
    static constexpr std::size_t kMsDosApplicationImageCount = 2;
    static constexpr qint64 kFastBootStageTimeoutMs = 60'000;

  public:
    /** Creates the emulator window, menus, display, and paced run timer. */
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /** Mounts a floppy image in drive A or B and refreshes machine status. */
    bool mount_floppy(const std::filesystem::path& path, std::size_t drive = 0);

    /** Mounts physical SASI disk 1 or 2 and refreshes machine status. */
    bool mount_hard_disk(const std::filesystem::path& path, std::size_t drive);

  private:
    /** Creates machine and media actions. */
    void create_menus();

    /** Updates the prototype status shown on the emulated display. */
    void refresh_screen();

    /** Prompts for and loads mainboard IPL firmware. */
    void load_ipl_rom();

    /** Prompts for and mounts a writable raw FLP image in one drive. */
    void open_floppy(std::size_t drive);

    /** Prompts for and mounts a writable raw HDA image. */
    void open_hard_disk(std::size_t drive);

    /** Saves the currently mounted floppy and switches to that persistent file. */
    void save_floppy_as(std::size_t drive);

    /** Saves the currently mounted hard disk and switches to that persistent file. */
    void save_hard_disk_as(std::size_t drive);

    /** Records an external image and rebuilds its drive's five recent entries. */
    void remember_recent_image(const QString& path, bool hard_disk,
                               std::size_t drive);

    /** Rebuilds all per-drive recent-image submenus from persistent settings. */
    void refresh_recent_image_menus();

    /** Refreshes the Media menu and persistent drive-status indicators. */
    void refresh_media_indicators();

    /** Refreshes one live floppy-track or hard-disk-block readout. */
    void refresh_drive_position(bool hard_disk, std::size_t drive);

    /** Refreshes the live 64 KiB RAM-page visualization. */
    void refresh_memory_panel();

    /** Mounts a disposable copy of the pristine CP/M system template. */
    void mount_bundled_system_floppy(std::size_t drive);

    /** Mounts the P2093 CoPower CP/M boot disk used to launch MSBOOT. */
    bool mount_bundled_copower_cpm_floppy(std::size_t drive);

    /** Mounts the P2093 CoPower MS-DOS 2.11 disk after the MSBOOT prompt. */
    bool mount_bundled_copower_dos_floppy(std::size_t drive);

    /** Mounts one bundled P2093 CoPower MS-DOS application disk. */
    void mount_bundled_msdos_application(std::size_t drive,
                                         std::size_t application);

    /** Installs P2093 CoPower and ejects incompatible blank hard disks. */
    bool prepare_for_copower_floppy();

    /** Starts the documented CP/M-to-MS-DOS P2093 boot sequence. */
    void start_fast_boot_msdos();

    /** Advances the one-click P2093 boot sequence when a prompt appears. */
    void advance_fast_boot_msdos();

    /** Ends the one-click boot sequence and restores its menu action. */
    void finish_fast_boot_msdos(const QString& status);

    /** Mounts a disposable copy of the pristine ZORK template. */
    void mount_bundled_zork_floppy(std::size_t drive);

    /** Mounts a disposable copy of the pristine CHESS template. */
    void mount_bundled_chess_floppy(std::size_t drive);

    /** Mounts the bundled ASM/LOAD/IPLDUMP development floppy. */
    void mount_bundled_ipldump_floppy(std::size_t drive);

    /** Mounts the bundled compiled P2FILE development floppy. */
    void mount_bundled_p2file_floppy(std::size_t drive);

    /** Mounts the bundled compiled P2EDIT development floppy. */
    void mount_bundled_p2edit_floppy(std::size_t drive);

    /** Mounts a disposable blank 640 KiB data floppy. */
    void mount_bundled_blank_floppy(std::size_t drive);

    /** Mounts a disposable copy of the bundled blank HDA template. */
    void mount_bundled_hard_disk(std::size_t drive);

    /** Installs the documented A/B plus C-F startup media arrangement. */
    void mount_default_media();

    /** Applies and remembers one fixed display presentation size. */
    void set_display_resolution(const QSize& resolution);

    /** Opens the persistent phosphor color settings panel. */
    void open_screen_color_settings();

    /** Opens the persistent master hardware-audio volume control. */
    void open_audio_volume_settings();

    /** Shows package, license, dependency, and historical-asset notices. */
    void open_about();

    /** Saves the complete rendered CRT display to a PNG image. */
    void save_screenshot();

    /** Sets the wall-clock multiplier relative to the 4 MHz machine. */
    void set_emulation_speed(double multiplier);

    /** Runs the machine for the T-states accrued since the previous tick. */
    void run_emulation_slice();

    P2000cMachine machine_;
    DisplayWidget* display_ = nullptr;
    QTimer* timer_ = nullptr;
    std::array<QMenu*, 2> media_drive_menus_{};
    std::array<QAction*, 2> current_media_actions_{};
    QAction* copower_action_ = nullptr;
    QAction* fast_boot_msdos_action_ = nullptr;
    std::array<QAction*, 2> bundled_system_actions_{};
    std::array<QAction*, 2> bundled_copower_cpm_actions_{};
    std::array<QAction*, 2> bundled_copower_dos_actions_{};
    std::array<std::array<QAction*, kMsDosApplicationImageCount>, 2>
        bundled_msdos_application_actions_{};
    std::array<QAction*, 2> bundled_zork_actions_{};
    std::array<QAction*, 2> bundled_chess_actions_{};
    std::array<QAction*, 2> bundled_ipldump_actions_{};
    std::array<QAction*, 2> bundled_p2edit_actions_{};
    std::array<QAction*, 2> bundled_p2file_actions_{};
    std::array<QAction*, 2> bundled_blank_actions_{};
    std::array<QAction*, 2> save_floppy_actions_{};
    std::array<QMenu*, 2> recent_floppy_menus_{};
    std::array<QString, 2> bundled_system_paths_{};
    std::array<QString, 2> bundled_copower_cpm_paths_{};
    std::array<QString, 2> bundled_copower_dos_paths_{};
    std::array<std::array<QString, kMsDosApplicationImageCount>, 2>
        bundled_msdos_application_paths_{};
    std::array<QString, 2> bundled_zork_paths_{};
    std::array<QString, 2> bundled_chess_paths_{};
    std::array<QString, 2> bundled_ipldump_paths_{};
    std::array<QString, 2> bundled_p2edit_paths_{};
    std::array<QString, 2> bundled_p2file_paths_{};
    std::array<QString, 2> bundled_blank_paths_{};
    std::array<QLabel*, 2> media_status_labels_{};
    std::array<DrivePositionDisplay*, 2> floppy_position_displays_{};
    std::array<DriveLed*, 2> floppy_activity_leds_{};
    std::array<QMenu*, 2> hard_disk_menus_{};
    std::array<QAction*, 2> current_hard_disk_actions_{};
    std::array<QAction*, 2> bundled_hard_disk_actions_{};
    std::array<QAction*, 2> save_hard_disk_actions_{};
    std::array<QMenu*, 2> recent_hard_disk_menus_{};
    std::array<QLabel*, 2> hard_disk_status_labels_{};
    std::array<DrivePositionDisplay*, 2> hard_disk_position_displays_{};
    std::array<DriveLed*, 2> hard_disk_activity_leds_{};
    std::array<QString, 2> temporary_floppy_paths_{};
    std::array<QString, 2> temporary_hard_disk_paths_{};
    QVBoxLayout* drive_panel_layout_ = nullptr;
    MemoryMapDisplay* memory_map_display_ = nullptr;
    QTemporaryDir media_session_;
    std::unique_ptr<HardwareAudio> hardware_audio_;
    bool audio_enabled_ = true;
    QElapsedTimer execution_timer_;
    QElapsedTimer memory_panel_timer_;
    QElapsedTimer fast_boot_stage_timer_;
    FastBootController fast_boot_controller_;
    double pending_t_states_ = 0.0;
    double speed_multiplier_ = 1.0;
    std::uint64_t terminal_revision_ = 0;
    std::uint64_t terminal_bell_revision_ = 0;
};

}  // namespace p2000c

#endif  // P2000C_APP_MAIN_WINDOW_H_
