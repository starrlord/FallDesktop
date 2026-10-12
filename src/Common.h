// Shared includes and small helpers used across falldesktop.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

// The name users see. The exe, window classes, settings key and Run value use
// "falldesktop".
#define FALLDESKTOP_APP_NAME L"Fall Desktop"
constexpr wchar_t kAppName[] = FALLDESKTOP_APP_NAME;
// The project page, as shown in links (prefix L"https://" to open it).
#define FALLDESKTOP_REPO L"github.com/starrlord/FallDesktop"

// Private window messages shared between the UI pieces.
enum : UINT {
    WM_APP_TRAY = WM_APP + 1,        // tray icon callback
    WM_APP_SHOW_SETTINGS,            // second instance asks the first to open settings
    WM_APP_SETTINGS_CHANGED,         // settings window -> app
    WM_APP_SETTINGS_CLOSED,          // settings window -> app
    WM_APP_CLEAR_PILES,              // settings window -> app
    WM_APP_RENDER_FAILED,            // render thread -> app (wParam = HRESULT)
};

// The desktop (Progman, WorkerW).
inline bool IsDesktop(HWND hwnd) {
    wchar_t cls[64];
    if (!GetClassNameW(hwnd, cls, 64)) return false;
    return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0;
}

// The desktop or a taskbar.
inline bool IsShellWindow(HWND hwnd) {
    wchar_t cls[64];
    if (!GetClassNameW(hwnd, cls, 64)) return false;
    return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Shell_TrayWnd") == 0 ||
           wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0;
}

// ---------------------------------------------------------------------------
// Math
// ---------------------------------------------------------------------------
constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

template <typename T>
constexpr T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }

inline float SmoothStep(float e0, float e1, float x) {
    float t = Saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

// Frame-rate independent exponential approach: fraction of the gap closed in dt
// when the time constant is tau seconds.
inline float ApproachFactor(float dt, float tau) {
    return tau <= 0.0f ? 1.0f : 1.0f - std::exp(-dt / tau);
}

// ---------------------------------------------------------------------------
// Random numbers: PCG32 (small, fast, good statistical quality).
// ---------------------------------------------------------------------------
class Rng {
public:
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        Seed(seed, stream);
    }

    void Seed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        state_ = 0;
        inc_ = (stream << 1u) | 1u;
        Next();
        state_ += seed;
        Next();
    }

    uint32_t Next() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // [0, 1)
    float Float() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
    float Range(float lo, float hi) { return lo + (hi - lo) * Float(); }
    int Int(int lo, int hiInclusive) {
        return lo + static_cast<int>(Next() % static_cast<uint32_t>(hiInclusive - lo + 1));
    }
    bool Chance(float p) { return Float() < p; }

    // Standard normal via Box-Muller (one value per call; simple over fast).
    float Normal() {
        float u1 = std::max(Float(), 1e-7f);
        float u2 = Float();
        return std::sqrt(-2.0f * std::log(u1)) * std::cos(kTau * u2);
    }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
};

// Integer hash -> [0,1). Used for stable per-pixel / per-column variation.
inline uint32_t Hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline float HashFloat(uint32_t x) { return static_cast<float>(Hash32(x) >> 8) * (1.0f / 16777216.0f); }
inline float HashFloat(int x, int y) {
    return HashFloat(static_cast<uint32_t>(x) * 0x9E3779B1u ^ Hash32(static_cast<uint32_t>(y) + 0x632BE5ABu));
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
inline double QpcSeconds() {
    static const double inv = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1.0 / static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * inv;
}
