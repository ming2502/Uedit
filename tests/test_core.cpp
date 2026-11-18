#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "edit/editable_text.h"
#include "extract/extractor.h"
#include "file/encoding.h"
#include "file/line_index.h"
#include "search/literal_search.h"
#include "search/search.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                   #condition);                                            \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

#define CHECK_EQ(actual, expected)                                         \
  do {                                                                     \
    auto actual_value = (actual);                                          \
    auto expected_value = (expected);                                      \
    if (!(actual_value == expected_value)) {                               \
      std::fprintf(stderr,                                                 \
                   "FAIL %s:%d: %s == %s (got %llu, want %llu)\n",         \
                   __FILE__, __LINE__, #actual, #expected,                 \
                   static_cast<unsigned long long>(actual_value),          \
                   static_cast<unsigned long long>(expected_value));       \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

#define CHECK_STREQ(actual, expected)                                      \
  do {                                                                     \
    if (!((actual) == (expected))) {                                       \
      std::fprintf(stderr, "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__,   \
                   #actual, #expected);                                    \
      ++g_failures;                                                        \
    }                                                                      \
  } while (0)

std::vector<uint8_t> ToUtf16Bytes(const std::wstring& text, bool big_endian) {
  std::vector<uint8_t> bytes;
  bytes.reserve(text.size() * 2);
  for (wchar_t value : text) {
    const uint16_t unit = static_cast<uint16_t>(value);
    if (big_endian) {
      bytes.push_back(static_cast<uint8_t>((unit >> 8) & 0xFF));
      bytes.push_back(static_cast<uint8_t>(unit & 0xFF));
    } else {
      bytes.push_back(static_cast<uint8_t>(unit & 0xFF));
      bytes.push_back(static_cast<uint8_t>((unit >> 8) & 0xFF));
    }
  }
  return bytes;
}

void TestLineIndexLf() {
  const std::string text = "alpha\r\nbeta\ngamma\n";
  uedit::LineIndex index;
  index.Prepare(reinterpret_cast<const uint8_t*>(text.data()), text.size(), 0,
                1, false);
  index.Build(nullptr);

  CHECK(index.complete());
  CHECK_EQ(index.total_lines(), 4);
  CHECK_EQ(index.line_start(1), 0);
  CHECK_EQ(index.line_start(2), 7);
  CHECK_EQ(index.line_start(3), 12);
  CHECK_EQ(index.line_start(4), 18);
  CHECK_EQ(index.line_end(1), 7);
  CHECK_EQ(index.line_end(2), 12);
  CHECK_EQ(index.offset_to_line(0), 1);
  CHECK_EQ(index.offset_to_line(6), 1);
  CHECK_EQ(index.offset_to_line(7), 2);
  CHECK_EQ(index.offset_to_line(12), 3);
  CHECK_EQ(index.offset_to_line(18), 4);
}

void TestLineIndexNoTrailingNewline() {
  const std::string text = "one\ntwo";
  uedit::LineIndex index;
  index.Prepare(reinterpret_cast<const uint8_t*>(text.data()), text.size(), 0,
                1, false);
  index.Build(nullptr);
  CHECK_EQ(index.total_lines(), 2);
  CHECK_EQ(index.line_start(1), 0);
  CHECK_EQ(index.line_start(2), 4);
  CHECK_EQ(index.offset_to_line(7), 2);
}

void TestLineIndexUtf16() {
  const std::wstring wide = L"a\r\nb\n";
  for (bool big_endian : {false, true}) {
    const std::vector<uint8_t> bytes = ToUtf16Bytes(wide, big_endian);
    uedit::LineIndex index;
    index.Prepare(bytes.data(), bytes.size(), 0, 2, big_endian);
    index.Build(nullptr);
    CHECK(index.complete());
    CHECK_EQ(index.total_lines(), 3);
    CHECK_EQ(index.line_start(1), 0);
    CHECK_EQ(index.line_start(2), 6);
    CHECK_EQ(index.line_start(3), 10);
    CHECK_EQ(index.offset_to_line(6), 2);
  }
}

void TestLineIndexEmpty() {
  uedit::LineIndex index;
  index.Prepare(nullptr, 0, 0, 1, false);
  index.Build(nullptr);
  CHECK_EQ(index.total_lines(), 1);
  CHECK_EQ(index.line_start(1), 0);
}

void TestEncodingDetection() {
  const uint8_t bom_utf8[] = {0xEF, 0xBB, 0xBF, 'h', 'i'};
  uedit::EncodingInfo info =
      uedit::DetectEncoding(bom_utf8, sizeof(bom_utf8));
  CHECK(info.encoding == uedit::TextEncoding::Utf8);
  CHECK_EQ(info.bom_size, static_cast<uint32_t>(3));

  const uint8_t bom_utf16le[] = {0xFF, 0xFE, 'a', 0x00};
  info = uedit::DetectEncoding(bom_utf16le, sizeof(bom_utf16le));
  CHECK(info.encoding == uedit::TextEncoding::Utf16LE);
  CHECK_EQ(info.bom_size, static_cast<uint32_t>(2));

  const std::string chinese = "中文测试";
  info = uedit::DetectEncoding(
      reinterpret_cast<const uint8_t*>(chinese.data()), chinese.size());
  CHECK(info.encoding == uedit::TextEncoding::Utf8);

  const uint8_t invalid[] = {0x80, 0x81, 0x82, 0xFF};
  info = uedit::DetectEncoding(invalid, sizeof(invalid));
  CHECK(info.encoding == uedit::TextEncoding::Ansi);

  uedit::EncodingInfo utf8_info;
  const std::wstring decoded = uedit::DecodeToWide(
      reinterpret_cast<const uint8_t*>(chinese.data()), chinese.size(),
      utf8_info);
  CHECK(decoded == L"中文测试");
}

void TestLiteralSearch() {
  const std::string data = "foo bar foo foobar";
  uedit::SearchOptions options;
  options.case_sensitive = true;
  uedit::SearchOutcome outcome = uedit::LiteralSearch(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), "foo",
      options, 100, nullptr);
  CHECK_EQ(outcome.total_hits, 3);
  CHECK_EQ(outcome.hits.size(), static_cast<size_t>(3));
  CHECK_EQ(outcome.hits[0].offset, 0);
  CHECK_EQ(outcome.hits[1].offset, 8);
  CHECK_EQ(outcome.hits[2].offset, 12);
  CHECK(!outcome.truncated);

  options.whole_word = true;
  outcome = uedit::LiteralSearch(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), "foo",
      options, 100, nullptr);
  CHECK_EQ(outcome.total_hits, 2);

  options.whole_word = false;
  options.case_sensitive = false;
  outcome = uedit::LiteralSearch(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), "FOO",
      options, 100, nullptr);
  CHECK_EQ(outcome.total_hits, 3);

  uedit::SearchOptions case_sensitive;
  case_sensitive.case_sensitive = true;
  outcome = uedit::LiteralSearch(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), "foo",
      case_sensitive, 1, nullptr);
  CHECK_EQ(outcome.total_hits, 3);
  CHECK_EQ(outcome.hits.size(), static_cast<size_t>(1));
  CHECK(outcome.truncated);
}

void TestRegexSearch() {
  const std::string data = "foo bar\nbaz foo\nqux";
  uedit::SearchRequest request;
  request.data = reinterpret_cast<const uint8_t*>(data.data());
  request.size = data.size();
  request.encoding = uedit::TextEncoding::Utf8;
  request.codepage = 65001;

  uedit::SearchOptions options;
  options.regex = true;
  options.case_sensitive = true;

  uedit::SearchOutcome outcome =
      uedit::SearchBuffer(request, "fo+", options, 100, nullptr);
  CHECK(!outcome.pattern_error);
  CHECK_EQ(outcome.total_hits, 2);

  options.case_sensitive = false;
  outcome = uedit::SearchBuffer(request, "FOO", options, 100, nullptr);
  CHECK_EQ(outcome.total_hits, 2);

  outcome = uedit::SearchBuffer(request, "(", options, 100, nullptr);
  CHECK(outcome.pattern_error);
  CHECK(!outcome.error_message.empty());

  options.whole_word = true;
  outcome = uedit::SearchBuffer(request, "foo", options, 100, nullptr);
  CHECK_EQ(outcome.total_hits, 2);
}

void TestUtf16Search() {
  const std::wstring wide = L"foo bar\nfoo\n";
  for (bool big_endian : {false, true}) {
    const std::vector<uint8_t> bytes = ToUtf16Bytes(wide, big_endian);
    uedit::SearchRequest request;
    request.data = bytes.data();
    request.size = bytes.size();
    request.encoding = big_endian ? uedit::TextEncoding::Utf16BE
                                  : uedit::TextEncoding::Utf16LE;
    request.big_endian = big_endian;

    uedit::SearchOptions literal;
    literal.case_sensitive = true;
    uedit::SearchOutcome outcome =
        uedit::SearchBuffer(request, "foo", literal, 100, nullptr);
    CHECK_EQ(outcome.total_hits, 2);
    CHECK_EQ(outcome.hits[0].offset, static_cast<uint64_t>(0));
    CHECK_EQ(outcome.hits[1].offset, static_cast<uint64_t>(16));
    CHECK_EQ(outcome.hits[0].length, static_cast<uint32_t>(6));

    uedit::SearchOptions regex = literal;
    regex.regex = true;
    outcome = uedit::SearchBuffer(request, "foo\\s+bar", regex, 100, nullptr);
    CHECK_EQ(outcome.total_hits, 1);
    CHECK_EQ(outcome.hits[0].offset, static_cast<uint64_t>(0));
  }
}

void TestSearchChunkBoundary() {
  const uint64_t chunk = 8ull << 20;
  std::vector<uint8_t> data(static_cast<size_t>(chunk + 16), 'a');
  const char needle[] = "XYZ";

  // One match crosses the 8 MiB chunk boundary, one starts well inside the
  // next chunk.
  std::memcpy(data.data() + chunk - 2, needle, 3);
  std::memcpy(data.data() + chunk + 8, needle, 3);

  uedit::SearchOptions options;
  options.case_sensitive = true;
  uedit::SearchOutcome outcome = uedit::LiteralSearch(
      data.data(), data.size(), "XYZ", options, 10, nullptr);
  CHECK_EQ(outcome.total_hits, 2);
  CHECK_EQ(outcome.hits[0].offset, chunk - 2);
  CHECK_EQ(outcome.hits[1].offset, chunk + 8);
}

std::wstring TempPath(const wchar_t* suffix) {
  wchar_t directory[MAX_PATH]{};
  GetTempPathW(MAX_PATH, directory);
  return std::wstring(directory) + L"uedit_test_" +
         std::to_wstring(GetTickCount64()) + suffix;
}

bool WriteFileBytes(const std::wstring& path, const std::string& bytes) {
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const BOOL ok = WriteFile(handle, bytes.data(),
                            static_cast<DWORD>(bytes.size()), &written,
                            nullptr);
  CloseHandle(handle);
  return ok && written == bytes.size();
}

std::string ReadFileBytes(const std::wstring& path) {
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER size{};
  GetFileSizeEx(handle, &size);
  std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  if (!bytes.empty()) {
    ReadFile(handle, &bytes[0], static_cast<DWORD>(bytes.size()), &read,
             nullptr);
    bytes.resize(read);
  }
  CloseHandle(handle);
  return bytes;
}

void TestExtractRanges() {
  const std::string data = "aa\nbb\ncc\n";
  uedit::EncodingInfo encoding;
  uedit::SearchHit hit;
  hit.offset = 1;
  hit.length = 1;
  std::vector<uedit::SearchHit> hits{hit};

  auto whole = uedit::BuildExtractRanges(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), hits,
      uedit::ExtractMode::WholeLines, 0, false, encoding);
  CHECK_EQ(whole.size(), static_cast<size_t>(1));
  CHECK_EQ(whole[0].begin, static_cast<uint64_t>(0));
  CHECK_EQ(whole[0].end, static_cast<uint64_t>(3));

  auto context = uedit::BuildExtractRanges(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), hits,
      uedit::ExtractMode::WithContext, 1, false, encoding);
  CHECK_EQ(context.size(), static_cast<size_t>(1));
  CHECK_EQ(context[0].begin, static_cast<uint64_t>(0));
  CHECK_EQ(context[0].end, static_cast<uint64_t>(6));

  auto match = uedit::BuildExtractRanges(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), hits,
      uedit::ExtractMode::MatchOnly, 0, false, encoding);
  CHECK_EQ(match.size(), static_cast<size_t>(1));
  CHECK_EQ(match[0].begin, static_cast<uint64_t>(1));
  CHECK_EQ(match[0].end, static_cast<uint64_t>(2));

  const std::wstring wide = L"aa\nbb\n";
  const std::vector<uint8_t> utf16 = ToUtf16Bytes(wide, false);
  uedit::EncodingInfo utf16_info;
  utf16_info.encoding = uedit::TextEncoding::Utf16LE;
  uedit::SearchHit utf16_hit;
  utf16_hit.offset = 6;
  utf16_hit.length = 2;
  auto utf16_ranges = uedit::BuildExtractRanges(
      utf16.data(), utf16.size(), std::vector<uedit::SearchHit>{utf16_hit},
      uedit::ExtractMode::WholeLines, 0, false, utf16_info);
  CHECK_EQ(utf16_ranges.size(), static_cast<size_t>(1));
  CHECK_EQ(utf16_ranges[0].begin, static_cast<uint64_t>(6));
  CHECK_EQ(utf16_ranges[0].end, static_cast<uint64_t>(12));
}

