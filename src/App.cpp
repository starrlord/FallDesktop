#include "App.h"

#include "SettingsWindow.h"
#include "resource.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <wtsapi32.h>

namespace {

constexpr UINT kTrayId = 1;
constexpr wchar_t kOverlayClass[] = L"falldesktopOverlay";
constexpr wchar_t kRegKey[] = L"Software\\falldesktop";

// Private messages (Common.h owns WM_APP + 1..).
constexpr UINT WM_APP_RAISE = WM_APP + 50;
constexpr UINT WM_APP_CHECK_FULLSCREEN = WM_APP + 51;
constexpr UINT WM_APP_REBUILD = WM_APP + 52;

enum TimerId : UINT_PTR {
    kTimerRebuild = 1,  // debounced display/taskbar change
    kTimerFade,         // overlay fade in/out
    kTimerWatch,        // periodic fullscreen check + z-order fallback
    kTimerRaise,        // delayed z-order re-check
    kTimerSave,         // debounced settings save
    kTimerTray,         // retry adding the tray icon (Explorer not ready yet)
};

enum MenuId : UINT {
    IDM_PAUSE = 100,
    IDM_SETTINGS,
    IDM_ABOUT,
    IDM_EXIT,
    IDM_CLEAR_PILES,
    IDM_MOUSE,
    IDM_FULLSCREEN,
    IDM_ALL_MONITORS,
    IDM_STARTUP,
    IDM_SHADOW,
    IDM_GUSTY,
    IDM_BEHIND_WINDOWS,
    IDM_PUMPKINS,
    IDM_SCARECROWS,
    IDM_COBWEBS,
    IDM_HALLOWEEN,
    IDM_CHOOSE_LEAVES,
    IDM_LEAVES = 200,  // + index into kLeafPresets
    IDM_AMOUNT = 300,  // + preset index
    IDM_SPEED = 320,
    IDM_WIND = 340,
    IDM_PILES = 360,
    IDM_COLOR = 380,  // + LeafColor
    IDM_PUMPKIN_AMOUNT = 400,  // + preset index
    IDM_SCARECROW_AMOUNT = 420,
};

// The Leaves submenu: sets of leaf kinds (Settings.h), in menu order. The
// first is the default.
struct LeafPreset {
    const wchar_t* name;
    uint32_t mask;
};
constexpr LeafPreset kLeafPresets[] = {
    {L"&Leaves, keys and flecks", kDefaultLeafMask},
    {L"All &tree leaves", kFoliageMask},
    {L"&Seeds and acorns", kSeedMask},
    {L"C&rumbs and soft focus", kSmallMask},
    {L"&Pixel art", kPixelMask},
    {L"&Everything", kAllLeavesMask},
};

// The Colour submenu, in LeafColor order.
constexpr const wchar_t* kColorNames[] = {L"&Autumn", L"&Golden", L"C&rimson", L"&Custom..."};
static_assert(std::size(kColorNames) == static_cast<size_t>(LeafColor::Count));

App* g_app = nullptr;

bool IsTaskbarWindow(HWND hwnd) {
    wchar_t cls[64];
    if (!GetClassNameW(hwnd, cls, 64)) return false;
    return wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0;
}

// The top-level window showing the wallpaper and desktop icons: Progman, or on
// older builds the WorkerW it hands the icons to once a wallpaper change has
// been animated.
HWND DesktopWindow() {
    HWND shell = GetShellWindow();
    if (shell && FindWindowExW(shell, nullptr, L"SHELLDLL_DefView", nullptr)) return shell;
    for (HWND w = nullptr; (w = FindWindowExW(nullptr, w, L"WorkerW", nullptr)) != nullptr;)
        if (FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr)) return w;
    return shell;
}

// True when `overlay` is above the desktop with no visible window in between
// (other overlays aside), so that every window covers its leaves.
bool RestsOnDesktop(HWND overlay, HWND desktop) {
    for (HWND w = GetWindow(desktop, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV)) {
        if (w == overlay) return true;
        if (!IsWindowVisible(w) || IsIconic(w)) continue;
        wchar_t cls[64];
        if (GetClassNameW(w, cls, 64) && wcscmp(cls, kOverlayClass) == 0) continue;
        BOOL cloaked = FALSE;
        if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        return false;
    }
    return false;  // below the desktop, e.g. after Show desktop raised it
}

// True when `hwnd` covers its whole monitor (borderless fullscreen games,
// F11 browsers, video players). Returns the monitor through `out`.
bool IsFullscreenWindow(HWND hwnd, HMONITOR& out) {
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd) || IsShellWindow(hwnd)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return false;

    RECT r;
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (style & (WS_CAPTION | WS_THICKFRAME)) {
        if (!GetClientRect(hwnd, &r)) return false;
        MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    } else if (!GetWindowRect(hwnd, &r)) {
        return false;
    }
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return false;
    const RECT& m = mi.rcMonitor;
    if (r.left <= m.left && r.top <= m.top && r.right >= m.right && r.bottom >= m.bottom) {
        out = mon;
        return true;
    }
    return false;
}

DWORD OsBuild() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    static const DWORD build = [] {
        OSVERSIONINFOW vi{sizeof(vi)};
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))))
            fn(&vi);
        return vi.dwBuildNumber;
    }();
    return build;
}

// Lets popup menus follow the system dark/light app theme (Windows 10 1903+).
// Uses the long-stable, undocumented uxtheme ordinals that Explorer and many
// Win32 apps rely on; skipped silently if unavailable.
void EnableDarkMenus() {
    if (OsBuild() < 18362) return;
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(
        reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(
        reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
    if (setMode) setMode(1);  // AllowDark: follow the system setting
    if (flush) flush();
}

void FlushDarkMenus() {
    if (OsBuild() < 18362) return;
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) return;
    using FlushMenuThemesFn = void(WINAPI*)();
    if (auto flush = reinterpret_cast<FlushMenuThemesFn>(
            reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136)))))
        flush();
}

// Opts the process out of EcoQoS so Windows doesn't park the render threads on
// slow cores while the app has no foreground window (keeps frames on time).
void DisablePowerThrottling() {
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
}

