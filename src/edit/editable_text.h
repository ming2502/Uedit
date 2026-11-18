#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "file/encoding.h"

namespace uedit {

// In-memory editable document for small files. Line starts are rebuilt after
// each edit (acceptable up to the editable-size limit used by the app).
class EditableText {
 public:
  bool Load(const uint8_t* data, uint64_t size, const EncodingInfo& encoding);

  const uint8_t* data() const { return data_.data(); }
  uint64_t size() const { return data_.size(); }
  const EncodingInfo& encoding() const { return encoding_; }

  uint64_t total_lines() const { return starts_.size(); }
  uint64_t line_start(uint64_t line) const;
  uint64_t line_end(uint64_t line) const;
  uint64_t content_end(uint64_t line) const;
  uint64_t offset_to_line(uint64_t offset) const;
  bool next_line_start(uint64_t pos, uint64_t max_scan, uint64_t* next) const;

  void Insert(uint64_t offset, const std::string& bytes);
  void Erase(uint64_t begin, uint64_t end);
  void Replace(uint64_t begin, uint64_t end, const std::string& bytes);
  std::string Slice(uint64_t begin, uint64_t end) const;

  // Maps a caret position (UTF-16 code-unit index within the line's rendered
  // text) to a byte offset, and back.
  uint64_t byte_offset_for_char(uint64_t line, uint32_t char_index) const;
  uint32_t char_index_for_byte(uint64_t line, uint64_t offset) const;

  std::wstring DecodeRange(uint64_t begin, uint64_t end) const;
  std::string EncodeWide(const std::wstring& text) const;

 private:
  void Rebuild();

  std::vector<uint8_t> data_;
  std::vector<uint64_t> starts_;
  EncodingInfo encoding_;
  uint64_t bom_ = 0;
};

}  // namespace uedit
