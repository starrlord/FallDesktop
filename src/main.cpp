// Fall Desktop: autumn leaves falling over the Windows desktop and piling up on the
// taskbar, with pumpkins, scarecrows and cobwebs.
#include "App.h"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#ifdef FALLDESKTOP_DEV_TOOLS
int DevPreviewMain(int argc, wchar_t** argv);  // DevPreview.cpp: -1 unless asked for a render
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // The manifest declares per-monitor v2 awareness; this is a fallback for
    // builds that lose the manifest.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

#ifdef FALLDESKTOP_DEV_TOOLS
    // Offscreen renders for checking the artwork (no windows, no tray icon).
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        int rendered = DevPreviewMain(argc, argv);
        // Any other developer option is a mistake: never start the real overlays for it.
        for (int i = 1; i < argc && rendered < 0; ++i)
            if (wcsncmp(argv[i], L"--", 2) == 0) rendered = 2;
        LocalFree(argv);
        if (rendered >= 0) return rendered;
    }
#endif

    // One instance per session. Launching again opens the settings window of
    // the running copy instead.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\falldesktop.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(App::kWindowClass, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(existing, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(existing, WM_APP_SHOW_SETTINGS, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    int rc = 1;
    {
        App app;
        if (app.Init(instance)) rc = app.Run();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return rc;
}
