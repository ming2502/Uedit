#pragma once

#include <string>

namespace uedit {

// Appends to %LOCALAPPDATA%\UEdit\uedit.log. Thread-safe; no-op before Init.
void LogInit();
void LogInfo(const std::wstring& message);
void LogWarn(const std::wstring& message);
void LogError(const std::wstring& message);

}  // namespace uedit
