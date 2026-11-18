#include "render/canvas.h"

#include <d2d1helper.h>

#include <algorithm>

#include "common/log.h"

namespace uedit {
namespace {

bool CompositionDisabled() {
  wchar_t value[8]{};
  return GetEnvironmentVariableW(L"UEDIT_NO_COMPOSITION", value, 8) != 0;
}

}  // namespace

D2D1_COLOR_F Color(uint32_t rgb, float alpha) {
  return D2D1::ColorF(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                      (rgb & 0xFF) / 255.0f, alpha);
}

bool Canvas::Initialize(HWND hwnd) {
  hwnd_ = hwnd;
  D2D1_FACTORY_OPTIONS options{};
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               __uuidof(ID2D1Factory1), &options,
                               reinterpret_cast<void**>(
                                   factory_.GetAddressOf())))) {
    return false;
  }
  if (FAILED(DWriteCreateFactory(
          DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
          reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
    return false;
  }
  if (!CreateFormats()) return false;

  if (!CompositionDisabled()) {
    InitializeComposition();
  }
  return true;
}

bool Canvas::InitializeComposition() {
  UINT creation_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creation_flags, nullptr, 0,
      D3D11_SDK_VERSION, d3d_device_.GetAddressOf(), nullptr, nullptr);
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                           creation_flags, nullptr, 0, D3D11_SDK_VERSION,
                           d3d_device_.GetAddressOf(), nullptr, nullptr);
  }
  if (FAILED(hr)) return false;

  if (FAILED(d3d_device_.As(&dxgi_device_))) return false;
  if (FAILED(dxgi_device_->GetAdapter(dxgi_adapter_.GetAddressOf()))) {
    return false;
  }
  if (FAILED(dxgi_adapter_->GetParent(
          __uuidof(IDXGIFactory2),
          reinterpret_cast<void**>(dxgi_factory_.GetAddressOf())))) {
    return false;
  }
  if (FAILED(factory_->CreateDevice(dxgi_device_.Get(),
                                    d2d_device_.GetAddressOf()))) {
    return false;
  }
  if (FAILED(d2d_device_->CreateDeviceContext(
          D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
          d2d_context_.GetAddressOf()))) {
    return false;
  }
  d2d_context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

  if (FAILED(DCompositionCreateDevice(
          dxgi_device_.Get(), __uuidof(IDCompositionDevice),
          reinterpret_cast<void**>(dcomp_device_.GetAddressOf())))) {
    return false;
  }
  if (FAILED(dcomp_device_->CreateTargetForHwnd(hwnd_, TRUE,
                                                dcomp_target_.GetAddressOf()))) {
    return false;
  }
  if (FAILED(dcomp_device_->CreateVisual(dcomp_visual_.GetAddressOf()))) {
    return false;
  }
  if (FAILED(dcomp_target_->SetRoot(dcomp_visual_.Get()))) return false;
  if (FAILED(dcomp_device_->Commit())) return false;

  composition_ = true;
  return true;
}

void Canvas::Shutdown() {
  brush_.Reset();
  rt_.Reset();
  target_bitmap_.Reset();
  composition_bitmaps_.clear();
  if (d2d_context_) d2d_context_->SetTarget(nullptr);
  dcomp_visual_.Reset();
  dcomp_target_.Reset();
  dcomp_device_.Reset();
  d2d_context_.Reset();
  d2d_device_.Reset();
  swap_chain_.Reset();
  dxgi_factory_.Reset();
  dxgi_adapter_.Reset();
  dxgi_device_.Reset();
  d3d_device_.Reset();
  composition_ = false;
  target_ = nullptr;

  mono_.Reset();
  mono_small_.Reset();
  gutter_.Reset();
  ui_.Reset();
  ui_small_.Reset();
  ui_bold_.Reset();
  title_.Reset();
  hint_.Reset();
  center_small_.Reset();
  logo_.Reset();
  dwrite_.Reset();
  factory_.Reset();
  hwnd_ = nullptr;
}

