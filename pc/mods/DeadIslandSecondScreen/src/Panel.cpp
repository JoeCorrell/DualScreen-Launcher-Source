#include "Panel.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Config.h"
#include "Layout.h"
#include "Log.h"
#include "Ui.h"

namespace dis {
namespace {

// Process-lifetime panel. Explicit shutdown is available outside DllMain, but
// normal process teardown must not destroy D3D/window state under loader lock.
Panel& g_panel = *new Panel();
const wchar_t* kClassName = L"DeadIslandSecondScreenPanel";
bool g_class_registered = false;

struct TabDefinition {
  const wchar_t* label;
  const wchar_t* binding;
  const wchar_t* alternate;
  WORD fallback;
};

constexpr TabDefinition kTabs[] = {
    {L"HUD",       nullptr,       nullptr,       VK_ESCAPE},
    {L"MAP",       L"Map1",      L"Map2",      'M'},
    {L"QUESTS",    L"Objectives1", L"Objectives2", 'U'},
    {L"INVENTORY", L"Inventory1", L"Inventory2", 'I'},
    {L"AUGS",      L"TechTree1", L"TechTree2", 'O'},
    {L"LOGS",      L"Logs1",     L"Logs2",     VK_OEM_4},
};

WORD ReadBinding(const TabDefinition& tab) {
  if (!tab.binding) return tab.fallback;
  constexpr wchar_t key[] = L"Software\\Eidos\\Deus Ex: HRDC\\Controls";
  DWORD type = 0;
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_CURRENT_USER, key, tab.binding,
                   RRF_RT_REG_DWORD, &type, &value, &size) == ERROR_SUCCESS &&
      value != 0 && value <= 0xFF) {
    return static_cast<WORD>(value);
  }
  value = 0;
  size = sizeof(value);
  if (tab.alternate &&
      RegGetValueW(HKEY_CURRENT_USER, key, tab.alternate,
                   RRF_RT_REG_DWORD, &type, &value, &size) == ERROR_SUCCESS &&
      value != 0 && value <= 0xFF) {
    return static_cast<WORD>(value);
  }
  return tab.fallback;
}

void SendKeyPress(WORD key) {
  INPUT input[2] = {};
  input[0].type = INPUT_KEYBOARD;
  input[0].ki.wVk = key;
  input[1] = input[0];
  input[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, input, sizeof(INPUT));
}

