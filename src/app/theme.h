#pragma once

#include <cstdint>
#include <string>

namespace uedit {

struct Theme {
  const wchar_t* name = L"dark";
  bool dark = true;

  uint32_t window_bg = 0;
  uint32_t panel_bg = 0;
  uint32_t status_bg = 0;
  uint32_t titlebar_bg = 0;
  uint32_t titlebar_hover = 0;
  uint32_t text = 0;
  uint32_t muted = 0;
  uint32_t muted_dark = 0;
  uint32_t accent = 0;
  uint32_t accent_text = 0;
  uint32_t on_accent = 0xFFFFFF;
  uint32_t danger = 0;
  uint32_t match = 0;
  float match_alpha = 0.32f;
  uint32_t overlay = 0;
  uint32_t chip = 0;
  uint32_t scroll_track = 0;
  uint32_t scroll_thumb = 0;
  uint32_t row_hover = 0;
  uint32_t border = 0;
  uint32_t close_hover = 0xC42B1C;
  uint32_t close_hover_text = 0xFFFFFF;
};

const Theme& DarkTheme();
const Theme& LightTheme();
const Theme& ThemeByName(const std::wstring& name);
bool SystemUsesLightTheme();

}  // namespace uedit