bool Canvas::CreateFormats() {
  auto make = [&](const wchar_t* family, DWRITE_FONT_WEIGHT weight, float size,
                  DWRITE_TEXT_ALIGNMENT alignment,
                  ComPtr<IDWriteTextFormat>* out) -> bool {
    ComPtr<IDWriteTextFormat> format;
    HRESULT hr = dwrite_->CreateTextFormat(
        family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, size, L"", format.GetAddressOf());
    if (FAILED(hr)) return false;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTextAlignment(alignment);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    *out = format;
    return true;
  };

  const bool created =
      make(L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, 14.0f,
           DWRITE_TEXT_ALIGNMENT_LEADING, &mono_) &&
      make(L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, 13.0f,
           DWRITE_TEXT_ALIGNMENT_LEADING, &mono_small_) &&
         make(L"Consolas", DWRITE_FONT_WEIGHT_NORMAL, 13.0f,
              DWRITE_TEXT_ALIGNMENT_TRAILING, &gutter_) &&
         make(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.5f,
              DWRITE_TEXT_ALIGNMENT_LEADING, &ui_) &&
         make(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 11.5f,
              DWRITE_TEXT_ALIGNMENT_LEADING, &ui_small_) &&
         make(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 13.0f,
              DWRITE_TEXT_ALIGNMENT_LEADING, &ui_bold_) &&
         make(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD, 34.0f,
              DWRITE_TEXT_ALIGNMENT_CENTER, &title_) &&
         make(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 14.0f,
              DWRITE_TEXT_ALIGNMENT_CENTER, &hint_) &&
         make(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL, 12.0f,
              DWRITE_TEXT_ALIGNMENT_CENTER, &center_small_) &&
         make(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD, 11.5f,
              DWRITE_TEXT_ALIGNMENT_CENTER, &logo_);
  if (created) {
    mono_->SetIncrementalTabStop(32.0f);
    mono_small_->SetIncrementalTabStop(30.0f);
    title_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    hint_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    center_small_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    logo_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  }
  return created;
}

bool Canvas::CreateBrush() {
  brush_.Reset();
  ID2D1RenderTarget* target = Target();
  if (target == nullptr) return false;
  return SUCCEEDED(
      target->CreateSolidColorBrush(Color(0xFFFFFF), brush_.GetAddressOf()));
}

bool Canvas::CreateTarget(UINT width_px, UINT height_px, float dpi_scale) {
  if (hwnd_ == nullptr) return false;
  width_px_ = width_px == 0 ? 1 : width_px;
  height_px_ = height_px == 0 ? 1 : height_px;
  system_scale_ = dpi_scale <= 0.0f ? 1.0f : dpi_scale;
  const float dpi = 96.0f * system_scale_ * zoom_;

  if (rt_) {
    rt_->SetDpi(dpi, dpi);
    HRESULT hr = rt_->Resize(D2D1::SizeU(width_px_, height_px_));
    if (SUCCEEDED(hr)) {
      target_ = rt_.Get();
      return true;
    }
    brush_.Reset();
    rt_.Reset();
  }

  D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
  D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_properties =
      D2D1::HwndRenderTargetProperties(
          hwnd_, D2D1::SizeU(width_px_, height_px_),
          D2D1_PRESENT_OPTIONS_NONE);
  if (FAILED(factory_->CreateHwndRenderTarget(
          properties, hwnd_properties, rt_.GetAddressOf()))) {
    rt_.Reset();
    target_ = nullptr;
    return false;
  }
  rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  target_ = rt_.Get();
  return CreateBrush();
}

