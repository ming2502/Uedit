#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>
#include <shellapi.h>

#include <algorithm>
#include <string>

#include "app/settings.h"
#include "app/viewer.h"
#include "common/log.h"

namespace {

uedit::Viewer* g_viewer = nullptr;
constexpr wchar_t kClassName[] = L"UEditMainWindow";
constexpr wchar_t kMutexName[] = L"Local\\UEdit.SingleInstance";

std::wstring LocalAppDataPath() {
  wchar_t path[MAX_PATH]{};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                 nullptr, SHGFP_TYPE_CURRENT, path))) {
    return path;
  }
  return L".";
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info) {
  const std::wstring base = LocalAppDataPath() + L"\\UEdit";
  const std::wstring crash = base + L"\\crash";
  CreateDirectoryW(base.c_str(), nullptr);
  CreateDirectoryW(crash.c_str(), nullptr);

  SYSTEMTIME now{};
  GetLocalTime(&now);
  wchar_t file[MAX_PATH]{};
  swprintf_s(file, L"%s\\crash_%04d%02d%02d_%02d%02d%02d.dmp", crash.c_str(),
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
             now.wSecond);

  HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception{};
    exception.ThreadId = GetCurrentThreadId();
    exception.ExceptionPointers = info;
    exception.ClientPointers = FALSE;
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), handle,
                      MiniDumpNormal, &exception, nullptr, nullptr);
    CloseHandle(handle);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  switch (message) {
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = 760;
      info->ptMinTrackSize.y = 480;
      return 0;
    }
    case WM_DROPFILES: {
      if (g_viewer != nullptr) {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
          const UINT length = DragQueryFileW(drop, i, nullptr, 0);
          if (length == 0) continue;
          std::wstring path(length, L'\0');
          DragQueryFileW(drop, i, &path[0], length + 1);
          g_viewer->OpenFile(path);
        }
        DragFinish(drop);
      }
      return 0;
    }
    case WM_COPYDATA: {
      auto* data = reinterpret_cast<COPYDATASTRUCT*>(lparam);
      if (g_viewer != nullptr && data != nullptr && data->dwData == 1 &&
          data->lpData != nullptr) {
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        g_viewer->OpenFile(static_cast<const wchar_t*>(data->lpData));
        return TRUE;
      }
      break;
    }
    case WM_DESTROY:
      if (g_viewer != nullptr) g_viewer->Shutdown();
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  if (g_viewer != nullptr) {
    return g_viewer->HandleMessage(message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  SetUnhandledExceptionFilter(CrashHandler);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  uedit::LogInit();
  uedit::LogInfo(L"UEdit starting");

  int argument_count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  std::wstring initial_path;
  std::wstring theme;
  bool open_palette = false;
  bool crash_test = false;
  bool regex_mode = false;
  bool show_find = false;
  bool show_settings = false;
  bool cut_mode = false;
  std::wstring extract_to;
  if (arguments != nullptr) {
    for (int i = 1; i < argument_count; ++i) {
      std::wstring argument = arguments[i];
      if (argument.rfind(L"--theme=", 0) == 0) {
        theme = argument.substr(8);
      } else if (argument == L"--palette") {
        open_palette = true;
      } else if (argument == L"--crash-test") {
        crash_test = true;
      } else if (argument == L"--regex") {
        regex_mode = true;
      } else if (argument == L"--find") {
        show_find = true;
      } else if (argument == L"--cut") {
        cut_mode = true;
      } else if (argument == L"--settings") {
        show_settings = true;
      } else if (argument.rfind(L"--extract-to=", 0) == 0) {
        extract_to = argument.substr(13);
      } else if (argument.rfind(L"--", 0) != 0 && initial_path.empty()) {
        initial_path = argument;
      }
    }
  }

  if (!initial_path.empty()) {
    wchar_t full_path[4096]{};
    const DWORD length =
        GetFullPathNameW(initial_path.c_str(), 4096, full_path, nullptr);
    if (length > 0 && length < 4096) {
      initial_path.assign(full_path, length);
    }
  }

  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  const DWORD mutex_error = GetLastError();
  if (mutex != nullptr && mutex_error == ERROR_ALREADY_EXISTS) {
    HWND existing = FindWindowW(kClassName, nullptr);
    if (existing != nullptr) {
      uedit::LogInfo(L"single instance: forwarding request");
      if (!initial_path.empty()) {
        COPYDATASTRUCT data{};
        data.dwData = 1;
        data.cbData = static_cast<DWORD>((initial_path.size() + 1) *
                                         sizeof(wchar_t));
        data.lpData = const_cast<wchar_t*>(initial_path.c_str());
        SendMessageW(existing, WM_COPYDATA, 0,
                     reinterpret_cast<LPARAM>(&data));
      }
      if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
      SetForegroundWindow(existing);
      CloseHandle(mutex);
      if (arguments != nullptr) LocalFree(arguments);
      CoUninitialize();
      return 0;
    }
  }

  uedit::Settings settings;
  settings.Load();

  if (initial_path.empty() && settings.restore_last_file &&
      !settings.last_file.empty()) {
    const DWORD attributes = GetFileAttributesW(settings.last_file.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      initial_path = settings.last_file;
    }
  }

  int window_width = std::max(760, std::min(4096, settings.window_width));
  int window_height = std::max(480, std::min(2160, settings.window_height));

  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  RECT target{settings.window_x, settings.window_y,
              settings.window_x + window_width,
              settings.window_y + window_height};
  HMONITOR monitor = nullptr;
  if (settings.has_placement) {
    monitor = MonitorFromRect(&target, MONITOR_DEFAULTTONEAREST);
  } else {
    monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  }
  GetMonitorInfoW(monitor, &monitor_info);
  const RECT& work = monitor_info.rcWork;
  const int work_width = work.right - work.left;
  const int work_height = work.bottom - work.top;
  window_width = std::min(window_width, work_width - 20);
  window_height = std::min(window_height, work_height - 20);

  int window_x = 0;
  int window_y = 0;
  if (settings.has_placement) {
    window_x = std::max<int>(
        static_cast<int>(work.left),
        std::min(settings.window_x, static_cast<int>(work.right) - window_width));
    window_y = std::max<int>(
        static_cast<int>(work.top),
        std::min(settings.window_y,
                 static_cast<int>(work.bottom) - window_height));
  } else {
    window_x = work.left + (work_width - window_width) / 2;
    window_y = work.top + (work_height - window_height) / 2;
  }

  uedit::Viewer viewer;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
  window_class.hIconSm = static_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR));
  if (window_class.hIconSm == nullptr) {
    window_class.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
  }
  window_class.hbrBackground = nullptr;
  window_class.lpszClassName = kClassName;
  if (!RegisterClassExW(&window_class)) {
    if (mutex != nullptr) CloseHandle(mutex);
    if (arguments != nullptr) LocalFree(arguments);
    CoUninitialize();
    return 2;
  }

  HWND hwnd = CreateWindowExW(
      0, kClassName, L"UEdit", WS_OVERLAPPEDWINDOW, window_x, window_y,
      window_width, window_height, nullptr, nullptr, instance, nullptr);
  if (hwnd == nullptr) {
    if (mutex != nullptr) CloseHandle(mutex);
    if (arguments != nullptr) LocalFree(arguments);
    CoUninitialize();
    return 3;
  }

  DragAcceptFiles(hwnd, TRUE);
  if (!viewer.Initialize(hwnd, settings)) {
    DestroyWindow(hwnd);
    if (mutex != nullptr) CloseHandle(mutex);
    if (arguments != nullptr) LocalFree(arguments);
    CoUninitialize();
    return 4;
  }
  g_viewer = &viewer;

  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

  if (!theme.empty()) viewer.SetTheme(theme);
  if (!initial_path.empty()) viewer.OpenFile(initial_path);

  ShowWindow(hwnd, settings.maximized ? SW_MAXIMIZE : show_command);
  UpdateWindow(hwnd);

  if (regex_mode) viewer.SetRegexMode(true);
  if (arguments != nullptr) {
    for (int i = 1; i < argument_count; ++i) {
      std::wstring argument = arguments[i];
      if (argument.rfind(L"--search=", 0) == 0) {
        viewer.RunSearch(argument.substr(9));
      } else if (argument.rfind(L"--goto=", 0) == 0) {
        viewer.GoToLine(_wcstoui64(argument.c_str() + 7, nullptr, 10));
      } else if (argument.rfind(L"--zoom=", 0) == 0) {
        viewer.SetZoomPercent(_wtoi(argument.c_str() + 7));
      }
    }
  }
  if (!extract_to.empty()) viewer.RunExtractTo(extract_to, cut_mode);
  if (show_settings) viewer.ShowSettingsPanel();
  if (show_find) viewer.ShowFindBar();
  if (open_palette) viewer.ShowCommandPalette();

  if (crash_test) {
    volatile int* pointer = nullptr;
    *pointer = 1;
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  g_viewer = nullptr;
  uedit::LogInfo(L"UEdit exited");
  if (arguments != nullptr) LocalFree(arguments);
  if (mutex != nullptr) CloseHandle(mutex);
  CoUninitialize();
  return static_cast<int>(message.wParam);
}