DWORD ReadRegDword(const wchar_t* name, DWORD fallback) {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS)
        return fallback;
    return value;
}

void WriteRegDword(const wchar_t* name, DWORD value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, name, REG_DWORD, &value, sizeof(value));
}

// Tray tooltip: what the leaves are doing right now.
std::wstring TrayTip(const Settings& s) {
    if (s.paused) return FALLDESKTOP_APP_NAME L" — paused";
    const wchar_t* amount = s.amount < 20   ? L"A few leaves falling"
                            : s.amount < 38 ? L"Leaves floating down"
                            : s.amount < 58 ? L"Leaves falling"
                            : s.amount < 85 ? L"Leaves tumbling down"
                                            : L"A leaf storm";
    return std::wstring(FALLDESKTOP_APP_NAME L" — ") + amount;
}

// The preset nearest to a slider value, so the menu always shows where the
// setting is even after fine-tuning in the settings window.
template <size_t N>
int NearestPreset(const Preset (&presets)[N], int value) {
    int best = 0;
    for (size_t i = 1; i < N; ++i)
        if (std::abs(presets[i].value - value) < std::abs(presets[best].value - value)) best = static_cast<int>(i);
    return best;
}

// GDI names (\\.\DISPLAYn) of the monitors Windows composes in floating point:
// HDR, or Auto Color Management on an SDR display. There it clamps pixels whose
// colour exceeds their alpha, which the jack-o'-lanterns' candlelight uses to
// add light.
std::vector<std::wstring> AdvancedColorDisplays() {
    std::vector<std::wstring> out;
    UINT32 paths = 0, modes = 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> path;
    std::vector<DISPLAYCONFIG_MODE_INFO> mode;
    LONG rc = ERROR_INSUFFICIENT_BUFFER;
    // (Displays coming and going between the two calls make the buffers too small: ask again.)
    for (int tries = 0; tries < 4 && rc == ERROR_INSUFFICIENT_BUFFER; ++tries) {
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &paths, &modes) != ERROR_SUCCESS) return out;
        path.resize(paths);
        mode.resize(modes);
        rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &paths, path.data(), &modes, mode.data(), nullptr);
    }
    if (rc != ERROR_SUCCESS) return out;
    for (UINT32 i = 0; i < paths; ++i) {
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
        color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        color.header.size = sizeof(color);
        color.header.adapterId = path[i].targetInfo.adapterId;
        color.header.id = path[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&color.header) != ERROR_SUCCESS ||
            !(color.advancedColorEnabled || color.wideColorEnforced))
            continue;
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path[i].sourceInfo.adapterId;
        source.header.id = path[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS) out.emplace_back(source.viewGdiDeviceName);
    }
    return out;
}

bool SameTargets(const std::vector<MonitorTarget>& a, const std::vector<MonitorTarget>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const MonitorTarget& x = a[i];
        const MonitorTarget& y = b[i];
        if (x.key != y.key || x.monitor != y.monitor || !EqualRect(&x.rect, &y.rect) || x.groundY != y.groundY ||
            x.dpi != y.dpi || x.refreshHz != y.refreshHz || x.leftOpen != y.leftOpen || x.rightOpen != y.rightOpen ||
            x.primaryCadence != y.primaryCadence || x.autoHideGround != y.autoHideGround ||
            x.originScale != y.originScale || x.advancedColor != y.advancedColor || x.workTop != y.workTop ||
            x.workLeft != y.workLeft || x.workRight != y.workRight)
            return false;
    }
    return true;
}

}  // namespace

App::App() { g_app = this; }

App::~App() {
    DestroyOverlays();
    if (icon_) DestroyIcon(icon_);
    if (iconPaused_) DestroyIcon(iconPaused_);
    if (iconLarge_) DestroyIcon(iconLarge_);
    g_app = nullptr;
}