void DrawBar(HDC dc, const wchar_t* label, int y, COLORREF fill, float value,
             int left, int right) {
  const int width = (std::max)(1, right - left);
  const int bar_height = (std::max)(24, width / 34);
  const int label_height = (std::max)(28, bar_height * 2);

  RECT label_rect{left, y - label_height, right, y - 6};
  SetTextColor(dc, RGB(232, 224, 196));
  DrawTextW(dc, label, -1, &label_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  wchar_t value_text[64] = {};
  _snwprintf(value_text, 63, L"%.0f%%", (std::max)(0.0f, (std::min)(1.0f, value)) * 100.0f);
  RECT value_rect{left, y - label_height, right - 12, y - 6};
  DrawTextW(dc, value_text, -1, &value_rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

  RECT back{left, y, right, y + bar_height};
  HBRUSH back_brush = CreateSolidBrush(RGB(26, 28, 28));
  FillRect(dc, &back, back_brush);
  DeleteObject(back_brush);

  const int fill_width = static_cast<int>(width * (std::max)(0.0f, (std::min)(1.0f, value)));
  if (fill_width > 0) {
    RECT front{left + 2, y + 2, left + fill_width - 2, y + bar_height - 2};
    HBRUSH front_brush = CreateSolidBrush(fill);
    FillRect(dc, &front, front_brush);
    DeleteObject(front_brush);
  }

  HPEN edge = CreatePen(PS_SOLID, 2, RGB(72, 58, 28));
  HGDIOBJ old_pen = SelectObject(dc, edge);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, left, y, right, y + bar_height);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(edge);
}

void DrawCustomHudToDc(HDC dc, int width, int height, float health, float stamina) {
  RECT full{0, 0, width, height};
  HBRUSH background = CreateSolidBrush(RGB(8, 10, 12));
  FillRect(dc, &full, background);
  DeleteObject(background);

  const int margin = (std::max)(48, width / 18);
  HFONT title_font = CreateFontW(-(std::max)(42, width / 26), 0, 0, 0, FW_BOLD, FALSE,
                                 FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FF_DONTCARE, L"Impact");
  HFONT label_font = CreateFontW(-(std::max)(24, width / 48), 0, 0, 0, FW_SEMIBOLD,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FF_DONTCARE, L"Segoe UI Semibold");

  HGDIOBJ old_title = title_font ? SelectObject(dc, title_font) : nullptr;
  SetBkMode(dc, TRANSPARENT);
  RECT title{0, height / 16, width, height / 8};
  SetTextColor(dc, RGB(224, 164, 48));
  DrawTextW(dc, L"DEAD ISLAND", -1, &title, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (old_title && title_font) SelectObject(dc, old_title);

  HGDIOBJ old_label = label_font ? SelectObject(dc, label_font) : nullptr;
  DrawBar(dc, L"HEALTH", height * 4 / 10, RGB(171, 39, 28), health, margin, width - margin);
  DrawBar(dc, L"STAMINA", height * 5 / 10, RGB(53, 125, 52), stamina, margin, width - margin);
  if (old_label && label_font) SelectObject(dc, old_label);
  if (title_font) DeleteObject(title_font);
  if (label_font) DeleteObject(label_font);
}

LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CLOSE:
      // The panel is not a window the player owns; closing it would leave the
      // interface with nowhere to go while the session carried on.
      return 0;
    case WM_ERASEBKGND:
      return 1;  // the swapchain owns every pixel
    case WM_MOUSEACTIVATE:
      // Touching the panel must never take focus off the game, or the keyboard
      // and controller stop reaching the player character.
      return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
      // Move is routed as well as down and up so a press that slides off its
      // control can cancel, rather than firing wherever the finger lifts.
      if (ThePanel().HandlePointer(m, GET_X_LPARAM(l), GET_Y_LPARAM(l))) return 0;
      break;
    default:
      return DefWindowProcW(h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

struct MonitorScan { RECT primary; RECT second; bool found; int count; };

BOOL CALLBACK ScanProc(HMONITOR mon, HDC, LPRECT, LPARAM param) {
  auto* c = reinterpret_cast<MonitorScan*>(param);
  MONITORINFO mi;
  ZeroMemory(&mi, sizeof(mi));
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(mon, &mi)) return TRUE;
  c->count++;
  const bool primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
  DIS_INFO("[panel] monitor %d: %ldx%ld at (%ld,%ld)%s", c->count,
           mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
           mi.rcMonitor.left, mi.rcMonitor.top, primary ? "  PRIMARY" : "");
  if (primary) c->primary = mi.rcMonitor;
  else if (!c->found) { c->second = mi.rcMonitor; c->found = true; }
  return TRUE;
}

}  // namespace

Panel& ThePanel() { return g_panel; }

// Where the panel goes.
//
// A non-primary monitor is the answer whenever there is one, because that is
// what WEMU's RandR layout produces: THOR-TOP at the origin and THOR-BOTTOM
// directly below it. When the container reports a single tall desktop instead
// -- which happens if the second screen was not enabled for the container --
// panel.x and panel.y place it by hand, and the defaults put it immediately
// below a 1080-tall top panel, which is where THOR-BOTTOM would have been.
bool Panel::ResolveRect(RECT* out) {
  MonitorScan scan;
  ZeroMemory(&scan, sizeof(scan));
  EnumDisplayMonitors(nullptr, nullptr, ScanProc, reinterpret_cast<LPARAM>(&scan));
  found_second_monitor_ = scan.found;

  const int cfg_x = Cfg().GetInt("panel.x", -1);
  const int cfg_y = Cfg().GetInt("panel.y", -1);
  const int cfg_w = Cfg().GetInt("panel.width", 0);
  const int cfg_h = Cfg().GetInt("panel.height", 0);

  if (cfg_x >= 0 && cfg_y >= 0 && cfg_w > 0 && cfg_h > 0) {
    out->left = cfg_x; out->top = cfg_y;
    out->right = cfg_x + cfg_w; out->bottom = cfg_y + cfg_h;
    DIS_INFO("[panel] using the position from the ini");
    return true;
  }
  if (scan.found) {
    *out = scan.second;
    return true;
  }

  const int w = cfg_w > 0 ? cfg_w : 1240;
  const int h = cfg_h > 0 ? cfg_h : 1080;
  const int x = cfg_x >= 0 ? cfg_x : 0;
  const int y = cfg_y >= 0 ? cfg_y : (scan.primary.bottom - scan.primary.top);
  out->left = x; out->top = y; out->right = x + w; out->bottom = y + h;
  DIS_WARN("[panel] no second monitor reported; assuming the bottom panel is at "
           "(%d,%d) %dx%d. If the container reports one tall desktop, set panel.x "
           "and panel.y in DeadIslandSecondScreen.ini.", x, y, w, h);
  return false;
}

bool Panel::Create(IDXGIFactory* factory, ID3D11Device* device, ID3D11DeviceContext* context,
                   CreateSwapChainFn create_swapchain) {
  if (!factory || !device || !context) return false;
  factory_ = factory;
  device_ = device;
  context_ = context;
  create_swapchain_ = create_swapchain;

  RECT rect;
  ZeroMemory(&rect, sizeof(rect));
  ResolveRect(&rect);
  width_ = rect.right - rect.left;
  height_ = rect.bottom - rect.top;
  if (width_ <= 0 || height_ <= 0) {
    DIS_ERR("[panel] resolved a zero-sized panel");
    return false;
  }

  if (!g_class_registered) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
      DIS_ERR("[panel] RegisterClassExW failed (%lu)", GetLastError());
      return false;
    }
    g_class_registered = true;
  }

  // WS_POPUP so the client area is the whole panel and the window's top-left is
  // exactly the monitor origin -- the coordinate WEMU tests. WS_EX_NOACTIVATE
  // so the panel can never steal focus from the game, which is the single most
  // important property of this window: a panel that takes focus stops the
  // player moving.
  window_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            kClassName, L"Dead Island: Definitive Edition",
                            WS_POPUP | WS_VISIBLE,
                            rect.left, rect.top, width_, height_,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    DIS_ERR("[panel] CreateWindowExW failed (%lu)", GetLastError());
    return false;
  }
  ShowWindow(window_, SW_SHOWNOACTIVATE);

  // Desktop testing only. On the Thor the panel is the only thing on its own
  // display and topmost means nothing; on a development machine it is the
  // difference between seeing the panel and having the game's window sit on
  // top of it. SWP_NOACTIVATE throughout, because the one property this window
  // must never lose is that it cannot take focus from the game.
  if (Cfg().GetBool("panel.topmost", false)) {
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    DIS_INFO("[panel] topmost (panel.topmost=1, for desktop testing)");
  }

  if (!CreateSurface()) {
    DestroyWindow(window_);
    window_ = nullptr;
    return false;
  }
  DIS_INFO("[panel] ready: %dx%d at (%ld,%ld), hwnd %p, second monitor %s",
           width_, height_, rect.left, rect.top, static_cast<void*>(window_),
           found_second_monitor_ ? "found" : "NOT found");
  return true;
}

