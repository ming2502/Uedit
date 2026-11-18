#include "app/viewer.h"

#include <dwmapi.h>
#include <shobjidl.h>
#include <imm.h>
#include <psapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <string>

#include "common/log.h"
#include "common/stopwatch.h"
#include "search/search.h"

namespace uedit {
namespace {

constexpr float kTitleBarHeight = 40.0f;
constexpr float kCaptionButtonWidth = 46.0f;
constexpr float kLineHeight = 21.0f;
constexpr float kStatusHeight = 26.0f;
constexpr float kTabBarHeight = 34.0f;
constexpr float kPopupRowHeight = 34.0f;
constexpr float kGutterWidth = 64.0f;
constexpr uint64_t kEditableLimit = 32ull << 20;

HWND g_dialog_owner = nullptr;
WNDPROC g_dialog_original_proc = nullptr;
constexpr UINT_PTR kDialogSettleTimer = 0x61;

LRESULT CALLBACK DialogCenterWndProc(HWND window, UINT message, WPARAM wparam,
                                     LPARAM lparam) {
  WNDPROC original = g_dialog_original_proc;
  if (message == WM_WINDOWPOSCHANGING) {
    auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
    RECT owner_rect{};
    HWND owner = GetWindow(window, GW_OWNER);
    if (!(pos->flags & SWP_NOMOVE) && owner &&
        GetWindowRect(owner, &owner_rect)) {
      int width = pos->cx;
      int height = pos->cy;
      if (pos->flags & SWP_NOSIZE) {
        RECT rect{};
        if (GetWindowRect(window, &rect)) {
          width = rect.right - rect.left;
          height = rect.bottom - rect.top;
        }
      }
      pos->x =
          owner_rect.left + (owner_rect.right - owner_rect.left - width) / 2;
      pos->y =
          owner_rect.top + (owner_rect.bottom - owner_rect.top - height) / 2;
    }
  } else if (message == WM_TIMER && wparam == kDialogSettleTimer) {
    KillTimer(window, kDialogSettleTimer);
    SetWindowLongPtrW(window, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(original));
    g_dialog_original_proc = nullptr;
    return 0;
  } else if (message == WM_NCDESTROY) {
    g_dialog_original_proc = nullptr;
  }
  return CallWindowProcW(original, window, message, wparam, lparam);
}

void HookDialogWindow(HWND window) {
  if (g_dialog_original_proc) return;
  g_dialog_original_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
      window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DialogCenterWndProc)));
  SetTimer(window, kDialogSettleTimer, 1000, nullptr);
}

LRESULT CALLBACK DialogCenterCbt(int code, WPARAM wparam, LPARAM lparam) {
  if (code == HCBT_CREATEWND) {
    HWND window = reinterpret_cast<HWND>(wparam);
    auto* create = reinterpret_cast<CBT_CREATEWND*>(lparam);
    HWND parent = create && create->lpcs ? create->lpcs->hwndParent : nullptr;
    HWND owner = GetWindow(window, GW_OWNER);
    if (!owner) owner = parent;
    wchar_t class_name[16]{};
    GetClassNameW(window, class_name, 16);
    if (wcscmp(class_name, L"#32770") == 0 && owner == g_dialog_owner) {
      if (create && create->lpcs && create->lpcs->cx > 0 &&
          create->lpcs->cy > 0) {
        RECT owner_rect{};
        if (GetWindowRect(owner, &owner_rect)) {
          create->lpcs->x =
              owner_rect.left +
              (owner_rect.right - owner_rect.left - create->lpcs->cx) / 2;
          create->lpcs->y =
              owner_rect.top +
              (owner_rect.bottom - owner_rect.top - create->lpcs->cy) / 2;
        }
      }
      HookDialogWindow(window);
    }
  }
  return CallNextHookEx(nullptr, code, wparam, lparam);
}

HRESULT ShowDialogCentered(IModalWindow* dialog, HWND owner) {
  g_dialog_owner = owner;
  HHOOK hook = SetWindowsHookExW(WH_CBT, DialogCenterCbt, nullptr,
                                 GetCurrentThreadId());
  const HRESULT result = dialog->Show(owner);
  if (hook) UnhookWindowsHookEx(hook);
  g_dialog_owner = nullptr;
  g_dialog_original_proc = nullptr;
  return result;
}

std::wstring ShellItemPath(IShellItem* item) {
  PWSTR raw = nullptr;
  if (!item || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw))) return {};
  std::wstring path(raw ? raw : L"");
  CoTaskMemFree(raw);
  return path;
}

bool ShowOpenFileDialog(HWND owner, std::wstring* path) {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return false;
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST |
                     FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
  const COMDLG_FILTERSPEC filters[] = {
      {L"文本与日志", L"*.log;*.txt;*.csv;*.json;*.jsonl"},
      {L"所有文件", L"*.*"}};
  dialog->SetFileTypes(ARRAYSIZE(filters), filters);
  dialog->SetFileTypeIndex(1);
  dialog->SetTitle(L"打开文件");
  bool ok = false;
  if (SUCCEEDED(ShowDialogCentered(dialog, owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      *path = ShellItemPath(item);
      ok = !path->empty();
      item->Release();
    }
  }
  dialog->Release();
  return ok;
}

bool ShowSaveFileDialog(HWND owner, const std::wstring& directory,
                        const std::wstring& name, std::wstring* path) {
  IFileSaveDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return false;
  }
  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST |
                     FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
  const COMDLG_FILTERSPEC filters[] = {
      {L"文本文件", L"*.txt"}, {L"所有文件", L"*.*"}};
  dialog->SetFileTypes(ARRAYSIZE(filters), filters);
  dialog->SetFileTypeIndex(1);
  dialog->SetDefaultExtension(L"txt");
  dialog->SetTitle(L"选择导出文件");
  dialog->SetFileName(name.c_str());
  IShellItem* folder = nullptr;
  if (!directory.empty() &&
      SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(), nullptr,
                                            IID_PPV_ARGS(&folder)))) {
    dialog->SetFolder(folder);
    folder->Release();
  }
  bool ok = false;
  if (SUCCEEDED(ShowDialogCentered(dialog, owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dialog->GetResult(&item))) {
      *path = ShellItemPath(item);
      ok = !path->empty();
      item->Release();
    }
  }
  dialog->Release();
  return ok;
}

std::wstring Format(const wchar_t* format, ...) {
  wchar_t buffer[512];
  va_list args;
  va_start(args, format);
  vswprintf_s(buffer, 512, format, args);
  va_end(args);
  return buffer;
}

std::string WideToUtf8(const std::wstring& text) {
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

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) return {};
  int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
  if (length <= 0) return {};
  std::wstring out(static_cast<size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(),
                      static_cast<int>(text.size()), &out[0], length);
  return out;
}

std::vector<std::pair<size_t, size_t>> FindRanges(const std::wstring& text,
                                                  const std::wstring& needle,
                                                  bool case_sensitive,
                                                  size_t cap) {
  std::vector<std::pair<size_t, size_t>> ranges;
  if (needle.empty() || text.size() < needle.size()) return ranges;

  if (case_sensitive) {
    size_t position = 0;
    while (ranges.size() < cap) {
      size_t found = text.find(needle, position);
      if (found == std::wstring::npos) break;
      ranges.emplace_back(found, needle.size());
      position = found + needle.size();
    }
    return ranges;
  }

  for (size_t i = 0; i + needle.size() <= text.size() && ranges.size() < cap;
       ++i) {
    bool match = true;
    for (size_t k = 0; k < needle.size(); ++k) {
      if (towlower(text[i + k]) != towlower(needle[k])) {
        match = false;
        break;
      }
    }
    if (match) {
      ranges.emplace_back(i, needle.size());
      i += needle.size() - 1;
    }
  }
  return ranges;
}

std::wstring MakeContext(const uint8_t* data, uint64_t size,
                         const EncodingInfo& encoding, uint64_t offset,
                         uint32_t length, uint32_t* match_pos,
                         uint32_t* match_len) {
  *match_pos = 0;
  *match_len = 0;
  uint64_t begin = offset;
  if (encoding.unit_size() == 2) {
    const uint64_t limit = offset > 4096 ? offset - 4096 : 0;
    uint64_t scan = offset;
    while (scan > limit && scan >= 2) {
      const uint16_t unit =
          encoding.big_endian()
              ? static_cast<uint16_t>((data[scan - 2] << 8) | data[scan - 1])
              : static_cast<uint16_t>(data[scan - 2] |
                                      (data[scan - 1] << 8));
      if (unit == 0x000A) break;
      scan -= 2;
    }
    begin = scan;
  } else {
    const uint64_t limit = offset > 4096 ? offset - 4096 : 0;
    uint64_t scan = offset;
    while (scan > limit && data[scan - 1] != '\n') --scan;
    begin = scan;
  }
  const uint64_t match_stop = std::min<uint64_t>(size, offset + length);
  uint64_t end = match_stop;
  if (encoding.unit_size() == 2) {
    uint64_t scan = match_stop;
    if (scan & 1) ++scan;
    while (scan + 1 < size) {
      const uint16_t unit =
          encoding.big_endian()
              ? static_cast<uint16_t>((data[scan] << 8) | data[scan + 1])
              : static_cast<uint16_t>(data[scan] | (data[scan + 1] << 8));
      if (unit == 0x000A) break;
      scan += 2;
    }
    end = std::min<uint64_t>(size, scan);
  } else {
    uint64_t scan = match_stop;
    while (scan < size && data[scan] != '\n') ++scan;
    end = scan;
  }
  end = std::min<uint64_t>(
      end, begin + (encoding.unit_size() == 2 ? 32768 : 65536));
  std::wstring text =
      DecodeToWide(data + begin, static_cast<size_t>(end - begin), encoding);
  const std::wstring prefix =
      DecodeToWide(data + begin, static_cast<size_t>(offset - begin), encoding);
  const std::wstring matched = DecodeToWide(
      data + offset, static_cast<size_t>(match_stop - offset), encoding);

  uint32_t position = static_cast<uint32_t>(prefix.size());
  uint32_t length_units = static_cast<uint32_t>(matched.size());

  for (wchar_t& c : text) {
    if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
  }

  const size_t cap = 16384;
  const bool truncated = text.size() > cap;
  if (truncated) text.resize(cap);
  if (position >= cap) {
    position = 0;
    length_units = 0;
  } else if (position + length_units > cap) {
    length_units = static_cast<uint32_t>(cap - position);
  }
  *match_pos = position;
  *match_len = length_units;
  if (truncated) text += L"…";
  return text;
}

void DebugLog(const std::string& text) {
  wchar_t enabled[8]{};
  if (GetEnvironmentVariableW(L"UEDIT_DEBUG", enabled, 8) == 0) return;
  wchar_t module[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, module, MAX_PATH) == 0) return;
  std::wstring path(module);
  size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) path.resize(slash + 1);
  path += L"uedit_debug.log";
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written,
            nullptr);
  CloseHandle(file);
}

double CurrentMemoryMB() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                           sizeof(counters))) {
    return static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
  }
  return 0.0;
}

std::wstring FileNameOf(const std::wstring& path) {
  size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring SizeText(uint64_t bytes) {
  if (bytes >= (1ull << 20)) {
    return Format(L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  }
  if (bytes >= 1024) {
    return Format(L"%.1f KB", static_cast<double>(bytes) / 1024.0);
  }
  return Format(L"%llu B", static_cast<unsigned long long>(bytes));
}

bool PosBefore(const TextPos& a, const TextPos& b) {
  return a.line < b.line || (a.line == b.line && a.index < b.index);
}

bool PosSame(const TextPos& a, const TextPos& b) {
  return a.line == b.line && a.index == b.index;
}

bool IsWordChar(wchar_t c) { return iswalnum(c) != 0 || c == L'_'; }

uint16_t ReadUnit(const uint8_t* data, uint64_t offset, bool big_endian) {
  return big_endian
             ? static_cast<uint16_t>((data[offset] << 8) | data[offset + 1])
             : static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

bool UnitIsNewline(const uint8_t* data, uint64_t offset, bool big_endian) {
  return ReadUnit(data, offset, big_endian) == 0x000A;
}

bool UnitIsCarriageReturn(const uint8_t* data, uint64_t offset,
                          bool big_endian) {
  return ReadUnit(data, offset, big_endian) == 0x000D;
}

uint64_t PreviousCharOffset(const uint8_t* data, uint64_t size,
                            uint64_t offset, const EncodingInfo& encoding) {
  if (offset == 0 || offset > size) return 0;
  if (encoding.unit_size() == 2) {
    if (offset >= 4 && UnitIsNewline(data, offset - 2, encoding.big_endian()) &&
        UnitIsCarriageReturn(data, offset - 4, encoding.big_endian())) {
      return offset - 4;
    }
    if (offset >= 4) {
      const uint16_t last = ReadUnit(data, offset - 2, encoding.big_endian());
      const uint16_t previous =
          ReadUnit(data, offset - 4, encoding.big_endian());
      if (last >= 0xDC00 && last <= 0xDFFF && previous >= 0xD800 &&
          previous <= 0xDBFF) {
        return offset - 4;
      }
    }
    return offset >= 2 ? offset - 2 : 0;
  }
  if (encoding.encoding == TextEncoding::Utf8) {
    if (offset >= 2 && data[offset - 1] == '\n' && data[offset - 2] == '\r') {
      return offset - 2;
    }
    uint64_t cursor = offset - 1;
    while (cursor > 0 && (data[cursor] & 0xC0) == 0x80) --cursor;
    return cursor;
  }
  if (offset >= 2 && data[offset - 1] == '\n' && data[offset - 2] == '\r') {
    return offset - 2;
  }
  if (offset >= 2 &&
      IsDBCSLeadByteEx(encoding.codepage, data[offset - 2]) != FALSE) {
    return offset - 2;
  }
  return offset - 1;
}

uint64_t NextCharOffset(const uint8_t* data, uint64_t size, uint64_t offset,
                        const EncodingInfo& encoding) {
  if (offset >= size) return size;
  if (encoding.unit_size() == 2) {
    if (offset + 4 <= size &&
        UnitIsCarriageReturn(data, offset, encoding.big_endian()) &&
        UnitIsNewline(data, offset + 2, encoding.big_endian())) {
      return offset + 4;
    }
    if (offset + 4 <= size) {
      const uint16_t first = ReadUnit(data, offset, encoding.big_endian());
      const uint16_t second =
          ReadUnit(data, offset + 2, encoding.big_endian());
      if (first >= 0xD800 && first <= 0xDBFF && second >= 0xDC00 &&
          second <= 0xDFFF) {
        return offset + 4;
      }
    }
    return std::min<uint64_t>(size, offset + 2);
  }
  if (data[offset] == '\r' && offset + 1 < size && data[offset + 1] == '\n') {
    return offset + 2;
  }
  if (encoding.encoding == TextEncoding::Utf8) {
    const uint8_t lead = data[offset];
    size_t length = 1;
    if ((lead & 0xE0) == 0xC0) {
      length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
    }
    return std::min<uint64_t>(size, offset + length);
  }
  if (IsDBCSLeadByteEx(encoding.codepage, data[offset]) != FALSE &&
      offset + 1 < size) {
    return offset + 2;
  }
  return offset + 1;
}

std::wstring DetectEolName(const uint8_t* data, uint64_t size,
                           const EncodingInfo& encoding) {
  if (encoding.unit_size() == 2) return L"UTF-16";
  const uint64_t limit = std::min<uint64_t>(size, 1u << 20);
  uint64_t crlf = 0;
  uint64_t lf = 0;
  uint64_t cr = 0;
  for (uint64_t i = 0; i < limit; ++i) {
    if (data[i] == '\r') {
      if (i + 1 < limit && data[i + 1] == '\n') {
        ++crlf;
        ++i;
      } else {
        ++cr;
      }
    } else if (data[i] == '\n') {
      ++lf;
    }
  }
  if (crlf == 0 && lf == 0 && cr == 0) return L"LF";
  if (crlf >= lf && crlf >= cr) return L"CRLF";
  if (lf >= cr) return L"LF";
  return L"CR";
}

bool LooksBinary(const uint8_t* data, uint64_t size,
                 const EncodingInfo& encoding) {
  if (encoding.unit_size() == 2) return false;
  const uint64_t limit = std::min<uint64_t>(size, 64u << 10);
  for (uint64_t i = 0; i < limit; ++i) {
    if (data[i] == 0) return true;
  }
  return false;
}

bool SetClipboardText(HWND owner, const std::wstring& text) {
  if (text.empty() || !OpenClipboard(owner)) return false;
  EmptyClipboard();
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory == nullptr) {
    CloseClipboard();
    return false;
  }
  void* destination = GlobalLock(memory);
  if (destination == nullptr) {
    GlobalFree(memory);
    CloseClipboard();
    return false;
  }
  std::memcpy(destination, text.c_str(), bytes);
  GlobalUnlock(memory);
  if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
    GlobalFree(memory);
    CloseClipboard();
    return false;
  }
  CloseClipboard();
  return true;
}

std::wstring GetClipboardText(HWND owner) {
  if (!OpenClipboard(owner)) return {};
  std::wstring text;
  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (handle != nullptr) {
    const wchar_t* data = static_cast<const wchar_t*>(GlobalLock(handle));
    if (data != nullptr) text.assign(data);
    GlobalUnlock(handle);
  }
  CloseClipboard();
  return text;
}

}  // namespace

Viewer::~Viewer() { Shutdown(); }

bool Viewer::Initialize(HWND hwnd, const Settings& settings) {
  hwnd_ = hwnd;
  settings_ = settings;
  if (settings_.theme != L"light" && settings_.theme != L"dark" &&
      settings_.theme != L"system") {
    settings_.theme = L"dark";
  }
  theme_ = ThemeByName(settings_.theme == L"system"
                           ? (SystemUsesLightTheme() ? L"light" : L"dark")
                           : settings_.theme);
  case_sensitive_ = settings_.search_case_sensitive;
  whole_word_ = settings_.search_whole_word;
  regex_ = settings_.search_regex;
  settings_.result_limit =
      std::max(1000, std::min(100000, settings_.result_limit));
  search_history_.clear();
  {
    size_t start = 0;
    while (start <= settings_.search_history.size() &&
           search_history_.size() < 20) {
      const size_t end = settings_.search_history.find(L'\n', start);
      const std::wstring item = settings_.search_history.substr(
          start, end == std::wstring::npos ? std::wstring::npos : end - start);
      if (!item.empty()) search_history_.push_back(item);
      if (end == std::wstring::npos) break;
      start = end + 1;
    }
  }
  if (!canvas_.Initialize(hwnd)) return false;
  QueryPerformanceFrequency(&perf_freq_);
  BuildCommands();

  RECT rc{};
  GetClientRect(hwnd_, &rc);
  UINT dpi = GetDpiForWindow(hwnd_);
  if (dpi == 0) dpi = 96;
  canvas_.Resize(static_cast<UINT>(rc.right), static_cast<UINT>(rc.bottom),
                 static_cast<float>(dpi) / 96.0f);
  int zoom_percent = settings_.zoom_percent;
  if (zoom_percent < 80 || zoom_percent > 200) zoom_percent = 100;
  zoom_ = zoom_percent / 100.0f;
  canvas_.SetZoom(zoom_);
  SyncCompositionStyle();

  ApplyWindowChrome();
  SetTimer(hwnd_, 1, 33, nullptr);
  SetFocus(hwnd_);
  initialized_ = true;
  LogInfo(canvas_.composition() ? L"renderer: composition"
                                : L"renderer: hwnd");
  LogInfo(mica_enabled_ ? L"mica: enabled" : L"mica: disabled");
  LogInfo(L"viewer initialized");
  return true;
}

void Viewer::Shutdown() {
  if (!initialized_) return;
  SaveSettings();
  LogInfo(L"viewer shutdown");
  KillTimer(hwnd_, 1);
  CloseAllDocuments();
  canvas_.Shutdown();
  initialized_ = false;
}

void Viewer::ApplyWindowChrome() {
  const DWORD kDarkMode = 20;        // DWMWA_USE_IMMERSIVE_DARK_MODE
  const DWORD kBackdrop = 38;        // DWMWA_SYSTEMBACKDROP_TYPE
  const DWORD kCorner = 33;          // DWMWA_WINDOW_CORNER_PREFERENCE
  BOOL dark = theme_.dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd_, kDarkMode, &dark, sizeof(dark));
  int backdrop = 2;  // DWMSBT_MAINWINDOW (Mica)
  const HRESULT backdrop_result =
      DwmSetWindowAttribute(hwnd_, kBackdrop, &backdrop, sizeof(backdrop));
  mica_enabled_ =
      canvas_.composition() && SUCCEEDED(backdrop_result);
  int corner = 2;    // DWMWCP_ROUND
  DwmSetWindowAttribute(hwnd_, kCorner, &corner, sizeof(corner));
}

