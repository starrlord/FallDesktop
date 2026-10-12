#include "SettingsWindow.h"

#include "Palette.h"
#include "resource.h"

#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <iterator>

namespace {

constexpr wchar_t kClass[] = L"falldesktopSettings";
constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr float kClientW = 720.0f;  // DIPs
constexpr float kClientH = 826.0f;
constexpr float kMargin = 26.0f;

constexpr float kPreviewTop = 16.0f;
constexpr float kPreviewH = 150.0f;
constexpr float kPreviewTaskbar = 22.0f;  // the little taskbar in the preview

// The harvest moon over the preview: its centre (DIPs in from the preview's
// top-right corner), the disc's radius and how far its glow reaches.
constexpr float kMoonInset = 108.0f;
constexpr float kMoonTop = 42.0f;
constexpr float kMoonR = 17.0f;
constexpr float kMoonGlowR = 170.0f;

constexpr int kTileCols = 10;
constexpr float kTile = 56.0f;
constexpr float kTileGap = 12.0f;

constexpr float kSliderPitch = 38.0f;  // between slider rows
constexpr float kTogglePitch = 32.0f;  // between toggle rows
constexpr float kSwatchR = 7.0f;       // a colour chip's swatch

enum Id : int {
    kIdTile = 100,  // + LeafKind
    kIdAmount = 200, kIdSpeed, kIdSize, kIdWind, kIdGusts, kIdPiles, kIdPumpkinAmount, kIdScarecrowAmount,
    kIdColor = 300,  // + LeafColor
    kIdPause = 400, kIdMouse, kIdShadow, kIdFullscreen, kIdMonitors, kIdStartup, kIdBehind, kIdPumpkins,
    kIdScarecrows, kIdCobwebs, kIdHalloween,
    kIdClear = 500, kIdDefaults, kIdDone, kIdAllLeaves, kIdRecommended, kIdCredit,
};

bool IsTileId(int id) { return id >= kIdTile && id < kIdTile + kLeafKindCount; }

// UI scale (px per DIP) on a monitor: its DPI scale, reduced when the window
// would not fit the work area (e.g. 1920x1080 at 150%, 1366x768 at 100%).
float FitScale(HMONITOR mon, UINT dpi) {
#ifdef FALLDESKTOP_DEV_TOOLS
    // FALLDESKTOP_UI_SCALE=px-per-DIP: snapshots at other scales, as a small work area would force.
    wchar_t forced[16];
    if (GetEnvironmentVariableW(L"FALLDESKTOP_UI_SCALE", forced, 16) > 0) {
        const float s = std::wcstof(forced, nullptr);
        if (s >= 0.5f && s <= 4.0f) return s;
    }
#endif
    RECT nc{};
    AdjustWindowRectExForDpi(&nc, kStyle, FALSE, 0, dpi);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return static_cast<float>(dpi) / 96.0f;
    const float w = static_cast<float>(mi.rcWork.right - mi.rcWork.left - (nc.right - nc.left));
    const float h = static_cast<float>(mi.rcWork.bottom - mi.rcWork.top - (nc.bottom - nc.top));
    return std::max(0.5f, std::min({static_cast<float>(dpi) / 96.0f, w / kClientW, h / kClientH}));
}

// Outer window size for a UI scale at a DPI.
SIZE WindowSize(float scale, UINT dpi) {
    RECT rc = {0, 0, std::lround(kClientW * scale), std::lround(kClientH * scale)};
    AdjustWindowRectExForDpi(&rc, kStyle, FALSE, 0, dpi);
    return {rc.right - rc.left, rc.bottom - rc.top};
}

RECT WorkArea(HMONITOR mon) {
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    return mi.rcWork;
}

// Where a window whose DPI changes to `dpi` is headed: the monitor at that DPI
// overlapping `r` most. (During a drag, WM_GETDPISCALEDSIZE arrives while the
// window still sits mostly on the old monitor.)
HMONITOR MonitorAtDpi(const RECT& r, UINT dpi) {
    struct Search {
        RECT r;
        UINT dpi;
        HMONITOR best = nullptr;
        LONGLONG area = -1;
    } s{r, dpi};
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR m, HDC, LPRECT mr, LPARAM lp) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(lp);
            UINT dx = 0, dy = 0;
            if (FAILED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &dx, &dy)) || dx != search.dpi) return TRUE;
            RECT i{};
            IntersectRect(&i, mr, &search.r);
            const LONGLONG area = static_cast<LONGLONG>(i.right - i.left) * (i.bottom - i.top);
            if (area > search.area) {
                search.best = m;
                search.area = area;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&s));
    return s.best ? s.best : MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
}

D2D1_COLOR_F Rgb(uint32_t rgb, float a = 1.0f) { return D2D1::ColorF(rgb, a); }
D2D1_COLOR_F Rgb(const LeafRgb& c, float a = 1.0f) { return {c.r, c.g, c.b, a}; }

COLORREF ToColorRef(const D2D1_COLOR_F& c) {
    auto byte = [](float v) { return static_cast<BYTE>(std::lround(Saturate(v) * 255.0f)); };
    return RGB(byte(c.r), byte(c.g), byte(c.b));
}

D2D1_COLOR_F Mix(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}

D2D1_RECT_F Rect(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }

D2D1_RECT_F Inflate(const D2D1_RECT_F& r, float d) { return {r.left - d, r.top - d, r.right + d, r.bottom + d}; }

bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

bool SystemDarkMode() {
#ifdef FALLDESKTOP_DEV_TOOLS
    // FALLDESKTOP_THEME=light|dark: snapshots of the other theme.
    wchar_t forced[8];
    if (GetEnvironmentVariableW(L"FALLDESKTOP_THEME", forced, 8) > 0) return _wcsicmp(forced, L"dark") == 0;
#endif
    DWORD v = 1, size = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v == 0;
}

// Descriptions for slider values (the tray menu's presets land on the same words).
const wchar_t* AmountText(int v) {
    if (v < 20) return L"A few leaves";
    if (v < 38) return L"Light";
    if (v < 58) return L"Moderate";
    if (v < 85) return L"Heavy";
    return L"Leaf storm";
}
const wchar_t* SpeedText(int v) {
    if (v < 15) return L"Drifting";
    if (v < 38) return L"Slow";
    if (v < 63) return L"Normal";
    if (v < 85) return L"Fast";
    return L"Very fast";
}
const wchar_t* SizeText(int v) {
    if (v < 15) return L"Tiny";
    if (v < 38) return L"Small";
    if (v < 63) return L"Medium";
    if (v < 85) return L"Large";
    return L"Huge";
}
std::wstring WindText(int v) {
    int a = std::abs(v);
    if (a < 6) return L"Calm";
    const wchar_t* name = a < 40 ? L"Breeze" : (a < 75 ? L"Windy" : L"Gale");
    return std::wstring(v < 0 ? L"← " : L"") + name + (v > 0 ? L" →" : L"");
}
const wchar_t* GustText(int v) {
    if (v < 8) return L"Steady";
    if (v < 40) return L"Gentle";
    if (v < 75) return L"Gusty";
    return L"Stormy";
}
const wchar_t* PileText(int v) {
    if (v <= 0) return L"Off";
    if (v < 40) return L"Scattered";
    if (v < 75) return L"Piled up";
    if (v < 130) return L"Deep";
    return L"Great heaps";
}
const wchar_t* PumpkinText(int v) {
    if (v < 20) return L"A few";
    if (v < 50) return L"Some";
    if (v < 85) return L"Lots";
    return L"A patch";
}
const wchar_t* ScarecrowText(int v) {
    if (v < 15) return L"One";
    if (v < 50) return L"Some";
    if (v < 85) return L"Lots";
    return L"A field";
}

// Radius at which a kind is drawn in its tile, so the picker hints at the
// relative size of each leaf like a field guide.
float TileRadius(LeafKind k) {
    switch (k) {
    case LeafKind::Fleck: return 7.0f;
    case LeafKind::Blur: return 19.0f;    // a leaf close up, out of focus
    case LeafKind::Samara: return 19.0f;  // long and slender
    case LeafKind::Acorn: return 13.0f;
    case LeafKind::Aspen: return 16.0f;
    default: return 21.0f;
    }
}

