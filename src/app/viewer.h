#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "app/settings.h"
#include "app/theme.h"
#include "common/cancel_token.h"
#include "edit/editable_text.h"
#include "extract/extractor.h"
#include "file/encoding.h"
#include "file/line_index.h"
#include "file/mapped_file.h"
#include "render/canvas.h"

namespace uedit {

struct DisplayHit {
  uint64_t offset = 0;
  uint32_t length = 0;
  uint64_t line = 0;
  std::wstring context;
  uint32_t match_pos = 0;
  uint32_t match_len = 0;
  mutable Microsoft::WRL::ComPtr<IDWriteTextLayout> context_layout;
  mutable float context_width = 0.0f;
  mutable bool width_measured = false;
  mutable float measured_width = 0.0f;
  mutable bool match_measured = false;
  mutable float match_x1 = 0.0f;
  mutable float match_x2 = 0.0f;
};

struct TextPos {
  uint64_t line = 1;
  uint32_t index = 0;
};

class Viewer {
 public:
  Viewer() = default;
  ~Viewer();

  Viewer(const Viewer&) = delete;
  Viewer& operator=(const Viewer&) = delete;

  bool Initialize(HWND hwnd, const Settings& settings);
  void Shutdown();
  bool OpenFile(const std::wstring& path);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  // Automation hooks (command line / tests).
  void RunSearch(const std::wstring& query);
  void GoToLine(uint64_t line);
  void SetTheme(const std::wstring& name);
  void ShowCommandPalette();
  void SetZoomPercent(int percent);
  void SetRegexMode(bool enabled);
  void ShowFindBar();
  void ShowExtractPanel();
  void ShowSettingsPanel();
  void RunExtractTo(const std::wstring& output, bool cut);
  void ReloadFile();

 private:
  enum class InputMode { None, Goto, Find, Palette, Extract, Settings };

  struct EditRecord {
    uint64_t offset = 0;
    std::string removed;
    std::string inserted;
  };

  struct Document {
    std::wstring path;
    std::unique_ptr<MappedFile> file;
    EncodingInfo encoding;
    bool editable = false;
    bool dirty = false;
    EditableText editable_text;
    std::vector<EditRecord> undo_stack;
    std::vector<EditRecord> redo_stack;
    LineIndex index;
    std::thread index_thread;
    CancelToken index_cancel;

    uint64_t first_line = 1;
    float h_offset = 0.0f;
    TextPos caret;
    TextPos anchor;
    std::wstring eol_name;
    bool binary = false;

    FILETIME file_write_time{};
    uint64_t file_size_snapshot = 0;
    bool external_changed = false;
    bool read_only = false;

    std::wstring query;
    std::wstring query_wide;
    std::thread search_thread;
    std::shared_ptr<CancelToken> search_cancel;
    std::atomic<uint64_t> search_scanned{0};
    std::atomic<bool> search_finished{false};
    std::atomic<bool> search_pattern_error{false};
    std::wstring pending_search_message;
    bool searching = false;
    std::vector<DisplayHit> results;
    std::vector<DisplayHit> pending_results;
    double search_seconds = 0.0;
    uint64_t total_hits = 0;
    bool truncated = false;
    bool search_error = false;
    std::wstring search_message;
    bool ansi_case_forced = false;
    int panel_scroll = 0;
    size_t current_hit = 0;
    ULONGLONG search_start_tick = 0;

    bool results_open = false;
    size_t results_selected = 0;
    float results_h_offset = 0.0f;
    int results_hover_row = -1;
    int results_hover_button = -1;
    std::wstring pending_extract_output;
    bool pending_extract_cut = false;
  };

  struct PaletteCommand {
    std::wstring title;
    std::wstring shortcut;
    std::function<void()> run;
  };

  struct PaletteGeometry {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float row_height = 34.0f;
    int rows = 0;
  };

