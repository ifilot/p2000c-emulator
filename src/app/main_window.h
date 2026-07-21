#ifndef P2000C_APP_MAIN_WINDOW_H_
#define P2000C_APP_MAIN_WINDOW_H_

#include <QElapsedTimer>
#include <QMainWindow>
#include <QSize>
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

  private:
    /** Creates machine and media actions. */
    void create_menus();

    /** Updates the prototype status shown on the emulated display. */
    void refresh_screen();

    /** Prompts for and loads mainboard IPL firmware. */
    void load_ipl_rom();

    /** Prompts for and mounts a writable ImageDisk floppy in one drive. */
    void open_floppy(std::size_t drive);

    /** Refreshes the Media menu and persistent drive-status indicators. */
    void refresh_media_indicators();

    /** Mounts a persistent writable copy of the bundled CP/M system floppy. */
    void mount_bundled_system_floppy(std::size_t drive);

    /** Mounts a persistent blank 640 KiB data floppy. */
    void mount_bundled_blank_floppy(std::size_t drive);

    /** Applies and remembers one fixed display presentation size. */
    void set_display_resolution(const QSize& resolution);

    /** Opens the persistent phosphor color settings panel. */
    void open_screen_color_settings();

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
    std::array<QAction*, 2> bundled_blank_actions_{};
    std::array<QLabel*, 2> media_status_labels_{};
    QElapsedTimer execution_timer_;
    double pending_t_states_ = 0.0;
    double speed_multiplier_ = 1.0;
    std::uint64_t terminal_revision_ = 0;
};

}  // namespace p2000c

#endif  // P2000C_APP_MAIN_WINDOW_H_