// The colour gene each tile's leaf is shown with (see LeafColors). Chosen so
// that in the autumn mix every leaf wears a colour it really turns: a scarlet
// maple, a crimson Japanese maple, russet oak, amber aspen, golden ginkgo,
// lemon birch, burgundy sweetgum, a willow still turning from green. The other
// settings spread over their palettes from tile to tile, and the faint designs
// (the fleck, the blur) keep clear of the darkest reds.
constexpr float kTileGene[] = {
    0.42f,   // Fleck: pumpkin
    0.705f,  // Blur: golden yellow
    0.30f,   // Samara: burnt orange
    0.0f,    // Acorn: keeps its own colours
    0.545f,  // Aspen: amber
    0.07f,   // Maple: scarlet
    0.80f,   // Oak: russet
    0.735f,  // Birch: lemon
    0.695f,  // Ginkgo: golden yellow
    0.975f,  // Sweetgum: burgundy
    0.93f,   // Willow: late green
    0.185f,  // Japanese maple: crimson
    0.03f, 0.43f, 0.69f, 0.20f, 0.50f, 0.78f, 0.25f, 0.92f,  // pixel art
};
static_assert(std::size(kTileGene) == kLeafKindCount, "a tile gene per leaf kind");

// The pixel-art kinds' bitmaps (LoadPixelLeaf's index), or -1.
int PixelIndex(LeafKind k) {
    const int i = static_cast<int>(k) - static_cast<int>(LeafKind::Pixel1);
    return i >= 0 && i < 8 ? i : -1;
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, HWND app) : instance_(instance), app_(app) {
    origin_ = QpcSeconds();
    CreateDeviceIndependent();
}

SettingsWindow::~SettingsWindow() {
    Close();
    ReleaseResources();
}

void SettingsWindow::Close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

HRESULT SettingsWindow::CreateDeviceIndependent() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    if (FAILED(hr)) return hr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    if (FAILED(hr)) return hr;

    // Windows 11's UI font, falling back to Segoe UI on Windows 10.
    const wchar_t* family = L"Segoe UI";
    ComPtr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(&fonts, FALSE))) {
        UINT32 index;
        BOOL exists = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists)
            family = L"Segoe UI Variable Text";
    }
    auto make = [&](ComPtr<IDWriteTextFormat>& out, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) {
        if (FAILED(dwrite_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                             DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &out)))
            return;
        out->SetTextAlignment(align);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(out.Get(), &ellipsis)))
            out->SetTrimming(&trim, ellipsis.Get());
    };
    make(fmtTitle_, 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fmtSection_, 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fmtBody_, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fmtSmall_, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING);
    make(fmtValue_, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
    make(fmtButton_, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    make(fmtCaption_, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);

    previewRect_ = Rect(kMargin - 10.0f, kPreviewTop, kClientW - 2.0f * (kMargin - 10.0f), kPreviewH);
    factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(previewRect_, 8.0f, 8.0f), &previewClip_);
    return S_OK;
}

// Windows 11's neutrals warmed a shade, a pumpkin accent, and leaf tiles the
// colour of the evening sky so every autumn colour shows up on them.
void SettingsWindow::ApplyTheme() {
    Theme t;
    t.dark = SystemDarkMode();
    if (t.dark) {
        t.bg = Rgb(0x211F1E);
        t.card = Rgb(0x2E2B29);
        t.text = Rgb(0xFFFFFF);
        t.subtext = Rgb(0xC9C2BB);
        t.track = Rgb(0x8E8780);
        t.border = Rgb(0x423D39);
        t.tile = Rgb(0x1D1A2E);
        t.tileHover = Rgb(0x2A2541);
        t.focus = Rgb(0xFFFFFF);
        t.accent = Rgb(0xF29A45);    // lit pumpkin, with dark text on it like Windows' dark-theme accents
        t.onAccent = Rgb(0x261405);
    } else {
        t.bg = Rgb(0xF5F2EE);
        t.card = Rgb(0xFCFAF8);
        t.text = Rgb(0x1C1A18);
        t.subtext = Rgb(0x615A54);
        t.track = Rgb(0x8C847D);
        t.border = Rgb(0xDDD5CC);
        t.tile = Rgb(0x221E36);
        t.tileHover = Rgb(0x2F2A4A);
        t.focus = Rgb(0x000000);
        t.accent = Rgb(0xB8530E);    // burnt pumpkin: white text reads on it
        t.onAccent = Rgb(0xFFFFFF);
    }
    theme_ = t;
    if (hwnd_) {
        BOOL dark = t.dark;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }
}

void SettingsWindow::Show(const Settings& s) {
    settings_ = s;
    startWithWindows_ = IsStartWithWindows();
    if (!hwnd_) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{sizeof(wc)};
            wc.lpfnWndProc = WndProc;
            wc.hInstance = instance_;
            wc.lpszClassName = kClass;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
            LoadIconMetric(instance_, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &wc.hIconSm);
            registered = RegisterClassExW(&wc) != 0;
        }
        ApplyTheme();

        // Open on the monitor the user is pointing at, centred in its work area.
        POINT pt;
        GetCursorPos(&pt);
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
        UINT dpi = 96, dpiY = 96;
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpi, &dpiY);
        auto place = [&](HMONITOR m, UINT d) {
            scale_ = FitScale(m, d);
            const SIZE size = WindowSize(scale_, d);
            const RECT work = WorkArea(m);
            const LONG x = work.left + std::max(0L, (work.right - work.left - size.cx) / 2);
            const LONG y = work.top + std::max(0L, (work.bottom - work.top - size.cy) / 2);
            return RECT{x, y, x + size.cx, y + size.cy};
        };
        RECT rc = place(mon, dpi);
        hwnd_ = CreateWindowExW(0, kClass, FALLDESKTOP_APP_NAME L" settings", kStyle, rc.left, rc.top, rc.right - rc.left,
                                rc.bottom - rc.top, nullptr, nullptr, instance_, this);
        if (!hwnd_) return;
        dpi_ = GetDpiForWindow(hwnd_);
        if (dpi_ != dpi) {
            rc = place(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), dpi_);
            SetWindowPos(hwnd_, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ApplyTheme();
        ConfigurePreview();
        spooky_ = settings_.halloween ? 1.0f : 0.0f;  // opens on its sky; later toggles fade
#ifdef FALLDESKTOP_DEV_TOOLS
        wchar_t snap[MAX_PATH];
        if (GetEnvironmentVariableW(L"FALLDESKTOP_SNAPSHOT", snap, MAX_PATH) > 0) snapshotPath_ = snap;
#endif
        // Stay invisible until the first frame is drawn, so the window never
        // flashes blank. (Cloaked windows get no idle-loop frames.)
        Cloak(true);
        ShowWindow(hwnd_, SW_SHOW);
        Render();
        if (cloaked_ && !SnapshotPending()) Cloak(false);  // drawing failed: show the plain background meanwhile
    } else if (IsIconic(hwnd_)) {
        ShowWindow(hwnd_, SW_RESTORE);
    }
    // With the colour picker up, bring that forward instead of the disabled owner.
    if (hwnd_) SetForegroundWindow(busy_ ? GetLastActivePopup(hwnd_) : hwnd_);
}

void SettingsWindow::RequestCustomColor() {
    if (hwnd_ && !busy_) action_ = Action::CustomColor;
}

void SettingsWindow::Cloak(bool on) {
    BOOL value = on;
    DwmSetWindowAttribute(hwnd_, DWMWA_CLOAK, &value, sizeof(value));
    cloaked_ = on;
}

void SettingsWindow::Update(const Settings& s) {
    if (s == settings_) return;
    settings_ = s;  // the atlas (outline) and the tiles (colour) catch up in Render()
    if (preview_) preview_->SetParams(PreviewParams());
}

bool SettingsWindow::WantsFrame() const {
    if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) return false;
    if (SnapshotPending()) return true;  // cloaked on purpose
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd_, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return !cloaked;
}

