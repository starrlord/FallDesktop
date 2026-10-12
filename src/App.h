// The UI-thread controller: tray icon and menu, one click-through overlay
// window per monitor, monitor/taskbar geometry, keeping the overlays above the
// taskbar (or just above the desktop, behind windows), pausing for fullscreen
// apps, and the settings window.
#pragma once

#include "Common.h"
#include "Renderer.h"
#include "Settings.h"

#include <map>
#include <memory>

class SettingsWindow;

class App {
public:
    App();
    ~App();

    bool Init(HINSTANCE instance);
    int Run();

    static constexpr wchar_t kWindowClass[] = L"falldesktopApp";

private:
    struct Overlay {
        HWND hwnd = nullptr;
        MonitorTarget target;
        std::unique_ptr<MonitorRenderer> renderer;
        bool shown = false;      // window currently shown
        bool wantVisible = true;
        int alpha = 0;           // current fade alpha 0..255
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                      DWORD thread, DWORD time);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // Tray
    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayIcon();
    void ShowContextMenu(POINT pt);
    void OnCommand(UINT id);

    // Overlays
    std::vector<MonitorTarget> EnumerateMonitors() const;
    void RebuildOverlays(bool force);
    void DestroyOverlays();
    void UpdateVisibility();
    void StepFades();
    void PlaceOverlay(HWND overlay) const;
    void RestackOverlays();
    bool TaskbarAboveOverlay(HWND overlay) const;
    void CheckFullscreen();
    void UpdateFrameCap();

    // Settings
    void ApplySettings(const Settings& s, bool save = true);
    void ShowSettingsWindow();
    void ShowAbout();
    void ShowFirstRunTip();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    HICON icon_ = nullptr;
    HICON iconPaused_ = nullptr;
    HICON iconLarge_ = nullptr;  // balloon
    bool trayAdded_ = false;

    Settings settings_;
    GpuDeviceHub gpuHub_;  // declared before overlays_: renderers use it until they stop
    std::vector<Overlay> overlays_;
    std::map<std::wstring, std::unique_ptr<Scene>> sceneCache_;
    double clockOrigin_ = 0.0;

    HWINEVENTHOOK reorderHook_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;
    HWINEVENTHOOK showHook_ = nullptr;      // windows shown or hidden
    HWINEVENTHOOK minimizeHook_ = nullptr;  // minimized or restored
    HWINEVENTHOOK moveSizeHook_ = nullptr;  // moved or resized by the user
    bool raisePending_ = false;
    bool fadeTimer_ = false;
    double lastRaise_ = 0.0;

    bool sessionLocked_ = false;
    bool displayOff_ = false;
    bool userAway_ = false;  // screen saver running
    float frameCap_ = 0.0f;  // fps cap from power state (0 = none)
    int trayRetries_ = 0;
    HPOWERNOTIFY displayNotify_ = nullptr;
    std::vector<HMONITOR> fullscreenMonitors_;
    std::map<HMONITOR, HWND> lastActive_;  // last window activated on each monitor
    bool fullscreenAll_ = false;

    std::unique_ptr<SettingsWindow> settingsWindow_;
};