bool Panel::CreateSurface() {
  DXGI_SWAP_CHAIN_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.BufferDesc.Width = static_cast<UINT>(width_);
  desc.BufferDesc.Height = static_cast<UINT>(height_);
  desc.BufferDesc.Format = static_cast<DXGI_FORMAT>(format_);
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.OutputWindow = window_;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  last_error_ = create_swapchain_
      ? create_swapchain_(factory_, reinterpret_cast<IUnknown*>(device_), &desc, &swapchain_)
      : factory_->CreateSwapChain(device_, &desc, &swapchain_);
  if (FAILED(last_error_) || !swapchain_) {
    DIS_ERR("[panel] CreateSwapChain failed 0x%08lX", last_error_);
    swapchain_ = nullptr;
    return false;
  }

  // The panel must never take the display into exclusive fullscreen: the game
  // owns the display mode and a second swapchain fighting it for that is how a
  // session ends up with two surfaces and no picture.
  factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

  ID3D11Texture2D* back = nullptr;
  last_error_ = swapchain_->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&back));
  if (FAILED(last_error_) || !back) {
    DIS_ERR("[panel] GetBuffer failed 0x%08lX", last_error_);
    ReleaseSurface();
    return false;
  }
  ID3D11RenderTargetView* view = nullptr;
  last_error_ = device_->CreateRenderTargetView(back, nullptr, &view);
  if (FAILED(last_error_)) {
    DIS_ERR("[panel] CreateRenderTargetView failed 0x%08lX", last_error_);
    if (view) view->Release();
    back->Release();
    ReleaseSurface();
    return false;
  }
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    rtv_ = view;
    back_buffer_ = back;
    chrome_height_ = 0;
    chrome_dirty_.store(true, std::memory_order_release);
    surface_ready_.store(true, std::memory_order_release);
  }
  return true;
}