bool SettingsWindow::Frame() {
#ifdef FALLDESKTOP_DEV_TOOLS
    if (SnapshotPending() && QpcSeconds() - origin_ > 2.0) {
        SaveSnapshot(snapshotPath_.c_str());
        snapshotPath_.clear();
        // Its WM_PAINT draws and reveals the window. (A snapshot run has closed it by then.)
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
#endif
    // An occluded window (screen saver, lock screen, covered) presents without
    // waiting for vsync; skip drawing rather than spin.
    if (hwndRt_ && (hwndRt_->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED)) return false;
    Render();
    return rt_ != nullptr;
}

SimParams SettingsWindow::PreviewParams() const {
    SimParams p = MakeSimParams(settings_);
    p.density *= 4.5f;  // the preview is small; show off the selected leaves
    // Its sky is short: deep piles are squeezed so they don't bury it.
    if (p.pileMaxDip > 40.0f) p.pileMaxDip = 40.0f + 0.3f * (p.pileMaxDip - 40.0f);
    return p;
}

void SettingsWindow::ConfigurePreview() {
    if (!preview_) {
        preview_ = std::make_unique<Scene>();
        preview_->SetPileBoost(7.0f);     // build leaf piles quickly so the setting is visible
        preview_->SetScenerySettle(0.0f); // and show new pumpkin and scarecrow amounts as the slider moves
    }
    SceneGeometry geo;
    geo.width = static_cast<int>(std::lround((previewRect_.right - previewRect_.left) * scale_));
    geo.height = static_cast<int>(std::lround((previewRect_.bottom - previewRect_.top) * scale_));
    geo.dpiScale = scale_;
    geo.groundY = std::round((kPreviewH - kPreviewTaskbar) * scale_);
    geo.leftOpen = geo.rightOpen = true;
    preview_->Configure(geo, PreviewParams());
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self = nullptr;
    }
    return self ? self->Handle(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

// `hwnd` equals hwnd_ until WM_DESTROY clears it; default processing must still get the real handle.
LRESULT SettingsWindow::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto mouseAt = [&](LPARAM l) {
        input_.mouse = {static_cast<float>(GET_X_LPARAM(l)) / scale_, static_cast<float>(GET_Y_LPARAM(l)) / scale_};
    };
    switch (msg) {
    case WM_MOUSEMOVE:
        mouseAt(lp);
        input_.inside = true;
        if (!tracking_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            tracking_ = TrackMouseEvent(&tme) != FALSE;
        }
        return 0;
    case WM_MOUSELEAVE:
        tracking_ = false;
        input_.inside = false;
        if (!input_.down) input_.mouse = {-1000, -1000};
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        mouseAt(lp);
        SetCapture(hwnd_);
        input_.down = true;
        input_.pressed = true;
        return 0;
    case WM_LBUTTONUP:
        mouseAt(lp);
        // Before ReleaseCapture(): the WM_CAPTURECHANGED it sends must not cancel this release.
        input_.down = false;
        input_.released = true;
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        // Capture taken away mid-press (Alt+Tab, another window): a cancel, not a click.
        if (input_.down) {
            input_.down = false;
            input_.pressed = false;
            active_ = 0;
        }
        return 0;
    case WM_MOUSEWHEEL:
        input_.wheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            Close();
            return 0;
        }
        input_.keys.push_back(static_cast<UINT>(wp));
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, hot_ ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        EndPaint(hwnd_, &ps);
        Render();
        return 0;
    }
    case WM_ERASEBKGND:
        // Direct2D paints everything. Without a target yet (or after losing
        // it) show the theme background rather than stale pixels.
        if (!hwndRt_) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (HBRUSH brush = CreateSolidBrush(ToColorRef(theme_.bg))) {
                FillRect(reinterpret_cast<HDC>(wp), &rc, brush);
                DeleteObject(brush);
            }
        }
        return 1;
    case WM_SIZE:
        if (hwndRt_) hwndRt_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        return 0;
    case WM_ENTERSIZEMOVE:
    case WM_EXITSIZEMOVE:
        moving_ = msg == WM_ENTERSIZEMOVE;
        break;
    case WM_GETDPISCALEDSIZE: {
        // Propose the size fitted to the monitor being entered, so the
        // rectangle WM_DPICHANGED suggests already matches.
        const UINT dpi = static_cast<UINT>(wp);
        RECT wr;
        GetWindowRect(hwnd, &wr);
        *reinterpret_cast<SIZE*>(lp) = WindowSize(FitScale(MonitorAtDpi(wr, dpi), dpi), dpi);
        return TRUE;
    }
    case WM_DPICHANGED: {
        // Fit the destination monitor (the suggested rectangle's) rather than
        // scaling linearly, which can push the footer under the taskbar.
        const RECT& r = *reinterpret_cast<const RECT*>(lp);
        HMONITOR mon = MonitorAtDpi(r, HIWORD(wp));
        dpi_ = HIWORD(wp);
        scale_ = FitScale(mon, dpi_);
        const SIZE size = WindowSize(scale_, dpi_);
        LONG x = r.left, y = r.top;
        if (!moving_) {
            // E.g. a scaling change: keep it on the work area. (A drag places the window itself.)
            const RECT work = WorkArea(mon);
            x += std::max(work.left - x, std::min(0L, work.right - (x + size.cx)));
            y += std::max(work.top - y, std::min(0L, work.bottom - (y + size.cy)));
        }
        ReleaseResources();
        SetWindowPos(hwnd, nullptr, x, y, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
        ConfigurePreview();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        // Light/dark switched. (The accent is our own pumpkin, so Windows' accent colour doesn't matter.)
        if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) {
            ApplyTheme();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) startWithWindows_ = IsStartWithWindows();
        break;
    case WM_CLOSE:
        Close();
        return 0;
    case WM_DESTROY:
        ReleaseResources();
        hwnd_ = nullptr;
        // Start clean should the object be shown again.
        input_ = Input{};
        active_ = hot_ = 0;
        action_ = Action::None;
        tracking_ = moving_ = cloaked_ = false;
        PostMessageW(app_, WM_APP_SETTINGS_CLOSED, 0, 0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

HRESULT SettingsWindow::CreateResources() {
    if (!factory_) return E_FAIL;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    // Opaque window: ignore alpha so text can use ClearType.
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f * scale_, 96.0f * scale_);
    HRESULT hr = factory_->CreateHwndRenderTarget(
        rtp, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(static_cast<UINT32>(rc.right), static_cast<UINT32>(rc.bottom))),
        &hwndRt_);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = hwndRt_.As(&rt_))) return hr;
    return CreateTargetResources();
}

HRESULT SettingsWindow::CreateTargetResources() {
    HRESULT hr;
    if (FAILED(hr = rt_.As(&dc_))) return hr;
    if (FAILED(hr = rt_->CreateSolidColorBrush(theme_.text, &brush_))) return hr;
    CreateGradients();  // without them the preview's sky is just the window background, the tiles flat
    for (int i = 0; i < static_cast<int>(LeafColor::Count); ++i) CreateSwatch(static_cast<LeafColor>(i));

    for (int i = 0; i < 8; ++i) {
        ComPtr<IWICBitmapSource> src;
        if (SUCCEEDED(LoadPixelLeaf(wic_.Get(), i, &src)))
            rt_->CreateBitmapFromWicBitmap(src.Get(), nullptr, &pixelArt_[i]);
    }
    BuildTileImages();

    // Without an atlas the preview just has no leaves; Render() retries.
    LeafArtOptions opt;
    opt.shadow = settings_.shadow;
    if (SUCCEEDED(atlas_.Build(dc_.Get(), wic_.Get(), opt))) atlasShadow_ = settings_.shadow;
    else atlasRetry_ = QpcSeconds() + 1.0;
    return S_OK;
}