void Viewer::SyncCompositionStyle() {
  const bool wanted = canvas_.composition();
  if (wanted == composition_style_) return;
  composition_style_ = wanted;
  LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
  if (wanted) {
    style |= WS_EX_NOREDIRECTIONBITMAP;
  } else {
    style &= ~static_cast<LONG_PTR>(WS_EX_NOREDIRECTIONBITMAP);
  }
  SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
  SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

bool Viewer::OpenFile(const std::wstring& path) {
  if (extract_running_) {
    SetNotice(L"正在导出，请稍候");
    return false;
  }
  std::wstring full_path = path;
  wchar_t buffer[4096];
  const DWORD length =
      GetFullPathNameW(path.c_str(), 4096, buffer, nullptr);
  if (length > 0 && length < 4096) full_path.assign(buffer, length);

  const auto existing = std::find_if(
      docs_.begin(), docs_.end(), [&](const std::unique_ptr<Document>& open) {
        return _wcsicmp(open->path.c_str(), full_path.c_str()) == 0;
      });
  if (existing != docs_.end()) {
    ActivateDocument(static_cast<size_t>(existing - docs_.begin()));
    return true;
  }

  auto document = std::make_unique<Document>();
  std::wstring error;
  if (!LoadDocument(*document, full_path, &error)) {
    LogError(L"open failed: " + error);
    MessageBoxW(hwnd_, error.c_str(), L"UEdit", MB_OK | MB_ICONERROR);
    return false;
  }
  docs_.push_back(std::move(document));
  active_ = docs_.size() - 1;
  input_mode_ = InputMode::None;
  input_text_.clear();
  notice_.clear();
  line_offsets_.clear();
  line_offsets_valid_ = false;
  h_content_width_ = 0.0f;
  line_render_cache_.clear();
  ++render_generation_;
  AddRecentFile(full_path);
  UpdateDocumentTitle();
  LogInfo(L"opened " + FileNameOf(full_path) + L" (" +
          std::to_wstring(DataSize()) + L" bytes, " +
          EncodingName(doc().encoding) +
          (doc().editable ? L", editable)" : L", read-only)"));
  InvalidateRect(hwnd_, nullptr, FALSE);
  return true;
}

bool Viewer::LoadDocument(Document& document, const std::wstring& path,
                          std::wstring* error) {
  std::unique_ptr<MappedFile> file = MappedFile::Open(path, error);
  if (!file) return false;
  document.file = std::move(file);
  document.path = path;
  document.encoding =
      DetectEncoding(document.file->data(), document.file->size());
  document.eol_name = DetectEolName(document.file->data(),
                                    document.file->size(), document.encoding);
  document.binary = LooksBinary(document.file->data(),
                                document.file->size(), document.encoding);
  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  const bool file_read_only =
      GetFileAttributesExW(path.c_str(), GetFileExInfoStandard,
                           &attributes) != FALSE &&
      (attributes.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  document.editable = !document.binary && !file_read_only &&
                      document.file->size() <= kEditableLimit;
  document.index_cancel.Reset();
  if (document.editable) {
    document.editable_text.Load(document.file->data(), document.file->size(),
                                document.encoding);
    document.index.Prepare(nullptr, 0, 0, 1, false);
  } else {
    document.index.Prepare(document.file->data(), document.file->size(),
                           document.encoding.bom_size,
                           document.encoding.unit_size(),
                           document.encoding.big_endian());
    Document* raw = &document;
    document.index_thread =
        std::thread([raw]() { raw->index.Build(&raw->index_cancel); });
  }
  RefreshFileStamp(document);
  return true;
}

void Viewer::ActivateDocument(size_t index) {
  if (index >= docs_.size() || index == active_) return;
  active_ = index;
  input_mode_ = InputMode::None;
  input_text_.clear();
  dragging_selection_ = false;
  h_dragging_ = false;
  scroll_dragging_ = false;
  results_vscroll_dragging_ = false;
  results_hscroll_dragging_ = false;
  results_list_dragging_ = false;
  results_drag_moved_ = false;
  if (GetCapture() == hwnd_) ReleaseCapture();
  line_offsets_.clear();
  line_offsets_valid_ = false;
  h_content_width_ = 0.0f;
  line_render_cache_.clear();
  ++render_generation_;
  notice_.clear();
  CheckExternalChange(doc());
  if (doc().external_changed) {
    SetNotice(doc().dirty ? L"文件已被外部修改，保存将覆盖外部更改"
                          : L"文件已被外部修改，按 F5 重新加载");
  }
  UpdateDocumentTitle();
  MaybeStartPendingExtraction(doc());
  InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Viewer::CloseActiveDocument() {
  if (docs_.empty()) return true;
  if (extract_running_) {
    SetNotice(L"正在导出，请稍候");
    return false;
  }
  if (!PromptSaveIfDirty()) return false;
  Document* document = docs_[active_].get();
  if (document->file) LogInfo(L"closed " + FileNameOf(document->path));
  CancelSearch(*document);
  document->index_cancel.Cancel();
  if (document->index_thread.joinable()) document->index_thread.join();
  docs_.erase(docs_.begin() + static_cast<ptrdiff_t>(active_));
  if (docs_.empty()) {
    active_ = 0;
  } else if (active_ >= docs_.size()) {
    active_ = docs_.size() - 1;
  }
  input_mode_ = InputMode::None;
  input_text_.clear();
  notice_.clear();
  line_offsets_.clear();
  line_offsets_valid_ = false;
  h_content_width_ = 0.0f;
  line_render_cache_.clear();
  ++render_generation_;
  UpdateDocumentTitle();
  InvalidateRect(hwnd_, nullptr, FALSE);
  return true;
}

bool Viewer::CloseTabAt(size_t index) {
  if (index >= docs_.size()) return false;
  if (index != active_) ActivateDocument(index);
  return CloseActiveDocument();
}

void Viewer::CloseAllDocuments() {
  if (extract_thread_.joinable()) CancelExtraction();
  for (auto& owning : docs_) {
    Document& document = *owning;
    CancelSearch(document);
    document.index_cancel.Cancel();
    if (document.index_thread.joinable()) document.index_thread.join();
  }
  docs_.clear();
  active_ = 0;
  line_offsets_.clear();
  line_offsets_valid_ = false;
  h_content_width_ = 0.0f;
  line_render_cache_.clear();
  ++render_generation_;
}

void Viewer::ReplaceActiveDocument(const std::wstring& path) {
  if (docs_.empty()) {
    OpenFile(path);
    return;
  }
  auto fresh = std::make_unique<Document>();
  std::wstring error;
  if (!LoadDocument(*fresh, path, &error)) {
    SetNotice(L"重新加载失败：" + error);
    return;
  }
  Document* old = docs_[active_].get();
  CancelSearch(*old);
  old->index_cancel.Cancel();
  if (old->index_thread.joinable()) old->index_thread.join();
  docs_[active_] = std::move(fresh);
  input_mode_ = InputMode::None;
  notice_.clear();
  line_offsets_.clear();
  line_offsets_valid_ = false;
  h_content_width_ = 0.0f;
  line_render_cache_.clear();
  ++render_generation_;
  UpdateDocumentTitle();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::CycleTab(int delta) {
  if (docs_.size() < 2) return;
  const int count = static_cast<int>(docs_.size());
  int next = static_cast<int>(active_) + delta;
  next = (next % count + count) % count;
  ActivateDocument(static_cast<size_t>(next));
}

bool Viewer::PromptSaveAll() {
  for (size_t i = 0; i < docs_.size(); ++i) {
    if (!docs_[i]->dirty) continue;
    ActivateDocument(i);
    if (!PromptSaveIfDirty()) return false;
  }
  return true;
}

void Viewer::OpenFileDialog() {
  std::wstring path;
  if (ShowOpenFileDialog(hwnd_, &path)) OpenFile(path);
}

void Viewer::OnSize(int width_px, int height_px) {
  UINT dpi = GetDpiForWindow(hwnd_);
  if (dpi == 0) dpi = 96;
  canvas_.Resize(static_cast<UINT>(std::max(1, width_px)),
                 static_cast<UINT>(std::max(1, height_px)),
                 static_cast<float>(dpi) / 96.0f);
  SyncCompositionStyle();
  line_offsets_valid_ = false;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

float Viewer::ClientWidthDip() const {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  float scale = canvas_.dpi_scale();
  return static_cast<float>(rc.right) / (scale > 0.0f ? scale : 1.0f);
}

float Viewer::ClientHeightDip() const {
  RECT rc{};
  GetClientRect(hwnd_, &rc);
  float scale = canvas_.dpi_scale();
  return static_cast<float>(rc.bottom) / (scale > 0.0f ? scale : 1.0f);
}

float Viewer::ToDipX(int pixel) const {
  float scale = canvas_.dpi_scale();
  return static_cast<float>(pixel) / (scale > 0.0f ? scale : 1.0f);
}

float Viewer::ToDipY(int pixel) const {
  return ToDipX(pixel);
}

void Viewer::OnTimer() {
  PollSearch();
  PollExtraction();
  ULONGLONG now = GetTickCount64();
  if (now - last_memory_tick_ > 800) {
    last_memory_tick_ = now;
    memory_mb_ = CurrentMemoryMB();
  }
  if (now - last_external_check_ > 1500 && !extract_running_) {
    last_external_check_ = now;
    for (auto& owning : docs_) CheckExternalChange(*owning);
  }
  if (doc().searching || extract_running_ || (doc().file && !IndexComplete())) {
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  const bool input_active =
      input_mode_ == InputMode::Find || input_mode_ == InputMode::Goto;
  if (input_active) {
    if (now - caret_blink_tick_ > 500) {
      caret_blink_tick_ = now;
      caret_visible_ = !caret_visible_;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  } else if (doc().file && GetFocus() == hwnd_ && !HasSelection()) {
    if (now - caret_blink_tick_ > 500) {
      caret_blink_tick_ = now;
      caret_visible_ = !caret_visible_;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }
  if (!notice_.empty() && GetTickCount64() >= notice_until_) {
    notice_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void Viewer::RebuildVisibleOffsets() {
  line_offsets_.clear();
  if (!doc().file) {
    line_offsets_valid_ = true;
    return;
  }
  uint64_t position = LineStart(doc().first_line);
  line_offsets_.push_back(position);
  for (int i = 1; i <= visible_lines_ + 2; ++i) {
    uint64_t next = 0;
    if (!NextLineStart(position, 8ull << 20, &next)) break;
    if (next <= position) break;
    position = next;
    line_offsets_.push_back(position);
    if (position >= DataSize()) break;
  }

  h_content_width_ = 0.0f;
  for (size_t i = 0;
       i < line_offsets_.size() &&
       i < static_cast<size_t>(visible_lines_ + 2);
       ++i) {
    const uint64_t begin = line_offsets_[i];
    const uint64_t end = (i + 1 < line_offsets_.size())
                             ? line_offsets_[i + 1]
                             : DataSize();
    const std::wstring text = DecodeBytes(begin, end, 8192, true);
    ComPtr<IDWriteTextLayout> layout =
        GetLineLayout(doc().first_line + static_cast<uint64_t>(i), text);
    if (layout) {
      DWRITE_TEXT_METRICS metrics{};
      if (SUCCEEDED(layout->GetMetrics(&metrics))) {
        h_content_width_ = std::max(h_content_width_, metrics.width);
      }
    }
  }
  line_offsets_valid_ = true;
}

std::wstring Viewer::DecodeBytes(uint64_t begin, uint64_t end,
                                 uint64_t max_bytes,
                                 bool expand_tabs) const {
  if (!doc().file || begin >= DataSize()) return {};
  uint64_t stop = std::min(end, DataSize());
  if (stop > begin + max_bytes) stop = begin + max_bytes;
  std::wstring text =
      DecodeToWide(Data() + begin, static_cast<size_t>(stop - begin),
                   doc().encoding);
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) {
    text.pop_back();
  }
  (void)expand_tabs;  // Tabs are rendered via the text format's tab stop.
  return text;
}

std::wstring Viewer::DecodeLine(uint64_t begin, uint64_t end,
                                bool* truncated) const {
  *truncated = false;
  if (!doc().file || begin >= DataSize()) return {};
  const uint64_t cap = 8192;
  uint64_t stop = std::min(end, DataSize());
  if (stop > begin + cap) {
    stop = begin + cap;
    *truncated = true;
  }
  std::wstring text = DecodeBytes(begin, stop, cap, true);
  if (*truncated) text += L" …";
  return text;
}

std::wstring Viewer::LineText(uint64_t line, uint64_t max_bytes,
                              bool expand_tabs) const {
  if (!doc().file) return {};
  const uint64_t begin = LineStart(line);
  const uint64_t end = LineStart(line + 1);
  return DecodeBytes(begin, end, max_bytes, expand_tabs);
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> Viewer::GetLineLayout(
    uint64_t line, const std::wstring& text) const {
  if (line_render_cache_.size() > 1024) line_render_cache_.clear();
  LineRenderCache& entry = line_render_cache_[line];
  if (!entry.layout) {
    entry.layout = canvas_.CreateLayout(text, canvas_.mono(), 1000000.0f,
                                        kLineHeight);
  }
  return entry.layout;
}

const std::vector<std::pair<float, float>>& Viewer::GetMatchRects(
    uint64_t line, const std::wstring& text,
    const std::vector<std::pair<size_t, size_t>>& ranges) const {
  if (line_render_cache_.size() > 1024) line_render_cache_.clear();
  LineRenderCache& entry = line_render_cache_[line];
  if (!entry.layout) {
    entry.layout = canvas_.CreateLayout(text, canvas_.mono(), 1000000.0f,
                                        kLineHeight);
  }
  if (entry.generation != render_generation_) {
    entry.match_rects.clear();
    entry.match_rects.reserve(ranges.size());
    if (entry.layout) {
      for (const auto& range : ranges) {
        const float x1 =
            canvas_.MeasurePrefix(entry.layout.Get(), range.first);
        const float x2 = canvas_.MeasurePrefix(
            entry.layout.Get(), range.first + range.second);
        entry.match_rects.emplace_back(x1, x2);
      }
    }
    entry.generation = render_generation_;
  }
  return entry.match_rects;
}

const uint8_t* Viewer::Data() const {
  if (doc().editable) return doc().editable_text.data();
  return doc().file ? doc().file->data() : nullptr;
}

uint64_t Viewer::DataSize() const {
  if (doc().editable) return doc().editable_text.size();
  return doc().file ? doc().file->size() : 0;
}

uint64_t Viewer::TotalLines() const {
  return doc().editable ? doc().editable_text.total_lines() : doc().index.total_lines();
}

uint64_t Viewer::EstimatedLines() const {
  return doc().editable ? doc().editable_text.total_lines() : doc().index.estimated_lines();
}

bool Viewer::IndexComplete() const {
  return doc().editable ? true : doc().index.complete();
}

double Viewer::IndexProgress() const {
  return doc().editable ? 1.0 : doc().index.progress();
}

uint64_t Viewer::LineStart(uint64_t line) const {
  return doc().editable ? doc().editable_text.line_start(line) : doc().index.line_start(line);
}

uint64_t Viewer::LineEnd(uint64_t line) const {
  return doc().editable ? doc().editable_text.line_end(line) : doc().index.line_end(line);
}

uint64_t Viewer::OffsetToLine(uint64_t offset) const {
  return doc().editable ? doc().editable_text.offset_to_line(offset)
                   : doc().index.offset_to_line(offset);
}

bool Viewer::NextLineStart(uint64_t pos, uint64_t max_scan,
                           uint64_t* next) const {
  return doc().editable ? doc().editable_text.next_line_start(pos, max_scan, next)
                   : doc().index.next_line_start(pos, max_scan, next);
}

uint64_t Viewer::ByteOffsetForPos(const TextPos& position) const {
  if (!doc().editable) return 0;
  const uint64_t line = std::max<uint64_t>(
      1, std::min(position.line, doc().editable_text.total_lines()));
  return doc().editable_text.byte_offset_for_char(line, position.index);
}

TextPos Viewer::PosFromByte(uint64_t offset) const {
  TextPos position;
  if (!doc().editable) return position;
  position.line = OffsetToLine(offset);
  position.index =
      doc().editable_text.char_index_for_byte(position.line, offset);
  return position;
}

std::wstring Viewer::EolWide() const {
  if (doc().encoding.unit_size() == 2) {
    const uint8_t* data = Data();
    const uint64_t size = DataSize();
    const uint64_t limit = std::min<uint64_t>(size, 1u << 20);
    for (uint64_t i = doc().encoding.bom_size; i + 1 < limit; i += 2) {
      if (ReadUnit(data, i, doc().encoding.big_endian()) == 0x000A) {
        if (i >= doc().encoding.bom_size + 2 &&
            ReadUnit(data, i - 2, doc().encoding.big_endian()) == 0x000D) {
          return L"\r\n";
        }
        return L"\n";
      }
    }
    return L"\r\n";
  }
  if (doc().eol_name == L"CRLF") return L"\r\n";
  if (doc().eol_name == L"CR") return L"\r";
  return L"\n";
}

std::wstring Viewer::NormalizeEol(const std::wstring& text) const {
  const std::wstring eol = EolWide();
  std::wstring out;
  out.reserve(text.size() + 16);
  for (size_t i = 0; i < text.size(); ++i) {
    const wchar_t c = text[i];
    if (c == L'\r') {
      if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
      out += eol;
    } else if (c == L'\n') {
      out += eol;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

void Viewer::ReplaceDocumentRange(uint64_t begin, uint64_t end,
                                  const std::string& inserted,
                                  bool record_undo) {
  if (!doc().editable || extract_running_) return;
  begin = std::min(begin, doc().editable_text.size());
  end = std::min(std::max(end, begin), doc().editable_text.size());
  if (begin == end && inserted.empty()) return;

  CancelSearch();
  const std::string removed = doc().editable_text.Slice(begin, end);
  doc().editable_text.Replace(begin, end, inserted);

  if (record_undo) {
    doc().undo_stack.push_back(EditRecord{begin, removed, inserted});
    size_t total_bytes = 0;
    for (const EditRecord& record : doc().undo_stack) {
      total_bytes += record.removed.size() + record.inserted.size();
    }
    while (!doc().undo_stack.empty() &&
           (doc().undo_stack.size() > 200 || total_bytes > (32ull << 20))) {
      total_bytes -= doc().undo_stack.front().removed.size() +
                     doc().undo_stack.front().inserted.size();
      doc().undo_stack.erase(doc().undo_stack.begin());
    }
    doc().redo_stack.clear();
  }

  AfterDocumentEdit(begin + inserted.size());
}

void Viewer::AfterDocumentEdit(uint64_t caret_offset) {
  doc().dirty = true;
  doc().results.clear();
  doc().pending_results.clear();
  doc().query.clear();
  doc().query_wide.clear();
  doc().total_hits = 0;
  doc().truncated = false;
  doc().search_error = false;
  doc().search_message.clear();
  doc().panel_scroll = 0;
  doc().current_hit = static_cast<size_t>(-1);
  doc().results_open = false;
  doc().results_selected = 0;
  ++render_generation_;
  line_render_cache_.clear();
  line_offsets_valid_ = false;
  h_content_width_ = 0.0f;
  doc().caret = PosFromByte(caret_offset);
  doc().anchor = doc().caret;
  caret_visible_ = true;
  caret_blink_tick_ = GetTickCount64();
  EnsureCaretVisible();
  UpdateDocumentTitle();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::InsertTextAtCaret(const std::wstring& text,
                               bool replace_selection) {
  if (!doc().editable) {
    SetNotice(L"大文件为只读模式（编辑仅支持 ≤32MB 文本文件）");
    return;
  }
  if (text.empty()) return;
  uint64_t begin = 0;
  uint64_t end = 0;
  if (replace_selection && HasSelection()) {
    begin = ByteOffsetForPos(SelectionStart());
    end = ByteOffsetForPos(SelectionEnd());
  } else {
    begin = end = ByteOffsetForPos(doc().caret);
  }
  const std::string bytes = doc().editable_text.EncodeWide(text);
  if (bytes.empty()) return;
  ReplaceDocumentRange(begin, end, bytes, true);
}

void Viewer::DeleteSelection() {
  if (!doc().editable) {
    SetNotice(L"大文件为只读模式");
    return;
  }
  if (!HasSelection()) return;
  const uint64_t begin = ByteOffsetForPos(SelectionStart());
  const uint64_t end = ByteOffsetForPos(SelectionEnd());
  ReplaceDocumentRange(begin, end, std::string(), true);
}

void Viewer::DeleteBackward() {
  if (!doc().editable) {
    SetNotice(L"大文件为只读模式");
    return;
  }
  if (HasSelection()) {
    DeleteSelection();
    return;
  }
  const uint64_t caret_byte = ByteOffsetForPos(doc().caret);
  const uint64_t previous =
      PreviousCharOffset(Data(), DataSize(), caret_byte, doc().encoding);
  if (previous < caret_byte) {
    ReplaceDocumentRange(previous, caret_byte, std::string(), true);
  }
}

void Viewer::DeleteForward() {
  if (!doc().editable) {
    SetNotice(L"大文件为只读模式");
    return;
  }
  if (HasSelection()) {
    DeleteSelection();
    return;
  }
  const uint64_t caret_byte = ByteOffsetForPos(doc().caret);
  const uint64_t next =
      NextCharOffset(Data(), DataSize(), caret_byte, doc().encoding);
  if (next > caret_byte) {
    ReplaceDocumentRange(caret_byte, next, std::string(), true);
  }
}

void Viewer::InsertNewline() {
  InsertTextAtCaret(EolWide(), true);
}

void Viewer::PasteClipboard() {
  if (!doc().editable) {
    SetNotice(L"大文件为只读模式");
    return;
  }
  const std::wstring text = GetClipboardText(hwnd_);
  if (text.empty()) return;
  InsertTextAtCaret(NormalizeEol(text), true);
}

void Viewer::PasteIntoInput() {
  if (input_mode_ == InputMode::None || input_mode_ == InputMode::Extract ||
      input_mode_ == InputMode::Settings) {
    return;
  }
  const std::wstring text = GetClipboardText(hwnd_);
  if (text.empty()) return;
  if (input_mode_ == InputMode::Palette) {
    for (wchar_t c : text) {
      if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
      if (palette_input_.size() >= 512) break;
      palette_input_.push_back(c);
    }
    RebuildPaletteMatches();
  } else if (input_mode_ == InputMode::Goto) {
    for (wchar_t c : text) {
      if (c >= L'0' && c <= L'9' && input_text_.size() < 20) {
        input_text_.push_back(c);
      }
    }
  } else {
    for (wchar_t c : text) {
      if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
      if (input_text_.size() >= 4096) break;
      input_text_.push_back(c);
    }
    history_index_ = -1;
  }
  ResetInputCaret();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::CutSelection() {
  if (!doc().editable || !HasSelection()) return;
  CopySelection();
  DeleteSelection();
}

void Viewer::UndoEdit() {
  if (!doc().editable || doc().undo_stack.empty()) return;
  CancelSearch();
  const EditRecord record = doc().undo_stack.back();
  doc().undo_stack.pop_back();
  doc().editable_text.Replace(record.offset,
                         record.offset + record.inserted.size(),
                         record.removed);
  doc().redo_stack.push_back(record);
  AfterDocumentEdit(record.offset + record.removed.size());
}

void Viewer::RedoEdit() {
  if (!doc().editable || doc().redo_stack.empty()) return;
  CancelSearch();
  const EditRecord record = doc().redo_stack.back();
  doc().redo_stack.pop_back();
  doc().editable_text.Replace(record.offset,
                         record.offset + record.removed.size(),
                         record.inserted);
  doc().undo_stack.push_back(record);
  AfterDocumentEdit(record.offset + record.inserted.size());
}

void Viewer::SaveDocument() {
  if (!doc().file || doc().path.empty()) return;
  if (!doc().editable) {
    SetNotice(L"大文件为只读模式，无法保存");
    return;
  }
  if (!doc().dirty) {
    SetNotice(L"没有需要保存的修改");
    return;
  }
  std::wstring error;
  const RewriteResult result = SaveBufferAtomically(
      doc().path, doc().editable_text.data(), doc().editable_text.size(), &error);
  if (!result.ok) {
    SetNotice(L"保存失败：" + result.error);
    LogError(L"save failed: " + result.error);
    return;
  }
  doc().dirty = false;
  RefreshFileStamp(doc());
  UpdateDocumentTitle();
  SetNotice(L"已保存");
  LogInfo(L"saved " + FileNameOf(doc().path) + L" (" +
          std::to_wstring(doc().editable_text.size()) + L" bytes)");
  InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Viewer::PromptSaveIfDirty() {
  if (!doc().dirty) return true;
  const std::wstring name =
      doc().path.empty() ? L"当前文件" : FileNameOf(doc().path);
  const std::wstring prompt =
      name + L" 有未保存的修改。是否保存？";
  const int choice = MessageBoxW(hwnd_, prompt.c_str(), L"UEdit",
                                 MB_YESNOCANCEL | MB_ICONQUESTION);
  if (choice == IDCANCEL) return false;
  if (choice == IDYES) {
    SaveDocument();
    return !doc().dirty;
  }
  doc().dirty = false;
  return true;
}

void Viewer::UpdateDocumentTitle() {
  std::wstring title = doc().path.empty() ? L"UEdit" : FileNameOf(doc().path);
  if (doc().dirty) title += L" *";
  if (!doc().path.empty()) title += L" — UEdit";
  SetWindowTextW(hwnd_, title.c_str());
}

bool Viewer::HasSelection() const { return !PosSame(doc().anchor, doc().caret); }

TextPos Viewer::SelectionStart() const {
  return PosBefore(doc().anchor, doc().caret) ? doc().anchor : doc().caret;
}

TextPos Viewer::SelectionEnd() const {
  return PosBefore(doc().anchor, doc().caret) ? doc().caret : doc().anchor;
}

void Viewer::ClearSelection() {
  doc().anchor = doc().caret;
  caret_visible_ = true;
  caret_blink_tick_ = GetTickCount64();
}

void Viewer::SetCaret(TextPos position, bool extend) {
  if (!doc().file) return;
  const uint64_t total = std::max<uint64_t>(TotalLines(), 1);
  if (position.line < 1) position.line = 1;
  if (position.line > total) position.line = total;
  const std::wstring text = LineText(position.line, 8192, true);
  if (position.index > text.size()) {
    position.index = static_cast<uint32_t>(text.size());
  }
  doc().caret = position;
  if (!extend) doc().anchor = position;
  caret_visible_ = true;
  caret_blink_tick_ = GetTickCount64();
  EnsureCaretVisible();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::SelectAll() {
  if (!doc().file) return;
  const uint64_t total = std::max<uint64_t>(TotalLines(), 1);
  doc().anchor = TextPos{1, 0};
  doc().caret = TextPos{total, static_cast<uint32_t>(
                               LineText(total, 8192, true).size())};
  caret_visible_ = true;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

TextPos Viewer::HitTestPosition(float x_dip, float y_dip) const {
  TextPos position;
  position.line = doc().first_line;
  if (!doc().file || text_area_width_ <= 0.0f) return position;

  int row = static_cast<int>((y_dip - text_area_y_) / kLineHeight);
  row = std::max(0, std::min(row, visible_lines_ - 1));
  uint64_t line = doc().first_line + static_cast<uint64_t>(row);
  const uint64_t total = std::max<uint64_t>(TotalLines(), 1);
  line = std::max<uint64_t>(1, std::min(line, total));
  position.line = line;

  const std::wstring text = LineText(line, 8192, true);
  ComPtr<IDWriteTextLayout> layout = GetLineLayout(line, text);
  if (layout) {
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    const float local_x = x_dip - text_area_x_ + doc().h_offset;
    if (SUCCEEDED(layout->HitTestPoint(local_x, kLineHeight * 0.5f, &trailing,
                                       &inside, &metrics))) {
      uint32_t index = metrics.textPosition + (trailing ? 1u : 0u);
      if (index > text.size()) index = static_cast<uint32_t>(text.size());
      position.index = index;
    }
  }
  return position;
}

void Viewer::SelectWordAt(TextPos position) {
  const std::wstring text = LineText(position.line, 8192, true);
  size_t index = std::min<size_t>(position.index, text.size());
  size_t start = index;
  size_t end = index;
  if (index < text.size() && !IsWordChar(text[index])) {
    end = index + 1;
  } else {
    while (start > 0 && IsWordChar(text[start - 1])) --start;
    while (end < text.size() && IsWordChar(text[end])) ++end;
  }
  doc().anchor = TextPos{position.line, static_cast<uint32_t>(start)};
  doc().caret = TextPos{position.line, static_cast<uint32_t>(end)};
  caret_visible_ = true;
  caret_blink_tick_ = GetTickCount64();
}

void Viewer::MoveCaret(int dx, int dy, bool extend, bool by_word) {
  if (!doc().file) return;

  if (HasSelection() && !extend && (dx != 0 || dy != 0)) {
    TextPos edge = (dx < 0 || dy < 0) ? SelectionStart() : SelectionEnd();
    doc().anchor = edge;
    doc().caret = edge;
    caret_visible_ = true;
    EnsureCaretVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  TextPos position = doc().caret;
  const uint64_t total = std::max<uint64_t>(TotalLines(), 1);

  if (dy != 0) {
    int64_t line = static_cast<int64_t>(position.line) + dy;
    line = std::max<int64_t>(
        1, std::min<int64_t>(line, static_cast<int64_t>(total)));
    position.line = static_cast<uint64_t>(line);
    const uint32_t length =
        static_cast<uint32_t>(LineText(position.line, 8192, true).size());
    if (position.index > length) position.index = length;
  } else if (dx != 0) {
    const std::wstring text = LineText(position.line, 8192, true);
    if (by_word) {
      int index =
          static_cast<int>(std::min<size_t>(position.index, text.size()));
      if (dx > 0) {
        while (index < static_cast<int>(text.size()) &&
               !IsWordChar(text[index])) {
          ++index;
        }
        while (index < static_cast<int>(text.size()) &&
               IsWordChar(text[index])) {
          ++index;
        }
        if (index >= static_cast<int>(text.size()) && position.line < total) {
          ++position.line;
          position.index = 0;
        } else {
          position.index = static_cast<uint32_t>(index);
        }
      } else {
        while (index > 0 && !IsWordChar(text[index - 1])) --index;
        while (index > 0 && IsWordChar(text[index - 1])) --index;
        if (index == 0 && position.line > 1) {
          --position.line;
          position.index = static_cast<uint32_t>(
              LineText(position.line, 8192, true).size());
        } else {
          position.index = static_cast<uint32_t>(index);
        }
      }
    } else if (dx < 0) {
      if (position.index > 0) {
        --position.index;
      } else if (position.line > 1) {
        --position.line;
        position.index = static_cast<uint32_t>(
            LineText(position.line, 8192, true).size());
      }
    } else {
      if (position.index < text.size()) {
        ++position.index;
      } else if (position.line < total) {
        ++position.line;
        position.index = 0;
      }
    }
  }

  doc().caret = position;
  if (!extend) doc().anchor = position;
  caret_visible_ = true;
  caret_blink_tick_ = GetTickCount64();
  EnsureCaretVisible();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::MoveCaretToDocumentEdge(bool end, bool extend) {
  if (!doc().file) return;
  const uint64_t total = std::max<uint64_t>(TotalLines(), 1);
  TextPos position;
  if (end) {
    position = TextPos{total, static_cast<uint32_t>(
                                  LineText(total, 8192, true).size())};
  } else {
    position = TextPos{1, 0};
  }
  SetCaret(position, extend);
}

void Viewer::EnsureCaretVisible() {
  if (!doc().file) return;
  bool vertical_changed = false;
  if (doc().caret.line < doc().first_line) {
    doc().first_line = doc().caret.line;
    vertical_changed = true;
  } else if (doc().caret.line >=
             doc().first_line + static_cast<uint64_t>(visible_lines_)) {
    doc().first_line = doc().caret.line - static_cast<uint64_t>(visible_lines_) + 1;
    vertical_changed = true;
  }
  if (doc().first_line < 1) doc().first_line = 1;
  if (vertical_changed) line_offsets_valid_ = false;

  if (text_area_width_ > 0.0f) {
    const std::wstring text = LineText(doc().caret.line, 8192, true);
    ComPtr<IDWriteTextLayout> layout = GetLineLayout(doc().caret.line, text);
    float x = layout ? canvas_.MeasurePrefix(
                           layout.Get(),
                           std::min<size_t>(doc().caret.index, text.size()))
                     : 0.0f;
    const float max_offset =
        std::max(0.0f, h_content_width_ - text_area_width_);
    if (x < doc().h_offset) {
      doc().h_offset = x;
    } else if (x > doc().h_offset + text_area_width_ - 24.0f) {
      doc().h_offset = x - text_area_width_ + 24.0f;
    }
    doc().h_offset = std::max(0.0f, std::min(doc().h_offset, max_offset));
  }
}

std::wstring Viewer::SelectedText(size_t max_chars, bool* truncated) const {
  *truncated = false;
  if (!doc().file || !HasSelection()) return {};
  TextPos start = SelectionStart();
  TextPos end = SelectionEnd();
  const uint64_t total = std::max<uint64_t>(TotalLines(), 1);
  if (start.line > total) return {};
  if (end.line > total) {
    end.line = total;
    end.index = static_cast<uint32_t>(LineText(total, 8192, true).size());
  }

  std::wstring out;
  uint64_t cursor = LineStart(start.line);
  for (uint64_t line = start.line; line <= end.line; ++line) {
    uint64_t next = cursor;
    if (!NextLineStart(cursor, 1ull << 30, &next)) {
      next = DataSize();
    }
    const std::wstring text = DecodeBytes(cursor, next, 1ull << 20, true);
    uint32_t from = (line == start.line) ? start.index : 0;
    uint32_t to =
        (line == end.line) ? end.index : static_cast<uint32_t>(text.size());
    from = std::min<uint32_t>(from, static_cast<uint32_t>(text.size()));
    to = std::min<uint32_t>(to, static_cast<uint32_t>(text.size()));
    if (to < from) to = from;
    const size_t add = static_cast<size_t>(to - from);
    if (out.size() + add > max_chars) {
      out.append(text, from, max_chars - out.size());
      *truncated = true;
      break;
    }
    out.append(text, from, add);
    if (line < end.line) {
      if (out.size() + 2 > max_chars) {
        *truncated = true;
        break;
      }
      out += L"\r\n";
    }
    cursor = next;
    if (cursor >= DataSize() && line < end.line) break;
  }
  return out;
}

void Viewer::CopySelection() {
  if (!doc().file || !HasSelection()) return;
  bool truncated = false;
  const std::wstring text = SelectedText(8000000, &truncated);
  if (text.empty()) return;
  if (!SetClipboardText(hwnd_, text)) {
    SetNotice(L"复制失败");
    return;
  }
  LogInfo(L"clipboard copy chars=" + std::to_wstring(text.size()) +
          (truncated ? L" (truncated)" : L""));
  SetNotice(truncated
                ? L"选区过大，已复制前 8M 字符"
                : L"已复制 " + std::to_wstring(text.size()) + L" 字符");
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::SetNotice(const std::wstring& text) {
  notice_ = text;
  notice_until_ = GetTickCount64() + 2600;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::ResetInputCaret() {
  caret_visible_ = true;
  caret_blink_tick_ = GetTickCount64();
}

void Viewer::Paint() {
  PAINTSTRUCT paint{};
  HDC dc = BeginPaint(hwnd_, &paint);
  (void)dc;

  if (!canvas_.valid()) {
    EndPaint(hwnd_, &paint);
    return;
  }

  LARGE_INTEGER paint_start{};
  QueryPerformanceCounter(&paint_start);

  const float width = ClientWidthDip();
  const float height = ClientHeightDip();
  const float content_top = ContentTop();

  canvas_.BeginDraw(theme_.window_bg);
  if (mica_enabled_) {
    if (doc().file) {
      canvas_.FillRect(0.0f, content_top, width, height - kStatusHeight,
                       Color(theme_.window_bg));
    } else {
      canvas_.FillRect(0.0f, content_top, width, height - kStatusHeight,
                       Color(theme_.window_bg));
    }
  } else {
    canvas_.FillRect(0, 0, width, height, Color(theme_.window_bg));
  }
  DrawTitleBar(width);
  DrawTabBar(width);

  const float text_top = content_top + 8.0f;
  const float text_bottom = std::max(60.0f, height - kStatusHeight - 12.0f);
  const float text_x = 8.0f + kGutterWidth + 8.0f;
  const float text_right = width - 14.0f;
  const float text_width = std::max(40.0f, text_right - text_x);
  visible_lines_ = std::max(
      1, static_cast<int>((text_bottom - text_top) / kLineHeight));

  text_area_x_ = text_x;
  text_area_y_ = text_top;
  text_area_width_ = text_width;
  text_area_bottom_ = text_bottom;

  if (doc().file) {
    if (!line_offsets_valid_) RebuildVisibleOffsets();
    const float max_offset = std::max(0.0f, h_content_width_ - text_width);
    doc().h_offset = std::max(0.0f, std::min(doc().h_offset, max_offset));

    const uint64_t total = EstimatedLines();
    const bool has_selection = HasSelection();
    const TextPos selection_start = SelectionStart();
    const TextPos selection_end = SelectionEnd();

    for (int i = 0; i < visible_lines_; ++i) {
      if (i >= static_cast<int>(line_offsets_.size())) break;
      const uint64_t line_number = doc().first_line + static_cast<uint64_t>(i);
      if (line_number > total) break;
      const float y = text_top + i * kLineHeight;
      canvas_.DrawTextW(std::to_wstring(line_number), 8.0f, y,
                        8.0f + kGutterWidth, y + kLineHeight,
                        canvas_.gutter(), Color(theme_.muted));
    }

    canvas_.PushClip(text_x, text_top, text_x + text_width, text_bottom);
    for (int i = 0; i < visible_lines_; ++i) {
      if (i >= static_cast<int>(line_offsets_.size())) break;
      uint64_t line_number = doc().first_line + static_cast<uint64_t>(i);
      if (line_number > total) break;

      const float y = text_top + i * kLineHeight;
      const uint64_t begin = line_offsets_[i];
      const uint64_t end =
          (i + 1 < static_cast<int>(line_offsets_.size()))
              ? line_offsets_[i + 1]
              : DataSize();

      bool truncated = false;
      std::wstring text = DecodeLine(begin, end, &truncated);
      const size_t text_size = text.size();

      bool has_hit = false;
      if (!doc().results.empty()) {
        auto it = std::lower_bound(
            doc().results.begin(), doc().results.end(), begin,
            [](const DisplayHit& hit, uint64_t value) {
              return hit.offset < value;
            });
        if (it != doc().results.end() && it->offset < end) has_hit = true;
      }

      uint32_t selection_from = 0;
      uint32_t selection_to = 0;
      bool selection_line = false;
      if (has_selection) {
        auto clamp_index = [text_size](uint32_t index) {
          return static_cast<uint32_t>(std::min<size_t>(index, text_size));
        };
        if (line_number > selection_start.line &&
            line_number < selection_end.line) {
          selection_to = static_cast<uint32_t>(text_size);
          selection_line = selection_to > 0;
        } else if (line_number == selection_start.line &&
                   line_number == selection_end.line) {
          selection_from = clamp_index(selection_start.index);
          selection_to = clamp_index(selection_end.index);
          selection_line = selection_to > selection_from;
        } else if (line_number == selection_start.line) {
          selection_from = clamp_index(selection_start.index);
          selection_to = static_cast<uint32_t>(text_size);
          selection_line = selection_to > selection_from;
        } else if (line_number == selection_end.line) {
          selection_to = clamp_index(selection_end.index);
          selection_line = selection_to > 0;
        }
      }

      const bool caret_here = (doc().caret.line == line_number);
      auto ranges = has_hit && !doc().query_wide.empty()
                        ? FindRanges(text, doc().query_wide, case_sensitive_, 12)
                        : std::vector<std::pair<size_t, size_t>>();

      ComPtr<IDWriteTextLayout> layout = GetLineLayout(line_number, text);
      if (layout) {
          const float origin_x = text_x - doc().h_offset;
          if (selection_line) {
            const float x1 =
                canvas_.MeasurePrefix(layout.Get(), selection_from);
            const float x2 =
                canvas_.MeasurePrefix(layout.Get(), selection_to);
            canvas_.FillRect(origin_x + x1, y + 1.0f, origin_x + x2,
                             y + kLineHeight - 1.0f,
                             Color(theme_.accent, 0.26f));
          }
          if (!ranges.empty()) {
            const auto& rects =
                GetMatchRects(line_number, text, ranges);
            for (const auto& rect : rects) {
              canvas_.FillRoundedRect(origin_x + rect.first - 2.0f, y + 1.0f,
                                      origin_x + rect.second + 2.0f,
                                      y + kLineHeight - 2.0f, 3.0f,
                                      Color(theme_.match, theme_.match_alpha));
            }
          }
          canvas_.DrawTextLayout(layout.Get(), origin_x, y,
                                 Color(theme_.text));
          if (caret_here && caret_visible_ && window_active_) {
            const float caret_x = canvas_.MeasurePrefix(
                layout.Get(), std::min<size_t>(doc().caret.index, text_size));
            canvas_.FillRect(origin_x + caret_x, y + 1.0f,
                             origin_x + caret_x + 1.4f,
                             y + kLineHeight - 1.0f, Color(theme_.text));
          }
      } else {
        canvas_.DrawTextW(text, text_x - doc().h_offset, y,
                          text_x - doc().h_offset + text_width, y + kLineHeight,
                          canvas_.mono(), Color(theme_.text));
      }
    }
    canvas_.PopClip();

    DrawScrollbar(width, text_top, text_bottom, 0.0f);
    DrawHorizontalScrollbar(text_top, text_bottom);
  } else {
    DrawWelcome(width, height);
  }

  DrawSearchOverlay(width, height, text_x, text_right);
  DrawStatusBar(width, height, kStatusHeight);
  DrawPalette(width, height);
  if (input_mode_ == InputMode::Extract) DrawExtractPanel(width, height);
  if (input_mode_ == InputMode::Settings) DrawSettingsPanel(width, height);
  if (extract_running_) DrawExtractProgress(width, height);
  DrawResultsPopup(width, height);

  canvas_.EndDraw();
  EndPaint(hwnd_, &paint);

  LARGE_INTEGER paint_end{};
  QueryPerformanceCounter(&paint_end);
  if (perf_freq_.QuadPart > 0) {
    double elapsed = static_cast<double>(paint_end.QuadPart - paint_start.QuadPart) *
                     1000.0 / static_cast<double>(perf_freq_.QuadPart);
    frame_ms_ = elapsed;
  }
}

void Viewer::DrawWelcome(float width, float height) {
  const float area_top = ContentTop();
  const float area_height = height - area_top - kStatusHeight;
  const float base = area_top + area_height * 0.28f;
  canvas_.DrawTextW(L"UEdit", 0.0f, base, width, base + 56.0f,
                    canvas_.title(), Color(theme_.text));
  canvas_.DrawTextW(
      L"Ctrl+O 打开文件 · 拖入大文件 · Ctrl+F 搜索 · Ctrl+G 跳行 · "
      L"F9 设置 · Ctrl+Tab 切换标签 · Ctrl+Shift+P 命令面板",
      0.0f, base + 64.0f, width, base + 92.0f, canvas_.hint(),
      Color(theme_.muted));
  canvas_.DrawTextW(L"1.1.1 · 多标签与搜索结果弹窗", 0.0f, base + 98.0f,
                    width, base + 120.0f, canvas_.center_small(),
                    Color(theme_.muted_dark));
}

void Viewer::DrawScrollbar(float width, float text_top, float text_bottom,
                           float panel_width) {
  const float track_x = width - panel_width - 12.0f;
  const float track_height = std::max(20.0f, text_bottom - text_top);
  scroll_x_ = track_x;
  scroll_top_ = text_top;
  scroll_height_ = track_height;

  uint64_t total = std::max<uint64_t>(EstimatedLines(), 1);
  float visible_fraction =
      std::min(1.0f, static_cast<float>(visible_lines_) /
                          static_cast<float>(total));
  float thumb_height = std::max(28.0f, track_height * visible_fraction);
  if (thumb_height > track_height) thumb_height = track_height;

  float progress = 0.0f;
  if (total > static_cast<uint64_t>(visible_lines_)) {
    progress = static_cast<float>(doc().first_line - 1) /
               static_cast<float>(total - visible_lines_);
  }
  progress = std::max(0.0f, std::min(1.0f, progress));
  float thumb_top = text_top + (track_height - thumb_height) * progress;
  scroll_thumb_top_ = thumb_top;
  scroll_thumb_height_ = thumb_height;

  canvas_.FillRoundedRect(track_x, text_top, track_x + 6.0f,
                          text_top + track_height, 3.0f, Color(theme_.scroll_track));
  canvas_.FillRoundedRect(track_x, thumb_top, track_x + 6.0f,
                          thumb_top + thumb_height, 3.0f,
                          Color(theme_.scroll_thumb));

  if (!doc().results.empty() && doc().file && DataSize() > 0) {
    const int bins = 140;
    int counts[140] = {};
    for (const DisplayHit& hit : doc().results) {
      int bin = static_cast<int>(static_cast<double>(hit.offset) /
                                 static_cast<double>(DataSize()) *
                                 (bins - 1));
      if (bin >= 0 && bin < bins) counts[bin]++;
    }
    for (int b = 0; b < bins; ++b) {
      if (counts[b] == 0) continue;
      float y = text_top + track_height * (static_cast<float>(b) / bins);
      float alpha = std::min(
          0.9f, 0.25f + 0.1f * std::log2(static_cast<float>(counts[b]) + 1.0f));
      canvas_.FillRect(track_x - 5.0f, y, track_x - 1.0f, y + 2.0f,
                       Color(theme_.accent, alpha));
    }
  }
}

void Viewer::DrawHorizontalScrollbar(float text_top, float text_bottom) {
  (void)text_top;
  h_track_width_ = 0.0f;
  if (!doc().file || h_content_width_ <= text_area_width_ + 1.0f) {
    doc().h_offset = 0.0f;
    return;
  }
  const float track_y = text_bottom + 3.0f;
  const float track_height = 5.0f;
  h_track_x_ = text_area_x_;
  h_track_y_ = track_y;
  h_track_width_ = text_area_width_;

  const float max_offset =
      std::max(1.0f, h_content_width_ - text_area_width_);
  const float visible_fraction =
      std::min(1.0f, text_area_width_ / h_content_width_);
  float thumb_width = std::max(28.0f, h_track_width_ * visible_fraction);
  if (thumb_width > h_track_width_) thumb_width = h_track_width_;
  h_thumb_width_ = thumb_width;
  const float progress =
      std::max(0.0f, std::min(1.0f, doc().h_offset / max_offset));
  h_thumb_x_ = h_track_x_ + (h_track_width_ - thumb_width) * progress;

  canvas_.FillRoundedRect(h_track_x_, track_y, h_track_x_ + h_track_width_,
                          track_y + track_height, 2.5f,
                          Color(theme_.scroll_track, 0.55f));
  canvas_.FillRoundedRect(h_thumb_x_, track_y, h_thumb_x_ + thumb_width,
                          track_y + track_height, 2.5f,
                          Color(theme_.scroll_thumb));
}

void Viewer::UpdateHorizontalScrollFromX(float x_dip) {
  if (h_track_width_ <= 0.0f || h_content_width_ <= text_area_width_) return;
  const float span = std::max(1.0f, h_track_width_ - h_thumb_width_);
  const float relative =
      (x_dip - h_track_x_ - h_thumb_width_ * 0.5f) / span;
  const float clamped = std::max(0.0f, std::min(1.0f, relative));
  doc().h_offset =
      clamped * std::max(0.0f, h_content_width_ - text_area_width_);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::DrawSearchOverlay(float width, float height, float text_x,
                               float text_right) {
  (void)width;
  (void)height;
  if (input_mode_ != InputMode::Find && input_mode_ != InputMode::Goto) {
    return;
  }

  const float box_width = 560.0f;
  const float box_height = 42.0f;
  float box_x = text_right - box_width;
  if (box_x < text_x) box_x = text_x;
  const float box_y = ContentTop() + 10.0f;
  canvas_.FillRoundedRect(box_x - 1.5f, box_y - 1.5f,
                          box_x + box_width + 1.5f,
                          box_y + box_height + 1.5f, 9.5f,
                          Color(theme_.accent_text, 0.85f));
  canvas_.FillRoundedRect(box_x, box_y, box_x + box_width, box_y + box_height,
                          8.0f, Color(theme_.overlay));

  const bool find = input_mode_ == InputMode::Find;
  canvas_.DrawTextW(find ? L"搜索" : L"跳到行", box_x + 14.0f, box_y + 12.0f,
                    box_x + 76.0f, box_y + 32.0f, canvas_.ui_small(),
                    Color(theme_.muted));

  float input_right = box_x + box_width - 14.0f;
  if (find) {
    const float chip_y = box_y + 10.0f;
    const float chip_height = 22.0f;
    const float chip_width = 56.0f;
    float chip3_x = box_x + box_width - 14.0f - chip_width;
    float chip2_x = chip3_x - chip_width - 6.0f;
    float chip1_x = chip2_x - chip_width - 6.0f;
    auto chip = [&](int index, float x, const wchar_t* label, bool active) {
      search_chip_rects_[index][0] = x;
      search_chip_rects_[index][1] = chip_y;
      search_chip_rects_[index][2] = x + chip_width;
      search_chip_rects_[index][3] = chip_y + chip_height;
      canvas_.FillRoundedRect(x, chip_y, x + chip_width, chip_y + chip_height,
                              4.0f,
                              Color(active ? theme_.accent : theme_.chip,
                                    active ? 0.9f : 0.55f));
      canvas_.DrawTextW(label, x, chip_y, x + chip_width,
                        chip_y + chip_height, canvas_.center_small(),
                        Color(active ? theme_.on_accent : theme_.muted));
    };
    chip(0, chip1_x, L"Aa", case_sensitive_);
    chip(1, chip2_x, L"全词", whole_word_);
    chip(2, chip3_x, L".*", regex_);
    input_right = chip1_x - 10.0f;
  }

  search_box_rect_[0] = box_x + 80.0f;
  search_box_rect_[1] = box_y + 6.0f;
  search_box_rect_[2] = input_right;
  search_box_rect_[3] = box_y + box_height - 6.0f;

  if (input_text_.empty()) {
    if (caret_visible_) {
      canvas_.DrawTextW(L"|", box_x + 80.0f, box_y + 11.0f, box_x + 88.0f,
                        box_y + 32.0f, canvas_.mono_small(),
                        Color(theme_.text));
    }
    canvas_.DrawTextW(L"输入关键字，↑↓ 浏览历史", box_x + 92.0f,
                      box_y + 13.0f, input_right, box_y + 32.0f,
                      canvas_.ui_small(), Color(theme_.muted));
  } else {
    std::wstring shown_text = input_text_;
    if (shown_text.size() > 64) {
      shown_text = L"…" + shown_text.substr(shown_text.size() - 64);
    }
    std::wstring shown = shown_text + (caret_visible_ ? L"|" : L"");
    canvas_.DrawTextW(shown, box_x + 80.0f, box_y + 11.0f, input_right,
                      box_y + 32.0f, canvas_.mono_small(),
                      Color(theme_.text));
  }
}

float Viewer::ContentTop() const {
  return kTitleBarHeight + (docs_.empty() ? 0.0f : kTabBarHeight);
}

int Viewer::TabAt(float x_dip, float y_dip, bool* close) const {
  *close = false;
  if (docs_.empty()) return -1;
  if (y_dip < kTitleBarHeight || y_dip >= kTitleBarHeight + kTabBarHeight) {
    return -1;
  }
  for (size_t i = 0; i < tab_rects_.size(); ++i) {
    const auto& rect = tab_rects_[i];
    if (x_dip < rect[0] || x_dip > rect[2] || y_dip < rect[1] ||
        y_dip > rect[3]) {
      continue;
    }
    if (i < tab_close_rects_.size()) {
      const auto& close_rect = tab_close_rects_[i];
      *close = close_rect[2] > close_rect[0] && x_dip >= close_rect[0] &&
               x_dip <= close_rect[2] && y_dip >= close_rect[1] &&
               y_dip <= close_rect[3];
    }
    return static_cast<int>(i);
  }
  return -1;
}

void Viewer::DrawTabBar(float width) {
  tab_rects_.clear();
  tab_close_rects_.clear();
  tab_plus_rect_ = {0.0f, 0.0f, 0.0f, 0.0f};
  if (docs_.empty()) return;

  const float top = kTitleBarHeight;
  const float bottom = top + kTabBarHeight;
  canvas_.FillRect(0.0f, top, width, bottom, Color(theme_.titlebar_bg));
  canvas_.FillRect(0.0f, bottom - 1.0f, width, bottom, Color(theme_.border));

  const float start = 8.0f;
  const float gap = 2.0f;
  const float plus_width = 26.0f;
  const float available =
      std::max(120.0f, width - start - plus_width - 24.0f);
  const float tab_width = std::max(
      86.0f, std::min(210.0f, available / static_cast<float>(docs_.size())));
  float x = start;
  for (size_t i = 0; i < docs_.size(); ++i) {
    const float left = x;
    const float right =
        std::min(width - plus_width - 16.0f, x + tab_width - gap);
    if (right <= left) break;
    const bool active = i == active_;
    const bool hovered = static_cast<int>(i) == tab_hover_;
    tab_rects_.push_back({left, top + 4.0f, right, bottom - 1.0f});

    if (active) {
      canvas_.FillRoundedRect(left, top + 5.0f, right, bottom + 4.0f, 6.0f,
                              Color(theme_.window_bg));
      canvas_.FillRect(left + 6.0f, bottom - 2.0f, right - 6.0f,
                       bottom - 0.5f, Color(theme_.accent));
    } else if (hovered) {
      canvas_.FillRoundedRect(left, top + 5.0f, right, bottom - 1.0f, 6.0f,
                              Color(theme_.titlebar_hover,
                                    theme_.dark ? 0.07f : 0.05f));
    }

    std::wstring name = FileNameOf(docs_[i]->path);
    const std::wstring lookup = name;
    const int duplicates = static_cast<int>(std::count_if(
        docs_.begin(), docs_.end(),
        [&](const std::unique_ptr<Document>& other) {
          return _wcsicmp(FileNameOf(other->path).c_str(), lookup.c_str()) == 0;
        }));
    if (duplicates > 1) {
      std::wstring parent = docs_[i]->path;
      const size_t slash = parent.find_last_of(L"\\/");
      if (slash != std::wstring::npos) {
        parent.resize(slash);
        const size_t parent_slash = parent.find_last_of(L"\\/");
        if (parent_slash != std::wstring::npos) {
          parent = parent.substr(parent_slash + 1);
        }
        if (!parent.empty() && parent.find(L':') == std::wstring::npos) {
          name = parent + L"\\" + name;
        }
      }
    }
    if (docs_[i]->dirty) name += L" *";
    const bool show_close = active || hovered;
    const float close_size = 16.0f;
    const float close_left = right - close_size - 6.0f;
    if (show_close) {
      tab_close_rects_.push_back({close_left, top + 12.0f,
                                  close_left + close_size, top + 28.0f});
    } else {
      tab_close_rects_.push_back({0.0f, 0.0f, 0.0f, 0.0f});
    }
    canvas_.DrawTextW(name, left + 12.0f, top + 8.0f,
                      show_close ? close_left - 4.0f : right - 8.0f,
                      bottom - 4.0f, canvas_.ui_small(),
                      Color(active ? theme_.text : theme_.muted));
    if (show_close) {
      const float cx = close_left + close_size * 0.5f;
      const float cy = top + 20.0f;
      const D2D1_COLOR_F color =
          Color(active ? theme_.text : theme_.muted, active ? 0.8f : 0.6f);
      canvas_.DrawLine(cx - 3.5f, cy - 3.5f, cx + 3.5f, cy + 3.5f, 1.1f,
                       color);
      canvas_.DrawLine(cx + 3.5f, cy - 3.5f, cx - 3.5f, cy + 3.5f, 1.1f,
                       color);
    }
    x = right + gap;
  }

  const float plus_x = std::min(width - plus_width - 10.0f, x + 4.0f);
  tab_plus_rect_ = {plus_x, top + 6.0f, plus_x + 22.0f, top + 26.0f};
  canvas_.FillRoundedRect(plus_x, top + 6.0f, plus_x + 22.0f, top + 26.0f,
                          5.0f, Color(theme_.chip, 0.45f));
  const float center_x = plus_x + 11.0f;
  const float center_y = top + 16.0f;
  canvas_.DrawLine(center_x - 4.5f, center_y, center_x + 4.5f, center_y, 1.2f,
                   Color(theme_.muted));
  canvas_.DrawLine(center_x, center_y - 4.5f, center_x, center_y + 4.5f, 1.2f,
                   Color(theme_.muted));
}

Viewer::ResultsGeometry Viewer::ResultsLayout(float width,
                                              float height) const {
  ResultsGeometry geometry;
  const float content_top = ContentTop();
  const float available_height =
      std::max(260.0f, height - content_top - 44.0f);
  geometry.width = std::max(380.0f, std::min(960.0f, width - 100.0f));
  const int stored = static_cast<int>(doc().results.size());
  const int wanted_rows = std::max(5, stored);
  const float needed_height =
      std::max(384.0f, 78.0f + wanted_rows * kPopupRowHeight + 46.0f);
  geometry.height =
      std::min(std::min(640.0f, available_height), needed_height);
  geometry.x = (width - geometry.width) * 0.5f;
  geometry.y = content_top +
               std::max(10.0f, (available_height - geometry.height) * 0.5f);
  geometry.list_x = geometry.x + 18.0f;
  geometry.list_y = geometry.y + 78.0f;
  geometry.action_width = 172.0f;
  geometry.list_right =
      geometry.x + geometry.width - geometry.action_width - 34.0f;
  geometry.list_bottom = geometry.y + geometry.height - 54.0f;
  geometry.action_x =
      geometry.x + geometry.width - geometry.action_width - 18.0f;
  geometry.rows = std::max(
      1, static_cast<int>((geometry.list_bottom - geometry.list_y) /
                          kPopupRowHeight));
  return geometry;
}

void Viewer::EnsureResultsSelectionVisible(int rows) {
  Document& document = doc();
  if (document.results.empty() || rows <= 0) return;
  if (document.results_selected >= document.results.size()) {
    document.results_selected = document.results.size() - 1;
  }
  const int selected = static_cast<int>(document.results_selected);
  if (selected < document.panel_scroll) {
    document.panel_scroll = selected;
  } else if (selected >= document.panel_scroll + rows) {
    document.panel_scroll = selected - rows + 1;
  }
}

void Viewer::UpdateResultsVScroll(float y_dip) {
  Document& document = doc();
  const int count = static_cast<int>(document.results.size());
  const int rows = std::max(1, panel_rows_visible_);
  if (count <= rows) return;
  const float track_height =
      results_vscroll_track_[3] - results_vscroll_track_[1];
  const float thumb_height =
      results_vscroll_thumb_[3] - results_vscroll_thumb_[1];
  const float span = std::max(1.0f, track_height - thumb_height);
  const float relative =
      (y_dip - results_vscroll_grab_ - results_vscroll_track_[1]) / span;
  const float clamped = std::max(0.0f, std::min(1.0f, relative));
  document.panel_scroll =
      static_cast<int>(clamped * static_cast<float>(count - rows) + 0.5f);
  document.panel_scroll =
      std::max(0, std::min(document.panel_scroll, count - rows));
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::UpdateResultsHScroll(float x_dip) {
  Document& document = doc();
  const float track_width =
      results_hscroll_track_[2] - results_hscroll_track_[0];
  const float thumb_width =
      results_hscroll_thumb_[2] - results_hscroll_thumb_[0];
  const float span = std::max(1.0f, track_width - thumb_width);
  const float max_h_offset =
      std::max(0.0f, results_content_width_ - track_width);
  const float relative =
      (x_dip - results_hscroll_grab_ - results_hscroll_track_[0]) / span;
  const float clamped = std::max(0.0f, std::min(1.0f, relative));
  document.results_h_offset = clamped * max_h_offset;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::DrawResultsPopup(float width, float height) {
  Document& document = doc();
  if (!document.results_open || !document.file || extract_running_ ||
      input_mode_ == InputMode::Extract ||
      input_mode_ == InputMode::Settings) {
    return;
  }

  canvas_.FillRect(0.0f, kTitleBarHeight, width, height,
                   Color(0x000000, 0.30f));
  const ResultsGeometry geometry = ResultsLayout(width, height);
  results_box_rect_[0] = geometry.x;
  results_box_rect_[1] = geometry.y;
  results_box_rect_[2] = geometry.x + geometry.width;
  results_box_rect_[3] = geometry.y + geometry.height;

  canvas_.FillRoundedRect(geometry.x - 2.0f, geometry.y + 6.0f,
                          geometry.x + geometry.width + 2.0f,
                          geometry.y + geometry.height + 8.0f, 12.0f,
                          Color(0x000000, 0.22f));
  canvas_.FillRoundedRect(geometry.x, geometry.y,
                          geometry.x + geometry.width,
                          geometry.y + geometry.height, 10.0f,
                          Color(theme_.overlay));
  canvas_.FillRect(geometry.x + 1.0f, geometry.y,
                   geometry.x + geometry.width - 1.0f, geometry.y + 1.0f,
                   Color(theme_.border));

  const std::wstring title = document.query.empty()
                                 ? L"搜索结果"
                                 : L"“" + document.query + L"”";
  canvas_.DrawTextW(title, geometry.x + 20.0f, geometry.y + 13.0f,
                    geometry.x + geometry.width - 52.0f,
                    geometry.y + 40.0f, canvas_.ui_bold(),
                    Color(theme_.text));

  std::wstring stats;
  if (document.search_error) {
    stats = document.search_message;
  } else if (document.searching) {
    double percent = 0.0;
    if (document.file && DataSize() > 0) {
      percent = static_cast<double>(
                    document.search_scanned.load(std::memory_order_relaxed)) /
                static_cast<double>(DataSize()) * 100.0;
    }
    double elapsed = 0.0;
    if (document.search_start_tick != 0) {
      elapsed = static_cast<double>(GetTickCount64() -
                                    document.search_start_tick) /
                1000.0;
    }
    stats = Format(L"搜索中 %.0f%% · %.2f s", percent, elapsed);
  } else if (!document.query.empty()) {
    if (document.total_hits == 0) {
      stats = L"无命中";
    } else {
      stats = Format(L"%llu 个命中 · %.2f s", document.total_hits,
                     document.search_seconds);
      if (document.truncated ||
          document.total_hits > document.results.size()) {
        stats += L" · 已列前 " + std::to_wstring(document.results.size());
      }
      if (document.ansi_case_forced) stats += L" · ANSI 区分大小写";
    }
  } else {
    stats = L"输入关键字后按回车";
  }
  if (regex_ && !document.search_error) stats = L"正则 · " + stats;
  canvas_.DrawTextW(stats, geometry.x + 20.0f, geometry.y + 43.0f,
                    geometry.x + geometry.width - 52.0f,
                    geometry.y + 66.0f, canvas_.ui_small(),
                    Color(document.search_error ? theme_.danger
                                                : theme_.muted));

  results_close_rect_[0] = geometry.x + geometry.width - 38.0f;
  results_close_rect_[1] = geometry.y + 12.0f;
  results_close_rect_[2] = geometry.x + geometry.width - 14.0f;
  results_close_rect_[3] = geometry.y + 36.0f;
  {
    const float cx = (results_close_rect_[0] + results_close_rect_[2]) * 0.5f;
    const float cy = (results_close_rect_[1] + results_close_rect_[3]) * 0.5f;
    const D2D1_COLOR_F color = Color(theme_.muted);
    canvas_.DrawLine(cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f, 1.2f, color);
    canvas_.DrawLine(cx + 5.0f, cy - 5.0f, cx - 5.0f, cy + 5.0f, 1.2f, color);
  }

  canvas_.FillRect(geometry.x, geometry.y + 72.0f,
                   geometry.x + geometry.width, geometry.y + 73.0f,
                   Color(theme_.border));

  if (document.searching) {
    const float fraction =
        document.file && DataSize() > 0
            ? std::min(1.0f,
                       static_cast<float>(document.search_scanned.load(
                                              std::memory_order_relaxed)) /
                           static_cast<float>(DataSize()))
            : 0.0f;
    canvas_.FillRect(geometry.x + 18.0f, geometry.y + 73.0f,
                     geometry.list_right, geometry.y + 75.0f,
                     Color(theme_.scroll_track));
    canvas_.FillRect(geometry.x + 18.0f, geometry.y + 73.0f,
                     geometry.x + 18.0f +
                         (geometry.list_right - geometry.x - 18.0f) * fraction,
                     geometry.y + 75.0f, Color(theme_.accent));
  }

  panel_rows_visible_ = geometry.rows;
  const int count = static_cast<int>(document.results.size());
  const int max_scroll = std::max(0, count - geometry.rows);
  document.panel_scroll =
      std::max(0, std::min(document.panel_scroll, max_scroll));
  if (count > 0 && document.results_selected >= document.results.size()) {
    document.results_selected = document.results.size() - 1;
  }

  const float text_x = geometry.list_x + 78.0f;
  const float view_width =
      std::max(40.0f, geometry.list_right - geometry.list_x);
  results_content_width_ = 0.0f;

  for (int i = 0; i < geometry.rows; ++i) {
    const int index = document.panel_scroll + i;
    if (index >= count) break;
    const float row_y = geometry.list_y + i * kPopupRowHeight;
    const bool selected =
        index == static_cast<int>(document.results_selected);
    if (selected) {
      canvas_.FillRoundedRect(geometry.list_x, row_y, geometry.list_right,
                              row_y + kPopupRowHeight - 2.0f, 5.0f,
                              Color(theme_.accent, 0.20f));
    } else if (index == document.results_hover_row) {
      canvas_.FillRoundedRect(geometry.list_x, row_y, geometry.list_right,
                              row_y + kPopupRowHeight - 2.0f, 5.0f,
                              Color(theme_.row_hover, 0.8f));
    }
    DisplayHit& hit = document.results[static_cast<size_t>(index)];
    canvas_.DrawTextW(std::to_wstring(hit.line), geometry.list_x + 8.0f,
                      row_y + 6.0f, geometry.list_x + 72.0f,
                      row_y + kPopupRowHeight - 5.0f, canvas_.gutter(),
                      Color(theme_.accent_text));
    if (!hit.context_layout ||
        std::abs(hit.context_width - 1000000.0f) > 0.5f) {
      hit.context_layout = canvas_.CreateLayout(
          hit.context, canvas_.mono_small(), 1000000.0f, kPopupRowHeight);
      hit.context_width = 1000000.0f;
      hit.width_measured = false;
      hit.match_measured = false;
    }
    if (hit.context_layout && !hit.width_measured) {
      DWRITE_TEXT_METRICS metrics{};
      hit.measured_width =
          SUCCEEDED(hit.context_layout->GetMetrics(&metrics))
              ? metrics.width
              : view_width;
      hit.width_measured = true;
    }
    results_content_width_ =
        std::max(results_content_width_, 78.0f + hit.measured_width + 12.0f);
  }

  const float max_h_offset =
      std::max(0.0f, results_content_width_ - view_width);
  document.results_h_offset =
      std::max(0.0f, std::min(document.results_h_offset, max_h_offset));

  canvas_.PushClip(text_x, geometry.list_y, geometry.list_right,
                   geometry.list_bottom);
  for (int i = 0; i < geometry.rows; ++i) {
    const int index = document.panel_scroll + i;
    if (index >= count) break;
    const float row_y = geometry.list_y + i * kPopupRowHeight;
    DisplayHit& hit = document.results[static_cast<size_t>(index)];
    if (!hit.context_layout) continue;
    const float origin_x = text_x - document.results_h_offset;
    if (hit.match_len > 0 && hit.match_pos < hit.context.size()) {
      if (!hit.match_measured) {
        uint32_t match_end = hit.match_pos + hit.match_len;
        if (match_end > hit.context.size()) {
          match_end = static_cast<uint32_t>(hit.context.size());
        }
        hit.match_x1 =
            canvas_.MeasurePrefix(hit.context_layout.Get(), hit.match_pos);
        hit.match_x2 =
            canvas_.MeasurePrefix(hit.context_layout.Get(), match_end);
        hit.match_measured = true;
      }
      canvas_.FillRect(origin_x + hit.match_x1 - 1.0f, row_y + 6.0f,
                       origin_x + hit.match_x2 + 1.0f,
                       row_y + kPopupRowHeight - 7.0f,
                       Color(theme_.accent, 0.22f));
    }
    canvas_.DrawTextLayout(hit.context_layout.Get(), origin_x,
                           row_y + 6.0f, Color(theme_.text));
  }
  canvas_.PopClip();

  for (int i = 0; i < 4; ++i) {
    results_vscroll_track_[i] = 0.0f;
    results_vscroll_thumb_[i] = 0.0f;
  }
  if (count > geometry.rows) {
    const float track_x = geometry.list_right + 4.0f;
    const float track_top = geometry.list_y;
    const float track_height = geometry.list_bottom - geometry.list_y;
    const float thumb_height =
        std::max(28.0f, track_height * static_cast<float>(geometry.rows) /
                            static_cast<float>(count));
    const float progress =
        static_cast<float>(document.panel_scroll) /
        static_cast<float>(std::max(1, count - geometry.rows));
    const float thumb_top =
        track_top + (track_height - thumb_height) *
                        std::max(0.0f, std::min(1.0f, progress));
    results_vscroll_track_[0] = geometry.list_right + 1.0f;
    results_vscroll_track_[1] = track_top;
    results_vscroll_track_[2] = geometry.list_right + 14.0f;
    results_vscroll_track_[3] = track_top + track_height;
    results_vscroll_thumb_[0] = track_x;
    results_vscroll_thumb_[1] = thumb_top;
    results_vscroll_thumb_[2] = track_x + 4.0f;
    results_vscroll_thumb_[3] = thumb_top + thumb_height;
    canvas_.FillRoundedRect(track_x, track_top, track_x + 4.0f,
                            track_top + track_height, 2.0f,
                            Color(theme_.scroll_track, 0.75f));
    canvas_.FillRoundedRect(track_x, thumb_top, track_x + 4.0f,
                            thumb_top + thumb_height, 2.0f,
                            Color(theme_.scroll_thumb));
  }

  for (int i = 0; i < 4; ++i) {
    results_hscroll_track_[i] = 0.0f;
    results_hscroll_thumb_[i] = 0.0f;
  }
  if (max_h_offset > 0.0f) {
    const float track_y = geometry.list_bottom + 5.0f;
    const float track_width = geometry.list_right - geometry.list_x;
    const float thumb_width =
        std::max(28.0f, track_width * view_width /
                            std::max(1.0f, results_content_width_));
    const float progress = document.results_h_offset / max_h_offset;
    const float thumb_x =
        geometry.list_x +
        (track_width - thumb_width) *
            std::max(0.0f, std::min(1.0f, progress));
    results_hscroll_track_[0] = geometry.list_x;
    results_hscroll_track_[1] = track_y - 3.0f;
    results_hscroll_track_[2] = geometry.list_right;
    results_hscroll_track_[3] = track_y + 9.0f;
    results_hscroll_thumb_[0] = thumb_x;
    results_hscroll_thumb_[1] = track_y;
    results_hscroll_thumb_[2] = thumb_x + thumb_width;
    results_hscroll_thumb_[3] = track_y + 5.0f;
    canvas_.FillRoundedRect(geometry.list_x, track_y, geometry.list_right,
                            track_y + 5.0f, 2.5f,
                            Color(theme_.scroll_track, 0.75f));
    canvas_.FillRoundedRect(thumb_x, track_y, thumb_x + thumb_width,
                            track_y + 5.0f, 2.5f,
                            Color(theme_.scroll_thumb));
  }

  static const wchar_t* kActions[6] = {L"保存并打开", L"导出选项…",
                                       L"转到所选",   L"复制命中",
                                       L"重新搜索",   L"关闭"};
  const bool ready = count > 0 && !document.searching;
  const bool enabled[6] = {ready, ready, ready, ready,
                           !document.query.empty() && !document.searching,
                           true};
  for (int i = 0; i < 6; ++i) {
    const float button_x = geometry.action_x;
    const float button_y = geometry.list_y + i * 46.0f;
    results_button_rects_[i][0] = button_x;
    results_button_rects_[i][1] = button_y;
    results_button_rects_[i][2] = button_x + geometry.action_width;
    results_button_rects_[i][3] = button_y + 38.0f;

    const bool primary = i == 0;
    const bool hovered = document.results_hover_button == i;
    const D2D1_COLOR_F background =
        primary ? (enabled[i] ? Color(theme_.accent)
                              : Color(theme_.accent, 0.30f))
                : Color(theme_.chip, hovered ? 0.85f : 0.50f);
    canvas_.FillRoundedRect(button_x, button_y,
                            button_x + geometry.action_width,
                            button_y + 38.0f, 7.0f, background);
    canvas_.DrawTextW(kActions[i], button_x, button_y,
                      button_x + geometry.action_width, button_y + 38.0f,
                      canvas_.center_small(),
                      Color(enabled[i]
                                ? (primary ? theme_.on_accent : theme_.text)
                                : theme_.muted_dark));
  }

  if (geometry.height >= 380.0f) {
    canvas_.DrawTextW(L"↑↓ 选择 · Enter 跳转 · F3 下一个 · Esc 关闭",
                      geometry.x + 20.0f,
                      geometry.y + geometry.height - 28.0f,
                      geometry.x + geometry.width - 20.0f,
                      geometry.y + geometry.height - 6.0f,
                      canvas_.ui_small(), Color(theme_.muted));
  }
}

void Viewer::HandleResultsAction(int action) {
  Document& document = doc();
  switch (action) {
    case 0:
      SaveHitsAndOpen();
      return;
    case 1:
      document.results_open = false;
      ShowExtractPanel();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    case 2:
      if (!document.results.empty() &&
          document.results_selected < document.results.size()) {
        const DisplayHit& hit = document.results[document.results_selected];
        document.current_hit = document.results_selected;
        document.results_open = false;
        JumpToOffset(hit.offset);
      }
      return;
    case 3:
      CopyHitsToClipboard();
      return;
    case 4:
      if (!document.query.empty()) StartSearch();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    case 5:
      document.results_open = false;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    default:
      return;
  }
}

std::wstring Viewer::BuildHitsPath(const std::wstring& source) const {
  std::wstring directory = source;
  const size_t slash = directory.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    directory.resize(slash);
  } else {
    directory = L".";
  }
  std::wstring base = FileNameOf(source);
  const size_t dot = base.find_last_of(L'.');
  if (dot != std::wstring::npos && dot > 0) base.resize(dot);
  for (int attempt = 1; attempt < 1000; ++attempt) {
    std::wstring name = base + L".hits";
    if (attempt > 1) {
      name += L" (" + std::to_wstring(attempt) + L")";
    }
    name += L".txt";
    const std::wstring candidate = directory + L"\\" + name;
    if (GetFileAttributesW(candidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
      return candidate;
    }
  }
  return directory + L"\\" + base + L".hits.txt";
}

void Viewer::SaveHitsAndOpen() {
  Document& document = doc();
  if (!document.file || document.query.empty() ||
      document.total_hits == 0 || document.searching || extract_running_) {
    SetNotice(document.searching ? L"请等待搜索完成" : L"没有可保存的命中");
    return;
  }
  const std::wstring output = BuildHitsPath(document.path);
  document.results_open = false;
  extract_mode_ = ExtractMode::WholeLines;
  extract_context_ = 0;
  extract_dedupe_ = false;
  extract_cut_ = false;
  extract_backup_ = false;
  extract_open_after_ = true;
  BeginExtraction(output);
  if (!extract_running_) extract_open_after_ = false;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::CopyHitsToClipboard() {
  Document& document = doc();
  if (document.results.empty()) return;
  std::wstring text;
  uint64_t last_line = 0;
  size_t copied = 0;
  bool first = true;
  bool capped = false;
  const size_t cap = 4ull << 20;
  for (const DisplayHit& hit : document.results) {
    if (!first && hit.line == last_line) continue;
    first = false;
    last_line = hit.line;
    std::wstring line = LineText(hit.line, 8192, false);
    if (text.size() + line.size() + 2 > cap) {
      capped = true;
      break;
    }
    text += line;
    text += L"\r\n";
    ++copied;
  }
  if (text.empty()) return;
  if (!SetClipboardText(hwnd_, text)) {
    SetNotice(L"复制失败");
    return;
  }
  SetNotice(L"已复制 " + std::to_wstring(copied) + L" 条命中行" +
            (capped ? L"（已达上限）" : L""));
}

void Viewer::DrawStatusBar(float width, float height, float status_height) {
  const float top = height - status_height;
  canvas_.FillRect(0.0f, top, width, height, Color(theme_.status_bg));
  std::wstring text = EncodingName(doc().encoding);
  if (doc().file) {
    if (!doc().eol_name.empty()) text += L" · " + doc().eol_name;
    if (doc().binary) text += L" · 二进制";
    if (doc().dirty) text += L" · 已修改";
    if (doc().read_only) {
      text += L" · 只读";
    } else if (!doc().editable && !doc().binary) {
      text += L" · 只读模式";
    }
    if (doc().external_changed) text += L" · 外部修改（F5）";
    text += L"    行 " + std::to_wstring(doc().caret.line) + L", 列 " +
            std::to_wstring(static_cast<uint64_t>(doc().caret.index) + 1);
    if (HasSelection()) {
      const TextPos start = SelectionStart();
      const TextPos end = SelectionEnd();
      text += L"    选区 " + std::to_wstring(start.line) + L":" +
              std::to_wstring(static_cast<uint64_t>(start.index) + 1) +
              L"–" + std::to_wstring(end.line) + L":" +
              std::to_wstring(static_cast<uint64_t>(end.index) + 1);
    }
    if (IndexComplete()) {
      text += L" / " + std::to_wstring(TotalLines());
    } else {
      text += L" / ~" + std::to_wstring(EstimatedLines());
      text += Format(L"    索引 %d%%",
                     static_cast<int>(IndexProgress() * 100.0));
    }
    if (doc().searching && DataSize() > 0) {
      double percent = static_cast<double>(
                           doc().search_scanned.load(std::memory_order_relaxed)) /
                       static_cast<double>(DataSize()) * 100.0;
      text += Format(L"    搜索 %d%%", static_cast<int>(percent));
    }
    const int zoom_percent =
        static_cast<int>(zoom_ * 100.0f + 0.5f);
    if (zoom_percent != 100) {
      text += Format(L"    %d%%", zoom_percent);
    }
    text += Format(L"    帧 %.1f ms    提交 %.0f MB", frame_ms_, memory_mb_);
    if (!notice_.empty() && GetTickCount64() < notice_until_) {
      text += L"    " + notice_;
    }
  } else {
    text += L"    未打开文件";
  }
  canvas_.DrawTextW(text, 10.0f, top + 5.0f, width - 10.0f,
                    top + status_height - 2.0f, canvas_.ui_small(),
                    Color(theme_.muted));
}

void Viewer::OnWheel(int delta, bool shift, int screen_x, int screen_y) {
  if (delta == 0) return;
  if (extract_running_ || input_mode_ == InputMode::Extract ||
      input_mode_ == InputMode::Settings) {
    return;
  }
  POINT point{screen_x, screen_y};
  ScreenToClient(hwnd_, &point);
  const float x = ToDipX(point.x);
  const float y = ToDipY(point.y);
  int notches = delta / WHEEL_DELTA;
  if (notches == 0) notches = delta > 0 ? 1 : -1;

  if (doc().results_open && doc().file &&
      input_mode_ == InputMode::None) {
    const ResultsGeometry geometry =
        ResultsLayout(ClientWidthDip(), ClientHeightDip());
    const bool inside =
        x >= geometry.x && x <= geometry.x + geometry.width &&
        y >= geometry.y && y <= geometry.y + geometry.height;
    if (inside) {
      if (shift) {
        const float view_width =
            std::max(40.0f, geometry.list_right - geometry.list_x);
        const float max_h =
            std::max(0.0f, results_content_width_ - view_width);
        if (max_h > 0.0f) {
          doc().results_h_offset -= notches * 60.0f;
          doc().results_h_offset = std::max(
              0.0f, std::min(doc().results_h_offset, max_h));
        }
      } else {
        doc().panel_scroll -= notches * 3;
        const int max_scroll =
            std::max(0, static_cast<int>(doc().results.size()) -
                            panel_rows_visible_);
        doc().panel_scroll =
            std::max(0, std::min(doc().panel_scroll, max_scroll));
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return;
  }

  if (!doc().file) return;
  if (shift && h_content_width_ > text_area_width_) {
    doc().h_offset -= notches * 60.0f;
    const float max_offset =
        std::max(0.0f, h_content_width_ - text_area_width_);
    doc().h_offset = std::max(0.0f, std::min(doc().h_offset, max_offset));
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  uint64_t total = std::max<uint64_t>(EstimatedLines(), 1);
  uint64_t max_first = total > static_cast<uint64_t>(visible_lines_)
                           ? total - static_cast<uint64_t>(visible_lines_) + 1
                           : 1;
  int64_t target = static_cast<int64_t>(doc().first_line) - notches * 3;
  if (target < 1) target = 1;
  if (target > static_cast<int64_t>(max_first)) target = max_first;
  doc().first_line = static_cast<uint64_t>(target);
  line_offsets_valid_ = false;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::OnKeyDown(WPARAM key) {
  const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

  if (extract_running_) {
    if (key == VK_ESCAPE && extract_cancel_) extract_cancel_->Cancel();
    return;
  }
  if (input_mode_ == InputMode::Extract) {
    switch (key) {
      case VK_ESCAPE:
        input_mode_ = InputMode::None;
        break;
      case VK_UP:
        extract_row_ = (extract_row_ + 5) % 6;
        break;
      case VK_DOWN:
        extract_row_ = (extract_row_ + 1) % 6;
        break;
      case VK_LEFT:
        AdjustExtractOption(-1);
        return;
      case VK_RIGHT:
      case VK_SPACE:
        AdjustExtractOption(1);
        return;
      case VK_RETURN:
        StartExtraction();
        return;
      default:
        return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  if (input_mode_ == InputMode::Settings) {
    switch (key) {
      case VK_ESCAPE:
        input_mode_ = InputMode::None;
        break;
      case VK_UP:
        settings_row_ = (settings_row_ + 7) % 8;
        break;
      case VK_DOWN:
        settings_row_ = (settings_row_ + 1) % 8;
        break;
      case VK_LEFT:
        AdjustSetting(-1);
        return;
      case VK_RIGHT:
      case VK_SPACE:
        AdjustSetting(1);
        return;
      case VK_RETURN:
        AdjustSetting(1);
        return;
      default:
        return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  if (ctrl && shift && key == 'F') {
    ShowExtractPanel();
    return;
  }

  if (ctrl && shift && key == 'P') {
    ShowCommandPalette();
    return;
  }
  if (ctrl && shift && key == 'L') {
    CycleThemeMode();
    return;
  }

  if (input_mode_ == InputMode::Palette) {
    switch (key) {
      case VK_ESCAPE:
        input_mode_ = InputMode::None;
        break;
      case VK_RETURN:
        RunPaletteSelection();
        break;
      case VK_UP:
        if (palette_selected_ > 0) --palette_selected_;
        break;
      case VK_DOWN:
        if (palette_selected_ + 1 <
            static_cast<int>(palette_matches_.size())) {
          ++palette_selected_;
        }
        break;
      case VK_BACK:
        if (!palette_input_.empty()) {
          palette_input_.pop_back();
          RebuildPaletteMatches();
        }
        break;
      default:
        return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  if (doc().results_open && input_mode_ == InputMode::None) {
    if (key == VK_ESCAPE) {
      doc().results_open = false;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (key == VK_UP || key == VK_DOWN) {
      if (!doc().results.empty()) {
        int selected = static_cast<int>(doc().results_selected);
        selected += key == VK_UP ? -1 : 1;
        selected = std::max(
            0, std::min(selected,
                        static_cast<int>(doc().results.size()) - 1));
        doc().results_selected = static_cast<size_t>(selected);
        EnsureResultsSelectionVisible(panel_rows_visible_);
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return;
    }
    if (key == VK_PRIOR || key == VK_NEXT) {
      if (!doc().results.empty()) {
        int selected = static_cast<int>(doc().results_selected);
        selected += (key == VK_PRIOR ? -1 : 1) *
                    std::max(1, panel_rows_visible_ - 1);
        selected = std::max(
            0, std::min(selected,
                        static_cast<int>(doc().results.size()) - 1));
        doc().results_selected = static_cast<size_t>(selected);
        EnsureResultsSelectionVisible(panel_rows_visible_);
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return;
    }
    if (key == VK_RETURN) {
      if (!doc().results.empty()) HandleResultsAction(2);
      return;
    }
    if (!ctrl && (key == VK_BACK || key == VK_DELETE || key == VK_LEFT ||
                  key == VK_RIGHT || key == VK_HOME || key == VK_END ||
                  key == VK_TAB)) {
      return;
    }
  }

  if (ctrl) {
    if (key == VK_TAB) {
      CycleTab(shift ? -1 : 1);
      return;
    }
    if (input_mode_ == InputMode::None) {
      if (key == 'S') {
        SaveDocument();
        return;
      }
      if (key == 'V') {
        PasteClipboard();
        return;
      }
      if (key == 'X') {
        CutSelection();
        return;
      }
      if (key == 'Z') {
        UndoEdit();
        return;
      }
      if (key == 'Y') {
        RedoEdit();
        return;
      }
    }
    if (key == 'V' && input_mode_ != InputMode::None) {
      PasteIntoInput();
      return;
    }
    if (key == 'C') {
      CopySelection();
      return;
    }
    if (key == 'A') {
      SelectAll();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (key == VK_LEFT) {
      MoveCaret(-1, 0, shift, true);
      return;
    }
    if (key == VK_RIGHT) {
      MoveCaret(1, 0, shift, true);
      return;
    }
    if (key == VK_HOME) {
      MoveCaretToDocumentEdge(false, shift);
      return;
    }
    if (key == VK_END) {
      MoveCaretToDocumentEdge(true, shift);
      return;
    }
    if (key == 'F') {
      doc().results_open = false;
      input_mode_ = InputMode::Find;
      input_text_ = doc().query;
      history_index_ = -1;
      ResetInputCaret();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (key == 'G') {
      doc().results_open = false;
      input_mode_ = InputMode::Goto;
      input_text_.clear();
      ResetInputCaret();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (key == 'O') {
      OpenFileDialog();
      return;
    }
    if (key == 'W') {
      CloseActiveDocument();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (key == VK_OEM_COMMA) {
      ShowSettingsPanel();
      return;
    }
    if (key == VK_OEM_PLUS || key == VK_ADD || key == '=') {
      SetZoomPercent(static_cast<int>(zoom_ * 100.0f + 0.5f) + 10);
      return;
    }
    if (key == VK_OEM_MINUS || key == VK_SUBTRACT) {
      SetZoomPercent(static_cast<int>(zoom_ * 100.0f + 0.5f) - 10);
      return;
    }
    if (key == '0') {
      SetZoomPercent(100);
      return;
    }
  }

  if (input_mode_ == InputMode::None) {
    switch (key) {
      case VK_LEFT:
        MoveCaret(-1, 0, shift, false);
        return;
      case VK_RIGHT:
        MoveCaret(1, 0, shift, false);
        return;
      case VK_UP:
        MoveCaret(0, -1, shift, false);
        return;
      case VK_DOWN:
        MoveCaret(0, 1, shift, false);
        return;
      case VK_PRIOR:
        MoveCaret(0, -(visible_lines_ > 1 ? visible_lines_ - 1 : 1), shift,
                  false);
        return;
      case VK_NEXT:
        MoveCaret(0, (visible_lines_ > 1 ? visible_lines_ - 1 : 1), shift,
                  false);
        return;
      case VK_HOME:
        SetCaret(TextPos{doc().caret.line, 0}, shift);
        return;
      case VK_END:
        SetCaret(TextPos{doc().caret.line,
                         static_cast<uint32_t>(
                             LineText(doc().caret.line, 8192, true).size())},
                 shift);
        return;
      default:
        break;
    }
  }

  if (input_mode_ == InputMode::Find && alt) {
    if (key == 'C') {
      case_sensitive_ = !case_sensitive_;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (key == 'W') {
      whole_word_ = !whole_word_;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (key == 'R') {
      regex_ = !regex_;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
  }

  if (input_mode_ == InputMode::Find) {
    if (key == VK_UP) {
      CycleSearchHistory(1);
      return;
    }
    if (key == VK_DOWN) {
      CycleSearchHistory(-1);
      return;
    }
  }

  switch (key) {
    case VK_ESCAPE:
      if (input_mode_ != InputMode::None) {
        input_mode_ = InputMode::None;
      } else if (HasSelection()) {
        ClearSelection();
      } else {
        CancelSearch();
        doc().query.clear();
        doc().query_wide.clear();
        doc().results.clear();
        doc().search_error = false;
        doc().search_message.clear();
        doc().total_hits = 0;
        doc().truncated = false;
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    case VK_RETURN:
      if (input_mode_ == InputMode::Goto) {
        if (!input_text_.empty()) {
          uint64_t line = _wcstoui64(input_text_.c_str(), nullptr, 10);
          if (line > 0) {
            JumpToLine(line);
            SetCaret(TextPos{line, 0}, false);
          }
        }
        input_mode_ = InputMode::None;
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else if (input_mode_ == InputMode::Find) {
        DebugLog("enter pressed: input_bytes=" +
                 std::to_string(input_text_.size()) + " query=\"" +
                 WideToUtf8(input_text_) + "\"\n");
        if (input_text_.empty()) {
          input_mode_ = InputMode::None;
        } else {
          doc().query = input_text_;
          doc().query_wide = doc().query;
          AddSearchHistory(doc().query);
          history_index_ = -1;
          input_mode_ = InputMode::None;
          StartSearch();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      } else if (input_mode_ == InputMode::None) {
        if (doc().editable) InsertNewline();
      }
      return;
    case VK_BACK:
      if (input_mode_ == InputMode::None) {
        if (doc().editable) DeleteBackward();
        return;
      }
      if (input_mode_ != InputMode::None && !input_text_.empty()) {
        input_text_.pop_back();
        ResetInputCaret();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return;
    case VK_INSERT:
      if (shift) {
        if (input_mode_ == InputMode::None) {
          PasteClipboard();
        } else {
          PasteIntoInput();
        }
      }
      return;
    case VK_DELETE:
      if (input_mode_ == InputMode::None && doc().editable) DeleteForward();
      return;
    case VK_TAB:
      if (input_mode_ == InputMode::None && doc().editable) {
        InsertTextAtCaret(L"\t", true);
      }
      return;
    case VK_F3:
      if (doc().results_open) {
        doc().results_open = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      NextHit();
      return;
    case VK_F4:
      if (doc().file && !doc().query.empty()) {
        doc().results_open = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return;
    case VK_F5:
      ReloadFile();
      return;
    case VK_F9:
      ShowSettingsPanel();
      return;
    default:
      break;
  }
}

void Viewer::OnChar(wchar_t ch) {
  if (extract_running_ || input_mode_ == InputMode::Extract ||
      input_mode_ == InputMode::Settings) {
    return;
  }
  if (doc().results_open && input_mode_ == InputMode::None) return;
  if (ch < 32) return;
  if (input_mode_ == InputMode::Palette) {
    palette_input_.push_back(ch);
    RebuildPaletteMatches();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (input_mode_ == InputMode::None) {
    if (doc().editable && !extract_running_) {
      InsertTextAtCaret(std::wstring(1, ch), true);
    }
    return;
  }
  if (input_mode_ == InputMode::Goto) {
    if (ch >= L'0' && ch <= L'9') input_text_.push_back(ch);
  } else {
    input_text_.push_back(ch);
    history_index_ = -1;
  }
  ResetInputCaret();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::OnLButtonDown(int client_x, int client_y) {
  const float x = ToDipX(client_x);
  const float y = ToDipY(client_y);
  const float width = ClientWidthDip();

  if (extract_running_) return;
  if (doc().results_open && doc().file &&
      input_mode_ == InputMode::None) {
    const ResultsGeometry geometry =
        ResultsLayout(width, ClientHeightDip());
    const bool inside =
        x >= geometry.x && x <= geometry.x + geometry.width &&
        y >= geometry.y && y <= geometry.y + geometry.height;
    if (!inside) {
      doc().results_open = false;
      bool on_close = false;
      const int tab = TabAt(x, y, &on_close);
      if (tab >= 0) {
        if (on_close) {
          CloseTabAt(static_cast<size_t>(tab));
        } else {
          ActivateDocument(static_cast<size_t>(tab));
        }
      } else if (tab_plus_rect_[2] > tab_plus_rect_[0] &&
                 x >= tab_plus_rect_[0] && x <= tab_plus_rect_[2] &&
                 y >= tab_plus_rect_[1] && y <= tab_plus_rect_[3]) {
        OpenFileDialog();
      } else {
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return;
    }
    if (results_close_rect_[2] > results_close_rect_[0] &&
        x >= results_close_rect_[0] && x <= results_close_rect_[2] &&
        y >= results_close_rect_[1] && y <= results_close_rect_[3]) {
      doc().results_open = false;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    for (int i = 0; i < 6; ++i) {
      if (results_button_rects_[i][2] <= results_button_rects_[i][0]) continue;
      if (x >= results_button_rects_[i][0] &&
          x <= results_button_rects_[i][2] &&
          y >= results_button_rects_[i][1] &&
          y <= results_button_rects_[i][3]) {
        HandleResultsAction(i);
        return;
      }
    }
    const auto in_rect = [&](const float* rect) {
      return rect[2] > rect[0] && rect[3] > rect[1] && x >= rect[0] &&
             x <= rect[2] && y >= rect[1] && y <= rect[3];
    };
    if (in_rect(results_vscroll_track_)) {
      results_vscroll_dragging_ = true;
      SetCapture(hwnd_);
      const bool on_thumb = in_rect(results_vscroll_thumb_);
      results_vscroll_grab_ =
          on_thumb ? y - results_vscroll_thumb_[1]
                   : (results_vscroll_thumb_[3] -
                      results_vscroll_thumb_[1]) * 0.5f;
      UpdateResultsVScroll(y);
      return;
    }
    if (in_rect(results_hscroll_track_)) {
      results_hscroll_dragging_ = true;
      SetCapture(hwnd_);
      const bool on_thumb = in_rect(results_hscroll_thumb_);
      results_hscroll_grab_ =
          on_thumb ? x - results_hscroll_thumb_[0]
                   : (results_hscroll_thumb_[2] -
                      results_hscroll_thumb_[0]) * 0.5f;
      UpdateResultsHScroll(x);
      return;
    }
    if (x >= geometry.list_x && x <= geometry.list_right &&
        y >= geometry.list_y && y <= geometry.list_bottom) {
      results_list_dragging_ = true;
      results_drag_moved_ = false;
      results_drag_start_x_ = x;
      results_drag_start_y_ = y;
      results_drag_start_h_ = doc().results_h_offset;
      results_drag_start_scroll_ = doc().panel_scroll;
      SetCapture(hwnd_);
      const int row =
          static_cast<int>((y - geometry.list_y) / kPopupRowHeight);
      const int index = doc().panel_scroll + row;
      if (index >= 0 && index < static_cast<int>(doc().results.size())) {
        doc().results_selected = static_cast<size_t>(index);
        doc().current_hit = static_cast<size_t>(index);
        const ULONGLONG now = GetTickCount64();
        const bool double_click = now - results_last_click_tick_ <=
                                      GetDoubleClickTime() &&
                                  results_last_click_row_ == index;
        results_last_click_tick_ = now;
        results_last_click_row_ = index;
        if (double_click) {
          HandleResultsAction(2);
        } else {
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
      }
    }
    return;
  }

  if (!docs_.empty()) {
    bool on_close = false;
    const int tab = TabAt(x, y, &on_close);
    if (tab >= 0) {
      if (on_close) {
        CloseTabAt(static_cast<size_t>(tab));
      } else {
        ActivateDocument(static_cast<size_t>(tab));
      }
      return;
    }
    if (y >= kTitleBarHeight && y < kTitleBarHeight + kTabBarHeight) {
      if (tab_plus_rect_[2] > tab_plus_rect_[0] &&
          x >= tab_plus_rect_[0] && x <= tab_plus_rect_[2] &&
          y >= tab_plus_rect_[1] && y <= tab_plus_rect_[3]) {
        OpenFileDialog();
      }
      return;
    }
  }

  if (input_mode_ == InputMode::Settings) {
    for (int i = 0; i < 8; ++i) {
      if (settings_row_rects_[i][2] <= settings_row_rects_[i][0]) continue;
      if (x >= settings_row_rects_[i][0] && x <= settings_row_rects_[i][2] &&
          y >= settings_row_rects_[i][1] && y <= settings_row_rects_[i][3]) {
        if (i == 7) {
          input_mode_ = InputMode::None;
        } else if (i == settings_row_) {
          AdjustSetting(1);
        } else {
          settings_row_ = i;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
    }
    input_mode_ = InputMode::None;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  if (input_mode_ == InputMode::Extract) {
    for (int i = 0; i < 6; ++i) {
      if (extract_row_rects_[i][2] <= extract_row_rects_[i][0]) continue;
      if (x >= extract_row_rects_[i][0] && x <= extract_row_rects_[i][2] &&
          y >= extract_row_rects_[i][1] && y <= extract_row_rects_[i][3]) {
        if (i == 5) {
          StartExtraction();
        } else if (i == extract_row_) {
          AdjustExtractOption(1);
        } else {
          extract_row_ = i;
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
      }
    }
    input_mode_ = InputMode::None;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  if (input_mode_ == InputMode::Palette) {
    const PaletteGeometry geometry =
        PaletteLayout(width, ClientHeightDip());
    const bool inside = x >= geometry.x && x <= geometry.x + geometry.width &&
                        y >= geometry.y && y <= geometry.y + geometry.height;
    if (!inside) {
      input_mode_ = InputMode::None;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    const float list_top = geometry.y + 50.0f;
    if (y >= list_top) {
      const int row =
          static_cast<int>((y - list_top) / geometry.row_height);
      if (row >= 0 && row < geometry.rows) {
        palette_selected_ = row;
        RunPaletteSelection();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
    }
    return;
  }

  if (input_mode_ == InputMode::Find) {
    for (int i = 0; i < 3; ++i) {
      if (search_chip_rects_[i][2] > search_chip_rects_[i][0] &&
          x >= search_chip_rects_[i][0] && x <= search_chip_rects_[i][2] &&
          y >= search_chip_rects_[i][1] && y <= search_chip_rects_[i][3]) {
        if (i == 0) {
          case_sensitive_ = !case_sensitive_;
        } else if (i == 1) {
          whole_word_ = !whole_word_;
        } else {
          regex_ = !regex_;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
    }
  }

  if (doc().file && h_track_width_ > 0.0f && y >= h_track_y_ - 3.0f &&
      y <= h_track_y_ + 8.0f && x >= h_track_x_ &&
      x <= h_track_x_ + h_track_width_) {
    h_dragging_ = true;
    SetCapture(hwnd_);
    UpdateHorizontalScrollFromX(x);
    return;
  }

  if (doc().file && x >= text_area_x_ && x <= text_area_x_ + text_area_width_ &&
      y >= text_area_y_ && y <= text_area_bottom_) {
    const ULONGLONG now = GetTickCount64();
    if (now - last_click_tick_ <= GetDoubleClickTime() &&
        std::abs(client_x - last_click_x_) <= 4 &&
        std::abs(client_y - last_click_y_) <= 4) {
      ++click_count_;
    } else {
      click_count_ = 1;
    }
    last_click_tick_ = now;
    last_click_x_ = client_x;
    last_click_y_ = client_y;

    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const TextPos position = HitTestPosition(x, y);
    if (click_count_ >= 3) {
      const std::wstring text = LineText(position.line, 8192, true);
      doc().anchor = TextPos{position.line, 0};
      doc().caret = TextPos{position.line,
                       static_cast<uint32_t>(text.size())};
      dragging_selection_ = false;
    } else if (click_count_ == 2) {
      SelectWordAt(position);
      dragging_selection_ = false;
    } else {
      if (shift) {
        doc().caret = position;
      } else {
        doc().anchor = position;
        doc().caret = position;
      }
      dragging_selection_ = true;
      SetCapture(hwnd_);
    }
    caret_visible_ = true;
    caret_blink_tick_ = now;
    EnsureCaretVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }

  if (doc().file && x >= scroll_x_ - 6.0f && x <= scroll_x_ + 10.0f &&
      y >= scroll_top_ && y <= scroll_top_ + scroll_height_) {
    scroll_dragging_ = true;
    SetCapture(hwnd_);
    JumpToLineFromScroll(y);
    return;
  }

  SetFocus(hwnd_);
}

void Viewer::OnLButtonUp() {
  bool captured = false;
  if (scroll_dragging_) {
    scroll_dragging_ = false;
    captured = true;
  }
  if (h_dragging_) {
    h_dragging_ = false;
    captured = true;
  }
  if (results_vscroll_dragging_) {
    results_vscroll_dragging_ = false;
    captured = true;
  }
  if (results_hscroll_dragging_) {
    results_hscroll_dragging_ = false;
    captured = true;
  }
  if (results_list_dragging_) {
    results_list_dragging_ = false;
    results_drag_moved_ = false;
    captured = true;
  }
  if (dragging_selection_) {
    dragging_selection_ = false;
    captured = true;
  }
  if (captured) ReleaseCapture();
}

void Viewer::OnMouseMove(int client_x, int client_y) {
  const float x = ToDipX(client_x);
  const float y = ToDipY(client_y);
  if (results_vscroll_dragging_) {
    UpdateResultsVScroll(y);
    return;
  }
  if (results_hscroll_dragging_) {
    UpdateResultsHScroll(x);
    return;
  }
  if (results_list_dragging_ && doc().results_open) {
    const float dx = x - results_drag_start_x_;
    const float dy = y - results_drag_start_y_;
    if (std::abs(dx) > 4.0f || std::abs(dy) > 4.0f) {
      results_drag_moved_ = true;
      const ResultsGeometry geometry =
          ResultsLayout(ClientWidthDip(), ClientHeightDip());
      const float view_width =
          std::max(40.0f, geometry.list_right - geometry.list_x);
      const float max_h =
          std::max(0.0f, results_content_width_ - view_width);
      doc().results_h_offset =
          std::max(0.0f, std::min(results_drag_start_h_ - dx, max_h));
      const int count = static_cast<int>(doc().results.size());
      const int rows = std::max(1, panel_rows_visible_);
      const int max_scroll = std::max(0, count - rows);
      int scroll = results_drag_start_scroll_ -
                   static_cast<int>(dy / kPopupRowHeight +
                                    (dy >= 0.0f ? 0.5f : -0.5f));
      doc().panel_scroll = std::max(0, std::min(scroll, max_scroll));
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return;
  }
  if (!docs_.empty() && y >= kTitleBarHeight &&
      y < kTitleBarHeight + kTabBarHeight) {
    bool on_close = false;
    const int tab = TabAt(x, y, &on_close);
    if (tab != tab_hover_) {
      tab_hover_ = tab;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  } else if (tab_hover_ != -1) {
    tab_hover_ = -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  Document& document = doc();
  if (document.results_open && document.file &&
      input_mode_ == InputMode::None && !extract_running_) {
    const ResultsGeometry geometry =
        ResultsLayout(ClientWidthDip(), ClientHeightDip());
    int button = -1;
    int row = -1;
    for (int i = 0; i < 6; ++i) {
      if (results_button_rects_[i][2] <= results_button_rects_[i][0]) continue;
      if (x >= results_button_rects_[i][0] &&
          x <= results_button_rects_[i][2] &&
          y >= results_button_rects_[i][1] &&
          y <= results_button_rects_[i][3]) {
        button = i;
        break;
      }
    }
    if (button < 0 && x >= geometry.list_x && x <= geometry.list_right &&
        y >= geometry.list_y && y <= geometry.list_bottom) {
      const int index = document.panel_scroll +
                        static_cast<int>((y - geometry.list_y) /
                                         kPopupRowHeight);
      if (index >= 0 && index < static_cast<int>(document.results.size())) {
        row = index;
      }
    }
    if (button != document.results_hover_button ||
        row != document.results_hover_row) {
      document.results_hover_button = button;
      document.results_hover_row = row;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  } else if (document.results_hover_button != -1 ||
             document.results_hover_row != -1) {
    document.results_hover_button = -1;
    document.results_hover_row = -1;
  }

  if (extract_running_ || input_mode_ == InputMode::Extract ||
      input_mode_ == InputMode::Settings) {
    return;
  }
  if (h_dragging_) {
    UpdateHorizontalScrollFromX(x);
    return;
  }
  if (dragging_selection_) {
    if (y < text_area_y_ && doc().first_line > 1) {
      --doc().first_line;
      line_offsets_valid_ = false;
      RebuildVisibleOffsets();
    } else if (y > text_area_bottom_) {
      const uint64_t total = TotalLines();
      if (doc().first_line + static_cast<uint64_t>(visible_lines_) <= total) {
        ++doc().first_line;
        line_offsets_valid_ = false;
        RebuildVisibleOffsets();
      }
    }
    const float clamped_y =
        std::max(text_area_y_, std::min(y, text_area_bottom_ - 1.0f));
    doc().caret = HitTestPosition(x, clamped_y);
    caret_visible_ = true;
    caret_blink_tick_ = GetTickCount64();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (!scroll_dragging_) return;
  (void)client_x;
  JumpToLineFromScroll(y);
}

void Viewer::JumpToLineFromScroll(float y_dip) {
  if (!doc().file) return;
  float span = std::max(1.0f, scroll_height_ - scroll_thumb_height_);
  float relative = (y_dip - scroll_top_) / span;
  relative = std::max(0.0f, std::min(1.0f, relative));
  uint64_t total = std::max<uint64_t>(EstimatedLines(), 1);
  uint64_t travel = total > static_cast<uint64_t>(visible_lines_)
                        ? total - static_cast<uint64_t>(visible_lines_)
                        : 1;
  uint64_t line = 1 + static_cast<uint64_t>(relative * travel);
  JumpToLine(line);
}

void Viewer::JumpToLine(uint64_t line) {
  if (!doc().file) return;
  if (line < 1) line = 1;
  if (IndexComplete()) {
    uint64_t total = std::max<uint64_t>(TotalLines(), 1);
    if (line > total) line = total;
  }
  doc().first_line = line;
  line_offsets_valid_ = false;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::JumpToOffset(uint64_t offset) {
  if (!doc().file) return;
  uint64_t line = OffsetToLine(offset);
  uint64_t lead = visible_lines_ > 2 ? visible_lines_ / 2 : 1;
  doc().first_line = line > lead ? line - lead : 1;
  doc().caret = TextPos{line, 0};
  doc().anchor = doc().caret;
  caret_visible_ = true;
  caret_blink_tick_ = GetTickCount64();
  line_offsets_valid_ = false;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::NextHit() {
  if (doc().results.empty()) return;
  const uint64_t from = LineStart(doc().first_line);
  size_t index = 0;
  if (doc().current_hit < doc().results.size() &&
      doc().results[doc().current_hit].offset >= from) {
    index = (doc().current_hit + 1) % doc().results.size();
  } else {
    const auto it = std::lower_bound(
        doc().results.begin(), doc().results.end(), from,
        [](const DisplayHit& hit, uint64_t value) {
          return hit.offset < value;
        });
    index = it == doc().results.end()
                ? 0
                : static_cast<size_t>(it - doc().results.begin());
  }
  doc().current_hit = index;
  doc().results_selected = index;
  EnsureResultsSelectionVisible(panel_rows_visible_);
  JumpToOffset(doc().results[index].offset);
}

void Viewer::RunSearch(const std::wstring& query) {
  doc().query = query;
  doc().query_wide = query;
  input_mode_ = InputMode::None;
  AddSearchHistory(query);
  StartSearch();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::SetRegexMode(bool enabled) {
  regex_ = enabled;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::ShowFindBar() {
  doc().results_open = false;
  input_mode_ = InputMode::Find;
  input_text_ = doc().query;
  history_index_ = -1;
  ResetInputCaret();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::ShowExtractPanel() {
  if (!doc().file || doc().query.empty() || doc().total_hits == 0) {
    SetNotice(L"没有可导出的命中");
    return;
  }
  if (doc().searching) {
    SetNotice(L"请等待搜索完成");
    return;
  }
  if (extract_running_) return;
  doc().results_open = false;
  input_mode_ = InputMode::Extract;
  extract_row_ = 0;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::AddRecentFile(const std::wstring& path) {
  if (path.empty()) return;
  std::vector<std::wstring> recent;
  size_t start = 0;
  while (start <= settings_.recent_files.size()) {
    const size_t end = settings_.recent_files.find(L'\n', start);
    const std::wstring item = settings_.recent_files.substr(
        start, end == std::wstring::npos ? std::wstring::npos : end - start);
    if (!item.empty()) recent.push_back(item);
    if (end == std::wstring::npos) break;
    start = end + 1;
  }
  recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
  recent.insert(recent.begin(), path);
  if (recent.size() > 10) recent.resize(10);

  std::wstring joined;
  for (size_t i = 0; i < recent.size(); ++i) {
    if (i > 0) joined.push_back(L'\n');
    joined += recent[i];
  }
  settings_.recent_files = joined;
  settings_.last_file = path;
  SaveSettings();
  BuildCommands();
}

void Viewer::RefreshFileStamp(Document& document) {
  document.file_size_snapshot = 0;
  document.file_write_time = FILETIME{};
  document.read_only = false;
  document.external_changed = false;
  if (document.path.empty()) return;
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (GetFileAttributesExW(document.path.c_str(), GetFileExInfoStandard,
                           &data)) {
    document.file_size_snapshot =
        (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    document.file_write_time = data.ftLastWriteTime;
    document.read_only =
        (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  }
}

void Viewer::CheckExternalChange(Document& document) {
  if (!document.file || document.path.empty()) return;
  WIN32_FILE_ATTRIBUTE_DATA data{};
  bool changed = false;
  if (!GetFileAttributesExW(document.path.c_str(), GetFileExInfoStandard,
                            &data)) {
    changed = true;
  } else {
    const uint64_t size =
        (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    changed = size != document.file_size_snapshot ||
              CompareFileTime(&data.ftLastWriteTime,
                              &document.file_write_time) != 0;
  }
  if (changed == document.external_changed) return;
  document.external_changed = changed;
  if (!changed || &document != &doc()) return;
  SetNotice(document.dirty ? L"文件已被外部修改，保存将覆盖外部更改"
                           : L"文件已被外部修改，按 F5 重新加载");
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::ReloadFile() {
  if (docs_.empty() || !doc().file || doc().path.empty() || extract_running_) {
    return;
  }
  if (!PromptSaveIfDirty()) return;
  ReplaceActiveDocument(doc().path);
  SetNotice(L"已重新加载");
}

void Viewer::ShowSettingsPanel() {
  if (extract_running_) return;
  doc().results_open = false;
  input_mode_ = InputMode::Settings;
  settings_row_ = 0;
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::AdjustSetting(int delta) {
  switch (settings_row_) {
    case 0:
      CycleThemeMode();
      return;
    case 1:
      SetZoomPercent(static_cast<int>(zoom_ * 100.0f + 0.5f) +
                     (delta > 0 ? 10 : -10));
      return;
    case 2:
      case_sensitive_ = !case_sensitive_;
      settings_.search_case_sensitive = case_sensitive_;
      SaveSettings();
      break;
    case 3:
      whole_word_ = !whole_word_;
      settings_.search_whole_word = whole_word_;
      SaveSettings();
      break;
    case 4:
      regex_ = !regex_;
      settings_.search_regex = regex_;
      SaveSettings();
      break;
    case 5: {
      static const int kLimits[] = {1000, 2000, 5000, 10000, 20000, 50000};
      int index = 0;
      for (int i = 0; i < 6; ++i) {
        if (kLimits[i] == settings_.result_limit) {
          index = i;
          break;
        }
      }
      index = (index + (delta > 0 ? 1 : 5)) % 6;
      settings_.result_limit = kLimits[index];
      SaveSettings();
      break;
    }
    case 6:
      settings_.restore_last_file = !settings_.restore_last_file;
      SaveSettings();
      break;
    case 7:
      if (delta > 0) input_mode_ = InputMode::None;
      break;
    default:
      break;
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::DrawSettingsPanel(float width, float height) {
  if (input_mode_ != InputMode::Settings) return;
  canvas_.FillRect(0.0f, kTitleBarHeight, width, height,
                   Color(0x000000, 0.35f));

  const float box_width = 620.0f;
  const float row_height = 40.0f;
  const float header_height = 78.0f;
  const float footer_height = 34.0f;
  const float box_height = header_height + 8 * row_height + footer_height;
  const float box_x = (width - box_width) * 0.5f;
  const float box_y = std::max(kTitleBarHeight + 16.0f,
                               (height - box_height) * 0.5f);

  canvas_.FillRoundedRect(box_x, box_y, box_x + box_width,
                          box_y + box_height, 10.0f,
                          Color(theme_.overlay));
  canvas_.DrawTextW(L"设置", box_x + 20.0f, box_y + 14.0f,
                    box_x + box_width - 20.0f, box_y + 40.0f,
                    canvas_.ui_bold(), Color(theme_.text));
  canvas_.DrawTextW(L"更改即时生效并保存", box_x + 20.0f, box_y + 42.0f,
                    box_x + box_width - 20.0f, box_y + 64.0f,
                    canvas_.ui_small(), Color(theme_.muted));

  const wchar_t* labels[8] = {L"主题模式",
                              L"界面缩放",
                              L"默认区分大小写",
                              L"默认整词",
                              L"默认正则",
                              L"结果上限",
                              L"启动时恢复上次文件",
                              L"关闭"};
  std::wstring values[8];
  if (settings_.theme == L"system") {
    values[0] = std::wstring(L"跟随系统（") +
                (theme_.dark ? L"深色" : L"浅色") + L"）";
  } else {
    values[0] = theme_.dark ? L"深色" : L"浅色";
  }
  values[1] = std::to_wstring(static_cast<int>(zoom_ * 100.0f + 0.5f)) + L"%";
  values[2] = case_sensitive_ ? L"开" : L"关";
  values[3] = whole_word_ ? L"开" : L"关";
  values[4] = regex_ ? L"开" : L"关";
  values[5] = std::to_wstring(settings_.result_limit) + L" 条";
  values[6] = settings_.restore_last_file ? L"开" : L"关";
  values[7] = L"Esc / 点击关闭";

  for (int i = 0; i < 8; ++i) {
    const float row_y = box_y + header_height + i * row_height;
    settings_row_rects_[i][0] = box_x + 10.0f;
    settings_row_rects_[i][1] = row_y;
    settings_row_rects_[i][2] = box_x + box_width - 10.0f;
    settings_row_rects_[i][3] = row_y + row_height - 2.0f;
    if (i == settings_row_) {
      canvas_.FillRoundedRect(box_x + 10.0f, row_y, box_x + box_width - 10.0f,
                              row_y + row_height - 2.0f, 6.0f,
                              Color(theme_.accent, 0.18f));
    }
    canvas_.DrawTextW(labels[i], box_x + 28.0f, row_y + 9.0f,
                      box_x + 300.0f, row_y + 31.0f, canvas_.ui(),
                      Color(theme_.text));
    canvas_.DrawTextW(values[i], box_x + 320.0f, row_y + 9.0f,
                      box_x + box_width - 28.0f, row_y + 31.0f, canvas_.ui(),
                      Color(i == 7 ? theme_.accent_text : theme_.text));
  }

  canvas_.DrawTextW(L"↑↓ 选择 · ←→ 修改 · Enter 执行 · Esc 关闭",
                    box_x + 20.0f,
                    box_y + box_height - footer_height + 6.0f,
                    box_x + box_width - 20.0f,
                    box_y + box_height - 6.0f, canvas_.ui_small(),
                    Color(theme_.muted));
}

void Viewer::AdjustExtractOption(int delta) {
  switch (extract_row_) {
    case 0: {
      int mode = static_cast<int>(extract_mode_);
      mode = (mode + (delta > 0 ? 1 : 2)) % 3;
      extract_mode_ = static_cast<ExtractMode>(mode);
      break;
    }
    case 1:
      extract_context_ =
          std::max(0, std::min(9, extract_context_ + (delta > 0 ? 1 : -1)));
      break;
    case 2:
      extract_dedupe_ = !extract_dedupe_;
      break;
    case 3:
      extract_cut_ = !extract_cut_;
      break;
    case 4:
      extract_backup_ = !extract_backup_;
      break;
    case 5:
      if (delta > 0) StartExtraction();
      return;
    default:
      break;
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::StartExtraction() {
  if (extract_running_ || !doc().file || doc().query.empty()) return;
  extract_open_after_ = false;
  if (extract_cut_ && doc().dirty) {
    SetNotice(L"请先保存修改后再剪切");
    return;
  }
  if (doc().total_hits == 0) {
    SetNotice(L"没有可导出的命中");
    return;
  }

  std::wstring base = FileNameOf(doc().path);
  const size_t dot = base.find_last_of(L'.');
  if (dot != std::wstring::npos && dot > 0) base.resize(dot);
  const std::wstring default_name = base + L"_extract.txt";
  std::wstring directory = doc().path;
  const size_t slash = directory.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    directory.resize(slash);
  } else {
    directory = L".";
  }

  std::wstring output;
  if (!ShowSaveFileDialog(hwnd_, directory, default_name, &output)) return;
  if (_wcsicmp(output.c_str(), doc().path.c_str()) == 0) {
    SetNotice(L"导出文件不能与源文件相同");
    return;
  }

  if (extract_cut_) {
    std::wstring writable_error;
    if (!IsFileWritable(doc().path, &writable_error)) {
      SetNotice(writable_error);
      return;
    }
    wchar_t message[512]{};
    swprintf_s(
        message,
        L"将导出命中的 %llu 条内容，并从源文件中删除对应内容。\n"
        L"源文件会通过临时文件原子替换保存%s。\n\n继续？",
        static_cast<unsigned long long>(doc().total_hits),
        extract_backup_ ? L"（保留 .bak 备份）" : L"");
    if (MessageBoxW(hwnd_, message, L"确认剪切",
                    MB_YESNO | MB_ICONWARNING) != IDYES) {
      return;
    }
  }

  BeginExtraction(output);
}

void Viewer::BeginExtraction(const std::wstring& output) {
  if (extract_running_ || !doc().file || doc().query.empty() || doc().total_hits == 0) return;
  if (extract_cut_) {
    std::wstring writable_error;
    if (!IsFileWritable(doc().path, &writable_error)) {
      SetNotice(writable_error);
      return;
    }
  }

  std::vector<SearchHit> stored;
  stored.reserve(doc().results.size());
  for (const DisplayHit& hit : doc().results) {
    SearchHit record;
    record.offset = hit.offset;
    record.length = hit.length;
    stored.push_back(record);
  }

  const uint8_t* data = Data();
  const uint64_t size = DataSize();
  const EncodingInfo encoding = doc().encoding;
  const std::wstring source_path = doc().path;
  const std::string needle = WideToUtf8(doc().query);
  SearchOptions options;
  options.case_sensitive =
      case_sensitive_ ||
      (doc().encoding.encoding == TextEncoding::Ansi && !case_sensitive_);
  options.whole_word = whole_word_;
  options.regex = regex_;
  SearchRequest request;
  request.data = data;
  request.size = size;
  request.encoding = doc().encoding.encoding;
  request.big_endian = doc().encoding.big_endian();
  request.codepage = doc().encoding.codepage;

  const ExtractMode mode = extract_mode_;
  const int context_lines = extract_context_;
  const bool dedupe = extract_dedupe_;
  const bool cut = extract_cut_;
  const bool backup = extract_backup_;
  const bool need_full_search = doc().total_hits > stored.size();
  const size_t max_export = 1000000;

  extract_cancel_ = std::make_shared<CancelToken>();
  auto cancel = extract_cancel_;
  extract_running_ = true;
  extract_finished_.store(false, std::memory_order_relaxed);
  extract_success_.store(false, std::memory_order_relaxed);
  extract_cancelled_.store(false, std::memory_order_relaxed);
  extract_cut_done_.store(false, std::memory_order_relaxed);
  extract_hits_truncated_.store(false, std::memory_order_relaxed);
  extract_written_.store(0, std::memory_order_relaxed);
  extract_total_.store(0, std::memory_order_relaxed);
  extract_stage_.store(1, std::memory_order_relaxed);
  extract_message_.clear();
  doc().results_open = false;
  extract_output_ = output;
  extract_bytes_out_ = 0;

  input_mode_ = InputMode::None;
  InvalidateRect(hwnd_, nullptr, FALSE);

  extract_thread_ = std::thread([=]() {
    Stopwatch extract_watch;
    std::vector<SearchHit> hits = stored;
    if (need_full_search) {
      extract_stage_.store(1, std::memory_order_relaxed);
      SearchOutcome outcome =
          SearchBuffer(request, needle, options, max_export, cancel.get(),
                       nullptr);
      if (outcome.pattern_error) {
        extract_message_ =
            L"正则错误：" + Utf8ToWide(outcome.error_message);
        extract_finished_.store(true, std::memory_order_release);
        return;
      }
      hits = outcome.hits;
      if (outcome.total_hits > hits.size()) {
        extract_hits_truncated_.store(true, std::memory_order_relaxed);
      }
    } else if (hits.size() > max_export) {
      hits.resize(max_export);
      extract_hits_truncated_.store(true, std::memory_order_relaxed);
    }
    if (cancel->cancelled()) {
      extract_cancelled_.store(true, std::memory_order_relaxed);
      extract_finished_.store(true, std::memory_order_release);
      return;
    }
    if (hits.empty()) {
      extract_message_ = L"没有可导出的命中";
      extract_finished_.store(true, std::memory_order_release);
      return;
    }

    const std::vector<ExtractRange> ranges = BuildExtractRanges(
        data, size, hits, mode, context_lines, dedupe, encoding);
    if (ranges.empty()) {
      extract_message_ = L"没有可导出的内容";
      extract_finished_.store(true, std::memory_order_release);
      return;
    }

    uint64_t write_total = 0;
    for (const ExtractRange& range : ranges) {
      write_total += range.end - range.begin;
    }
    if (mode == ExtractMode::MatchOnly && ranges.size() > 1) {
      write_total += 2 * (ranges.size() - 1);
    }
    extract_total_.store(write_total, std::memory_order_relaxed);
    extract_written_.store(0, std::memory_order_relaxed);
    extract_stage_.store(2, std::memory_order_relaxed);

    const ExtractWriteResult write = WriteExtractFile(
        data, size, ranges, encoding, output,
        mode == ExtractMode::MatchOnly, cancel.get(), &extract_written_);
    if (write.cancelled) {
      extract_cancelled_.store(true, std::memory_order_relaxed);
      extract_finished_.store(true, std::memory_order_release);
      return;
    }
    if (!write.ok) {
      extract_message_ = write.error;
      extract_finished_.store(true, std::memory_order_release);
      return;
    }
    extract_bytes_out_ = write.bytes_written;

    if (cut) {
      const ExtractMode delete_mode =
          mode == ExtractMode::MatchOnly ? ExtractMode::MatchOnly
                                         : ExtractMode::WholeLines;
      const std::vector<ExtractRange> deletions = BuildExtractRanges(
          data, size, hits, delete_mode, 0, false, encoding);
      uint64_t removed = 0;
      for (const ExtractRange& range : deletions) {
        removed += range.end - range.begin;
      }
      extract_total_.store(size > removed ? size - removed : 0,
                           std::memory_order_relaxed);
      extract_written_.store(0, std::memory_order_relaxed);
      extract_stage_.store(3, std::memory_order_relaxed);

      const RewriteResult rewrite = RewriteFileWithoutRanges(
          source_path, data, size, deletions, backup, cancel.get(),
          &extract_written_);
      if (rewrite.cancelled) {
        extract_cancelled_.store(true, std::memory_order_relaxed);
        extract_finished_.store(true, std::memory_order_release);
        return;
      }
      if (!rewrite.ok) {
        extract_message_ = rewrite.error;
        extract_finished_.store(true, std::memory_order_release);
        return;
      }
      extract_cut_done_.store(true, std::memory_order_relaxed);
    }

    LogInfo(Format(
        L"extract ranges=%zu bytes=%llu seconds=%.3f cut=%d",
        ranges.size(), static_cast<unsigned long long>(write.bytes_written),
        extract_watch.ElapsedSeconds(), cut ? 1 : 0));
    extract_success_.store(true, std::memory_order_relaxed);
    extract_finished_.store(true, std::memory_order_release);
  });
}

void Viewer::CancelExtraction() {
  if (extract_thread_.joinable()) {
    if (extract_cancel_) extract_cancel_->Cancel();
    extract_thread_.join();
  }
  extract_running_ = false;
  extract_open_after_ = false;
}

void Viewer::RunExtractTo(const std::wstring& output, bool cut) {
  if (extract_running_) return;
  extract_mode_ = ExtractMode::WholeLines;
  extract_context_ = 0;
  extract_dedupe_ = false;
  extract_cut_ = cut;
  extract_backup_ = true;
  extract_open_after_ = false;
  if (doc().searching) {
    doc().pending_extract_output = output;
    doc().pending_extract_cut = cut;
    return;
  }
  BeginExtraction(output);
}

void Viewer::PollExtraction() {
  if (!extract_thread_.joinable() ||
      !extract_finished_.load(std::memory_order_acquire)) {
    return;
  }
  extract_thread_.join();
  extract_running_ = false;

  const bool success = extract_success_.load(std::memory_order_relaxed);
  const bool cancelled = extract_cancelled_.load(std::memory_order_relaxed);
  const bool cut_done = extract_cut_done_.load(std::memory_order_relaxed);
  const bool hits_truncated =
      extract_hits_truncated_.load(std::memory_order_relaxed);
  const uint64_t bytes = extract_bytes_out_;
  const std::wstring output = extract_output_;
  const std::wstring message = extract_message_;

  extract_success_.store(false, std::memory_order_relaxed);
  extract_cancelled_.store(false, std::memory_order_relaxed);
  extract_cut_done_.store(false, std::memory_order_relaxed);
  extract_hits_truncated_.store(false, std::memory_order_relaxed);

  const bool open_after = extract_open_after_;
  extract_open_after_ = false;
  if (success) {
    if (open_after) {
      OpenFile(output);
      SetNotice(L"已保存命中到 " + FileNameOf(output) + L" 并打开");
      LogInfo(L"hits saved and opened: " + output);
    } else {
      std::wstring note =
          L"已导出 " + SizeText(bytes) + L" 到 " + FileNameOf(output);
      if (cut_done) note += L"，源文件已更新";
      if (hits_truncated) note += L"（命中超过 100 万条，仅处理前 100 万条）";
      LogInfo(note);
      if (cut_done) {
        const std::wstring path = doc().path;
        ReplaceActiveDocument(path);
      }
      SetNotice(note);
    }
  } else if (cancelled) {
    SetNotice(L"已取消导出");
  } else {
    SetNotice(L"导出失败：" + message);
    LogError(L"extract failed: " + message);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::GoToLine(uint64_t line) {
  JumpToLine(line);
  SetCaret(TextPos{line, 0}, false);
}

void Viewer::SetTheme(const std::wstring& name) {
  if (name == L"light" || name == L"dark" || name == L"system") {
    settings_.theme = name;
  } else {
    settings_.theme = L"dark";
  }
  const std::wstring resolved =
      settings_.theme == L"system"
          ? (SystemUsesLightTheme() ? L"light" : L"dark")
          : settings_.theme;
  theme_ = ThemeByName(resolved);
  ApplyWindowChrome();
  settings_.Save();
  InvalidateRect(hwnd_, nullptr, FALSE);
  LogInfo(L"theme mode: " + settings_.theme + L" (" + theme_.name + L")");
}

void Viewer::CycleThemeMode() {
  if (settings_.theme == L"dark") {
    SetTheme(L"light");
  } else if (settings_.theme == L"light") {
    SetTheme(L"system");
  } else {
    SetTheme(L"dark");
  }
}

void Viewer::RefreshSystemTheme() {
  if (settings_.theme != L"system") return;
  const wchar_t* resolved = SystemUsesLightTheme() ? L"light" : L"dark";
  if (_wcsicmp(theme_.name, resolved) == 0) return;
  theme_ = ThemeByName(resolved);
  ApplyWindowChrome();
  InvalidateRect(hwnd_, nullptr, FALSE);
  LogInfo(std::wstring(L"system theme changed: ") + resolved);
}

void Viewer::SetZoomPercent(int percent) {
  percent = std::max(80, std::min(200, percent));
  zoom_ = percent / 100.0f;
  canvas_.SetZoom(zoom_);
  settings_.zoom_percent = percent;
  settings_.Save();
  line_offsets_valid_ = false;
  InvalidateRect(hwnd_, nullptr, FALSE);
  LogInfo(L"zoom " + std::to_wstring(percent) + L"%");
}

void Viewer::SaveSettings() {
  if (hwnd_ == nullptr || !initialized_) return;
  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  if (GetWindowPlacement(hwnd_, &placement)) {
    settings_.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    const RECT& normal = placement.rcNormalPosition;
    settings_.window_x = normal.left;
    settings_.window_y = normal.top;
    settings_.window_width = normal.right - normal.left;
    settings_.window_height = normal.bottom - normal.top;
    settings_.has_placement = true;
  }
  settings_.zoom_percent = static_cast<int>(zoom_ * 100.0f + 0.5f);
  settings_.Save();
}

void Viewer::BuildCommands() {
  commands_.clear();
  commands_.push_back({L"打开文件", L"Ctrl+O",
                       [this]() { OpenFileDialog(); }});
  commands_.push_back({L"关闭标签页", L"Ctrl+W", [this]() {
                        CloseActiveDocument();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                      }});
  commands_.push_back({L"下一个标签页", L"Ctrl+Tab",
                       [this]() { CycleTab(1); }});
  commands_.push_back({L"上一个标签页", L"Ctrl+Shift+Tab",
                       [this]() { CycleTab(-1); }});
  commands_.push_back({L"搜索", L"Ctrl+F", [this]() {
                        doc().results_open = false;
                        input_mode_ = InputMode::Find;
                        input_text_ = doc().query;
                        history_index_ = -1;
                        ResetInputCaret();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                      }});
  commands_.push_back({L"显示搜索结果", L"F4", [this]() {
                        if (doc().file && !doc().query.empty()) {
                          doc().results_open = true;
                          InvalidateRect(hwnd_, nullptr, FALSE);
                        }
                      }});
  commands_.push_back({L"跳转到行", L"Ctrl+G", [this]() {
                        input_mode_ = InputMode::Goto;
                        input_text_.clear();
                        InvalidateRect(hwnd_, nullptr, FALSE);
                      }});
  commands_.push_back({L"下一个命中", L"F3", [this]() { NextHit(); }});
  commands_.push_back({L"切换主题模式（深/浅/跟随系统）", L"Ctrl+Shift+L",
                       [this]() { CycleThemeMode(); }});
  commands_.push_back({L"切换正则表达式", L"Alt+R", [this]() {
                        regex_ = !regex_;
                        InvalidateRect(hwnd_, nullptr, FALSE);
                      }});
  commands_.push_back({L"放大界面", L"Ctrl+=", [this]() {
                        SetZoomPercent(
                            static_cast<int>(zoom_ * 100.0f + 0.5f) + 10);
                      }});
  commands_.push_back({L"缩小界面", L"Ctrl+-", [this]() {
                        SetZoomPercent(
                            static_cast<int>(zoom_ * 100.0f + 0.5f) - 10);
                      }});
  commands_.push_back(
      {L"重置缩放", L"Ctrl+0", [this]() { SetZoomPercent(100); }});
  commands_.push_back({L"导出命中到新文件", L"Ctrl+Shift+F",
                       [this]() { ShowExtractPanel(); }});
  commands_.push_back({L"剪切命中到新文件", L"", [this]() {
                        extract_cut_ = true;
                        extract_mode_ = ExtractMode::WholeLines;
                        ShowExtractPanel();
                      }});
  commands_.push_back({L"设置…", L"F9", [this]() { ShowSettingsPanel(); }});
  commands_.push_back({L"重新加载文件", L"F5", [this]() { ReloadFile(); }});
  commands_.push_back({L"退出", L"Alt+F4",
                       [this]() { PostMessageW(hwnd_, WM_CLOSE, 0, 0); }});

  size_t recent_start = 0;
  while (recent_start <= settings_.recent_files.size()) {
    const size_t recent_end = settings_.recent_files.find(L'\n', recent_start);
    const std::wstring recent = settings_.recent_files.substr(
        recent_start,
        recent_end == std::wstring::npos ? std::wstring::npos
                                         : recent_end - recent_start);
    if (!recent.empty()) {
      commands_.push_back({L"最近：" + FileNameOf(recent), L"",
                           [this, recent]() { OpenFile(recent); }});
    }
    if (recent_end == std::wstring::npos) break;
    recent_start = recent_end + 1;
  }
}

void Viewer::AddSearchHistory(const std::wstring& query) {
  if (query.empty()) return;
  search_history_.erase(
      std::remove(search_history_.begin(), search_history_.end(), query),
      search_history_.end());
  search_history_.insert(search_history_.begin(), query);
  if (search_history_.size() > 20) search_history_.resize(20);

  std::wstring joined;
  for (size_t i = 0; i < search_history_.size(); ++i) {
    if (i > 0) joined.push_back(L'\n');
    joined += search_history_[i];
  }
  settings_.search_history = joined;
  SaveSettings();
}

void Viewer::CycleSearchHistory(int delta) {
  if (search_history_.empty()) return;
  if (history_index_ < 0) {
    if (delta > 0) {
      history_index_ = 0;
    } else {
      return;
    }
  } else {
    history_index_ += delta;
    if (history_index_ < 0) {
      history_index_ = -1;
      input_text_ = doc().query;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    }
    if (history_index_ >= static_cast<int>(search_history_.size())) {
      history_index_ = static_cast<int>(search_history_.size()) - 1;
    }
  }
  input_text_ = search_history_[static_cast<size_t>(history_index_)];
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::ExecuteCommand(size_t index) {
  if (index >= commands_.size()) return;
  input_mode_ = InputMode::None;
  auto action = commands_[index].run;
  if (action) action();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::RebuildPaletteMatches() {
  palette_matches_.clear();
  std::wstring needle = palette_input_;
  for (wchar_t& c : needle) c = static_cast<wchar_t>(towlower(c));
  for (size_t i = 0; i < commands_.size(); ++i) {
    std::wstring title = commands_[i].title;
    for (wchar_t& c : title) c = static_cast<wchar_t>(towlower(c));
    if (needle.empty() || title.find(needle) != std::wstring::npos) {
      palette_matches_.push_back(i);
    }
  }
  if (palette_selected_ >= static_cast<int>(palette_matches_.size())) {
    palette_selected_ =
        std::max(0, static_cast<int>(palette_matches_.size()) - 1);
  }
}

void Viewer::ShowCommandPalette() {
  if (input_mode_ == InputMode::Palette) {
    input_mode_ = InputMode::None;
  } else {
    doc().results_open = false;
    input_mode_ = InputMode::Palette;
    palette_input_.clear();
    palette_selected_ = 0;
    RebuildPaletteMatches();
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::RunPaletteSelection() {
  if (palette_matches_.empty()) {
    input_mode_ = InputMode::None;
    return;
  }
  int selected = std::max(
      0, std::min(palette_selected_,
                  static_cast<int>(palette_matches_.size()) - 1));
  size_t command_index = palette_matches_[static_cast<size_t>(selected)];
  input_mode_ = InputMode::None;
  auto action = commands_[command_index].run;
  if (action) action();
}

void Viewer::GetTitleButtonRect(int index, float width, float* left,
                                float* top, float* right,
                                float* bottom) const {
  *right = width - kCaptionButtonWidth * static_cast<float>(2 - index);
  *left = *right - kCaptionButtonWidth;
  *top = 0.0f;
  *bottom = kTitleBarHeight;
}

int Viewer::TitleButtonAt(float x_dip, float y_dip) const {
  if (y_dip < 0.0f || y_dip >= kTitleBarHeight) return -1;
  const float width = ClientWidthDip();
  for (int i = 0; i < 3; ++i) {
    float left = 0, top = 0, right = 0, bottom = 0;
    GetTitleButtonRect(i, width, &left, &top, &right, &bottom);
    if (x_dip >= left && x_dip < right) return i;
  }
  return -1;
}

void Viewer::DrawTitleBar(float width) {
  const float title_alpha =
      mica_enabled_ && theme_.dark ? (window_active_ ? 0.86f : 0.96f) : 1.0f;
  canvas_.FillRect(0.0f, 0.0f, width, kTitleBarHeight,
                   Color(theme_.titlebar_bg, title_alpha));
  canvas_.FillRect(0.0f, kTitleBarHeight - 1.0f, width, kTitleBarHeight,
                   Color(theme_.border));

  canvas_.FillRoundedRect(12.0f, 10.0f, 30.0f, 28.0f, 6.0f,
                          Color(theme_.accent));
  canvas_.DrawTextW(L"U", 12.0f, 10.0f, 30.0f, 28.0f, canvas_.logo(),
                    Color(theme_.on_accent));

  const float text_right = width - kCaptionButtonWidth * 3.0f - 12.0f;
  canvas_.DrawTextW(L"UEdit", 38.0f, 11.0f, text_right, 30.0f,
                    canvas_.ui_bold(),
                    Color(window_active_ ? theme_.text : theme_.muted));
  if (!doc().path.empty()) {
    std::wstring document_name = FileNameOf(doc().path);
    if (doc().dirty) document_name += L" *";
    canvas_.DrawTextW(document_name, 86.0f, 11.0f, text_right, 30.0f,
                      canvas_.ui(), Color(theme_.muted));
  }

  for (int i = 0; i < 3; ++i) {
    float left = 0, top = 0, right = 0, bottom = 0;
    GetTitleButtonRect(i, width, &left, &top, &right, &bottom);
    const bool hovered = title_hover_ == i;
    if (hovered) {
      D2D1_COLOR_F background =
          i == 2 ? Color(theme_.close_hover)
                 : Color(theme_.titlebar_hover, theme_.dark ? 0.10f : 0.06f);
      canvas_.FillRoundedRect(left + 4.0f, top + 4.0f, right - 4.0f,
                              bottom - 4.0f, 4.0f, background);
    }
    D2D1_COLOR_F foreground =
        (hovered && i == 2)
            ? Color(theme_.close_hover_text)
            : Color(window_active_ ? theme_.text : theme_.muted);
    const float cx = (left + right) * 0.5f;
    const float cy = (top + bottom) * 0.5f;
    if (i == 0) {
      canvas_.FillRect(cx - 5.0f, cy, cx + 5.0f, cy + 1.4f, foreground);
    } else if (i == 1) {
      if (IsZoomed(hwnd_)) {
        canvas_.DrawLine(cx - 1.0f, cy - 6.0f, cx + 6.0f, cy - 6.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx + 6.0f, cy - 6.0f, cx + 6.0f, cy + 1.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx + 6.0f, cy + 1.0f, cx - 1.0f, cy + 1.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx - 1.0f, cy + 1.0f, cx - 1.0f, cy - 6.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx - 6.0f, cy - 2.0f, cx + 1.0f, cy - 2.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx + 1.0f, cy - 2.0f, cx + 1.0f, cy + 5.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx + 1.0f, cy + 5.0f, cx - 6.0f, cy + 5.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx - 6.0f, cy + 5.0f, cx - 6.0f, cy - 2.0f, 1.2f,
                         foreground);
      } else {
        canvas_.DrawLine(cx - 5.0f, cy - 5.0f, cx + 5.0f, cy - 5.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx + 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx + 5.0f, cy + 5.0f, cx - 5.0f, cy + 5.0f, 1.2f,
                         foreground);
        canvas_.DrawLine(cx - 5.0f, cy + 5.0f, cx - 5.0f, cy - 5.0f, 1.2f,
                         foreground);
      }
    } else {
      canvas_.DrawLine(cx - 4.0f, cy - 4.0f, cx + 4.0f, cy + 4.0f, 1.2f,
                       foreground);
      canvas_.DrawLine(cx + 4.0f, cy - 4.0f, cx - 4.0f, cy + 4.0f, 1.2f,
                       foreground);
    }
  }
}

Viewer::PaletteGeometry Viewer::PaletteLayout(float width,
                                              float height) const {
  (void)height;
  PaletteGeometry geometry;
  geometry.width = std::min(600.0f, width - 80.0f);
  geometry.row_height = 34.0f;
  geometry.rows =
      std::min<int>(8, static_cast<int>(palette_matches_.size()));
  geometry.height = 56.0f + geometry.rows * geometry.row_height + 10.0f;
  geometry.x = (width - geometry.width) * 0.5f;
  geometry.y = kTitleBarHeight + 48.0f;
  return geometry;
}

void Viewer::DrawPalette(float width, float height) {
  if (input_mode_ != InputMode::Palette) return;

  canvas_.FillRect(0.0f, kTitleBarHeight, width, height,
                   Color(0x000000, 0.35f));
  const PaletteGeometry geometry = PaletteLayout(width, height);
  canvas_.FillRoundedRect(geometry.x, geometry.y,
                          geometry.x + geometry.width,
                          geometry.y + geometry.height, 10.0f,
                          Color(theme_.overlay));
  canvas_.FillRect(geometry.x + 12.0f, geometry.y + 44.0f,
                   geometry.x + geometry.width - 12.0f, geometry.y + 45.0f,
                   Color(theme_.border));

  std::wstring shown = palette_input_ + L"|";
  canvas_.DrawTextW(shown, geometry.x + 16.0f, geometry.y + 12.0f,
                    geometry.x + geometry.width - 16.0f, geometry.y + 34.0f,
                    canvas_.mono_small(), Color(theme_.text));

  if (palette_matches_.empty()) {
    canvas_.DrawTextW(L"没有匹配的命令", geometry.x + 16.0f,
                      geometry.y + 56.0f,
                      geometry.x + geometry.width - 16.0f,
                      geometry.y + 78.0f, canvas_.ui_small(),
                      Color(theme_.muted));
    return;
  }

  for (int i = 0; i < geometry.rows; ++i) {
    const size_t command_index = palette_matches_[static_cast<size_t>(i)];
    const PaletteCommand& command = commands_[command_index];
    const float row_y =
        geometry.y + 50.0f + i * geometry.row_height;
    if (i == palette_selected_) {
      canvas_.FillRoundedRect(geometry.x + 8.0f, row_y,
                              geometry.x + geometry.width - 8.0f,
                              row_y + geometry.row_height - 2.0f, 6.0f,
                              Color(theme_.accent, 0.22f));
    }
    canvas_.DrawTextW(command.title, geometry.x + 20.0f, row_y + 8.0f,
                      geometry.x + geometry.width - 150.0f, row_y + 30.0f,
                      canvas_.ui(), Color(theme_.text));
    canvas_.DrawTextW(command.shortcut,
                      geometry.x + geometry.width - 140.0f, row_y + 9.0f,
                      geometry.x + geometry.width - 20.0f, row_y + 29.0f,
                      canvas_.ui_small(), Color(theme_.muted));
  }
}

void Viewer::DrawExtractPanel(float width, float height) {
  if (input_mode_ != InputMode::Extract) return;
  canvas_.FillRect(0.0f, kTitleBarHeight, width, height,
                   Color(0x000000, 0.35f));

  const float box_width = 600.0f;
  const float row_height = 40.0f;
  const float header_height = 78.0f;
  const float footer_height = 34.0f;
  const float box_height = header_height + 6 * row_height + footer_height;
  const float box_x = (width - box_width) * 0.5f;
  const float box_y = std::max(kTitleBarHeight + 20.0f,
                               (height - box_height) * 0.5f);

  canvas_.FillRoundedRect(box_x, box_y, box_x + box_width,
                          box_y + box_height, 10.0f,
                          Color(theme_.overlay));
  canvas_.DrawTextW(L"导出命中内容", box_x + 20.0f, box_y + 14.0f,
                    box_x + box_width - 20.0f, box_y + 40.0f,
                    canvas_.ui_bold(), Color(theme_.text));

  std::wstring summary = Format(
      L"命中 %llu 条 · 已存储 %zu 条",
      static_cast<unsigned long long>(doc().total_hits), doc().results.size());
  if (doc().total_hits > 1000000) {
    summary += L" · 最多处理前 100 万条";
  }
  canvas_.DrawTextW(summary, box_x + 20.0f, box_y + 42.0f,
                    box_x + box_width - 20.0f, box_y + 64.0f,
                    canvas_.ui_small(), Color(theme_.muted));

  const wchar_t* labels[6] = {L"模式",           L"上下文行数", L"去重",
                              L"剪切源文件",   L"保留 .bak",  L"开始导出"};
  std::wstring values[6];
  values[0] = extract_mode_ == ExtractMode::WholeLines
                  ? L"匹配行"
                  : (extract_mode_ == ExtractMode::MatchOnly ? L"匹配片段"
                                                             : L"带上下文");
  values[1] = std::to_wstring(extract_context_) + L" 行";
  values[2] = extract_dedupe_ ? L"开" : L"关";
  values[3] = extract_cut_ ? L"开" : L"关";
  values[4] = extract_backup_ ? L"开" : L"关";
  values[5] = L"回车 / 点击执行";

  for (int i = 0; i < 6; ++i) {
    const float row_y = box_y + header_height + i * row_height;
    extract_row_rects_[i][0] = box_x + 10.0f;
    extract_row_rects_[i][1] = row_y;
    extract_row_rects_[i][2] = box_x + box_width - 10.0f;
    extract_row_rects_[i][3] = row_y + row_height - 2.0f;
    if (i == extract_row_) {
      canvas_.FillRoundedRect(box_x + 10.0f, row_y, box_x + box_width - 10.0f,
                              row_y + row_height - 2.0f, 6.0f,
                              Color(theme_.accent, 0.18f));
    }
    const bool dimmed = (i == 1 && extract_mode_ != ExtractMode::WithContext) ||
                        (i == 4 && !extract_cut_);
    canvas_.DrawTextW(labels[i], box_x + 28.0f, row_y + 9.0f,
                      box_x + 200.0f, row_y + 31.0f, canvas_.ui(),
                      Color(dimmed ? theme_.muted_dark : theme_.text));
    canvas_.DrawTextW(values[i], box_x + 220.0f, row_y + 9.0f,
                      box_x + box_width - 28.0f, row_y + 31.0f, canvas_.ui(),
                      Color(dimmed ? theme_.muted_dark
                                   : (i == 5 ? theme_.accent_text
                                             : theme_.text)));
  }

  canvas_.DrawTextW(L"↑↓ 选择 · ←→ 修改 · Enter 执行 · Esc 取消",
                    box_x + 20.0f,
                    box_y + box_height - footer_height + 6.0f,
                    box_x + box_width - 20.0f,
                    box_y + box_height - 6.0f, canvas_.ui_small(),
                    Color(theme_.muted));
}

void Viewer::DrawExtractProgress(float width, float height) {
  canvas_.FillRect(0.0f, kTitleBarHeight, width, height,
                   Color(0x000000, 0.4f));
  const float box_width = 480.0f;
  const float box_height = 150.0f;
  const float box_x = (width - box_width) * 0.5f;
  const float box_y = (height - box_height) * 0.5f;
  canvas_.FillRoundedRect(box_x, box_y, box_x + box_width,
                          box_y + box_height, 10.0f,
                          Color(theme_.overlay));

  const int stage = extract_stage_.load(std::memory_order_relaxed);
  const wchar_t* stage_text = stage == 1
                                  ? L"收集命中…"
                                  : (stage == 2 ? L"写入新文件…"
                                                : L"重写源文件…");
  canvas_.DrawTextW(stage_text, box_x + 20.0f, box_y + 18.0f,
                    box_x + box_width - 20.0f, box_y + 44.0f,
                    canvas_.ui_bold(), Color(theme_.text));

  const uint64_t total = extract_total_.load(std::memory_order_relaxed);
  const uint64_t written = extract_written_.load(std::memory_order_relaxed);
  const std::wstring detail =
      total > 0 ? SizeText(written) + L" / " + SizeText(total) : L"…";
  canvas_.DrawTextW(detail, box_x + box_width - 220.0f, box_y + 18.0f,
                    box_x + box_width - 20.0f, box_y + 44.0f,
                    canvas_.ui_small(), Color(theme_.muted));

  const float track_y = box_y + 72.0f;
  canvas_.FillRoundedRect(box_x + 20.0f, track_y, box_x + box_width - 20.0f,
                          track_y + 8.0f, 4.0f,
                          Color(theme_.scroll_track, 0.7f));
  if (total > 0 && written > 0) {
    const float fraction =
        std::min(1.0f, static_cast<float>(written) / static_cast<float>(total));
    canvas_.FillRoundedRect(box_x + 20.0f, track_y,
                            box_x + 20.0f + (box_width - 40.0f) * fraction,
                            track_y + 8.0f, 4.0f, Color(theme_.accent));
  }

  canvas_.DrawTextW(L"Esc 取消", box_x + 20.0f, box_y + 100.0f,
                    box_x + box_width - 20.0f, box_y + 126.0f,
                    canvas_.ui_small(), Color(theme_.muted));
}

void Viewer::OnNcMouseMove(int screen_x, int screen_y) {
  POINT point{screen_x, screen_y};
  ScreenToClient(hwnd_, &point);
  int button = TitleButtonAt(ToDipX(point.x), ToDipY(point.y));
  if (button != title_hover_) {
    title_hover_ = button;
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  TRACKMOUSEEVENT track{};
  track.cbSize = sizeof(track);
  track.dwFlags = TME_NONCLIENT | TME_LEAVE;
  track.hwndTrack = hwnd_;
  TrackMouseEvent(&track);
}

void Viewer::OnNcMouseLeave() {
  if (title_hover_ != -1) {
    title_hover_ = -1;
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void Viewer::StartSearch() {
  if (extract_running_) return;
  CancelSearch();
  doc().results.clear();
  doc().pending_results.clear();
  doc().current_hit = static_cast<size_t>(-1);
  doc().panel_scroll = 0;
  doc().search_error = false;
  doc().search_message.clear();
  doc().search_seconds = 0.0;
  doc().total_hits = 0;
  doc().truncated = false;
  doc().ansi_case_forced = false;
  ++render_generation_;
  if (!doc().file || doc().query.empty()) return;
  doc().results_open = true;
  doc().results_selected = 0;
  doc().results_h_offset = 0.0f;

  const bool force_case =
      doc().encoding.encoding == TextEncoding::Ansi && !case_sensitive_;
  doc().ansi_case_forced = force_case;
  std::string needle = WideToUtf8(doc().query);
  if (needle.empty()) return;

  SearchOptions options;
  options.case_sensitive = case_sensitive_ || force_case;
  options.whole_word = whole_word_;
  options.regex = regex_;

  DebugLog("search start: query_bytes=" + std::to_string(needle.size()) +
           " needle=\"" + needle + "\" case=" +
           std::to_string(options.case_sensitive ? 1 : 0) + " whole=" +
           std::to_string(options.whole_word ? 1 : 0) + " regex=" +
           std::to_string(options.regex ? 1 : 0) + " size=" +
           std::to_string(DataSize()) + "\n");

  doc().search_pattern_error.store(false, std::memory_order_relaxed);
  doc().pending_search_message.clear();

  SearchRequest request;
  request.data = Data();
  request.size = DataSize();
  request.encoding = doc().encoding.encoding;
  request.big_endian = doc().encoding.big_endian();
  request.codepage = doc().encoding.codepage;

  doc().search_cancel = std::make_shared<CancelToken>();
  doc().searching = true;
  doc().search_finished.store(false, std::memory_order_relaxed);
  doc().search_scanned.store(0, std::memory_order_relaxed);
  doc().search_start_tick = GetTickCount64();
  doc().query_wide = doc().query;

  const size_t result_limit = static_cast<size_t>(
      std::max(1000, std::min(100000, settings_.result_limit)));
  const uint8_t* data = Data();
  const uint64_t size = DataSize();
  Document* source = &doc();
  std::atomic<uint64_t>* progress = &doc().search_scanned;
  std::atomic<bool>* finished = &doc().search_finished;
  std::atomic<bool>* pattern_error = &doc().search_pattern_error;
  std::vector<DisplayHit>* pending = &doc().pending_results;
  std::wstring* pending_message = &doc().pending_search_message;
  double* seconds_out = &doc().search_seconds;
  uint64_t* total_out = &doc().total_hits;
  bool* truncated_out = &doc().truncated;
  const EncodingInfo encoding = doc().encoding;
  auto cancel = doc().search_cancel;

  doc().search_thread = std::thread([=]() {
    SearchOutcome outcome = SearchBuffer(request, needle, options,
                                         result_limit, cancel.get(), progress);
    if (outcome.pattern_error) {
      *pending_message = Utf8ToWide(outcome.error_message);
      pattern_error->store(true, std::memory_order_release);
      *seconds_out = outcome.seconds;
      finished->store(true, std::memory_order_release);
      return;
    }
    pending->clear();
    pending->reserve(outcome.hits.size());
    for (const SearchHit& hit : outcome.hits) {
      if (cancel->cancelled()) break;
      DisplayHit display;
      display.offset = hit.offset;
      display.length = hit.length;
      display.line = source->editable
                         ? source->editable_text.offset_to_line(hit.offset)
                         : source->index.offset_to_line(hit.offset);
      display.context = MakeContext(data, size, encoding, hit.offset,
                                    hit.length, &display.match_pos,
                                    &display.match_len);
      pending->push_back(std::move(display));
    }
    *seconds_out = outcome.seconds;
    *total_out = outcome.total_hits;
    *truncated_out = outcome.truncated;
    LogInfo(Format(
        L"search hits=%llu stored=%llu scanned=%llu seconds=%.3f regex=%d "
        L"cancelled=%d",
        static_cast<unsigned long long>(outcome.total_hits),
        static_cast<unsigned long long>(outcome.hits.size()),
        static_cast<unsigned long long>(outcome.scanned_bytes),
        outcome.seconds, options.regex ? 1 : 0,
        outcome.cancelled ? 1 : 0));
    DebugLog("search end: hits=" + std::to_string(outcome.total_hits) +
             " stored=" + std::to_string(outcome.hits.size()) +
             " scanned=" + std::to_string(outcome.scanned_bytes) +
             " seconds=" + std::to_string(outcome.seconds) +
             " cancelled=" + std::to_string(outcome.cancelled ? 1 : 0) + "\n");
    finished->store(true, std::memory_order_release);
  });
}

void Viewer::CancelSearch(Document& document) {
  if (document.search_thread.joinable()) {
    if (document.search_cancel) document.search_cancel->Cancel();
    document.search_thread.join();
  }
  document.searching = false;
  document.search_finished.store(false, std::memory_order_relaxed);
}

void Viewer::CancelSearch() { CancelSearch(doc()); }

void Viewer::PollSearch() {
  bool changed = false;
  for (auto& owning : docs_) {
    Document& document = *owning;
    if (!document.search_thread.joinable() ||
        !document.search_finished.load(std::memory_order_acquire)) {
      continue;
    }
    document.search_thread.join();
    document.results = std::move(document.pending_results);
    document.pending_results.clear();
    document.searching = false;
    document.panel_scroll = 0;
    document.results_selected = 0;
    document.current_hit = static_cast<size_t>(-1);
    document.results_h_offset = 0.0f;
    if (document.search_pattern_error.load(std::memory_order_acquire)) {
      document.search_error = true;
      document.search_message =
          regex_ ? L"正则错误：" + document.pending_search_message
                 : document.pending_search_message;
      document.pending_search_message.clear();
      document.search_pattern_error.store(false, std::memory_order_relaxed);
    }
    changed = true;
    if (&document == &doc()) MaybeStartPendingExtraction(document);
  }
  if (changed) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Viewer::MaybeStartPendingExtraction(Document& document) {
  if (&document != &doc()) return;
  if (document.pending_extract_output.empty() || document.searching) return;
  const std::wstring output = document.pending_extract_output;
  document.pending_extract_output.clear();
  extract_cut_ = document.pending_extract_cut;
  document.pending_extract_cut = false;
  BeginExtraction(output);
}

LRESULT Viewer::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      Paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      OnSize(LOWORD(lparam), HIWORD(lparam));
      return 0;
    case WM_TIMER:
      OnTimer();
      return 0;
    case WM_MOUSEWHEEL:
      OnWheel(GET_WHEEL_DELTA_WPARAM(wparam),
              (GET_KEYSTATE_WPARAM(wparam) & MK_SHIFT) != 0,
              GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_LBUTTONDOWN:
      OnLButtonDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_LBUTTONUP:
      OnLButtonUp();
      return 0;
    case WM_MOUSEMOVE:
      OnMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_KEYDOWN:
      OnKeyDown(wparam);
      return 0;
    case WM_CHAR:
      OnChar(static_cast<wchar_t>(wparam));
      return 0;
    case WM_SYSCHAR:
      return 0;
    case WM_COPY:
      CopySelection();
      return 0;
    case WM_CUT:
      CutSelection();
      return 0;
    case WM_PASTE:
      if (input_mode_ == InputMode::None) {
        PasteClipboard();
      } else {
        PasteIntoInput();
      }
      return 0;
    case WM_CLOSE:
      if (!PromptSaveAll()) return 0;
      break;
    case WM_QUERYENDSESSION:
      return PromptSaveAll() ? TRUE : FALSE;
    case WM_MBUTTONDOWN: {
      bool on_close = false;
      const int tab = TabAt(ToDipX(GET_X_LPARAM(lparam)),
                            ToDipY(GET_Y_LPARAM(lparam)), &on_close);
      if (tab >= 0) CloseTabAt(static_cast<size_t>(tab));
      return 0;
    }
    case WM_APP + 1:  // Automation hook: save.
      SaveDocument();
      return 0;
    case WM_APP + 2:  // Automation hook: undo.
      UndoEdit();
      return 0;
    case WM_APP + 3:  // Automation hook: redo.
      RedoEdit();
      return 0;
    case WM_SETFOCUS:
      caret_visible_ = true;
      caret_blink_tick_ = GetTickCount64();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_KILLFOCUS:
      caret_visible_ = false;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_SETCURSOR: {
      if (LOWORD(lparam) == HTCLIENT) {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(hwnd_, &point);
        const float x = ToDipX(point.x);
        const float y = ToDipY(point.y);
        const auto in_rect = [&](const float* rect) {
          return rect[2] > rect[0] && rect[3] > rect[1] && x >= rect[0] &&
                 x <= rect[2] && y >= rect[1] && y <= rect[3];
        };
        if ((input_mode_ == InputMode::Find ||
             input_mode_ == InputMode::Goto) &&
            in_rect(search_box_rect_)) {
          SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
          return TRUE;
        }
        if (doc().results_open && doc().file &&
            input_mode_ == InputMode::None) {
          const ResultsGeometry geometry =
              ResultsLayout(ClientWidthDip(), ClientHeightDip());
          if (x >= geometry.x && x <= geometry.x + geometry.width &&
              y >= geometry.y && y <= geometry.y + geometry.height) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
          }
        }
        if (doc().file && x >= text_area_x_ &&
            x <= text_area_x_ + text_area_width_ && y >= text_area_y_ &&
            y <= text_area_bottom_) {
          SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
          return TRUE;
        }
      }
      break;
    }
    case WM_IME_STARTCOMPOSITION: {
      HIMC context = ImmGetContext(hwnd_);
      if (context != nullptr) {
        float x = text_area_x_ + 90.0f;
        float y = ContentTop() + 24.0f;
        if (input_mode_ == InputMode::Palette) {
          const float width = ClientWidthDip();
          const float box_width = std::min(600.0f, width - 80.0f);
          x = (width - box_width) * 0.5f + 20.0f;
          y = kTitleBarHeight + 62.0f;
        }
        const float scale = canvas_.dpi_scale();
        COMPOSITIONFORM form{};
        form.dwStyle = CFS_POINT;
        form.ptCurrentPos.x = static_cast<LONG>(x * scale);
        form.ptCurrentPos.y = static_cast<LONG>(y * scale);
        ImmSetCompositionWindow(context, &form);
        ImmReleaseContext(hwnd_, context);
      }
      break;
    }
    case WM_NCCALCSIZE:
      if (wparam) {
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
        if (IsZoomed(hwnd_)) {
          const int frame = GetSystemMetrics(SM_CXSIZEFRAME) +
                            GetSystemMetrics(SM_CXPADDEDBORDER);
          params->rgrc[0].left += frame;
          params->rgrc[0].top += frame;
          params->rgrc[0].right -= frame;
          params->rgrc[0].bottom -= frame;
        }
        return 0;
      }
      break;
    case WM_NCHITTEST: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      POINT client = point;
      ScreenToClient(hwnd_, &client);
      const float x = ToDipX(client.x);
      const float y = ToDipY(client.y);
      const float width = ClientWidthDip();
      const float height = ClientHeightDip();
      const float border = 6.0f;
      if (!IsZoomed(hwnd_)) {
        const bool left = x < border;
        const bool right = x >= width - border;
        const bool top = y < border;
        const bool bottom = y >= height - border;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
      }
      if (y >= 0.0f && y < kTitleBarHeight) {
        const int button = TitleButtonAt(x, y);
        if (button == 0) return HTMINBUTTON;
        if (button == 1) return HTMAXBUTTON;
        if (button == 2) return HTCLOSE;
        return HTCAPTION;
      }
      return HTCLIENT;
    }
    case WM_NCLBUTTONDOWN:
      switch (wparam) {
        case HTMINBUTTON:
          ShowWindow(hwnd_, SW_MINIMIZE);
          return 0;
        case HTMAXBUTTON:
          ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
          return 0;
        case HTCLOSE:
          PostMessageW(hwnd_, WM_CLOSE, 0, 0);
          return 0;
        default:
          break;
      }
      break;
    case WM_NCMOUSEMOVE:
      OnNcMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      return 0;
    case WM_NCMOUSELEAVE:
      OnNcMouseLeave();
      return 0;
    case WM_NCACTIVATE: {
      const bool active = wparam != 0;
      if (window_active_ != active) {
        window_active_ = active;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return TRUE;
    }
    case WM_SETTINGCHANGE:
      RefreshSystemTheme();
      break;
    case WM_THEMECHANGED:
      RefreshSystemTheme();
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}  // namespace uedit