void Panel::ReleaseSurface() {
  surface_ready_.store(false, std::memory_order_release);
  ID3D11RenderTargetView* view = nullptr;
  ID3D11Texture2D* back = nullptr;
  ID3D11Texture2D* chrome = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    view = rtv_;
    rtv_ = nullptr;
    back = back_buffer_;
    back_buffer_ = nullptr;
    chrome = chrome_texture_;
    chrome_texture_ = nullptr;
    chrome_height_ = 0;
  }
  if (view) view->Release();
  if (back) back->Release();
  if (chrome) chrome->Release();
  if (swapchain_) { swapchain_->SetFullscreenState(FALSE, nullptr); swapchain_->Release(); swapchain_ = nullptr; }
}

bool Panel::ready() const {
  return surface_ready_.load(std::memory_order_acquire);
}

ID3D11RenderTargetView* Panel::AcquireRtv() const {
  std::lock_guard<std::mutex> guard(rtv_lock_);
  if (rtv_) rtv_->AddRef();
  return rtv_;
}

bool Panel::EnsureChrome() {
  const int requested_height = TheLayout().HeaderHeight();
  if (!device_ || requested_height <= 0 || width_ <= 0) return false;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    if (chrome_texture_ && chrome_height_ == requested_height) return true;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(width_);
  desc.Height = static_cast<UINT>(requested_height);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;

  ID3D11Texture2D* texture = nullptr;
  const HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &texture);
  if (FAILED(hr) || !texture) {
    DIS_ERR("[panel] could not create %dx%d tab texture (0x%08lX)",
            width_, requested_height, hr);
    if (texture) texture->Release();
    return false;
  }

  ID3D11Texture2D* replaced = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    replaced = chrome_texture_;
    chrome_texture_ = texture;
    chrome_height_ = requested_height;
    chrome_dirty_.store(true, std::memory_order_release);
  }
  if (replaced) replaced->Release();
  return true;
}