// The gradients, chiefly the preview's sky. An autumn evening: deep indigo
// overhead, plum lower down and an amber glow along the horizon, under a big
// warm harvest moon. The Halloween dusk is a touch more purple, its moon a
// paler orange.
HRESULT SettingsWindow::CreateGradients() {
    const D2D1_RECT_F& r = previewRect_;
    const D2D1_POINT_2F moon = {r.right - kMoonInset, r.top + kMoonTop};
    // The horizon's amber is kept a shade deep so that the leaf piles in front of it stand out.
    const D2D1_GRADIENT_STOP skies[kDuskCount][5] = {
        {{0.0f, Rgb(0x16183A)}, {0.38f, Rgb(0x2B2249)}, {0.66f, Rgb(0x50284B)}, {0.87f, Rgb(0x8E403F)},
         {1.0f, Rgb(0xCB7742)}},
        {{0.0f, Rgb(0x130E2D)}, {0.40f, Rgb(0x2E1A4B)}, {0.68f, Rgb(0x512250)}, {0.88f, Rgb(0x7E3247)},
         {1.0f, Rgb(0xB85E3C)}},
    };
    // Added to the sky (see DrawSky), so these are amounts of light.
    const D2D1_GRADIENT_STOP glows[kDuskCount][5] = {
        {{0.0f, Rgb(0xFFC27A, 0.6f)}, {0.1f, Rgb(0xFFA458, 0.38f)}, {0.25f, Rgb(0xF58A40, 0.17f)},
         {0.5f, Rgb(0xE06A2C, 0.06f)}, {1.0f, Rgb(0xC05020, 0.0f)}},
        {{0.0f, Rgb(0xF8D8B2, 0.45f)}, {0.11f, Rgb(0xF0C49A, 0.24f)}, {0.26f, Rgb(0xD0A090, 0.10f)},
         {0.5f, Rgb(0xA88090, 0.035f)}, {1.0f, Rgb(0x806070, 0.0f)}},
    };
    const D2D1_GRADIENT_STOP discs[kDuskCount][3] = {
        {{0.0f, Rgb(0xFFE9BF)}, {0.6f, Rgb(0xFDCD88)}, {1.0f, Rgb(0xF0A656)}},
        {{0.0f, Rgb(0xFCF0DE)}, {0.6f, Rgb(0xF4D8B4)}, {1.0f, Rgb(0xE0AC82)}},
    };
    HRESULT hr = S_OK;
    auto stops = [&](const D2D1_GRADIENT_STOP* s, UINT32 n) {
        ComPtr<ID2D1GradientStopCollection> c;
        if (SUCCEEDED(hr)) hr = rt_->CreateGradientStopCollection(s, n, &c);
        return c;
    };
    auto radial = [&](const D2D1_GRADIENT_STOP* s, UINT32 n, D2D1_POINT_2F c, D2D1_POINT_2F offset, float radius,
                      ComPtr<ID2D1RadialGradientBrush>& out) {
        ComPtr<ID2D1GradientStopCollection> col = stops(s, n);
        if (SUCCEEDED(hr))
            hr = rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(c, offset, radius, radius),
                                                col.Get(), &out);
    };
    for (int d = 0; d < kDuskCount; ++d) {
        ComPtr<ID2D1GradientStopCollection> col = stops(skies[d], 5);
        if (SUCCEEDED(hr))
            hr = rt_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties({0, r.top}, {0, r.bottom - kPreviewTaskbar}), col.Get(), &sky_[d]);
        radial(glows[d], 5, moon, {0, 0}, kMoonGlowR, moonGlow_[d]);
        // Lit a little from the upper left, darkening towards the limb.
        radial(discs[d], 3, moon, {-0.25f * kMoonR, -0.3f * kMoonR}, kMoonR, moonDisc_[d]);
    }
    // A soft dusky blot, moved and sized per use: the moon's seas, Halloween's cloud wisps.
    const D2D1_GRADIENT_STOP soft[] = {
        {0.0f, Rgb(0x2A1830, 1.0f)}, {0.35f, Rgb(0x2A1830, 0.72f)}, {0.7f, Rgb(0x2A1830, 0.22f)},
        {1.0f, Rgb(0x2A1830, 0.0f)},
    };
    radial(soft, 4, moon, {0, 0}, 1.0f, soft_);
    // Behind each tile's leaf, so that dark reds and browns still stand out on the evening blue.
    const D2D1_GRADIENT_STOP glow[] = {
        {0.0f, Rgb(0xFFF4E6, 0.11f)}, {0.55f, Rgb(0xFFF4E6, 0.04f)}, {1.0f, Rgb(0xFFF4E6, 0.0f)},
    };
    radial(glow, 3, {0, 0}, {0, 0}, kTile * 0.62f, tileGlow_);
    return hr;
}

// A colour chip's swatch: the colour as its leaves wear it, the blush at the
// heart. The autumn mix runs from a gold heart through orange to crimson.
void SettingsWindow::CreateSwatch(LeafColor color) {
    const int i = static_cast<int>(color);
    swatch_[i].Reset();
    D2D1_GRADIENT_STOP stops[4];
    UINT32 n = 0;
    auto add = [&](float at, const D2D1_COLOR_F& c) { stops[n++] = {at, c}; };
    switch (color) {
    case LeafColor::Autumn:
        add(0.0f, Rgb(palette::kAutumn[5].base));   // golden yellow
        add(0.4f, Rgb(palette::kAutumn[3].base));   // pumpkin
        add(0.75f, Rgb(palette::kAutumn[0].base));  // scarlet
        add(1.0f, Rgb(palette::kAutumn[1].base));   // crimson
        break;
    case LeafColor::Golden:
        add(0.0f, Rgb(palette::kGolden[1].blush));
        add(1.0f, Rgb(palette::kGolden[1].base));
        break;
    case LeafColor::Crimson:
        add(0.0f, Rgb(palette::kCrimson[0].blush));
        add(1.0f, Rgb(palette::kCrimson[0].base));
        break;
    default: {
        // The picked colour itself at the rim.
        const COLORREF c = settings_.customColor;
        LeafRgb base, blush;
        palette::Custom(c, 0.75f, 0.5f, base, blush);
        add(0.0f, Rgb(blush));
        add(1.0f, D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f));
        swatchCustom_ = c;
        break;
    }
    }
    ComPtr<ID2D1GradientStopCollection> col;
    if (SUCCEEDED(rt_->CreateGradientStopCollection(stops, n, &col)))
        rt_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, kSwatchR, kSwatchR),
                                       col.Get(), &swatch_[i]);
}

void SettingsWindow::ReleaseResources() {
    atlas_.Reset();
    if (preview_) preview_->ReleaseDeviceResources();
    for (auto& b : tileImages_) b.Reset();
    for (auto& b : pixelArt_) b.Reset();
    for (int d = 0; d < kDuskCount; ++d) {
        sky_[d].Reset();
        moonGlow_[d].Reset();
        moonDisc_[d].Reset();
    }
    soft_.Reset();
    tileGlow_.Reset();
    for (auto& b : swatch_) b.Reset();
    brush_.Reset();
    dc_.Reset();
    rt_.Reset();
    hwndRt_.Reset();
}

#ifdef FALLDESKTOP_DEV_TOOLS
HRESULT SettingsWindow::SaveSnapshot(const wchar_t* path) {
    // The whole window as designed, even where the screen is too small to show
    // it at this scale (FALLDESKTOP_UI_SCALE renders the Store's 150% shots on any monitor).
    const RECT rc = {0, 0, std::lround(kClientW * scale_), std::lround(kClientH * scale_)};
    ComPtr<IWICBitmap> bmp;
    HRESULT hr = wic_->CreateBitmap(static_cast<UINT>(rc.right), static_cast<UINT>(rc.bottom),
                                    GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
    if (FAILED(hr)) return hr;
    ReleaseResources();
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * scale_, 96.0f * scale_);
    if (SUCCEEDED(hr = factory_->CreateWicBitmapRenderTarget(bmp.Get(), rtp, &rt_)) &&
        SUCCEEDED(hr = CreateTargetResources())) {
        // Let the preview fill with leaves and build a little pile first.
        for (int i = 0; i < 600; ++i) preview_->Update(1.0f / 60.0f, i / 60.0, CursorState{});
        last_ = 0.0;
        Render();
    }
    ReleaseResources();  // the next frame recreates the window target
    if (FAILED(hr)) return hr;

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(hr = wic_->CreateStream(&stream))) return hr;
    if (FAILED(hr = stream->InitializeFromFilename(path, GENERIC_WRITE))) return hr;
    if (FAILED(hr = wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) return hr;
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    enc->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    hr = frame->WriteSource(bmp.Get(), nullptr);
    frame->Commit();
    enc->Commit();
    return hr;
}
#endif

bool SettingsWindow::TilesStale() const {
    return settings_.color != tileColor_ ||
           (settings_.color == LeafColor::Custom && settings_.customColor != tileCustom_);
}

