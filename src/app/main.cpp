#include <QApplication>
#include <filesystem>

#include "app/main_window.h"

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  QApplication::setApplicationName("P2000C Emulator");
  QApplication::setOrganizationName("P2000C Emulator Project");

  p2000c::MainWindow window;
  if (argc > 1) {
    window.mount_floppy(std::filesystem::path(argv[1]));
  }
  if (argc > 2) {
    window.mount_floppy(std::filesystem::path(argv[2]), 1);
  }
  window.show();
  return application.exec();
}