  struct ResultsGeometry {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float list_x = 0.0f;
    float list_y = 0.0f;
    float list_right = 0.0f;
    float list_bottom = 0.0f;
    float action_x = 0.0f;
    float action_width = 0.0f;
    int rows = 0;
  };

  void Paint();
  void OnSize(int width_px, int height_px);
  void OnTimer();
  void OnWheel(int delta, bool shift, int screen_x, int screen_y);
  void OnKeyDown(WPARAM key);
  void OnChar(wchar_t ch);
  void OnLButtonDown(int client_x, int client_y);
  void OnLButtonUp();
  void OnMouseMove(int client_x, int client_y);
  void OnNcMouseMove(int screen_x, int screen_y);
  void OnNcMouseLeave();

  bool HasDocument() const { return !docs_.empty(); }
  Document& doc() { return docs_.empty() ? blank_doc_ : *docs_[active_]; }
  const Document& doc() const {
    return docs_.empty() ? blank_doc_ : *docs_[active_];
  }

  void JumpToLine(uint64_t line);
  void JumpToOffset(uint64_t offset);
  void JumpToLineFromScroll(float y_dip);
  void NextHit();

  void StartSearch();
  void CancelSearch();
  void CancelSearch(Document& document);
  void PollSearch();
  void PollDocumentSearch(Document& document);
  void MaybeStartPendingExtraction(Document& document);
  bool LoadDocument(Document& document, const std::wstring& path,
                    std::wstring* error);
  void ActivateDocument(size_t index);
  bool CloseActiveDocument();
  bool CloseTabAt(size_t index);
  void CloseAllDocuments();
  void ReplaceActiveDocument(const std::wstring& path);
  void CycleTab(int delta);
  bool PromptSaveAll();
  void OpenFileDialog();
  void ApplyWindowChrome();
  void SyncCompositionStyle();
  void CycleThemeMode();
  void RefreshSystemTheme();
  void SaveSettings();
  void BuildCommands();
  void ExecuteCommand(size_t index);
  void RebuildPaletteMatches();
  void RunPaletteSelection();
  void AddSearchHistory(const std::wstring& query);
  void CycleSearchHistory(int delta);
  void StartExtraction();
  void BeginExtraction(const std::wstring& output);
  void CancelExtraction();
  void PollExtraction();
  void AdjustExtractOption(int delta);
  void DrawExtractPanel(float width, float height);
  void DrawExtractProgress(float width, float height);
  void SaveHitsAndOpen();
  void CopyHitsToClipboard();
  std::wstring BuildHitsPath(const std::wstring& source) const;
  void EnsureResultsSelectionVisible(int rows);
  void UpdateResultsVScroll(float y_dip);
  void UpdateResultsHScroll(float x_dip);
  const uint8_t* Data() const;
  uint64_t DataSize() const;
  uint64_t TotalLines() const;
  uint64_t EstimatedLines() const;
  bool IndexComplete() const;
  double IndexProgress() const;
  uint64_t LineStart(uint64_t line) const;
  uint64_t LineEnd(uint64_t line) const;
  uint64_t OffsetToLine(uint64_t offset) const;
  bool NextLineStart(uint64_t pos, uint64_t max_scan, uint64_t* next) const;
  uint64_t ByteOffsetForPos(const TextPos& position) const;
  TextPos PosFromByte(uint64_t offset) const;
  std::wstring EolWide() const;
  std::wstring NormalizeEol(const std::wstring& text) const;
  void ReplaceDocumentRange(uint64_t begin, uint64_t end,
                            const std::string& inserted, bool record_undo);
  void AfterDocumentEdit(uint64_t caret_offset);
  void InsertTextAtCaret(const std::wstring& text, bool replace_selection);
  void DeleteSelection();
  void DeleteBackward();
  void DeleteForward();
  void InsertNewline();
  void PasteClipboard();
  void PasteIntoInput();
  void CutSelection();
  void UndoEdit();
  void RedoEdit();
  void SaveDocument();
  bool PromptSaveIfDirty();
  void UpdateDocumentTitle();
  void AddRecentFile(const std::wstring& path);
  void AdjustSetting(int delta);
  void DrawSettingsPanel(float width, float height);
  void RefreshFileStamp(Document& document);
  void CheckExternalChange(Document& document);