bool App::Init(HINSTANCE instance) {
    instance_ = instance;
    clockOrigin_ = QpcSeconds();
    settings_ = LoadSettings();

    DisablePowerThrottling();
    EnableDarkMenus();
    // A Microsoft Store update closes the running app: have Windows start it again
    // afterwards (not after a reboot, where the startup task decides).
    if (IsPackaged()) RegisterApplicationRestart(nullptr, RESTART_NO_CRASH | RESTART_NO_HANG | RESTART_NO_REBOOT);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    if (!RegisterClassExW(&wc)) return false;

    WNDCLASSEXW oc{sizeof(oc)};
    oc.lpfnWndProc = OverlayProc;
    oc.hInstance = instance;
    oc.lpszClassName = kOverlayClass;
    oc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&oc)) return false;

    // Hidden top-level window: receives tray callbacks and the display /
    // settings broadcasts that message-only windows miss.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                            instance, this);
    if (!hwnd_) return false;

    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APP), LIM_SMALL, &icon_);
    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APP_PAUSED), LIM_SMALL, &iconPaused_);
    LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APP), LIM_LARGE, &iconLarge_);
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilterEx(hwnd_, taskbarCreatedMsg_, MSGFLT_ALLOW, nullptr);
    AddTrayIcon();

    WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
    displayNotify_ = RegisterPowerSettingNotification(hwnd_, &GUID_SESSION_DISPLAY_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Z-order changes anywhere (e.g. the taskbar being clicked) and
    // foreground changes (fullscreen apps) are reported to this thread.
    reorderHook_ = SetWinEventHook(EVENT_OBJECT_REORDER, EVENT_OBJECT_REORDER, nullptr, WinEventProc, 0, 0,
                                   WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, WinEventProc, 0, 0,
                                      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    // Windows appearing, disappearing, minimized, restored or moved: the
    // pumpkins, scarecrows and cobwebs look for windows over them right away.
    // (Not every location change: that event also fires for every move of the
    // mouse.)
    showHook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, WinEventProc, 0, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    minimizeHook_ = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, WinEventProc, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    moveSizeHook_ = SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, nullptr, WinEventProc, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    CheckFullscreen();
    UpdateFrameCap();
    RebuildOverlays(true);
    SetTimer(hwnd_, kTimerWatch, 1000, nullptr);
    ShowFirstRunTip();
    return true;
}

int App::Run() {
    // While the settings window is open its live preview renders from the
    // idle loop (paced by vsync in EndDraw). Rendering from WM_PAINT with
    // continuous invalidation would starve WM_TIMER, which drives fades and
    // the z-order watchdog.
    MSG msg;
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return static_cast<int>(msg.wParam);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Release a closed settings window only here, where none of its code
        // (e.g. a colour dialog's modal loop) can be on the stack.
        if (settingsWindow_ && !settingsWindow_->IsOpen() && !settingsWindow_->Busy()) settingsWindow_.reset();
        if (settingsWindow_ && settingsWindow_->WantsFrame()) {
            const double start = QpcSeconds();
            const bool drawn = settingsWindow_->Frame();
            // EndDraw normally blocks until vsync. When it clearly didn't
            // (occluded, remote session), wait out the rest of the refresh
            // instead of spinning a core.
            DWM_TIMING_INFO ti{};
            ti.cbSize = sizeof(ti);
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            double period = 1.0 / 60.0;
            if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcRefreshPeriod > 0)
                period = static_cast<double>(ti.qpcRefreshPeriod) / static_cast<double>(f.QuadPart);
            const double took = QpcSeconds() - start;
            if (!drawn) {
                MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            } else if (took < 0.5 * period) {
                const DWORD wait = static_cast<DWORD>((period - took) * 1000.0);
                if (wait > 0) MsgWaitForMultipleObjectsEx(0, nullptr, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        } else {
            WaitMessage();
        }
    }
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->hwnd_ = hwnd;
    } else if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (app) app->hwnd_ = nullptr;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return app ? app->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK App::OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DPICHANGED:
        // Keep covering the monitor exactly; re-read the geometry instead of
        // accepting the suggested (rescaled) rectangle.
        if (g_app) PostMessageW(g_app->hwnd_, WM_APP_REBUILD, 0, 0);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        // Content comes from DirectComposition; just validate. (ValidateRect
        // alone does not clear the update region of these layered windows,
        // which made the system re-send WM_PAINT in a tight loop.)
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void CALLBACK App::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (!g_app || !g_app->hwnd_) return;
    const bool shown = event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_HIDE;
    // Shown or hidden: only whole top-level windows (not every control, caret or cursor).
    if (shown && (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || !hwnd || GetAncestor(hwnd, GA_ROOT) != hwnd))
        return;
    // Anything that may put a window over the pumpkins, scarecrows and cobwebs
    // (or take one away).
    for (Overlay& o : g_app->overlays_)
        if (o.renderer) o.renderer->ProbeCoverSoon();
    if (event == EVENT_SYSTEM_FOREGROUND) PostMessageW(g_app->hwnd_, WM_APP_CHECK_FULLSCREEN, 0, 0);
    if (event != EVENT_OBJECT_REORDER && event != EVENT_SYSTEM_FOREGROUND) return;
    if (!g_app->raisePending_) {
        g_app->raisePending_ = true;
        PostMessageW(g_app->hwnd_, WM_APP_RAISE, 0, 0);
    }
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_ != 0) {
        // Explorer restarted: the tray icon is gone and taskbars were recreated.
        trayAdded_ = false;
        trayRetries_ = 0;
        AddTrayIcon();
        SetTimer(hwnd_, kTimerRebuild, 1500, nullptr);
        return 0;
    }

    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            ShowSettingsWindow();
            break;
        case WM_CONTEXTMENU:
            ShowContextMenu({GET_X_LPARAM(wp), GET_Y_LPARAM(wp)});
            break;
        default:
            break;
        }
        return 0;

    case WM_APP_SHOW_SETTINGS:
        ShowSettingsWindow();
        return 0;

    case WM_APP_SETTINGS_CHANGED:
        if (settingsWindow_) ApplySettings(settingsWindow_->Current());
        return 0;

    case WM_APP_SETTINGS_CLOSED:
        // Only wakes the loop in Run(), which releases the window object safely.
        return 0;

    case WM_APP_CLEAR_PILES:
        for (Overlay& o : overlays_)
            if (o.renderer) o.renderer->ClearPiles();
        return 0;

    case WM_APP_RENDER_FAILED:
        // Rendering keeps retrying in the background; nothing useful to show
        // beyond not crashing. Rebuild once in case the display set changed.
        SetTimer(hwnd_, kTimerRebuild, 2000, nullptr);
        return 0;

    case WM_APP_RAISE:
        RestackOverlays();
        return 0;

    case WM_APP_CHECK_FULLSCREEN:
        CheckFullscreen();
        return 0;

    case WM_APP_REBUILD:
        SetTimer(hwnd_, kTimerRebuild, 400, nullptr);
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_DEVICECHANGE:
        SetTimer(hwnd_, kTimerRebuild, 600, nullptr);
        return 0;

    case WM_SETTINGCHANGE:
        if (wp == SPI_SETWORKAREA || (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"TraySettings") == 0))
            SetTimer(hwnd_, kTimerRebuild, 400, nullptr);
        if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) FlushDarkMenus();
        return 0;

    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_LOCK) sessionLocked_ = true;
        else if (wp == WTS_SESSION_UNLOCK) sessionLocked_ = false;
        UpdateFrameCap();  // remote connects/disconnects change the cap
        UpdateVisibility();
        return 0;

    case WM_POWERBROADCAST:
        if (wp == PBT_POWERSETTINGCHANGE) {
            auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
            if (setting && IsEqualGUID(setting->PowerSetting, GUID_SESSION_DISPLAY_STATUS) &&
                setting->DataLength >= sizeof(DWORD)) {
                displayOff_ = *reinterpret_cast<const DWORD*>(setting->Data) == 0;
                UpdateVisibility();
            }
        } else if (wp == PBT_APMRESUMEAUTOMATIC) {
            SetTimer(hwnd_, kTimerRebuild, 1500, nullptr);
        } else if (wp == PBT_APMPOWERSTATUSCHANGE) {
            UpdateFrameCap();  // plugged in / unplugged / battery saver
        }
        return TRUE;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        // Signing out, shutting down, or an installer closing us through the
        // Restart Manager: persist and exit cleanly.
        if (wp) {
            KillTimer(hwnd_, kTimerSave);
            SaveSettings(settings_);
            DestroyWindow(hwnd_);
        }
        return 0;

    case WM_TIMER:
        switch (wp) {
        case kTimerRebuild:
            KillTimer(hwnd_, kTimerRebuild);
            RebuildOverlays(false);
            break;
        case kTimerFade:
            StepFades();
            break;
        case kTimerWatch:
            CheckFullscreen();
            UpdateFrameCap();
            RestackOverlays();
            break;
        case kTimerTray:
            KillTimer(hwnd_, kTimerTray);
            if (!trayAdded_) AddTrayIcon();
            break;
        case kTimerRaise:
            KillTimer(hwnd_, kTimerRaise);
            RestackOverlays();
            break;
        case kTimerSave:
            KillTimer(hwnd_, kTimerSave);
            SaveSettings(settings_);
            break;
        default:
            break;
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd_, kTimerSave);
        SaveSettings(settings_);
        // Close (not delete): we may be inside the settings window's colour
        // dialog loop; ~App releases the object once the stack has unwound.
        if (settingsWindow_) settingsWindow_->Close();
        for (HWINEVENTHOOK* hook : {&reorderHook_, &foregroundHook_, &showHook_, &minimizeHook_, &moveSizeHook_}) {
            if (*hook) UnhookWinEvent(*hook);
            *hook = nullptr;
        }
        if (displayNotify_) UnregisterPowerSettingNotification(displayNotify_);
        displayNotify_ = nullptr;
        WTSUnRegisterSessionNotification(hwnd_);
        DestroyOverlays();
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

