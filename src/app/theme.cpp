#include "app/theme.h"

#include <windows.h>

namespace uedit {
namespace {

Theme MakeDark() {
  Theme theme;
  theme.name = L"dark";
  theme.dark = true;
  theme.window_bg = 0x0F1419;
  theme.panel_bg = 0x161C23;
  theme.status_bg = 0x0B0F14;
  theme.titlebar_bg = 0x11171E;
  theme.titlebar_hover = 0xFFFFFF;
  theme.text = 0xE8EDF2;
  theme.muted = 0x9AA7B4;
  theme.muted_dark = 0x828C99;
  theme.accent = 0x2563EB;
  theme.accent_text = 0x7CB0FF;
  theme.on_accent = 0xFFFFFF;
  theme.danger = 0xF87171;
  theme.match = 0xD29922;
  theme.match_alpha = 0.35f;
  theme.overlay = 0x1B222B;
  theme.chip = 0x242C36;
  theme.scroll_track = 0x1A2028;
  theme.scroll_thumb = 0x3C4652;
  theme.row_hover = 0x1E2630;
  theme.border = 0x2A323C;
  return theme;
}

Theme MakeLight() {
  Theme theme;
  theme.name = L"light";
  theme.dark = false;
  theme.window_bg = 0xF6F8FA;
  theme.panel_bg = 0xFFFFFF;
  theme.status_bg = 0xE9EEF3;
  theme.titlebar_bg = 0xEDF0F4;
  theme.titlebar_hover = 0x000000;
  theme.text = 0x0F172A;
  theme.muted = 0x57606A;
  theme.muted_dark = 0x636C76;
  theme.accent = 0x2563EB;
  theme.accent_text = 0x1A5FD0;
  theme.on_accent = 0xFFFFFF;
  theme.danger = 0xC0392B;
  theme.match = 0xF2CC60;
  theme.match_alpha = 0.85f;
  theme.overlay = 0xFFFFFF;
  theme.chip = 0xE7ECF2;
  theme.scroll_track = 0xE1E6EB;
  theme.scroll_thumb = 0xB6BEC7;
  theme.row_hover = 0xEAF1FB;
  theme.border = 0xD8DEE4;
  return theme;
}

}  // namespace

const Theme& DarkTheme() {
  static const Theme theme = MakeDark();
  return theme;
}

const Theme& LightTheme() {
  static const Theme theme = MakeLight();
  return theme;
}

const Theme& ThemeByName(const std::wstring& name) {
  return name == L"light" ? LightTheme() : DarkTheme();
}

bool SystemUsesLightTheme() {
  const wchar_t* key =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, key, L"AppsUseLightTheme",
                   RRF_RT_REG_DWORD, nullptr, &value,
                   &size) == ERROR_SUCCESS) {
    return value != 0;
  }
  size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, key, L"SystemUsesLightTheme",
                   RRF_RT_REG_DWORD, nullptr, &value,
                   &size) == ERROR_SUCCESS) {
    return value != 0;
  }
  return false;
}

}  // namespace uedit
