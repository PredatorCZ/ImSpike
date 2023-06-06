#include <string>

#ifdef USEWIN
#include <windows.h>
void OpenInBrowser(const std::string &url) {
  ShellExecute(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
#else
#include <sys/fcntl.h>
void OpenInBrowser(const std::string &url) {
  std::string command("xdg-open " + url);
  system(command.c_str());
}

#endif