// ---------------------------------------------------------------------------
// Tray icon and menu
// ---------------------------------------------------------------------------

void App::AddTrayIcon() {
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = settings_.paused ? iconPaused_ : icon_;
    wcscpy_s(nid.szTip, TrayTip(settings_).c_str());
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    // NIM_ADD can time out while a busy Explorer still adds the icon; adopt it.
    if (!trayAdded_) trayAdded_ = Shell_NotifyIconW(NIM_MODIFY, &nid) != FALSE;
    if (!trayAdded_) {
        // At sign-in Explorer may not be ready; TaskbarCreated also re-adds.
        if (++trayRetries_ <= 30) SetTimer(hwnd_, kTimerTray, 2000, nullptr);
        return;
    }
    trayRetries_ = 0;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void App::RemoveTrayIcon() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayAdded_ = false;
}

void App::UpdateTrayIcon() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = settings_.paused ? iconPaused_ : icon_;
    wcscpy_s(nid.szTip, TrayTip(settings_).c_str());
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ShowFirstRunTip() {
    if (ReadRegDword(L"Welcomed", 0) || !trayAdded_) return;
    WriteRegDword(L"Welcomed", 1);
    NOTIFYICONDATAW nid{sizeof(nid)};
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    nid.hBalloonIcon = iconLarge_ ? iconLarge_ : icon_;
    wcscpy_s(nid.szInfoTitle, L"Autumn is here!");
    wcscpy_s(nid.szInfo, L"Click the leaf in the notification area to choose leaves and settings. "
                         L"Right-click it for quick options, such as switching Halloween on or off.");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ShowContextMenu(POINT pt) {
    const Settings& s = settings_;
    HMENU menu = CreatePopupMenu();

    // Each menu's access keys are unique (Resume's too): keep them so when adding items.
    AppendMenuW(menu, MF_STRING, IDM_PAUSE, s.paused ? L"&Resume leaves" : L"&Pause leaves");
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"&Settings...");
    SetMenuDefaultItem(menu, IDM_SETTINGS, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // The look of the season, up front: the Halloween switch says what it changes.
    AppendMenuW(menu, MF_STRING | (s.halloween ? MF_CHECKED : 0), IDM_HALLOWEEN,
                L"&Halloween: jack-o'-lanterns, spiders and bats");
    AppendMenuW(menu, MF_STRING | (s.cobwebs ? MF_CHECKED : 0), IDM_COBWEBS, L"Co&bwebs in the corners");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    HMENU leaves = CreatePopupMenu();
    UINT checkedLeaves = 0;
    for (UINT i = 0; i < std::size(kLeafPresets); ++i) {
        AppendMenuW(leaves, MF_STRING, IDM_LEAVES + i, kLeafPresets[i].name);
        if (s.leafMask == kLeafPresets[i].mask && !checkedLeaves) checkedLeaves = IDM_LEAVES + i;
    }
    if (checkedLeaves)  // a hand-picked mix matches none of them
        CheckMenuRadioItem(leaves, IDM_LEAVES, IDM_LEAVES + static_cast<UINT>(std::size(kLeafPresets)) - 1,
                           checkedLeaves, MF_BYCOMMAND);
    AppendMenuW(leaves, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(leaves, MF_STRING, IDM_CHOOSE_LEAVES, L"&Choose leaves...");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(leaves), L"&Leaves");

    auto presets = [](HMENU sub, const auto& list, UINT base, int value) {
        const int n = static_cast<int>(std::size(list));
        for (int i = 0; i < n; ++i) AppendMenuW(sub, MF_STRING, base + i, list[i].name);
        CheckMenuRadioItem(sub, base, base + n - 1, base + NearestPreset(list, value), MF_BYCOMMAND);
    };
    auto presetMenu = [&](const wchar_t* title, const auto& list, UINT base, int value) {
        HMENU sub = CreatePopupMenu();
        presets(sub, list, base, value);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(sub), title);
        return sub;
    };
    presetMenu(L"&Amount", kAmountPresets, IDM_AMOUNT, s.amount);
    presetMenu(L"Sp&eed", kSpeedPresets, IDM_SPEED, s.speed);
    HMENU wind = presetMenu(L"&Wind", kWindPresets, IDM_WIND, s.wind);
    AppendMenuW(wind, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(wind, MF_STRING | (s.gusts >= 8 ? MF_CHECKED : 0), IDM_GUSTY, L"&Gusty");
    HMENU piles = presetMenu(L"Leaf p&iles", kPilePresets, IDM_PILES, s.piles);
    AppendMenuW(piles, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(piles, MF_STRING, IDM_CLEAR_PILES, L"&Clear leaf piles");

    HMENU color = CreatePopupMenu();
    for (UINT i = 0; i < std::size(kColorNames); ++i) AppendMenuW(color, MF_STRING, IDM_COLOR + i, kColorNames[i]);
    CheckMenuRadioItem(color, IDM_COLOR, IDM_COLOR + static_cast<UINT>(std::size(kColorNames)) - 1,
                       IDM_COLOR + static_cast<UINT>(s.color), MF_BYCOMMAND);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(color), L"&Color");

    // Pumpkins and scarecrows: a switch and how many.
    HMENU pumpkins = CreatePopupMenu();
    AppendMenuW(pumpkins, MF_STRING | (s.pumpkins ? MF_CHECKED : 0), IDM_PUMPKINS, L"&Show pumpkins");
    AppendMenuW(pumpkins, MF_SEPARATOR, 0, nullptr);
    presets(pumpkins, kPumpkinPresets, IDM_PUMPKIN_AMOUNT, s.pumpkinAmount);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pumpkins), L"Pump&kins");
    HMENU scarecrows = CreatePopupMenu();
    AppendMenuW(scarecrows, MF_STRING | (s.scarecrows ? MF_CHECKED : 0), IDM_SCARECROWS, L"&Show scarecrows");
    AppendMenuW(scarecrows, MF_SEPARATOR, 0, nullptr);
    presets(scarecrows, kScarecrowPresets, IDM_SCARECROW_AMOUNT, s.scarecrowAmount);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(scarecrows), L"Scarecr&ows");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (s.mouse ? MF_CHECKED : 0), IDM_MOUSE, L"&Mouse plays with leaves");
    AppendMenuW(menu, MF_STRING | (s.shadow ? MF_CHECKED : 0), IDM_SHADOW, L"Outline leaves on li&ght windows");
    AppendMenuW(menu, MF_STRING | (s.pauseFullscreen ? MF_CHECKED : 0), IDM_FULLSCREEN,
                L"Hide during &fullscreen apps");
    AppendMenuW(menu, MF_STRING | (s.allMonitors ? MF_CHECKED : 0), IDM_ALL_MONITORS, L"Leaves on all mo&nitors");
    AppendMenuW(menu, MF_STRING | (s.behindWindows ? MF_CHECKED : 0), IDM_BEHIND_WINDOWS, L"Leaves behin&d windows");
    AppendMenuW(menu, MF_STRING | (IsStartWithWindows() ? MF_CHECKED : 0), IDM_STARTUP, L"S&tart with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"Abo&ut " FALLDESKTOP_APP_NAME);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"E&xit");

    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(hwnd_);
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY;
    flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    UINT cmd = static_cast<UINT>(TrackPopupMenuEx(menu, flags, pt.x, pt.y, hwnd_, nullptr));
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd) OnCommand(cmd);
}

