#ifndef P2000C_APP_MAIN_WINDOW_H_
#define P2000C_APP_MAIN_WINDOW_H_

#include <QElapsedTimer>
#include <QMainWindow>
#include <QSize>
#include <QString>
#include <array>
#include <cstddef>
#include <filesystem>

#include "core/p2000c_machine.h"

class QAction;
class QLabel;
class QMenu;
class QTimer;

namespace p2000c {

class DisplayWidget;

/** Main window for the minimal Qt P2000C emulator shell. */
class MainWindow : public QMainWindow {
  private:
    static constexpr double kBaseClockHz = 4'000'000.0;
    static constexpr qint64 kMaximumCatchUpNanoseconds = 100'000'000;

  public:
    /** Creates the emulator window, menus, display, and paced run timer. */
    explicit MainWindow(QWidget* parent = nullptr);

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

    /** Refreshes the Media menu and persistent drive-status indicators. */
    void refresh_media_indicators();

    /** Mounts a persistent writable copy of the bundled CP/M system floppy. */
    void mount_bundled_system_floppy(std::size_t drive);

    /** Mounts a persistent writable copy of the bundled ZORK floppy. */
    void mount_bundled_zork_floppy(std::size_t drive);

    /** Mounts a persistent writable copy of the bundled CHESS floppy. */
    void mount_bundled_chess_floppy(std::size_t drive);

    /** Mounts the bundled ASM/LOAD/IPLDUMP development floppy. */
    void mount_bundled_ipldump_floppy(std::size_t drive);

    /** Mounts a persistent blank 640 KiB data floppy. */
    void mount_bundled_blank_floppy(std::size_t drive);

    /** Mounts one persistent writable copy of the bundled blank HDA. */
    void mount_bundled_hard_disk(std::size_t drive);

    /** Installs the documented A/B plus C-F startup media arrangement. */
    void mount_default_media();

    /** Applies and remembers one fixed display presentation size. */
    void set_display_resolution(const QSize& resolution);

    /** Opens the persistent phosphor color settings panel. */
    void open_screen_color_settings();

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
    std::array<QAction*, 2> bundled_system_actions_{};
    std::array<QAction*, 2> bundled_zork_actions_{};
    std::array<QAction*, 2> bundled_chess_actions_{};
    std::array<QAction*, 2> bundled_ipldump_actions_{};
    std::array<QAction*, 2> bundled_blank_actions_{};
    std::array<QString, 2> bundled_system_paths_{};
    std::array<QString, 2> bundled_zork_paths_{};
    std::array<QString, 2> bundled_chess_paths_{};
    std::array<QString, 2> bundled_ipldump_paths_{};
    std::array<QLabel*, 2> media_status_labels_{};
    std::array<QMenu*, 2> hard_disk_menus_{};
    std::array<QAction*, 2> current_hard_disk_actions_{};
    std::array<QAction*, 2> bundled_hard_disk_actions_{};
    std::array<QLabel*, 2> hard_disk_status_labels_{};
    QElapsedTimer execution_timer_;
    double pending_t_states_ = 0.0;
    double speed_multiplier_ = 1.0;
    std::uint64_t terminal_revision_ = 0;
};

}  // namespace p2000c

#endif  // P2000C_APP_MAIN_WINDOW_H_