  void RebuildVisibleOffsets();
  std::wstring DecodeLine(uint64_t begin, uint64_t end, bool* truncated) const;
  std::wstring DecodeBytes(uint64_t begin, uint64_t end, uint64_t max_bytes,
                           bool expand_tabs) const;
  std::wstring LineText(uint64_t line, uint64_t max_bytes,
                        bool expand_tabs) const;
  Microsoft::WRL::ComPtr<IDWriteTextLayout> GetLineLayout(
      uint64_t line, const std::wstring& text) const;
  const std::vector<std::pair<float, float>>& GetMatchRects(
      uint64_t line, const std::wstring& text,
      const std::vector<std::pair<size_t, size_t>>& ranges) const;

  bool HasSelection() const;
  TextPos SelectionStart() const;
  TextPos SelectionEnd() const;
  void ClearSelection();
  void SetCaret(TextPos position, bool extend);
  void MoveCaret(int dx, int dy, bool extend, bool by_word);
  void MoveCaretToDocumentEdge(bool end, bool extend);
  void SelectAll();
  TextPos HitTestPosition(float x_dip, float y_dip) const;
  void SelectWordAt(TextPos position);
  void EnsureCaretVisible();
  std::wstring SelectedText(size_t max_chars, bool* truncated) const;
  void CopySelection();
  void SetNotice(const std::wstring& text);
  void ResetInputCaret();

  float ContentTop() const;
  void DrawTitleBar(float width);
  void DrawTabBar(float width);
  void DrawWelcome(float width, float height);
  void DrawScrollbar(float width, float text_top, float text_bottom,
                     float panel_width);
  void DrawHorizontalScrollbar(float text_top, float text_bottom);
  void UpdateHorizontalScrollFromX(float x_dip);
  void DrawSearchOverlay(float width, float height, float text_x,
                         float text_right);
  void DrawStatusBar(float width, float height, float status_height);
  void DrawPalette(float width, float height);
  PaletteGeometry PaletteLayout(float width, float height) const;
  ResultsGeometry ResultsLayout(float width, float height) const;
  void DrawResultsPopup(float width, float height);
  void HandleResultsAction(int action);

  int TitleButtonAt(float x_dip, float y_dip) const;
  void GetTitleButtonRect(int index, float width, float* left, float* top,
                          float* right, float* bottom) const;
  int TabAt(float x_dip, float y_dip, bool* close) const;

  float ClientWidthDip() const;
  float ClientHeightDip() const;
  float ToDipX(int pixel) const;
  float ToDipY(int pixel) const;

  HWND hwnd_ = nullptr;
  Canvas canvas_;
  bool initialized_ = false;
  Settings settings_;
  Theme theme_ = DarkTheme();
  bool window_active_ = true;
  bool mica_enabled_ = false;
  bool composition_style_ = false;
  int title_hover_ = -1;

  std::vector<std::unique_ptr<Document>> docs_;
  size_t active_ = 0;
  mutable Document blank_doc_;
  std::vector<std::array<float, 4>> tab_rects_;
  std::vector<std::array<float, 4>> tab_close_rects_;
  std::array<float, 4> tab_plus_rect_{};
  int tab_hover_ = -1;

  int visible_lines_ = 40;
  std::vector<uint64_t> line_offsets_;
  bool line_offsets_valid_ = false;
  float h_content_width_ = 0.0f;

  struct LineRenderCache {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    uint64_t generation = 0;
    std::vector<std::pair<float, float>> match_rects;
  };
  mutable std::unordered_map<uint64_t, LineRenderCache> line_render_cache_;
  uint64_t render_generation_ = 1;