void App::OnCommand(UINT id) {
    Settings s = settings_;
    switch (id) {
    case IDM_PAUSE: s.paused = !s.paused; break;
    case IDM_SETTINGS: ShowSettingsWindow(); return;
    case IDM_ABOUT: ShowAbout(); return;
    case IDM_EXIT: DestroyWindow(hwnd_); return;
    case IDM_CLEAR_PILES:
        for (Overlay& o : overlays_)
            if (o.renderer) o.renderer->ClearPiles();
        return;
    case IDM_MOUSE: s.mouse = !s.mouse; break;
    case IDM_SHADOW: s.shadow = !s.shadow; break;
    case IDM_FULLSCREEN: s.pauseFullscreen = !s.pauseFullscreen; break;
    case IDM_ALL_MONITORS: s.allMonitors = !s.allMonitors; break;
    case IDM_BEHIND_WINDOWS: s.behindWindows = !s.behindWindows; break;
    case IDM_PUMPKINS: s.pumpkins = !s.pumpkins; break;
    case IDM_SCARECROWS: s.scarecrows = !s.scarecrows; break;
    case IDM_COBWEBS: s.cobwebs = !s.cobwebs; break;
    case IDM_HALLOWEEN: s.halloween = !s.halloween; break;
    case IDM_STARTUP: SetStartWithWindows(!IsStartWithWindows()); return;
    case IDM_GUSTY: s.gusts = s.gusts >= 8 ? 0 : Settings{}.gusts; break;
    case IDM_CHOOSE_LEAVES: ShowSettingsWindow(); return;
    default:
        if (id >= IDM_LEAVES && id < IDM_LEAVES + std::size(kLeafPresets)) {
            s.leafMask = kLeafPresets[id - IDM_LEAVES].mask;
        } else if (id >= IDM_AMOUNT && id < IDM_AMOUNT + std::size(kAmountPresets)) {
            s.amount = kAmountPresets[id - IDM_AMOUNT].value;
        } else if (id >= IDM_SPEED && id < IDM_SPEED + std::size(kSpeedPresets)) {
            s.speed = kSpeedPresets[id - IDM_SPEED].value;
        } else if (id >= IDM_WIND && id < IDM_WIND + std::size(kWindPresets)) {
            s.wind = kWindPresets[id - IDM_WIND].value;
        } else if (id >= IDM_PILES && id < IDM_PILES + std::size(kPilePresets)) {
            s.piles = kPilePresets[id - IDM_PILES].value;
        } else if (id >= IDM_PUMPKIN_AMOUNT && id < IDM_PUMPKIN_AMOUNT + std::size(kPumpkinPresets)) {
            s.pumpkinAmount = kPumpkinPresets[id - IDM_PUMPKIN_AMOUNT].value;
            s.pumpkins = true;
        } else if (id >= IDM_SCARECROW_AMOUNT && id < IDM_SCARECROW_AMOUNT + std::size(kScarecrowPresets)) {
            s.scarecrowAmount = kScarecrowPresets[id - IDM_SCARECROW_AMOUNT].value;
            s.scarecrows = true;
        } else if (id >= IDM_COLOR && id < IDM_COLOR + static_cast<UINT>(LeafColor::Count)) {
            s.color = static_cast<LeafColor>(id - IDM_COLOR);
            if (s.color == LeafColor::Custom) {
                // The colour dialog belongs to the visible settings window, so
                // it opens in a sensible place and keeps a taskbar button.
                ShowSettingsWindow();
                if (settingsWindow_) settingsWindow_->RequestCustomColor();
                return;
            }
        } else {
            return;
        }
        break;
    }
    ApplySettings(s);
}

