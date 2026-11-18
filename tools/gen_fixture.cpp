#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) {
    wprintf(L"usage: uedit_gen <path> <size_mb>\n");
    return 1;
  }
  const std::wstring path = argv[1];
  const int megabytes = _wtoi(argv[2]);
  if (megabytes <= 0) {
    wprintf(L"size_mb must be positive\n");
    return 1;
  }

  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    wprintf(L"cannot create %ls (error %lu)\n", path.c_str(), GetLastError());
    return 2;
  }

  const uint64_t target = static_cast<uint64_t>(megabytes) << 20;
  const size_t buffer_capacity = 4u << 20;
  std::string buffer;
  buffer.reserve(buffer_capacity);

  uint64_t written = 0;
  uint64_t line_number = 0;
  uint32_t state = 0x12345678u;

  while (written < target) {
    buffer.clear();
    while (buffer.size() < buffer_capacity && written + buffer.size() < target) {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      const uint64_t candidate = line_number + 1;
      char line[320];
      int length;
      if (candidate % 997 == 0 || candidate % 4999 == 0) {
        length = snprintf(
            line, sizeof(line),
            "2026-11-17T12:%02u:%02u.%03uZ ERROR worker=%02u req=%08x "
            "NEEDLE_2026 seq=%012llu latency_ms=%u status=500 "
            "path=/api/v1/orders/%u payload={\"id\":%u,\"state\":\"failed\"}\n",
            static_cast<unsigned>(candidate / 60 % 60),
            static_cast<unsigned>(candidate % 60), state % 1000,
            state % 32, state, static_cast<unsigned long long>(candidate),
            state % 900, state % 100000, state % 100000);
      } else {
        length = snprintf(
            line, sizeof(line),
            "2026-11-17T12:%02u:%02u.%03uZ INFO  worker=%02u req=%08x "
            "seq=%012llu latency_ms=%u status=200 path=/api/v1/resource/%u "
            "payload={\"id\":%u,\"state\":\"ok\",\"user\":\"u%05u\"}\n",
            static_cast<unsigned>(candidate / 60 % 60),
            static_cast<unsigned>(candidate % 60), state % 1000,
            state % 32, state, static_cast<unsigned long long>(candidate),
            state % 250, state % 100000, state % 100000, state % 100000);
      }
      if (length <= 0) continue;
      if (buffer.size() + static_cast<size_t>(length) > buffer_capacity) break;
      buffer.append(line, static_cast<size_t>(length));
      line_number = candidate;
    }

    DWORD chunk = static_cast<DWORD>(buffer.size());
    DWORD wrote = 0;
    if (!WriteFile(file, buffer.data(), chunk, &wrote, nullptr) || wrote == 0) {
      wprintf(L"write failed (error %lu)\n", GetLastError());
      CloseHandle(file);
      return 3;
    }
    written += wrote;
  }

  CloseHandle(file);
  wprintf(L"generated %ls: %d MB, %llu lines\n", path.c_str(), megabytes,
          static_cast<unsigned long long>(line_number));
  return 0;
}
