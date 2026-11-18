#include "common/log.h"

#include <windows.h>
#include <shlobj.h>

#include <mutex>

namespace uedit {
namespace {

std::mutex g_mutex;
std::wstring g_path;
bool g_initialized = false;

std::wstring LogDirectory() {
  wchar_t path[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                 nullptr, SHGFP_TYPE_CURRENT, path))) {
    return std::wstring(path) + L"\\UEdit";
  }
  return L".";
}

std::string ToUtf8(const std::wstring& text) {
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

const wchar_t* LevelName(wchar_t level) {
  switch (level) {
    case L'W':
      return L"WARN";
    case L'E':
      return L"ERROR";
    default:
      return L"INFO";
  }
}

void Write(wchar_t level, const std::wstring& message) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized) return;

  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t line[4096]{};
  swprintf_s(line, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] [%lu] %s\r\n",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
             now.wSecond, now.wMilliseconds, LevelName(level),
             GetCurrentThreadId(), message.c_str());

  HANDLE file = CreateFileW(g_path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  const std::string utf8 = ToUtf8(line);
  DWORD written = 0;
  WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
            nullptr);
  CloseHandle(file);
}

}  // namespace

void LogInit() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::wstring directory = LogDirectory();
  CreateDirectoryW(directory.c_str(), nullptr);
  g_path = directory + L"\\uedit.log";

  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  if (GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &attributes)) {
    const uint64_t size = (static_cast<uint64_t>(attributes.nFileSizeHigh) << 32) |
                          attributes.nFileSizeLow;
    if (size > 2 * 1024 * 1024) {
      const std::wstring backup = g_path + L".1";
      MoveFileExW(g_path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
  }
  g_initialized = true;
}

void LogInfo(const std::wstring& message) { Write(L'I', message); }
void LogWarn(const std::wstring& message) { Write(L'W', message); }
void LogError(const std::wstring& message) { Write(L'E', message); }

}  // namespace uedit