void App::ShowAbout() {
    TASKDIALOGCONFIG tdc{sizeof(tdc)};
    tdc.hwndParent = nullptr;
    tdc.hInstance = instance_;
    tdc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_ENABLE_HYPERLINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    tdc.pszWindowTitle = L"About " FALLDESKTOP_APP_NAME;
    tdc.pszMainIcon = MAKEINTRESOURCEW(IDI_APP);
    tdc.pszMainInstruction = FALLDESKTOP_APP_NAME L" " FALLDESKTOP_VERSION_STR;
    tdc.pszContent =
        L"Autumn leaves, leaf piles, pumpkins, scarecrows and cobwebs for your Windows desktop: the leaves "
        L"float down across every monitor and pile up on the taskbar.\n\n"
        L"Created by Joshua Starr, 2026\n"
        L"<a href=\"https://" FALLDESKTOP_REPO L"\">" FALLDESKTOP_REPO L"</a>";
    tdc.pszFooter = L"Click the tray leaf for settings; right-click for quick options.";
    tdc.dwCommonButtons = TDCBF_OK_BUTTON;
    // The dialog only reports link clicks; opening them is up to us.
    tdc.pfCallback = [](HWND dialog, UINT msg, WPARAM, LPARAM lp, LONG_PTR) -> HRESULT {
        if (msg == TDN_HYPERLINK_CLICKED)
            ShellExecuteW(dialog, L"open", reinterpret_cast<const wchar_t*>(lp), nullptr, nullptr, SW_SHOWNORMAL);
        return S_OK;
    };
    SetForegroundWindow(hwnd_);
    TaskDialogIndirect(&tdc, nullptr, nullptr, nullptr);
}

void App::ShowSettingsWindow() {
    if (!settingsWindow_) settingsWindow_ = std::make_unique<SettingsWindow>(instance_, hwnd_);
    settingsWindow_->Show(settings_);
}

void App::ApplySettings(const Settings& next, bool save) {
    Settings s = next;
    s.Sanitize();
    const Settings old = settings_;
    settings_ = s;

    if (old.allMonitors != s.allMonitors) RebuildOverlays(true);
    // Overlays kept by a rebuild need the new settings too.
    for (Overlay& o : overlays_) {
        if (o.renderer) o.renderer->ApplySettings(settings_);
        if (old.behindWindows == s.behindWindows) continue;
        if (!s.behindWindows && o.shown) {
            // Coming out from behind the windows, the last frame shows the pumpkins,
            // scarecrows and cobwebs as if nothing covered them: hide the leaves and
            // fade them back in once the renderer has looked at what covers them now.
            o.alpha = 0;
            SetLayeredWindowAttributes(o.hwnd, 0, 0, LWA_ALPHA);
        }
        PlaceOverlay(o.hwnd);
    }
    if (old.paused != s.paused || old.amount != s.amount) UpdateTrayIcon();
    UpdateVisibility();
    if (settingsWindow_) settingsWindow_->Update(settings_);
    if (save) SetTimer(hwnd_, kTimerSave, 800, nullptr);
}

// ---------------------------------------------------------------------------
// Monitors and overlays
// ---------------------------------------------------------------------------

std::vector<MonitorTarget> App::EnumerateMonitors() const {
    struct Mon {
        MonitorTarget t;
        RECT work;
        std::wstring device;  // GDI name
    };
    std::vector<Mon> mons;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR m, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto& list = *reinterpret_cast<std::vector<Mon>*>(lp);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(m, &mi)) return TRUE;
            Mon mon;
            mon.t.monitor = m;
            mon.t.rect = mi.rcMonitor;
            mon.work = mi.rcWork;
            mon.t.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            UINT dx = 96, dy = 96;
            if (FAILED(GetDpiForMonitor(m, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
            mon.t.dpi = dx;
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            mon.t.refreshHz = (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) &&
                               dm.dmDisplayFrequency > 1)
                                  ? static_cast<float>(dm.dmDisplayFrequency)
                                  : 60.0f;
            // Key on the monitor's device interface path (stable across the
            // \\.\DISPLAYn renumbering Windows does) plus its position.
            DISPLAY_DEVICEW dd{};
            dd.cb = sizeof(dd);
            const wchar_t* id = mi.szDevice;
            if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME) && dd.DeviceID[0])
                id = dd.DeviceID;
            wchar_t rect[64];
            swprintf_s(rect, L"@%ld,%ld,%ld,%ld", mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right,
                       mi.rcMonitor.bottom);
            mon.t.key = std::wstring(id) + rect;
            mon.device = mi.szDevice;
            list.push_back(mon);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&mons));

    // Taskbars: the primary one plus one per extra display when the user
    // shows the taskbar on all displays.
    std::vector<std::pair<HMONITOR, RECT>> bars;
    auto addBar = [&](HWND bar) {
        RECT r;
        if (bar && IsWindowVisible(bar) && GetWindowRect(bar, &r))
            bars.emplace_back(MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST), r);
    };
    addBar(FindWindowW(L"Shell_TrayWnd", nullptr));
    for (HWND w = nullptr; (w = FindWindowExW(nullptr, w, L"Shell_SecondaryTrayWnd", nullptr)) != nullptr;) addBar(w);
    APPBARDATA abd{sizeof(abd)};
    const bool autoHide = (SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE) != 0;

    const std::vector<std::wstring> advanced = AdvancedColorDisplays();
    for (Mon& m : mons)
        m.t.advancedColor = std::find(advanced.begin(), advanced.end(), m.device) != advanced.end();

    float primaryHz = 60.0f, primaryScale = 1.0f;
    for (const Mon& m : mons) {
        if (!m.t.primary) continue;
        primaryHz = m.t.refreshHz;
        primaryScale = static_cast<float>(m.t.dpi) / 96.0f;
    }

    std::vector<MonitorTarget> out;
    for (Mon& m : mons) {
        if (!settings_.allMonitors && !m.t.primary) continue;
        MonitorTarget t = m.t;
        const int h = t.rect.bottom - t.rect.top;
        const int w = t.rect.right - t.rect.left;
        int ground = -1;
        if (autoHide) {
            ground = h;  // an auto-hidden taskbar comes and goes: settle on the screen edge
        } else {
            for (const auto& [mon, r] : bars) {
                bool bottomDocked = mon == t.monitor && r.bottom >= t.rect.bottom - 2 && r.top > t.rect.top + h / 2 &&
                                    (r.right - r.left) >= w / 2;
                if (bottomDocked) ground = r.top - t.rect.top;
            }
            if (ground < 0) ground = m.work.bottom - t.rect.top;  // taskbar elsewhere: other appbars or the edge
        }
        t.groundY = Clamp(ground, h / 2, h);
        // The cobwebs hang in the work area's top corners, clear of a taskbar docked at the top or side.
        t.workTop = Clamp(static_cast<int>(m.work.top - t.rect.top), 0, h / 3);
        t.workLeft = Clamp(static_cast<int>(m.work.left - t.rect.left), 0, w / 3);
        t.workRight = Clamp(static_cast<int>(t.rect.right - m.work.right), 0, w / 3);
        t.autoHideGround = autoHide;
        t.originScale = primaryScale;
        t.primaryCadence = std::fabs(t.refreshHz - primaryHz) < 0.5f;
        out.push_back(t);
    }

    // Leaf piles continue across monitors that sit side by side at the same level.
    for (MonitorTarget& a : out) {
        for (const MonitorTarget& b : out) {
            if (&a == &b) continue;
            int ga = a.rect.top + a.groundY, gb = b.rect.top + b.groundY;
            if (std::abs(ga - gb) > 2) continue;
            if (b.rect.left == a.rect.right) a.rightOpen = false;
            if (b.rect.right == a.rect.left) a.leftOpen = false;
        }
    }
    std::sort(out.begin(), out.end(), [](const MonitorTarget& a, const MonitorTarget& b) { return a.key < b.key; });
    return out;
}