  bool caret_visible_ = true;
  ULONGLONG caret_blink_tick_ = 0;
  bool dragging_selection_ = false;
  int click_count_ = 0;
  ULONGLONG last_click_tick_ = 0;
  int last_click_x_ = 0;
  int last_click_y_ = 0;

  bool h_dragging_ = false;
  float h_track_x_ = 0.0f;
  float h_track_y_ = 0.0f;
  float h_track_width_ = 0.0f;
  float h_thumb_x_ = 0.0f;
  float h_thumb_width_ = 0.0f;

  float text_area_x_ = 0.0f;
  float text_area_y_ = 0.0f;
  float text_area_width_ = 0.0f;
  float text_area_bottom_ = 0.0f;

  std::wstring notice_;
  ULONGLONG notice_until_ = 0;

  int settings_row_ = 0;
  float settings_row_rects_[8][4]{};

  ULONGLONG last_external_check_ = 0;

  ExtractMode extract_mode_ = ExtractMode::WholeLines;
  int extract_context_ = 0;
  bool extract_dedupe_ = false;
  bool extract_cut_ = false;
  bool extract_backup_ = true;
  bool extract_open_after_ = false;
  int extract_row_ = 0;
  float extract_row_rects_[6][4]{};

  bool extract_running_ = false;
  std::thread extract_thread_;
  std::shared_ptr<CancelToken> extract_cancel_;
  std::atomic<bool> extract_finished_{false};
  std::atomic<bool> extract_success_{false};
  std::atomic<bool> extract_cancelled_{false};
  std::atomic<bool> extract_cut_done_{false};
  std::atomic<bool> extract_hits_truncated_{false};
  std::atomic<uint64_t> extract_written_{0};
  std::atomic<uint64_t> extract_total_{0};
  std::atomic<int> extract_stage_{0};
  std::wstring extract_message_;
  std::wstring extract_output_;
  uint64_t extract_bytes_out_ = 0;

  InputMode input_mode_ = InputMode::None;
  float zoom_ = 1.0f;
  std::wstring input_text_;
  bool case_sensitive_ = false;
  bool whole_word_ = false;
  bool regex_ = false;
  std::vector<std::wstring> search_history_;
  int history_index_ = -1;
  float search_chip_rects_[3][4]{};
  float search_box_rect_[4]{};

  std::vector<PaletteCommand> commands_;
  std::wstring palette_input_;
  std::vector<size_t> palette_matches_;
  int palette_selected_ = 0;

  int panel_rows_visible_ = 20;

  float results_button_rects_[6][4]{};
  float results_close_rect_[4]{};
  float results_box_rect_[4]{};
  float results_vscroll_track_[4]{};
  float results_vscroll_thumb_[4]{};
  float results_hscroll_track_[4]{};
  float results_hscroll_thumb_[4]{};
  float results_content_width_ = 0.0f;
  float results_vscroll_grab_ = 0.0f;
  float results_hscroll_grab_ = 0.0f;
  bool results_vscroll_dragging_ = false;
  bool results_hscroll_dragging_ = false;
  bool results_list_dragging_ = false;
  bool results_drag_moved_ = false;
  float results_drag_start_x_ = 0.0f;
  float results_drag_start_y_ = 0.0f;
  float results_drag_start_h_ = 0.0f;
  int results_drag_start_scroll_ = 0;
  ULONGLONG results_last_click_tick_ = 0;
  int results_last_click_row_ = -1;

  bool scroll_dragging_ = false;
  float scroll_x_ = 0.0f;
  float scroll_top_ = 0.0f;
  float scroll_height_ = 0.0f;
  float scroll_thumb_top_ = 0.0f;
  float scroll_thumb_height_ = 0.0f;

  LARGE_INTEGER perf_freq_{};
  double frame_ms_ = 0.0;
  double memory_mb_ = 0.0;
  ULONGLONG last_memory_tick_ = 0;
};

}  // namespace uedit