void TestExtractWriteAndRewrite() {
  const std::string data = "aa\nbb\ncc\n";
  uedit::EncodingInfo encoding;
  uedit::SearchHit hit;
  hit.offset = 1;
  hit.length = 1;
  auto ranges = uedit::BuildExtractRanges(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(),
      std::vector<uedit::SearchHit>{hit}, uedit::ExtractMode::WholeLines, 0,
      false, encoding);
  const std::wstring output = TempPath(L"_extract.txt");
  DeleteFileW(output.c_str());

  uedit::ExtractWriteResult write = uedit::WriteExtractFile(
      reinterpret_cast<const uint8_t*>(data.data()), data.size(), ranges,
      encoding, output, false, nullptr, nullptr);
  CHECK(write.ok);
  CHECK_STREQ(ReadFileBytes(output), std::string("aa\n"));
  DeleteFileW(output.c_str());

  const std::wstring source = TempPath(L"_cut.txt");
  CHECK(WriteFileBytes(source, data));
  uedit::RewriteResult rewrite = uedit::RewriteFileWithoutRanges(
      source, reinterpret_cast<const uint8_t*>(data.data()), data.size(),
      std::vector<uedit::ExtractRange>{{3, 6}}, true, nullptr, nullptr);
  CHECK(rewrite.ok);
  CHECK_STREQ(ReadFileBytes(source), std::string("aa\ncc\n"));
  CHECK(GetFileAttributesW((source + L".bak").c_str()) !=
        INVALID_FILE_ATTRIBUTES);
  DeleteFileW(source.c_str());
  DeleteFileW((source + L".bak").c_str());
}

