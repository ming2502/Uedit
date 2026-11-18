#pragma once

#include <windows.h>

#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

namespace uedit {

using Microsoft::WRL::ComPtr;

D2D1_COLOR_F Color(uint32_t rgb, float alpha = 1.0f);

// Direct2D + DirectWrite wrapper. Prefers a DirectComposition swap chain with
// premultiplied alpha (needed for the DWM Mica backdrop) and falls back to an
// ID2D1HwndRenderTarget when composition is unavailable.
class Canvas {
 public:
  bool Initialize(HWND hwnd);
  void Shutdown();
  bool Resize(UINT width_px, UINT height_px, float dpi_scale);

  void BeginDraw(uint32_t clear_rgb);
  void EndDraw();

  void FillRect(float left, float top, float right, float bottom,
                const D2D1_COLOR_F& color);
  void FillRoundedRect(float left, float top, float right, float bottom,
                       float radius, const D2D1_COLOR_F& color);
  void DrawLine(float x1, float y1, float x2, float y2, float width,
                const D2D1_COLOR_F& color);
  void PushClip(float left, float top, float right, float bottom);
  void PopClip();
  void DrawTextW(const std::wstring& text, float left, float top, float right,
                 float bottom, IDWriteTextFormat* format,
                 const D2D1_COLOR_F& color, bool clip = true);
  void DrawTextLayout(IDWriteTextLayout* layout, float x, float y,
                      const D2D1_COLOR_F& color);
  ComPtr<IDWriteTextLayout> CreateLayout(const std::wstring& text,
                                         IDWriteTextFormat* format,
                                         float max_width,
                                         float max_height) const;
  float MeasurePrefix(IDWriteTextLayout* layout,
                      size_t text_position) const;

  IDWriteFactory* dwrite() const { return dwrite_.Get(); }
  IDWriteTextFormat* mono() const { return mono_.Get(); }
  IDWriteTextFormat* mono_small() const { return mono_small_.Get(); }
  IDWriteTextFormat* gutter() const { return gutter_.Get(); }
  IDWriteTextFormat* ui() const { return ui_.Get(); }
  IDWriteTextFormat* ui_small() const { return ui_small_.Get(); }
  IDWriteTextFormat* ui_bold() const { return ui_bold_.Get(); }
  IDWriteTextFormat* title() const { return title_.Get(); }
  IDWriteTextFormat* hint() const { return hint_.Get(); }
  IDWriteTextFormat* center_small() const { return center_small_.Get(); }
  IDWriteTextFormat* logo() const { return logo_.Get(); }

  float dpi_scale() const { return system_scale_ * zoom_; }
  void SetZoom(float zoom);
  bool valid() const;
  bool composition() const { return composition_; }

 private:
  bool CreateFormats();
  bool CreateTarget(UINT width_px, UINT height_px, float dpi_scale);
  bool InitializeComposition();
  bool ResizeComposition(UINT width_px, UINT height_px, float system_scale);
  bool EnsureCompositionTarget();
  bool CreateBrush();
  ID2D1RenderTarget* Target() const { return target_; }

  ComPtr<ID2D1Factory1> factory_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<ID2D1HwndRenderTarget> rt_;
  ComPtr<ID2D1SolidColorBrush> brush_;

  ComPtr<ID3D11Device> d3d_device_;
  ComPtr<IDXGIDevice> dxgi_device_;
  ComPtr<IDXGIAdapter> dxgi_adapter_;
  ComPtr<IDXGIFactory2> dxgi_factory_;
  ComPtr<IDXGISwapChain1> swap_chain_;
  ComPtr<ID2D1Device> d2d_device_;
  ComPtr<ID2D1DeviceContext> d2d_context_;
  ComPtr<ID2D1Bitmap1> target_bitmap_;
  std::vector<ComPtr<ID2D1Bitmap1>> composition_bitmaps_;
  ComPtr<IDCompositionDevice> dcomp_device_;
  ComPtr<IDCompositionTarget> dcomp_target_;
  ComPtr<IDCompositionVisual> dcomp_visual_;
  bool composition_ = false;
  UINT buffer_width_ = 0;
  UINT buffer_height_ = 0;

  ID2D1RenderTarget* target_ = nullptr;

  ComPtr<IDWriteTextFormat> mono_;
  ComPtr<IDWriteTextFormat> mono_small_;
  ComPtr<IDWriteTextFormat> gutter_;
  ComPtr<IDWriteTextFormat> ui_;
  ComPtr<IDWriteTextFormat> ui_small_;
  ComPtr<IDWriteTextFormat> ui_bold_;
  ComPtr<IDWriteTextFormat> title_;
  ComPtr<IDWriteTextFormat> hint_;
  ComPtr<IDWriteTextFormat> center_small_;
  ComPtr<IDWriteTextFormat> logo_;

  HWND hwnd_ = nullptr;
  float system_scale_ = 1.0f;
  float zoom_ = 1.0f;
  UINT width_px_ = 0;
  UINT height_px_ = 0;
};

}  // namespace uedit
