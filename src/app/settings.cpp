#include "app/settings.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdlib>
#include <string>

namespace uedit {
namespace {

std::wstring LocalAppData() {
  wchar_t path[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                 nullptr, SHGFP_TYPE_CURRENT, path))) {
    return path;
  }
  return L".";
}

std::string Narrow(const std::wstring& text) {
  if (text.empty()) return {};
  int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                   static_cast<int>(text.size()), nullptr, 0,
                                   nullptr, nullptr);
  if (length <= 0) return {};
  std::string out(static_cast<size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                      static_cast<int>(text.size()), &out[0], length, nullptr,
                      nullptr);
  return out;
}

std::wstring Widen(const std::string& text) {
  if (text.empty()) return {};
  int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring out(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(),
                      static_cast<int>(text.size()), &out[0], length);
  return out;
}

std::string ReadAll(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER size{};
  std::string content;
  if (GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
      size.QuadPart < 1024 * 1024) {
    content.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ReadFile(file, &content[0], static_cast<DWORD>(content.size()), &read,
             nullptr);
    content.resize(read);
  }
  CloseHandle(file);
  return content;
}

bool WriteAll(const std::wstring& path, const std::string& content) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  BOOL ok = WriteFile(file, content.data(),
                      static_cast<DWORD>(content.size()), &written, nullptr);
  CloseHandle(file);
  return ok && written == content.size();
}

bool ExtractValue(const std::string& text, const char* key,
                  std::string* value) {
  const std::string quoted = std::string("\"") + key + "\"";
  size_t position = text.find(quoted);
  if (position == std::string::npos) return false;
  position = text.find(':', position + quoted.size());
  if (position == std::string::npos) return false;
  ++position;
  while (position < text.size() &&
         (text[position] == ' ' || text[position] == '\t' ||
          text[position] == '\r' || text[position] == '\n')) {
    ++position;
  }
  if (position >= text.size()) return false;
  if (text[position] == '"') {
    ++position;
    std::string out;
    while (position < text.size() && text[position] != '"') {
      if (text[position] == '\\' && position + 1 < text.size()) {
        const char escaped = text[++position];
        switch (escaped) {
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          default:
            out.push_back(escaped);
            break;
        }
      } else {
        out.push_back(text[position]);
      }
      ++position;
    }
    *value = out;
    return true;
  }
  size_t end = position;
  while (end < text.size() && text[end] != ',' && text[end] != '}' &&
         text[end] != '\r' && text[end] != '\n') {
    ++end;
  }
  while (end > position &&
         (text[end - 1] == ' ' || text[end - 1] == '\t')) {
    --end;
  }
  *value = text.substr(position, end - position);
  return true;
}

std::string JsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

int ToInt(const std::string& value, int fallback) {
  if (value.empty()) return fallback;
  return static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
}

}  // namespace

std::wstring SettingsDirectory() {
  return LocalAppData() + L"\\UEdit";
}

std::wstring SettingsPath() { return SettingsDirectory() + L"\\settings.json"; }

void Settings::Load() {
  std::string text = ReadAll(SettingsPath());
  if (text.empty()) return;

  std::string value;
  if (ExtractValue(text, "theme", &value) &&
      (value == "dark" || value == "light")) {
    theme = Widen(value);
  }
  if (ExtractValue(text, "window_x", &value)) window_x = ToInt(value, window_x);
  if (ExtractValue(text, "window_y", &value)) window_y = ToInt(value, window_y);
  if (ExtractValue(text, "window_width", &value)) {
    window_width = ToInt(value, window_width);
  }
  if (ExtractValue(text, "window_height", &value)) {
    window_height = ToInt(value, window_height);
  }
  if (ExtractValue(text, "zoom_percent", &value)) {
    zoom_percent = ToInt(value, zoom_percent);
  }
  if (ExtractValue(text, "result_limit", &value)) {
    result_limit = ToInt(value, result_limit);
  }
  if (ExtractValue(text, "search_case_sensitive", &value)) {
    search_case_sensitive = value == "true";
  }
  if (ExtractValue(text, "search_whole_word", &value)) {
    search_whole_word = value == "true";
  }
  if (ExtractValue(text, "search_regex", &value)) {
    search_regex = value == "true";
  }
  if (ExtractValue(text, "restore_last_file", &value)) {
    restore_last_file = value == "true";
  }
  if (ExtractValue(text, "last_file", &value)) {
    last_file = Widen(value);
  }
  if (ExtractValue(text, "recent_files", &value)) {
    recent_files = Widen(value);
  }
  if (ExtractValue(text, "search_history", &value)) {
    search_history = Widen(value);
  }
  if (ExtractValue(text, "has_placement", &value)) {
    has_placement = value == "true";
  }
  if (ExtractValue(text, "maximized", &value)) maximized = value == "true";
}

void Settings::Save() const {
  CreateDirectoryW(SettingsDirectory().c_str(), nullptr);
  std::string text;
  text += "{\n";
  text += "  \"theme\": \"" + Narrow(theme) + "\",\n";
  text += "  \"window_x\": " + std::to_string(window_x) + ",\n";
  text += "  \"window_y\": " + std::to_string(window_y) + ",\n";
  text += "  \"window_width\": " + std::to_string(window_width) + ",\n";
  text += "  \"window_height\": " + std::to_string(window_height) + ",\n";
  text += "  \"zoom_percent\": " + std::to_string(zoom_percent) + ",\n";
  text += "  \"result_limit\": " + std::to_string(result_limit) + ",\n";
  text += "  \"search_case_sensitive\": ";
  text += search_case_sensitive ? "true" : "false";
  text += ",\n  \"search_whole_word\": ";
  text += search_whole_word ? "true" : "false";
  text += ",\n  \"search_regex\": ";
  text += search_regex ? "true" : "false";
  text += ",\n  \"restore_last_file\": ";
  text += restore_last_file ? "true" : "false";
  text += ",\n";
  text += "  \"last_file\": \"" + JsonEscape(Narrow(last_file)) + "\",\n";
  text += "  \"recent_files\": \"" + JsonEscape(Narrow(recent_files)) +
          "\",\n";
  text += "  \"search_history\": \"" + JsonEscape(Narrow(search_history)) +
          "\",\n";
  text += "  \"has_placement\": ";
  text += has_placement ? "true" : "false";
  text += ",\n  \"maximized\": ";
  text += maximized ? "true" : "false";
  text += "\n}\n";
  WriteAll(SettingsPath(), text);
}

}  // namespace uedit
