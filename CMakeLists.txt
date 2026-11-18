cmake_minimum_required(VERSION 3.12)
if (POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
project(uedit VERSION 1.1.1 LANGUAGES CXX)

# Static CRT so the portable build has no VC++ redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# VS2017 (MSVC 14.16) cannot do C++20; VS2022 (19.30+) uses C++20.
if (MSVC AND MSVC_VERSION GREATER_EQUAL 1930)
  set(CMAKE_CXX_STANDARD 20)
else()
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 PCRE2_STATIC)

# Vendored PCRE2 (8/16-bit, JIT) for regular expression search.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT ON CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_UNICODE ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/pcre2-10.44 EXCLUDE_FROM_ALL)
if (MSVC)
  # PCRE2's JIT sources are warning-noisy under /W4.
  target_compile_options(pcre2-8-static PRIVATE /wd4244 /wd4267)
  target_compile_options(pcre2-16-static PRIVATE /wd4244 /wd4267)
endif()

if (MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /EHsc /MP)
endif()

add_library(uedit_core STATIC
  src/common/log.cpp
  src/edit/editable_text.cpp
  src/file/mapped_file.cpp
  src/file/encoding.cpp
  src/file/line_index.cpp
  src/extract/extractor.cpp
  src/search/literal_search.cpp
  src/search/search.cpp
  src/search/regex_search.cpp
  src/search/regex8.cpp
  src/search/regex16.cpp
)
target_include_directories(uedit_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(uedit_core PUBLIC shell32 pcre2-8 pcre2-16)

enable_testing()

add_executable(uedit WIN32
  src/app/main.cpp
  src/app/viewer.cpp
  src/app/theme.cpp
  src/app/settings.cpp
  src/render/canvas.cpp
  res/uedit.rc
)
target_link_libraries(uedit PRIVATE uedit_core d2d1 dwrite dwmapi ole32 user32 gdi32 shell32 psapi dbghelp imm32 d3d11 dxgi dcomp)

add_executable(uedit_bench tools/bench.cpp)
target_link_libraries(uedit_bench PRIVATE uedit_core psapi)

add_executable(uedit_gen tools/gen_fixture.cpp)

add_executable(uedit_tests tests/test_core.cpp)
target_link_libraries(uedit_tests PRIVATE uedit_core)
add_test(NAME uedit_core COMMAND uedit_tests)