void Panel::DrawChrome(ID3D11DeviceContext* context) {
  ID3D11Texture2D* texture = nullptr;
  int bar_height = 0;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    texture = chrome_texture_;
    bar_height = chrome_height_;
    if (texture) texture->AddRef();
  }
  if (!texture || !context || bar_height <= 0) {
    if (texture) texture->Release();
    return;
  }

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width_;
  info.bmiHeader.biHeight = -bar_height;  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  HDC dc = CreateCompatibleDC(nullptr);
  void* dib_bits = nullptr;
  HBITMAP bitmap = dc
      ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &dib_bits, nullptr, 0) : nullptr;
  if (!dc || !bitmap || !dib_bits) {
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    texture->Release();
    return;
  }
  HGDIOBJ old_bitmap = SelectObject(dc, bitmap);

  RECT full{0, 0, width_, bar_height};
  HBRUSH background = CreateSolidBrush(RGB(12, 13, 12));
  FillRect(dc, &full, background);
  DeleteObject(background);

  const int tab_count = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));
  const int selected = (std::max)(0, (std::min)(active_tab_.load(
      std::memory_order_acquire), tab_count - 1));
  const int cut = (std::max)(5, bar_height / 9);
  const int font_height = (std::max)(12, static_cast<int>(bar_height * 0.27f));
  HFONT font = CreateFontW(-font_height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                           FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           FF_DONTCARE, L"Segoe UI Semibold");
  HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
  SetBkMode(dc, TRANSPARENT);

  for (int i = 0; i < tab_count; ++i) {
    const int left = (width_ * i) / tab_count;
    const int right = (width_ * (i + 1)) / tab_count;
    const bool active = i == selected;
    POINT shape[] = {
        {left + cut, 3}, {right - cut, 3}, {right - 3, cut},
        {right - 3, bar_height - 5}, {left + 3, bar_height - 5},
        {left + 3, cut}};
    HBRUSH fill = CreateSolidBrush(active ? RGB(48, 40, 22) : RGB(20, 21, 18));
    HPEN edge = CreatePen(PS_SOLID, active ? 2 : 1,
                          active ? RGB(224, 164, 48) : RGB(92, 74, 40));
    HGDIOBJ old_brush = SelectObject(dc, fill);
    HGDIOBJ old_pen = SelectObject(dc, edge);
    Polygon(dc, shape, static_cast<int>(sizeof(shape) / sizeof(shape[0])));
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(edge);
    DeleteObject(fill);

    if (active) {
      RECT accent{left + cut, bar_height - 8, right - cut, bar_height - 4};
      HBRUSH gold = CreateSolidBrush(RGB(239, 177, 52));
      FillRect(dc, &accent, gold);
      DeleteObject(gold);
    }
    RECT label{left + 5, 3, right - 5, bar_height - 7};
    SetTextColor(dc, active ? RGB(255, 221, 146) : RGB(181, 161, 116));
    DrawTextW(dc, kTabs[i].label, -1, &label,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }

  HPEN lower = CreatePen(PS_SOLID, 2, RGB(151, 105, 29));
  HGDIOBJ old_pen = SelectObject(dc, lower);
  MoveToEx(dc, 0, bar_height - 2, nullptr);
  LineTo(dc, width_, bar_height - 2);
  SelectObject(dc, old_pen);
  DeleteObject(lower);

  if (old_font) SelectObject(dc, old_font);
  if (font) DeleteObject(font);
  GdiFlush();

  // GDI's DIB is B,G,R,X while the panel surface is R,G,B,A.
  std::vector<uint32_t> rgba(static_cast<size_t>(width_) * bar_height);
  const auto* bgra = static_cast<const uint32_t*>(dib_bits);
  for (size_t i = 0; i < rgba.size(); ++i) {
    const uint32_t p = bgra[i];
    const uint32_t r = (p >> 16) & 0xFF;
    const uint32_t g = (p >> 8) & 0xFF;
    const uint32_t b = p & 0xFF;
    rgba[i] = r | (g << 8) | (b << 16) | 0xFF000000u;
  }

  SelectObject(dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(dc);
  context->UpdateSubresource(texture, 0, nullptr, rgba.data(),
                             static_cast<UINT>(width_ * sizeof(uint32_t)), 0);
  texture->Release();
  chrome_dirty_.store(false, std::memory_order_release);
}

void Panel::PrepareUiFrame(ID3D11DeviceContext* context) {
  if (!context) return;
  ID3D11RenderTargetView* view = AcquireRtv();
  if (!view) return;
  const float ink[4] = {0.043f, 0.039f, 0.031f, 1.0f};
  context->ClearRenderTargetView(view, ink);
  view->Release();

  if (!EnsureChrome()) return;
  if (chrome_dirty_.load(std::memory_order_acquire)) DrawChrome(context);

  ID3D11Texture2D* back = nullptr;
  ID3D11Texture2D* chrome = nullptr;
  int bar_height = 0;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    back = back_buffer_;
    chrome = chrome_texture_;
    bar_height = chrome_height_;
    if (back) back->AddRef();
    if (chrome) chrome->AddRef();
  }
  if (back && chrome && bar_height > 0) {
    D3D11_BOX source{0, 0, 0, static_cast<UINT>(width_),
                     static_cast<UINT>(bar_height), 1};
    context->CopySubresourceRegion(back, 0, 0, 0, 0, chrome, 0, &source);
  }
  if (chrome) chrome->Release();
  if (back) back->Release();
}

void Panel::PrepareCustomHud(ID3D11DeviceContext* context, float health, float stamina) {
  if (!context || !device_) return;
  ID3D11RenderTargetView* view = AcquireRtv();
  if (!view) return;
  const float ink[4] = {0.031f, 0.039f, 0.047f, 1.0f};
  context->ClearRenderTargetView(view, ink);
  view->Release();

  ID3D11Texture2D* back = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    back = back_buffer_;
    if (back) back->AddRef();
  }
  if (!back) return;

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width_;
  info.bmiHeader.biHeight = -height_;  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  HDC dc = CreateCompatibleDC(nullptr);
  void* dib_bits = nullptr;
  HBITMAP bitmap = dc
      ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &dib_bits, nullptr, 0) : nullptr;
  if (!dc || !bitmap || !dib_bits) {
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    back->Release();
    return;
  }
  HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
  DrawCustomHudToDc(dc, width_, height_, health, stamina);
  GdiFlush();

  std::vector<uint32_t> rgba(static_cast<size_t>(width_) * height_);
  const auto* bgra = static_cast<const uint32_t*>(dib_bits);
  for (size_t i = 0; i < rgba.size(); ++i) {
    const uint32_t p = bgra[i];
    const uint32_t r = (p >> 16) & 0xFF;
    const uint32_t g = (p >> 8) & 0xFF;
    const uint32_t b = p & 0xFF;
    rgba[i] = r | (g << 8) | (b << 16) | 0xFF000000u;
  }

  SelectObject(dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(dc);
  context->UpdateSubresource(back, 0, nullptr, rgba.data(),
                             static_cast<UINT>(width_ * sizeof(uint32_t)), 0);
  back->Release();
}