bool Canvas::ResizeComposition(UINT width_px, UINT height_px,
                               float system_scale) {
  width_px_ = width_px == 0 ? 1 : width_px;
  height_px_ = height_px == 0 ? 1 : height_px;
  system_scale_ = system_scale <= 0.0f ? 1.0f : system_scale;
  const float dpi = 96.0f * system_scale_ * zoom_;

  if (!swap_chain_) {
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = width_px_;
    description.Height = height_px_;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.Stereo = FALSE;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(dxgi_factory_->CreateSwapChainForComposition(
            d3d_device_.Get(), &description, nullptr,
            swap_chain_.GetAddressOf()))) {
      return false;
    }
    buffer_width_ = width_px_;
    buffer_height_ = height_px_;
    if (FAILED(dcomp_visual_->SetContent(swap_chain_.Get()))) return false;
    if (FAILED(dcomp_device_->Commit())) return false;
  } else if (buffer_width_ != width_px_ || buffer_height_ != height_px_) {
    d2d_context_->SetTarget(nullptr);
    target_bitmap_.Reset();
    composition_bitmaps_.clear();
    const HRESULT resize =
        swap_chain_->ResizeBuffers(0, width_px_, height_px_,
                                   DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(resize)) {
      LogError(L"composition ResizeBuffers failed: " +
               std::to_wstring(resize));
      return false;
    }
    buffer_width_ = width_px_;
    buffer_height_ = height_px_;
  }

  d2d_context_->SetDpi(dpi, dpi);
  if (!EnsureCompositionTarget()) return false;
  return CreateBrush();
}

bool Canvas::EnsureCompositionTarget() {
  if (!swap_chain_ || !d2d_context_) return false;
  UINT index = 0;
  ComPtr<IDXGISwapChain3> swap_chain3;
  if (SUCCEEDED(swap_chain_.As(&swap_chain3))) {
    index = swap_chain3->GetCurrentBackBufferIndex();
  }
  if (index >= composition_bitmaps_.size()) {
    composition_bitmaps_.resize(index + 1);
  }
  ComPtr<ID2D1Bitmap1>& bitmap = composition_bitmaps_[index];
  if (!bitmap) {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_chain_->GetBuffer(
            index, __uuidof(IDXGISurface),
            reinterpret_cast<void**>(surface.GetAddressOf())))) {
      return false;
    }
    const float dpi = 96.0f * system_scale_ * zoom_;
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED),
        dpi, dpi);
    if (FAILED(d2d_context_->CreateBitmapFromDxgiSurface(
            surface.Get(), &properties, bitmap.GetAddressOf()))) {
      return false;
    }
  }
  if (target_bitmap_.Get() != bitmap.Get()) {
    d2d_context_->SetTarget(bitmap.Get());
    target_bitmap_ = bitmap;
  }
  target_ = d2d_context_.Get();
  return true;
}

bool Canvas::Resize(UINT width_px, UINT height_px, float dpi_scale) {
  if (composition_) {
    if (ResizeComposition(width_px, height_px, dpi_scale)) return true;
    LogError(L"composition resize failed; using hwnd target");
    composition_ = false;
    target_ = nullptr;
    target_bitmap_.Reset();
    composition_bitmaps_.clear();
  }
  return CreateTarget(width_px, height_px, dpi_scale);
}

void Canvas::SetZoom(float zoom) {
  zoom_ = std::max(0.5f, std::min(3.0f, zoom));
  const float dpi = 96.0f * system_scale_ * zoom_;
  if (composition_ && d2d_context_) {
    d2d_context_->SetDpi(dpi, dpi);
  }
  if (rt_) rt_->SetDpi(dpi, dpi);
}

bool Canvas::valid() const {
  if (composition_) return d2d_context_ != nullptr && target_bitmap_ != nullptr;
  return rt_ != nullptr;
}

void Canvas::BeginDraw(uint32_t clear_rgb) {
  if (composition_) {
    if (d2d_context_ == nullptr || !EnsureCompositionTarget()) return;
    d2d_context_->BeginDraw();
    d2d_context_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    return;
  }
  if (!rt_ && hwnd_ != nullptr) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (!CreateTarget(static_cast<UINT>(rc.right), static_cast<UINT>(rc.bottom),
                      system_scale_)) {
      return;
    }
  }
  if (!rt_) return;
  rt_->BeginDraw();
  rt_->Clear(Color(clear_rgb));
}

