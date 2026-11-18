#pragma once

#include <string>

namespace uedit {

struct Settings {
  std::wstring theme = L"system";
  int window_x = 0;
  int window_y = 0;
  int window_width = 1280;
  int window_height = 820;
  int zoom_percent = 100;
  int result_limit = 5000;
  bool search_case_sensitive = false;
  bool search_whole_word = false;
  bool search_regex = false;
  bool restore_last_file = false;
  std::wstring last_file;
  std::wstring recent_files;  // newline-separated, newest first
  std::wstring search_history;  // newline-separated, newest first
  bool has_placement = false;
  bool maximized = false;

  void Load();
  void Save() const;
};

std::wstring SettingsDirectory();
std::wstring SettingsPath();

}  // namespace uedit