void TestEditableText() {
  const std::string text = "aa\nbb\ncc\n";
  uedit::EncodingInfo encoding;
  uedit::EditableText editable;
  CHECK(editable.Load(reinterpret_cast<const uint8_t*>(text.data()),
                      text.size(), encoding));
  CHECK_EQ(editable.total_lines(), static_cast<uint64_t>(4));
  CHECK_EQ(editable.line_start(1), static_cast<uint64_t>(0));
  CHECK_EQ(editable.line_start(2), static_cast<uint64_t>(3));
  CHECK_EQ(editable.line_start(3), static_cast<uint64_t>(6));
  CHECK_EQ(editable.line_start(4), static_cast<uint64_t>(9));
  CHECK_EQ(editable.content_end(1), static_cast<uint64_t>(2));
  CHECK_EQ(editable.offset_to_line(4), static_cast<uint64_t>(2));

  editable.Insert(1, "X");
  CHECK_EQ(editable.size(), static_cast<uint64_t>(10));
  CHECK_STREQ(editable.Slice(0, editable.size()), std::string("aXa\nbb\ncc\n"));
  CHECK_EQ(editable.total_lines(), static_cast<uint64_t>(4));

  editable.Insert(3, "\n");
  CHECK_EQ(editable.total_lines(), static_cast<uint64_t>(5));
  CHECK_EQ(editable.line_start(2), static_cast<uint64_t>(4));
  CHECK_EQ(editable.line_start(3), static_cast<uint64_t>(5));

  editable.Erase(0, 5);
  CHECK_STREQ(editable.Slice(0, editable.size()), std::string("bb\ncc\n"));

  const std::string chinese = "中a\n";
  uedit::EditableText unicode;
  CHECK(unicode.Load(reinterpret_cast<const uint8_t*>(chinese.data()),
                     chinese.size(), encoding));
  CHECK_EQ(unicode.byte_offset_for_char(1, 0), static_cast<uint64_t>(0));
  CHECK_EQ(unicode.byte_offset_for_char(1, 1), static_cast<uint64_t>(3));
  CHECK_EQ(unicode.byte_offset_for_char(1, 2), static_cast<uint64_t>(4));
  CHECK_EQ(unicode.char_index_for_byte(1, 3), static_cast<uint32_t>(1));

  const std::wstring wide = L"中文\n";
  const std::vector<uint8_t> utf16 = ToUtf16Bytes(wide, true);
  uedit::EncodingInfo utf16_info;
  utf16_info.encoding = uedit::TextEncoding::Utf16BE;
  uedit::EditableText be;
  CHECK(be.Load(utf16.data(), utf16.size(), utf16_info));
  CHECK_EQ(be.total_lines(), static_cast<uint64_t>(2));
  CHECK_EQ(be.byte_offset_for_char(1, 0), static_cast<uint64_t>(0));
  CHECK_EQ(be.byte_offset_for_char(1, 1), static_cast<uint64_t>(2));
  CHECK_EQ(be.byte_offset_for_char(1, 2), static_cast<uint64_t>(4));
  CHECK_STREQ(be.EncodeWide(L"中"), std::string("\x4E\x2D"));
}

}  // namespace

int main() {
  TestLineIndexLf();
  TestLineIndexNoTrailingNewline();
  TestLineIndexUtf16();
  TestLineIndexEmpty();
  TestEncodingDetection();
  TestLiteralSearch();
  TestRegexSearch();
  TestUtf16Search();
  TestSearchChunkBoundary();
  TestExtractRanges();
  TestExtractWriteAndRewrite();
  TestEditableText();

  if (g_failures == 0) {
    std::printf("all tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d test check(s) failed\n", g_failures);
  return 1;
}