void Canvas::EndDraw() {
  if (composition_) {
    if (d2d_context_ == nullptr) return;
    HRESULT hr = d2d_context_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
      d2d_context_->SetTarget(nullptr);
      target_bitmap_.Reset();
      composition_bitmaps_.clear();
    }
    if (swap_chain_) {
      const HRESULT present = swap_chain_->Present(1, 0);
      if (FAILED(present)) {
        LogError(L"composition Present failed: " +
                 std::to_wstring(present));
      }
    }
    return;
  }
  if (!rt_) return;
  HRESULT hr = rt_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    brush_.Reset();
    rt_.Reset();
    target_ = nullptr;
  }
}

void Canvas::FillRect(float left, float top, float right, float bottom,
                      const D2D1_COLOR_F& color) {
  ID2D1RenderTarget* target = Target();
  if (!target || !brush_) return;
  brush_->SetColor(color);
  target->FillRectangle(D2D1::RectF(left, top, right, bottom), brush_.Get());
}

void Canvas::FillRoundedRect(float left, float top, float right, float bottom,
                             float radius, const D2D1_COLOR_F& color) {
  ID2D1RenderTarget* target = Target();
  if (!target || !brush_) return;
  brush_->SetColor(color);
  D2D1_ROUNDED_RECT rect = D2D1::RoundedRect(
      D2D1::RectF(left, top, right, bottom), radius, radius);
  target->FillRoundedRectangle(rect, brush_.Get());
}

void Canvas::DrawLine(float x1, float y1, float x2, float y2, float width,
                      const D2D1_COLOR_F& color) {
  ID2D1RenderTarget* target = Target();
  if (!target || !brush_) return;
  brush_->SetColor(color);
  target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2),
                   brush_.Get(), width, nullptr);
}

void Canvas::PushClip(float left, float top, float right, float bottom) {
  ID2D1RenderTarget* target = Target();
  if (!target) return;
  target->PushAxisAlignedClip(D2D1::RectF(left, top, right, bottom),
                              D2D1_ANTIALIAS_MODE_ALIASED);
}

void Canvas::PopClip() {
  ID2D1RenderTarget* target = Target();
  if (!target) return;
  target->PopAxisAlignedClip();
}

void Canvas::DrawTextW(const std::wstring& text, float left, float top,
                       float right, float bottom, IDWriteTextFormat* format,
                       const D2D1_COLOR_F& color, bool clip) {
  ID2D1RenderTarget* target = Target();
  if (!target || !brush_ || format == nullptr || text.empty()) return;
  brush_->SetColor(color);
  D2D1_RECT_F rect = D2D1::RectF(left, top, right, bottom);
  target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format,
                    rect, brush_.Get(),
                    clip ? D2D1_DRAW_TEXT_OPTIONS_CLIP
                         : D2D1_DRAW_TEXT_OPTIONS_NONE,
                    DWRITE_MEASURING_MODE_NATURAL);
}

void Canvas::DrawTextLayout(IDWriteTextLayout* layout, float x, float y,
                            const D2D1_COLOR_F& color) {
  ID2D1RenderTarget* target = Target();
  if (!target || !brush_ || layout == nullptr) return;
  brush_->SetColor(color);
  target->DrawTextLayout(D2D1::Point2F(x, y), layout, brush_.Get(),
                         D2D1_DRAW_TEXT_OPTIONS_NONE);
}

ComPtr<IDWriteTextLayout> Canvas::CreateLayout(const std::wstring& text,
                                               IDWriteTextFormat* format,
                                               float max_width,
                                               float max_height) const {
  ComPtr<IDWriteTextLayout> layout;
  if (!dwrite_ || format == nullptr || text.empty()) return layout;
  dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                            format, max_width, max_height,
                            layout.GetAddressOf());
  return layout;
}

float Canvas::MeasurePrefix(IDWriteTextLayout* layout,
                            size_t text_position) const {
  if (layout == nullptr) return 0.0f;
  FLOAT x = 0.0f;
  FLOAT y = 0.0f;
  DWRITE_HIT_TEST_METRICS metrics{};
  if (FAILED(layout->HitTestTextPosition(static_cast<UINT32>(text_position),
                                         FALSE, &x, &y, &metrics))) {
    return 0.0f;
  }
  return x;
}

}  // namespace uedit