bool Panel::UploadPixels(ID3D11DeviceContext* context, const void* rgba,
                         int width, int height) {
  if (!context || !rgba) return false;
  // A size mismatch means the panel resized under the interface. Dropping the
  // frame is correct: UpdateSubresource would happily read past the end of the
  // caller's buffer, and the next frame will be the right size.
  if (width != width_ || height != height_) return false;

  ID3D11Texture2D* back = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    back = back_buffer_;
    if (back) back->AddRef();
  }
  if (!back) return false;

  context->UpdateSubresource(back, 0, nullptr, rgba,
                             static_cast<UINT>(width_ * 4), 0);
  back->Release();
  return true;
}

void Panel::ActivateTab(int index) {
  const int count = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));
  if (index < 0 || index >= count) return;
  // Tabs select pages; they are not toggle buttons. Re-sending a page binding
  // would close the page, while re-sending Escape on HUD would open the pause
  // menu. Keeping the selected tab idempotent avoids both surprises.
  if (active_tab_.load(std::memory_order_acquire) == index) return;
  active_tab_.store(index, std::memory_order_release);
  chrome_dirty_.store(true, std::memory_order_release);
  const WORD key = ReadBinding(kTabs[index]);
  DIS_INFO("[panel] tab %d (%ls) -> key 0x%02X", index, kTabs[index].label, key);
  SendKeyPress(key);
}

// Touch goes to the interface, and only to the interface.
//
// The earlier version of this turned a tap into a synthetic keypress so the
// game would change its own native page -- that belonged to the mirror design,
// where the bottom screen showed the game's UI and the only way to drive it was
// to pretend to be a keyboard. The panel now draws its own interface, so a tap
// is handled here and nothing is sent to the game at all.
//
// That is also what keeps touch from disturbing play. The window is
// WS_EX_NOACTIVATE, no key is injected, and no controller binding is consumed,
// so touching the lower screen mid-fight cannot swing a weapon or steal focus
// from the top one.
bool Panel::HandlePointer(UINT message, int x, int y) {
  Interface& ui = TheInterface();
  if (!ui.ready()) return false;

  switch (message) {
    case WM_LBUTTONDOWN:
      if (x < 0 || y < 0 || x >= width_ || y >= height_) return false;
      if (ui.OnTouchDown(x, y)) {
        // Capture so a press that slides off the panel still delivers its
        // release here, which is what lets a drag off a tab cancel it.
        SetCapture(window_);
        return true;
      }
      return false;
    case WM_MOUSEMOVE:
      return ui.OnTouchMove(x, y);
    case WM_LBUTTONUP: {
      if (GetCapture() == window_) ReleaseCapture();
      return ui.OnTouchUp(x, y);
    }
    default:
      return false;
  }
}

bool Panel::HandleNormalizedTap(float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) ||
      x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f ||
      width_ <= 0 || height_ <= 0) {
    return false;
  }
  Interface& ui = TheInterface();
  if (!ui.ready()) return false;
  // A normalized coordinate of exactly 1.0 is the final pixel, not the first
  // pixel outside the panel.
  const int pixel_x = std::clamp(static_cast<int>(x * width_), 0, width_ - 1);
  const int pixel_y = std::clamp(static_cast<int>(y * height_), 0, height_ - 1);
  // A normalized tap arrives already resolved as a complete press, so it is
  // played as down-then-up rather than being held.
  ui.OnTouchDown(pixel_x, pixel_y);
  return ui.OnTouchUp(pixel_x, pixel_y);
}