// Each tile shows its leaf in a colour it turns in the current colour setting:
// the grey design and its blush are drawn at the tile's exact pixel size, then
// tinted (a sprite batch multiplies by its colour) and laid one over the other.
void SettingsWindow::BuildTileImages() {
    tileColor_ = settings_.color;
    tileCustom_ = settings_.customColor;
    LeafArtOptions opt;
    opt.shadow = false;  // tiles sit on a dark background
    // Pixel size only: the images keep the target's DPI, so drawn at their own
    // size from a whole-pixel corner they map 1:1 even at fractional scales.
    const UINT32 px = static_cast<UINT32>(std::lround(kTile * scale_));
    const D2D1_SIZE_U pixels = D2D1::SizeU(px, px);
    const D2D1_PIXEL_FORMAT format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    auto target = [&](ComPtr<ID2D1BitmapRenderTarget>& brt) {
        return SUCCEEDED(rt_->CreateCompatibleRenderTarget(nullptr, &pixels, &format,
                                                           D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &brt));
    };
    // One layer of a design: in greys, or in its own colours for untinted kinds.
    auto layer = [&](LeafKind kind, float r, ID2D1Bitmap* pixel, bool blush) {
        ComPtr<ID2D1Bitmap> bitmap;
        ComPtr<ID2D1BitmapRenderTarget> brt;
        if (!target(brt)) return bitmap;
        const D2D1_SIZE_F size = brt->GetSize();
        brt->BeginDraw();
        brt->Clear(D2D1::ColorF(0, 0, 0, 0));
        DrawLeafDesign(brt.Get(), kind, 0, size.width * 0.5f, size.height * 0.5f, r, opt, pixel, blush);
        if (SUCCEEDED(brt->EndDraw())) brt->GetBitmap(&bitmap);
        return bitmap;
    };
    for (int k = 0; k < kLeafKindCount; ++k) {
        tileImages_[k].Reset();
        const LeafKind kind = static_cast<LeafKind>(k);
        const int pi = PixelIndex(kind);
        ID2D1Bitmap* pixel = pi >= 0 ? pixelArt_[pi].Get() : nullptr;
        float r = TileRadius(kind);
        if (pixel) {
            // Pixel art at two DIPs per art pixel; the design itself keeps to whole device pixels.
            const D2D1_SIZE_U art = pixel->GetPixelSize();
            r = std::min(24.0f, static_cast<float>(std::max(art.width, art.height)));
        }
        ComPtr<ID2D1Bitmap> base = layer(kind, r, pixel, false);
        if (!base || !GetLeafTraits(kind).tinted) {
            tileImages_[k] = base;  // acorns keep their own colours
            continue;
        }
        ComPtr<ID2D1Bitmap> blush;
        if (LeafAtlas::BlushIndex(kind, 0) >= 0) blush = layer(kind, r, pixel, true);
        LeafRgb c, b;
        LeafColors(settings_.color, settings_.customColor, kTileGene[k], c, b);

        ComPtr<ID2D1BitmapRenderTarget> brt;
        ComPtr<ID2D1DeviceContext3> dc3;
        ComPtr<ID2D1SpriteBatch> batch;
        if (!target(brt) || FAILED(brt.As(&dc3)) || FAILED(dc3->CreateSpriteBatch(&batch))) {
            tileImages_[k] = base;  // no sprite batches: the leaf in greys
            continue;
        }
        // Both layers cover the whole tile 1:1: sprite 0 takes the leaf's colour, sprite 1 its blush.
        const D2D1_SIZE_F size = brt->GetSize();
        const D2D1_RECT_F whole = {0.0f, 0.0f, size.width, size.height};
        const D2D1_RECT_F dst[2] = {whole, whole};
        const D2D1_RECT_U src[2] = {{0, 0, px, px}, {0, 0, px, px}};
        const D2D1_COLOR_F colors[2] = {Rgb(c), Rgb(b)};
        if (FAILED(batch->AddSprites(2, dst, src, colors, nullptr))) {
            tileImages_[k] = base;
            continue;
        }
        dc3->BeginDraw();
        dc3->Clear(D2D1::ColorF(0, 0, 0, 0));
        dc3->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);  // required by DrawSpriteBatch
        dc3->DrawSpriteBatch(batch.Get(), 0, 1, base.Get(), D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                             D2D1_SPRITE_OPTIONS_NONE);
        if (blush)
            dc3->DrawSpriteBatch(batch.Get(), 1, 1, blush.Get(), D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                                 D2D1_SPRITE_OPTIONS_NONE);
        if (SUCCEEDED(dc3->EndDraw())) brt->GetBitmap(&tileImages_[k]);
        else tileImages_[k] = base;
    }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void SettingsWindow::Text(const wchar_t* s, const D2D1_RECT_F& r, IDWriteTextFormat* fmt, const D2D1_COLOR_F& c) {
    if (!fmt || !s) return;
    brush_->SetColor(c);
    rt_->DrawTextW(s, static_cast<UINT32>(wcslen(s)), fmt, r, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// Advance width of a line of text, DIPs.
float SettingsWindow::TextWidth(const wchar_t* s, IDWriteTextFormat* fmt) const {
    ComPtr<IDWriteTextLayout> layout;
    DWRITE_TEXT_METRICS m{};
    if (!fmt || !s ||
        FAILED(dwrite_->CreateTextLayout(s, static_cast<UINT32>(wcslen(s)), fmt, 4096.0f, 64.0f, &layout)) ||
        FAILED(layout->GetMetrics(&m)))
        return 0.0f;
    return m.widthIncludingTrailingWhitespace;
}

void SettingsWindow::Fill(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    if (radius > 0.0f) rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
    else rt_->FillRectangle(r, brush_.Get());
}

void SettingsWindow::Stroke(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c, float width) {
    brush_->SetColor(c);
    D2D1_RECT_F in = Inflate(r, -width * 0.5f);
    if (radius > 0.0f) rt_->DrawRoundedRectangle(D2D1::RoundedRect(in, radius, radius), brush_.Get(), width);
    else rt_->DrawRectangle(in, brush_.Get(), width);
}

void SettingsWindow::Circle(float cx, float cy, float radius, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    rt_->FillEllipse(D2D1::Ellipse({cx, cy}, radius, radius), brush_.Get());
}

void SettingsWindow::FocusRing(int id, const D2D1_RECT_F& r, float radius) {
    if (focusVisible_ && focus_ == id) Stroke(Inflate(r, 3.0f), radius + 3.0f, theme_.focus, 2.0f);
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

void SettingsWindow::Interact(int id, const D2D1_RECT_F& r, bool& hovered, bool& held, bool& clicked) {
    if (!IsTileId(id)) order_.push_back(id);  // the tile grid is a single tab stop (see Render)
    hovered = Contains(r, input_.mouse) && (active_ == 0 || active_ == id);
    if (hovered) hot_ = id;
    clicked = false;
    if (hovered && input_.pressed) {
        active_ = id;
        focus_ = id;
        focusVisible_ = false;
    }
    held = active_ == id && (input_.down || input_.pressed);
    if (active_ == id && input_.released && hovered) clicked = true;
    if (focus_ == id)
        for (UINT k : input_.keys)
            if (k == VK_SPACE || k == VK_RETURN) clicked = true;
}

bool SettingsWindow::Slider(int id, float x, float y, float w, const wchar_t* label, int& value, int lo, int hi,
                            bool bipolar, const wchar_t* valueText) {
    const float labelW = 74.0f, valueW = 84.0f;
    const float tx0 = x + labelW + 10.0f, tx1 = x + w - valueW - 8.0f;
    const D2D1_RECT_F hit = {tx0 - 10.0f, y - 15.0f, tx1 + 10.0f, y + 15.0f};
    const float span = static_cast<float>(hi - lo);
    const float thumbX = Lerp(tx0, tx1, Saturate(static_cast<float>(value - lo) / span));  // before this frame's input
    bool hovered, held, clicked;
    Interact(id, hit, hovered, held, clicked);
    const int old = value;
    if (held && active_ == id) {
        // Pressing the thumb grabs it where it is; pressing the bare track jumps there.
        if (input_.pressed) {
            const float offset = input_.mouse.x - thumbX;
            grabOffset_ = std::fabs(offset) <= 10.0f ? offset : 0.0f;
        }
        float t = Saturate((input_.mouse.x - grabOffset_ - tx0) / (tx1 - tx0));
        value = lo + static_cast<int>(std::lround(t * span));
    }
    if (hovered) {
        // Whole steps only, keeping the remainder so a precision touchpad's small deltas add up.
        if (wheelSlider_ != id) {
            wheelSlider_ = id;
            wheelSteps_ = 0.0f;
        }
        wheelSteps_ += input_.wheel * 3.0f;
        const float steps = std::trunc(wheelSteps_);
        wheelSteps_ -= steps;
        value += static_cast<int>(steps);
    }
    if (focus_ == id) {
        for (UINT k : input_.keys) {
            if (k == VK_LEFT || k == VK_DOWN) value -= 1;
            else if (k == VK_RIGHT || k == VK_UP) value += 1;
            else if (k == VK_PRIOR) value += 10;
            else if (k == VK_NEXT) value -= 10;
            else if (k == VK_HOME) value = lo;
            else if (k == VK_END) value = hi;
        }
    }
    value = Clamp(value, lo, hi);

    Text(label, Rect(x, y - 12.0f, labelW, 24.0f), fmtBody_.Get(), theme_.text);
    const float t = static_cast<float>(value - lo) / static_cast<float>(hi - lo);
    const float tx = Lerp(tx0, tx1, t);
    Fill({tx0, y - 2.0f, tx1, y + 2.0f}, 2.0f, Mix(theme_.track, theme_.bg, 0.35f));
    float from = bipolar ? (tx0 + tx1) * 0.5f : tx0;
    if (bipolar) Fill({from - 1.0f, y - 6.0f, from + 1.0f, y + 6.0f}, 1.0f, theme_.track);
    Fill({std::min(from, tx), y - 2.0f, std::max(from, tx), y + 2.0f}, 2.0f, theme_.accent);
    // Thumb: Windows 11 style ring with an accent core that grows on hover.
    Circle(tx, y, 10.0f, theme_.border);
    Circle(tx, y, 9.0f, theme_.card);
    Circle(tx, y, held ? 4.5f : (hovered ? 6.5f : 5.5f), theme_.accent);
    Text(valueText, Rect(tx1 + 8.0f, y - 12.0f, valueW, 24.0f), fmtValue_.Get(), theme_.subtext);
    FocusRing(id, hit, 6.0f);
    return value != old;
}

bool SettingsWindow::Toggle(int id, float x, float y, float w, const wchar_t* label, bool& value,
                            const wchar_t* detail) {
    const D2D1_RECT_F hit = Rect(x - 4.0f, y - 14.0f, w, 28.0f);
    bool hovered, held, clicked;
    Interact(id, hit, hovered, held, clicked);
    bool changed = false;
    if (clicked) {
        value = !value;
        changed = true;
    }
    const D2D1_RECT_F sw = Rect(x, y - 10.0f, 40.0f, 20.0f);
    if (value) {
        Fill(sw, 10.0f, hovered ? Mix(theme_.accent, theme_.text, 0.1f) : theme_.accent);
        Circle(x + 30.0f, y, hovered ? 6.5f : 6.0f, theme_.onAccent);
    } else {
        Fill(sw, 10.0f, hovered ? Mix(theme_.card, theme_.text, 0.06f) : theme_.card);
        Stroke(sw, 10.0f, theme_.subtext, 1.0f);
        Circle(x + 10.0f, y, hovered ? 5.5f : 5.0f, theme_.subtext);
    }
    const D2D1_RECT_F text = Rect(x + 52.0f, y - 12.0f, w - 56.0f, 24.0f);
    Text(label, text, fmtBody_.Get(), theme_.text);
    if (detail) {
        const float indent = TextWidth(label, fmtBody_.Get()) + 7.0f;
        Text(detail, {text.left + indent, text.top, text.right, text.bottom}, fmtBody_.Get(), theme_.subtext);
    }
    FocusRing(id, hit, 6.0f);
    return changed;
}

bool SettingsWindow::Button(int id, const D2D1_RECT_F& r, const wchar_t* text, bool primary) {
    bool hovered, held, clicked;
    Interact(id, r, hovered, held, clicked);
    D2D1_COLOR_F fill = primary ? theme_.accent : theme_.card;
    if (held && hovered) fill = Mix(fill, theme_.bg, 0.25f);
    else if (hovered) fill = Mix(fill, theme_.text, 0.07f);
    Fill(r, 5.0f, fill);
    if (!primary) Stroke(r, 5.0f, theme_.border, 1.0f);
    Text(text, r, fmtButton_.Get(), primary ? theme_.onAccent : theme_.text);
    FocusRing(id, r, 6.0f);
    return clicked;
}

bool SettingsWindow::Link(int id, const D2D1_RECT_F& r, const wchar_t* text, IDWriteTextFormat* fmt) {
    bool hovered, held, clicked;
    Interact(id, r, hovered, held, clicked);
    Text(text, r, fmt ? fmt : fmtValue_.Get(), hovered ? Mix(theme_.accent, theme_.text, 0.25f) : theme_.accent);
    FocusRing(id, r, 4.0f);
    return clicked;
}

bool SettingsWindow::Tile(int id, const D2D1_RECT_F& r, LeafKind kind, bool selected) {
    bool hovered, held, clicked;
    Interact(id, r, hovered, held, clicked);
    if (hovered) hoveredTile_ = static_cast<int>(kind);
    Fill(r, 8.0f, hovered ? theme_.tileHover : theme_.tile);
    if (tileGlow_) {
        tileGlow_->SetCenter({(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f});
        rt_->FillRoundedRectangle(D2D1::RoundedRect(r, 8.0f, 8.0f), tileGlow_.Get());
    }
    if (const auto& img = tileImages_[static_cast<int>(kind)]) {
        // At its own size from the tile's whole-pixel corner: 1:1 with the device pixels.
        const D2D1_SIZE_F size = img->GetSize();
        rt_->DrawBitmap(img.Get(), Rect(r.left, r.top, size.width, size.height), selected ? 1.0f : 0.45f,
                        PixelIndex(kind) >= 0 ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                              : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    if (selected) {
        Stroke(r, 8.0f, theme_.accent, 2.0f);
        // Check badge.
        const float cx = r.right - 10.0f, cy = r.top + 10.0f;
        Circle(cx, cy, 7.0f, theme_.accent);
        brush_->SetColor(theme_.onAccent);
        rt_->DrawLine({cx - 3.2f, cy + 0.2f}, {cx - 0.8f, cy + 2.6f}, brush_.Get(), 1.6f);
        rt_->DrawLine({cx - 0.8f, cy + 2.6f}, {cx + 3.4f, cy - 2.4f}, brush_.Get(), 1.6f);
    } else if (!theme_.dark) {
        Stroke(r, 8.0f, Mix(theme_.tile, theme_.bg, 0.5f), 1.0f);
    }
    FocusRing(id, r, 10.0f);
    return clicked;
}

bool SettingsWindow::ColorChip(int id, const D2D1_RECT_F& r, LeafColor color, const wchar_t* text, bool selected) {
    bool hovered, held, clicked;
    Interact(id, r, hovered, held, clicked);
    Fill(r, 16.0f, hovered ? Mix(theme_.card, theme_.text, 0.06f) : theme_.card);
    Stroke(r, 16.0f, selected ? theme_.accent : theme_.border, selected ? 2.0f : 1.0f);
    const float cx = r.left + 18.0f, cy = (r.top + r.bottom) * 0.5f;
    if (color == LeafColor::Custom && settings_.customColor != swatchCustom_) CreateSwatch(color);
    Circle(cx, cy, kSwatchR + 1.0f, Mix(theme_.card, theme_.text, 0.28f));
    if (ID2D1RadialGradientBrush* swatch = swatch_[static_cast<int>(color)].Get()) {
        swatch->SetCenter({cx, cy});
        rt_->FillEllipse(D2D1::Ellipse({cx, cy}, kSwatchR, kSwatchR), swatch);
    }
    Text(text, {r.left + 32.0f, r.top, r.right - 8.0f, r.bottom}, fmtBody_.Get(), theme_.text);
    FocusRing(id, r, 18.0f);
    return clicked;
}

// The evening over the preview. Halloween fades its dusk in over the autumn
// one: the second sky and moon are drawn over the first at `spooky_` opacity.
void SettingsWindow::DrawSky(const D2D1_RECT_F& r, float time) {
    const float s = spooky_;
    // An opaque autumn layer is simply covered; a translucent one (the glow) fades out as the other fades in.
    auto layers = [&](ID2D1Brush* autumn, ID2D1Brush* halloween, bool translucent, auto&& fill) {
        if (s < 1.0f && autumn) {
            autumn->SetOpacity(translucent ? 1.0f - s : 1.0f);
            fill(autumn);
        }
        if (s > 0.0f && halloween) {
            halloween->SetOpacity(s);
            fill(halloween);
        }
    };
    layers(sky_[kAutumnDusk].Get(), sky_[kHalloweenDusk].Get(), false,
           [&](ID2D1Brush* b) { rt_->FillRectangle(r, b); });

    // A few early stars overhead, clear of the moon's glow, twinkling slowly.
    const float horizon = r.bottom - kPreviewTaskbar;
    const D2D1_POINT_2F moon = {r.right - kMoonInset, r.top + kMoonTop};
    for (uint32_t i = 0; i < 22; ++i) {
        const float x = Lerp(r.left + 6.0f, r.right - 6.0f, HashFloat(i * 4u + 101u));
        const float h = HashFloat(i * 4u + 102u);  // 0 at the top
        const float y = Lerp(r.top + 4.0f, Lerp(r.top, horizon, 0.6f), h * h);
        const float b = HashFloat(i * 4u + 103u);
        const float fromMoon = std::hypot(x - moon.x, y - moon.y) / (kMoonGlowR * 0.55f);
        const float twinkle = 0.72f + 0.28f * std::sin(time * (0.7f + 1.1f * b) + kTau * HashFloat(i * 4u + 104u));
        const float a = (0.18f + 0.42f * b) * (1.0f - h) * SmoothStep(0.45f, 1.0f, fromMoon) * twinkle;
        if (a > 0.02f) Circle(x, y, 0.5f + 0.45f * b, Rgb(0xFFF3E0, a));
    }

    // The harvest moon: a broad warm glow, the disc, and its faint seas. The glow
    // adds light, brightening the sky around the moon rather than veiling it.
    dc_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
    layers(moonGlow_[kAutumnDusk].Get(), moonGlow_[kHalloweenDusk].Get(), true,
           [&](ID2D1Brush* b) { rt_->FillEllipse(D2D1::Ellipse(moon, kMoonGlowR, kMoonGlowR), b); });
    dc_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    layers(moonDisc_[kAutumnDusk].Get(), moonDisc_[kHalloweenDusk].Get(), false,
           [&](ID2D1Brush* b) { rt_->FillEllipse(D2D1::Ellipse(moon, kMoonR, kMoonR), b); });
    if (!soft_) return;
    auto blot = [&](float x, float y, float rx, float ry, float opacity) {
        soft_->SetCenter({x, y});
        soft_->SetRadiusX(rx);
        soft_->SetRadiusY(ry);
        soft_->SetOpacity(opacity);
        rt_->FillEllipse(D2D1::Ellipse({x, y}, rx, ry), soft_.Get());
    };
    static constexpr struct {
        float x, y, r, a;  // in moon radii from its centre; opacity
    } kSeas[] = {{-0.28f, -0.22f, 0.36f, 0.16f}, {0.30f, 0.10f, 0.30f, 0.13f}, {-0.02f, 0.44f, 0.22f, 0.12f},
                 {0.14f, -0.46f, 0.17f, 0.10f}};
    for (const auto& sea : kSeas)
        blot(moon.x + sea.x * kMoonR, moon.y + sea.y * kMoonR, sea.r * kMoonR, sea.r * kMoonR, sea.a);

    // Halloween: thin wisps of cloud sliding slowly across the moon.
    if (s > 0.0f) {
        const float sway = std::sin(time * 0.11f);
        blot(moon.x + (0.5f + 0.6f * sway) * kMoonR, moon.y + 0.42f * kMoonR, 2.8f * kMoonR, 0.26f * kMoonR,
             0.85f * s);
        blot(moon.x - (0.9f + 0.5f * sway) * kMoonR, moon.y - 0.2f * kMoonR, 1.9f * kMoonR, 0.16f * kMoonR,
             0.6f * s);
    }
}

void SettingsWindow::DrawPreview(const D2D1_RECT_F& r, float time) {
    rt_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), previewClip_.Get()), nullptr);

    DrawSky(r, time);

    // A miniature taskbar for the leaves to settle on.
    const D2D1_RECT_F bar = {r.left, r.bottom - kPreviewTaskbar, r.right, r.bottom};
    Fill(bar, 0.0f, Rgb(0x1C1C1C, 0.96f));
    const D2D1_COLOR_F icons[] = {Rgb(0x3A8EE6), Rgb(0xF2B233), Rgb(0x4CB86B), Rgb(0xE5533C), Rgb(0x9A6BE0)};
    const float iconSize = 12.0f, gap = 8.0f;
    float ix = (r.left + r.right) * 0.5f - (5 * iconSize + 4 * gap) * 0.5f;
    for (const auto& c : icons) {
        Fill(Rect(ix, bar.top + (kPreviewTaskbar - iconSize) * 0.5f, iconSize, iconSize), 3.0f, c);
        ix += iconSize + gap;
    }

    // The leaves themselves, drawn exactly like the desktop overlay (in pixels).
    if (preview_ && atlas_.Ready()) {
        D2D1_MATRIX_3X2_F old;
        rt_->GetTransform(&old);
        rt_->SetTransform(D2D1::Matrix3x2F::Scale(1.0f / scale_, 1.0f / scale_) *
                          D2D1::Matrix3x2F::Translation(Snap(r.left), Snap(r.top)) * old);
        preview_->Draw(dc_.Get(), atlas_);
        rt_->SetTransform(old);
    }

    // Centred: the cobwebs have the corners.
    Text(settings_.paused ? L"Preview • leaves are paused on the desktop"
                          : L"Preview • move the mouse through the leaves",
         {r.left, r.top + 6.0f, r.right, r.top + 26.0f}, fmtCaption_.Get(), Rgb(0xFFF6EA, 0.6f));
    rt_->PopLayer();
    Stroke(r, 8.0f, theme_.border, 1.0f);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void SettingsWindow::Render() {
    if (!hwnd_) return;
    if (!rt_ && FAILED(CreateResources())) {
        ReleaseResources();
        return;
    }
    const double now = QpcSeconds();
    // Rebuild the atlas for a changed outline setting; retry a failed build about once a second.
    if ((!atlas_.Ready() || atlasShadow_ != settings_.shadow) && now >= atlasRetry_) {
        LeafArtOptions opt;
        opt.shadow = settings_.shadow;
        if (SUCCEEDED(atlas_.Build(dc_.Get(), wic_.Get(), opt))) atlasShadow_ = settings_.shadow;
        else atlasRetry_ = now + 1.0;
    }
    if (TilesStale()) BuildTileImages();  // recoloured (here or from the tray)

    // Advance the preview, letting the mouse play with it.
    float dt = last_ > 0.0 ? static_cast<float>(now - last_) : 1.0f / 60.0f;
    last_ = now;
    dt = Clamp(dt, 0.0f, 0.1f);
    // The sky turns to the Halloween dusk, or back, over about a second.
    const float spooky = settings_.halloween ? 1.0f : 0.0f;
    spooky_ += (spooky - spooky_) * ApproachFactor(dt, 0.3f);
    if (std::fabs(spooky - spooky_) < 0.002f) spooky_ = spooky;
    CursorState cursor;
    if (input_.inside && Contains(previewRect_, input_.mouse)) {
        if (lastMouseValid_ && dt > 0.0f) {
            float k = ApproachFactor(dt, 0.03f);
            mouseVx_ += ((input_.mouse.x - lastMouse_.x) * scale_ / dt - mouseVx_) * k;
            mouseVy_ += ((input_.mouse.y - lastMouse_.y) * scale_ / dt - mouseVy_) * k;
        }
        lastMouse_ = input_.mouse;
        lastMouseValid_ = true;
        cursor.valid = true;
        cursor.x = (input_.mouse.x - Snap(previewRect_.left)) * scale_;
        cursor.y = (input_.mouse.y - Snap(previewRect_.top)) * scale_;
        cursor.vx = mouseVx_;
        cursor.vy = mouseVy_;
    } else {
        lastMouseValid_ = false;
        mouseVx_ = mouseVy_ = 0.0f;
    }
    if (preview_) preview_->Update(dt, now - origin_, cursor);

    // Keyboard focus traversal uses the previous frame's widget order, in
    // which the tile grid is one stop (any tile matches it).
    const bool shift = GetKeyState(VK_SHIFT) < 0;
    for (UINT k : input_.keys) {
        if (k == VK_TAB && !prevOrder_.empty()) {
            auto it = std::find_if(prevOrder_.begin(), prevOrder_.end(),
                                   [&](int id) { return id == focus_ || (IsTileId(id) && IsTileId(focus_)); });
            size_t n = prevOrder_.size();
            size_t idx = it == prevOrder_.end() ? (shift ? n - 1 : 0)
                                                : (static_cast<size_t>(it - prevOrder_.begin()) + (shift ? n - 1 : 1)) % n;
            focus_ = prevOrder_[idx];
            focusVisible_ = true;
        } else if (IsTileId(focus_) && (k == VK_LEFT || k == VK_RIGHT || k == VK_UP || k == VK_DOWN)) {
            int t = focus_ - kIdTile;
            if (k == VK_LEFT) t -= 1;
            else if (k == VK_RIGHT) t += 1;
            else if (k == VK_UP) t -= kTileCols;
            else t += kTileCols;
            if (t >= 0 && t < kLeafKindCount) focus_ = kIdTile + t;  // nothing beyond the edges
            focusVisible_ = true;
        }
    }

    hot_ = 0;
    hoveredTile_ = -1;
    order_.clear();
    Settings& s = settings_;

    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(theme_.bg);

    DrawPreview(previewRect_, static_cast<float>(now - origin_));

    // --- Leaves -------------------------------------------------------------
    // The links sit above the grid, so they come first in the tab order.
    float y = kPreviewTop + kPreviewH + 12.0f;
    if (Link(kIdAllLeaves, Rect(kClientW - kMargin - 170.0f, y, 70.0f, 24.0f), L"Select all")) {
        s.leafMask = kAllLeavesMask;
        Changed();
    }
    if (Link(kIdRecommended, Rect(kClientW - kMargin - 90.0f, y, 90.0f, 24.0f), L"Recommended")) {
        s.leafMask = kDefaultLeafMask;
        Changed();
    }
    for (int k = 0; k < kLeafKindCount; ++k) {
        int col = k % kTileCols, row = k / kTileCols;
        // Whole-pixel corners keep the tile images crisp at fractional scales.
        D2D1_RECT_F r = Rect(Snap(kMargin + static_cast<float>(col) * (kTile + kTileGap)),
                             Snap(y + 30.0f + static_cast<float>(row) * (kTile + kTileGap)), kTile, kTile);
        const uint32_t bit = 1u << k;
        if (Tile(kIdTile + k, r, static_cast<LeafKind>(k), (s.leafMask & bit) != 0)) {
            // Keep at least one design selected.
            if ((s.leafMask & bit) == 0 || (s.leafMask & ~bit) != 0) {
                s.leafMask ^= bit;
                Changed();
            }
        }
    }
    // One tab stop for the grid: the focused tile, else the first. Arrow keys move within it.
    order_.push_back(IsTileId(focus_) ? focus_ : kIdTile);
    const wchar_t* title = L"Leaves";
    Text(title, Rect(kMargin, y, 120.0f, 24.0f), fmtSection_.Get(), theme_.text);
    const wchar_t* hint = hoveredTile_ >= 0 ? LeafKindName(static_cast<LeafKind>(hoveredTile_))
                                            : L"Pick the leaves that fall";
    const float hintX = kMargin + std::max(TextWidth(title, fmtSection_.Get()) + 18.0f, 62.0f);
    Text(hint, Rect(hintX, y, 300.0f, 24.0f), fmtSmall_.Get(), theme_.subtext);

    // --- Sliders ------------------------------------------------------------
    y += 30.0f + 2.0f * kTile + kTileGap + 14.0f;
    Fill(Rect(kMargin, y, kClientW - 2.0f * kMargin, 1.0f), 0.0f, theme_.border);
    y += 24.0f;
    const float colW = (kClientW - 2.0f * kMargin - 24.0f) * 0.5f;
    const float x2 = kMargin + colW + 24.0f;
    const std::wstring wind = WindText(s.wind);
    if (Slider(kIdAmount, kMargin, y, colW, L"Amount", s.amount, 0, 100, false, AmountText(s.amount))) Changed();
    if (Slider(kIdWind, x2, y, colW, L"Wind", s.wind, -100, 100, true, wind.c_str())) Changed();
    y += kSliderPitch;
    if (Slider(kIdSpeed, kMargin, y, colW, L"Speed", s.speed, 0, 100, false, SpeedText(s.speed))) Changed();
    if (Slider(kIdGusts, x2, y, colW, L"Gusts", s.gusts, 0, 100, false, GustText(s.gusts))) Changed();
    y += kSliderPitch;
    if (Slider(kIdSize, kMargin, y, colW, L"Size", s.size, 0, 100, false, SizeText(s.size))) Changed();
    if (Slider(kIdPiles, x2, y, colW, L"Leaf piles", s.piles, 0, 200, false, PileText(s.piles))) Changed();
    y += kSliderPitch;
    if (Slider(kIdPumpkinAmount, kMargin, y, colW, L"Pumpkins", s.pumpkinAmount, 0, 100, false,
               PumpkinText(s.pumpkinAmount)))
        Changed();
    if (Slider(kIdScarecrowAmount, x2, y, colW, L"Scarecrows", s.scarecrowAmount, 0, 100, false,
               ScarecrowText(s.scarecrowAmount)))
        Changed();

    // --- Colour -------------------------------------------------------------
    y += 30.0f;
    Text(L"Color", Rect(kMargin, y, 62.0f, 32.0f), fmtBody_.Get(), theme_.text);
    const wchar_t* colorNames[] = {L"Autumn", L"Golden", L"Crimson", L"Custom…"};
    for (int i = 0; i < static_cast<int>(LeafColor::Count); ++i) {
        D2D1_RECT_F r = Rect(kMargin + 72.0f + static_cast<float>(i) * 150.0f, y, 140.0f, 32.0f);
        LeafColor c = static_cast<LeafColor>(i);
        if (ColorChip(kIdColor + i, r, c, colorNames[i], s.color == c)) {
            if (c == LeafColor::Custom) {
                action_ = Action::CustomColor;  // modal dialog after EndDraw
            } else if (s.color != c) {
                s.color = c;
                Changed();
            }
        }
    }

    // --- Switches -----------------------------------------------------------
    y += 46.0f;
    Fill(Rect(kMargin, y, kClientW - 2.0f * kMargin, 1.0f), 0.0f, theme_.border);
    y += 22.0f;
    if (Toggle(kIdPause, kMargin, y, colW, L"Pause leaves", s.paused)) Changed();
    if (Toggle(kIdMouse, x2, y, colW, L"Mouse plays with leaves", s.mouse)) Changed();
    y += kTogglePitch;
    if (Toggle(kIdShadow, kMargin, y, colW, L"Outline leaves on light windows", s.shadow)) Changed();
    if (Toggle(kIdFullscreen, x2, y, colW, L"Hide during fullscreen apps", s.pauseFullscreen)) Changed();
    y += kTogglePitch;
    if (Toggle(kIdMonitors, kMargin, y, colW, L"Leaves on all monitors", s.allMonitors)) Changed();
    if (Toggle(kIdBehind, x2, y, colW, L"Leaves behind windows", s.behindWindows)) Changed();
    y += kTogglePitch;
    if (Toggle(kIdPumpkins, kMargin, y, colW, L"Show pumpkins", s.pumpkins)) Changed();
    if (Toggle(kIdScarecrows, x2, y, colW, L"Show scarecrows", s.scarecrows)) Changed();
    y += kTogglePitch;
    if (Toggle(kIdCobwebs, kMargin, y, colW, L"Cobwebs in the corners", s.cobwebs)) Changed();
    if (Toggle(kIdHalloween, x2, y, colW, L"Halloween", s.halloween, L"jack-o’-lanterns, spiders, bats")) Changed();
    y += kTogglePitch;
    bool startup = startWithWindows_;
    if (Toggle(kIdStartup, kMargin, y, colW, L"Start with Windows", startup)) {
        if (SetStartWithWindows(startup)) startWithWindows_ = startup;
    }

    // --- Footer -------------------------------------------------------------
    const float by = kClientH - 16.0f - 32.0f;
    const float defaultsX = kClientW - kMargin - 236.0f;
    if (Button(kIdClear, Rect(kMargin, by, 130.0f, 32.0f), L"Clear leaf piles", false)) {
        PostMessageW(app_, WM_APP_CLEAR_PILES, 0, 0);
        if (preview_) preview_->ClearPiles(true);
    }
    // Centred in the gap between Clear leaf piles and Defaults; opens the project page.
    const float creditX = (kMargin + 130.0f + defaultsX) * 0.5f;
    if (Link(kIdCredit, Rect(creditX - 110.0f, by, 220.0f, 32.0f), L"Created by Joshua Starr, 2026", fmtButton_.Get()))
        action_ = Action::OpenRepo;
    if (Button(kIdDefaults, Rect(defaultsX, by, 110.0f, 32.0f), L"Defaults", false)) {
        Settings d;
        d.paused = s.paused;
        s = d;
        Changed();
    }
    if (Button(kIdDone, Rect(kClientW - kMargin - 110.0f, by, 110.0f, 32.0f), L"Done", true)) action_ = Action::Close;

    // End of frame bookkeeping.
    if (input_.released) active_ = 0;
    if (hot_ != wheelSlider_) wheelSlider_ = 0;  // left the slider: its wheel remainder is dropped
    input_.pressed = input_.released = false;
    input_.wheel = 0.0f;
    input_.keys.clear();
    prevOrder_ = order_;

    HRESULT hr = rt_->EndDraw();
    if (SUCCEEDED(hr) && cloaked_ && hwndRt_ && !SnapshotPending()) Cloak(false);  // the first frame is up: reveal
    if (hr == D2DERR_RECREATE_TARGET) ReleaseResources();

    if (changed_) {
        changed_ = false;
        if (preview_) preview_->SetParams(PreviewParams());
        PostMessageW(app_, WM_APP_SETTINGS_CHANGED, 0, 0);
    }
    // Not while a dialog runs: WM_PAINT can render from inside its loop.
    if (action_ != Action::None && !busy_) RunAction();
}

void SettingsWindow::RunAction() {
    Action a = action_;
    action_ = Action::None;
    if (a == Action::Close) {
        Close();
    } else if (a == Action::OpenRepo) {
        ShellExecuteW(hwnd_, L"open", L"https://" FALLDESKTOP_REPO, nullptr, nullptr, SW_SHOWNORMAL);
    } else if (a == Action::CustomColor && hwnd_) {
        // The picker's custom colours start out as a few autumn suggestions.
        static COLORREF custom[16] = {RGB(232, 116, 36), RGB(214, 58, 34), RGB(184, 35, 43), RGB(138, 31, 51),
                                      RGB(242, 197, 58), RGB(240, 169, 46), RGB(156, 78, 36), RGB(122, 70, 40),
                                      RGB(142, 156, 52), RGB(110, 72, 140)};
        CHOOSECOLORW cc{sizeof(cc)};
        cc.hwndOwner = hwnd_;
        cc.rgbResult = settings_.customColor;
        cc.lpCustColors = custom;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
        // The dialog's message loop may close the window (e.g. Exit from the
        // tray); Busy() keeps the app from deleting this object meanwhile.
        busy_ = true;
        const BOOL ok = ChooseColorW(&cc);
        busy_ = false;
        // The dialog swallowed the button release.
        input_.down = false;
        active_ = 0;
        if (!hwnd_) return;
        if (ok) {
            settings_.customColor = cc.rgbResult;
            settings_.color = LeafColor::Custom;
            if (preview_) preview_->SetParams(PreviewParams());
            PostMessageW(app_, WM_APP_SETTINGS_CHANGED, 0, 0);
        }
    }
}