void App::RebuildOverlays(bool force) {
    std::vector<MonitorTarget> targets = EnumerateMonitors();
    if (!force) {
        std::vector<MonitorTarget> current;
        for (const Overlay& o : overlays_) current.push_back(o.target);
        if (SameTargets(current, targets)) {
            UpdateVisibility();
            return;
        }
    }

    // Overlays whose window can stay (same monitor, rectangle and DPI) keep
    // running; smaller changes (ground line, pile ends, refresh rate) are
    // applied in place. Only the rest are torn down and created afresh, so
    // e.g. turning on "Leaves on all monitors" doesn't blink the primary.
    std::vector<Overlay> kept;
    std::vector<MonitorTarget> fresh;
    for (MonitorTarget& t : targets) {
        auto it = std::find_if(overlays_.begin(), overlays_.end(), [&](const Overlay& o) {
            return o.hwnd && o.renderer && o.target.key == t.key && o.target.monitor == t.monitor &&
                   EqualRect(&o.target.rect, &t.rect) && o.target.dpi == t.dpi;
        });
        if (it == overlays_.end()) {
            fresh.push_back(t);
            continue;
        }
        t.hwnd = it->hwnd;
        if (!SameTargets({it->target}, {t})) it->renderer->Reconfigure(t);
        it->target = t;
        kept.push_back(std::move(*it));
        overlays_.erase(it);
    }
    DestroyOverlays();
    overlays_ = std::move(kept);
    for (MonitorTarget& t : fresh) {
        const int w = t.rect.right - t.rect.left, h = t.rect.bottom - t.rect.top;
        HWND hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                                        WS_EX_NOACTIVATE,
                                    kOverlayClass, kAppName, WS_POPUP, t.rect.left, t.rect.top, w, h, nullptr,
                                    nullptr, instance_, nullptr);
        if (!hwnd) continue;
        // A transparent, screen-sized topmost window would otherwise count as
        // a "fullscreen app": the taskbar would drop below other windows and
        // notifications would be suppressed.
        SetPropW(hwnd, L"NonRudeHWND", reinterpret_cast<HANDLE>(TRUE));
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);  // fade in
        // The window may have been created at a different DPI and resized.
        SetWindowPos(hwnd, nullptr, t.rect.left, t.rect.top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        PlaceOverlay(hwnd);

        Overlay o;
        o.hwnd = hwnd;
        o.target = t;
        o.target.hwnd = hwnd;
        std::unique_ptr<Scene> scene;
        if (auto it = sceneCache_.find(t.key); it != sceneCache_.end()) {
            scene = std::move(it->second);
            sceneCache_.erase(it);
        }
        o.renderer =
            std::make_unique<MonitorRenderer>(hwnd_, gpuHub_, o.target, std::move(scene), settings_, clockOrigin_);
        o.renderer->SetVisible(false);
        o.renderer->SetFrameCap(frameCap_);
        o.renderer->Start();
        overlays_.push_back(std::move(o));
    }
    // Keep a few scenes from displays that went away (e.g. a monitor that is
    // switched off and on again) so their leaf piles come back.
    while (sceneCache_.size() > 6) sceneCache_.erase(sceneCache_.begin());
    UpdateVisibility();
}

void App::DestroyOverlays() {
    for (Overlay& o : overlays_) {
        if (o.renderer) {
            if (auto scene = o.renderer->Stop()) sceneCache_[o.target.key] = std::move(scene);
            o.renderer.reset();
        }
        if (o.hwnd) DestroyWindow(o.hwnd);
    }
    overlays_.clear();
}