bool Panel::HandleNormalizedHold(float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || x > 1.0f ||
      y < 0.0f || y > 1.0f || width_ <= 0 || height_ <= 0) {
    return false;
  }
  Interface& ui = TheInterface();
  if (!ui.ready()) return false;
  const int pixel_x = std::clamp(static_cast<int>(x * width_), 0, width_ - 1);
  const int pixel_y = std::clamp(static_cast<int>(y * height_), 0, height_ - 1);
  if (ui.OnHold(pixel_x, pixel_y)) return true;
  // WEMU resolves a stationary press of 250 ms or more as `hold`. Inventory
  // rows intentionally consume that as their context menu; every other
  // control should retain normal button semantics instead of becoming a dead
  // zone merely because the player's tap was a fraction slower.
  ui.OnTouchDown(pixel_x, pixel_y);
  return ui.OnTouchUp(pixel_x, pixel_y);
}

bool Panel::HandleNormalizedPinch(float x, float y, float factor) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(factor) ||
      factor <= 0.0f || x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f ||
      width_ <= 0 || height_ <= 0) {
    return false;
  }
  Interface& ui = TheInterface();
  if (!ui.ready()) return false;
  const int pixel_x = std::clamp(static_cast<int>(x * width_), 0, width_ - 1);
  const int pixel_y = std::clamp(static_cast<int>(y * height_), 0, height_ - 1);
  return ui.OnPinch(pixel_x, pixel_y, factor);
}

bool Panel::HandleNormalizedPointer(UINT message, float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) ||
      width_ <= 0 || height_ <= 0) return false;
  Interface& ui = TheInterface();
  if (!ui.ready()) return false;
  // Move/up are intentionally allowed just outside [0,1]. A finger leaving a
  // control must cancel the press instead of leaving that control held. Clamp
  // the extended range before conversion so a malformed UDP float cannot
  // overflow an int inside the render thread.
  const float safe_x = std::clamp(x, -1.0f, 2.0f);
  const float safe_y = std::clamp(y, -1.0f, 2.0f);
  const bool inside = x >= 0.0f && x <= 1.0f &&
                      y >= 0.0f && y <= 1.0f;
  const int raw_x = static_cast<int>(safe_x * width_);
  const int raw_y = static_cast<int>(safe_y * height_);
  const int pixel_x = inside ? std::clamp(raw_x, 0, width_ - 1)
                             : std::clamp(raw_x, -width_, width_ * 2);
  const int pixel_y = inside ? std::clamp(raw_y, 0, height_ - 1)
                             : std::clamp(raw_y, -height_, height_ * 2);
  switch (message) {
    case WM_LBUTTONDOWN:
      return inside && ui.OnTouchDown(pixel_x, pixel_y);
    case WM_MOUSEMOVE:
      return ui.OnTouchMove(pixel_x, pixel_y);
    case WM_LBUTTONUP:
      return ui.OnTouchUp(pixel_x, pixel_y);
    default:
      return false;
  }
}

void Panel::CancelPointer() {
  Interface& ui = TheInterface();
  if (ui.ready()) ui.OnTouchUp(-1, -1);
}

void Panel::OnDisplayChanged() {
  if (!window_) return;
  RECT rect;
  ZeroMemory(&rect, sizeof(rect));
  ResolveRect(&rect);
  const int w = rect.right - rect.left;
  const int h = rect.bottom - rect.top;
  if (w <= 0 || h <= 0) {
    DIS_WARN("[panel] ignoring an invalid display geometry %dx%d", w, h);
    return;
  }
  RECT current;
  ZeroMemory(&current, sizeof(current));
  GetWindowRect(window_, &current);
  const bool moved = current.left != rect.left || current.top != rect.top;
  const bool resized = w != width_ || h != height_;
  if (!moved && !resized) return;

  DIS_INFO("[panel] geometry changed: (%ld,%ld) %dx%d -> (%ld,%ld) %dx%d",
           current.left, current.top, width_, height_, rect.left, rect.top, w, h);
  SetWindowPos(window_, nullptr, rect.left, rect.top, w, h,
               SWP_NOACTIVATE | SWP_NOZORDER);
  if (!resized) return;

  width_ = w; height_ = h;
  ReleaseSurface();
  if (!CreateSurface()) {
    DIS_ERR("[panel] could not rebuild the surface after a display change");
  }
}

// Lifts a rectangle of the game's finished frame onto the panel.
//
// This is a region blit, not a second render: the scene is drawn once, and the
// only extra GPU work is moving the pixels of one rectangle. It also cannot
// scale -- CopySubresourceRegion has no filtering -- so the panel is created
// with the game's own back-buffer format and the rectangle is taken at 1:1.
bool Panel::CopyFrom(ID3D11Texture2D* source, int sx, int sy, int sw, int sh,
                     int dest_x, int dest_y) {
  if (!source || !swapchain_ || !context_) return false;

  ID3D11Texture2D* back = nullptr;
  if (FAILED(swapchain_->GetBuffer(0, IID_ID3D11Texture2D,
                                   reinterpret_cast<void**>(&back))) || !back) {
    return false;
  }

  // Clamped to both surfaces. A box that runs past either one makes the whole
  // call a no-op with only a debug-layer message to say so, which on a handheld
  // is a black panel and no explanation.
  D3D11_TEXTURE2D_DESC sd, dd;
  source->GetDesc(&sd);
  back->GetDesc(&dd);
  if (sd.Format != dd.Format) {
    static bool told = false;
    if (!told) {
      told = true;
      DIS_ERR("[panel] format mismatch: game buffer %u, panel %u -- no copy possible",
              static_cast<unsigned>(sd.Format), static_cast<unsigned>(dd.Format));
    }
    back->Release();
    return false;
  }
  if (sx < 0) sx = 0;
  if (sy < 0) sy = 0;
  if (sx + sw > static_cast<int>(sd.Width)) sw = static_cast<int>(sd.Width) - sx;
  if (sy + sh > static_cast<int>(sd.Height)) sh = static_cast<int>(sd.Height) - sy;
  if (dest_x + sw > static_cast<int>(dd.Width)) sw = static_cast<int>(dd.Width) - dest_x;
  if (dest_y + sh > static_cast<int>(dd.Height)) sh = static_cast<int>(dd.Height) - dest_y;
  if (sw <= 0 || sh <= 0) { back->Release(); return false; }

  D3D11_BOX box;
  box.left = static_cast<UINT>(sx);
  box.top = static_cast<UINT>(sy);
  box.front = 0;
  box.right = static_cast<UINT>(sx + sw);
  box.bottom = static_cast<UINT>(sy + sh);
  box.back = 1;
  context_->CopySubresourceRegion(back, 0, static_cast<UINT>(dest_x),
                                  static_cast<UINT>(dest_y), 0, source, 0, &box);
  back->Release();
  return true;
}

void Panel::Clear() {
  ClearTo(0.043f, 0.039f, 0.031f, 1.0f);
}

void Panel::ClearTo(float r, float g, float b, float a) {
  ID3D11RenderTargetView* view = AcquireRtv();
  if (!view || !context_) {
    if (view) view->Release();
    return;
  }
  const float ink[4] = {r, g, b, a};
  context_->ClearRenderTargetView(view, ink);
  view->Release();
}

bool Panel::Present() {
  if (!swapchain_) return false;
  // The game may pump messages only for its own HWND. Service this window's
  // queue here so moving displays or a desktop test never leaves the panel (and
  // eventually the process) marked as hung.
  MSG message;
  while (PeekMessageW(&message, window_, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  // No vsync: the panel follows the game's frame, and asking the driver to wait
  // here would pace the game's own present behind the slower of two displays.
  last_error_ = swapchain_->Present(0, 0);
  ++presents_;
  if (last_error_ == DXGI_ERROR_DEVICE_REMOVED || last_error_ == DXGI_ERROR_DEVICE_RESET) {
    DIS_ERR("[panel] device lost on present (0x%08lX); releasing the surface", last_error_);
    ReleaseSurface();
    return false;
  }
  if (FAILED(last_error_)) {
    if (last_error_ != logged_present_error_) {
      logged_present_error_ = last_error_;
      repeated_present_errors_ = 0;
    }
    ++repeated_present_errors_;
    // Report a new HRESULT immediately, then only once every 300 repeated
    // failures. A disconnected panel must not turn into per-frame disk I/O.
    if (repeated_present_errors_ == 1 ||
        (repeated_present_errors_ % 300) == 0) {
      DIS_ERR("[panel] Present failed 0x%08lX at panel frame %u (%u repeats)",
              last_error_, presents_, repeated_present_errors_);
    }
    return false;
  }
  logged_present_error_ = 0;
  repeated_present_errors_ = 0;
  if (presents_ == 1 || (presents_ % 600) == 0) {
    DIS_INFO("[panel] presented %u frame(s), last result 0x%08lX", presents_, last_error_);
  }
  return SUCCEEDED(last_error_);
}

void Panel::Destroy() {
  ReleaseSurface();
  if (window_) { DestroyWindow(window_); window_ = nullptr; }
}

}  // namespace dis