void App::UpdateVisibility() {
    for (Overlay& o : overlays_) {
        bool fullscreen = fullscreenAll_ || std::find(fullscreenMonitors_.begin(), fullscreenMonitors_.end(),
                                                      o.target.monitor) != fullscreenMonitors_.end();
        o.wantVisible = !settings_.paused && !sessionLocked_ && !displayOff_ && !userAway_ &&
                        !(settings_.pauseFullscreen && fullscreen);
        if (o.wantVisible && !o.shown) {
            o.renderer->SetVisible(true);
            ShowWindow(o.hwnd, SW_SHOWNOACTIVATE);
            PlaceOverlay(o.hwnd);
            o.shown = true;
        }
    }
    // Re-arming a running timer restarts its countdown, which would stall a
    // fade while settings change every frame (slider drags).
    if (!fadeTimer_) {
        SetTimer(hwnd_, kTimerFade, 16, nullptr);
        fadeTimer_ = true;
    }
}

void App::StepFades() {
    bool active = false;
    const int step = 255 * 16 / 400;  // ~0.4 s fades
    for (Overlay& o : overlays_) {
        int target = o.wantVisible ? 255 : 0;
        if (o.alpha != target) {
            o.alpha = target > o.alpha ? std::min(target, o.alpha + step) : std::max(target, o.alpha - step);
            SetLayeredWindowAttributes(o.hwnd, 0, static_cast<BYTE>(o.alpha), LWA_ALPHA);
            active = true;
        }
        if (!o.wantVisible && o.alpha == 0 && o.shown) {
            ShowWindow(o.hwnd, SW_HIDE);
            o.renderer->SetVisible(false);
            o.shown = false;
        }
    }
    if (!active) {
        KillTimer(hwnd_, kTimerFade);
        fadeTimer_ = false;
    }
}

bool App::TaskbarAboveOverlay(HWND overlay) const {
    for (HWND w = GetWindow(overlay, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV))
        if (IsTaskbarWindow(w)) return true;
    return false;
}

// Puts an overlay where its leaves belong: above everything, taskbar included,
// or (behind windows) directly above the desktop, so that every window covers it.
void App::PlaceOverlay(HWND overlay) const {
    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    if (!settings_.behindWindows) {
        SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0, flags);
        return;
    }
    const HWND desktop = DesktopWindow();
    const HWND above = desktop ? GetWindow(desktop, GW_HWNDPREV) : nullptr;  // none: no shell running
    if (above == overlay) return;
    // Slot in under the window above the desktop, in its band: Show desktop
    // raises the desktop up to the topmost windows, so the leaves then join
    // those (below the taskbar), as Rainmeter does for "on desktop" skins.
    // HWND_BOTTOM drops topmost status without passing over other windows.
    const bool topmost = above && (GetWindowLongW(above, GWL_EXSTYLE) & WS_EX_TOPMOST);
    if (topmost != ((GetWindowLongW(overlay, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0))
        SetWindowPos(overlay, topmost ? HWND_TOPMOST : HWND_BOTTOM, 0, 0, 0, 0, flags);
    if (above) SetWindowPos(overlay, above, 0, 0, 0, 0, flags);
}

void App::RestackOverlays() {
    raisePending_ = false;
    const double now = QpcSeconds();
    if (now - lastRaise_ < 0.1) {
        // Rate limit so we never ping-pong with another app that wants the same spot.
        SetTimer(hwnd_, kTimerRaise, 120, nullptr);
        return;
    }
    const HWND desktop = settings_.behindWindows ? DesktopWindow() : nullptr;
    bool moved = false;
    for (Overlay& o : overlays_) {
        if (!o.shown) continue;
        // On top, only the taskbar matters: other topmost windows (picture-in-
        // picture players, tool palettes) may stay above the leaves. Behind
        // windows, nothing may come between the leaves and the desktop.
        const bool misplaced = settings_.behindWindows ? desktop && !RestsOnDesktop(o.hwnd, desktop)
                                                       : TaskbarAboveOverlay(o.hwnd);
        if (misplaced) {
            PlaceOverlay(o.hwnd);
            moved = true;
        }
    }
    if (moved) {
        lastRaise_ = now;
        SetTimer(hwnd_, kTimerRaise, 60, nullptr);  // the shell sometimes reorders again right after
    }
}

void App::UpdateFrameCap() {
    // Falling leaves are gentle enough to look smooth at 60 fps; save power
    // when it matters. Remote sessions send every frame over the network.
    float cap = 0.0f;
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemMetrics(SM_REMOTESESSION)) {
        cap = 30.0f;
    } else if (GetSystemPowerStatus(&sps)) {
        if (sps.SystemStatusFlag & 1) cap = 30.0f;  // battery saver / energy saver
        else if (sps.ACLineStatus == 0) cap = 60.0f;  // on battery
    }
    if (cap == frameCap_) return;
    frameCap_ = cap;
    for (Overlay& o : overlays_)
        if (o.renderer) o.renderer->SetFrameCap(cap);
}

void App::CheckFullscreen() {
    bool all = false, away = false;
    QUERY_USER_NOTIFICATION_STATE state;
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        all = state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE;
        away = state == QUNS_NOT_PRESENT;  // screen saver, locked, or switched-out session
    }
#ifdef FALLDESKTOP_DEV_TOOLS
    wchar_t ignore[4];
    if (GetEnvironmentVariableW(L"FALLDESKTOP_IGNORE_AWAY", ignore, 4) > 0) away = false;  // test while unattended
#endif
    // Remember the last window activated on each monitor: a fullscreen video
    // keeps its monitor clear of leaves while the user works on another
    // screen, until something else is activated on that monitor.
    HWND fg = GetForegroundWindow();
    DWORD fgPid = 0;
    if (fg) GetWindowThreadProcessId(fg, &fgPid);
    if (fg && fgPid != GetCurrentProcessId() && !IsShellWindow(fg))
        lastActive_[MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST)] = fg;
    std::vector<HMONITOR> monitors;
    for (auto it = lastActive_.begin(); it != lastActive_.end();) {
        if (!IsWindow(it->second)) {
            it = lastActive_.erase(it);
            continue;
        }
        HMONITOR mon = nullptr;
        if (IsFullscreenWindow(it->second, mon) && mon == it->first) monitors.push_back(it->first);
        ++it;
    }
    if (all != fullscreenAll_ || monitors != fullscreenMonitors_ || away != userAway_) {
        fullscreenAll_ = all;
        userAway_ = away;
        fullscreenMonitors_ = std::move(monitors);
        UpdateVisibility();
    }
}
