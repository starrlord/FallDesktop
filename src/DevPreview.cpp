// Developer tool (FALLDESKTOP_DEV_TOOLS builds only): renders a scene offscreen
// to PNG files on a Direct3D 11 / Direct2D device context like the overlay's,
// without windows, tray icon, single-instance check or saved settings. For
// checking the artwork and the simulation, and for the Store media (see
// tools/store):
//
//   falldesktop.exe --render out.png [options]   a desktop's worth of autumn
//   falldesktop.exe --atlas out.png [--shadow 0|1]  every leaf sprite, tinted
//   falldesktop.exe --settings out.png [options]  the settings window (kept
//     cloaked, so nothing shows on screen), as FALLDESKTOP_SNAPSHOT saves it
//
// Options (sizes in DIPs unless noted):
//   --width px (1600)  --height px (900)  --scale px-per-DIP (1.5)
//   --seconds s (12): simulated time; the frame at the end is saved
//   --shots t1,t2,...: save these moments instead, as out-<i>.png
//   --frames dir --from s --to s --fps n (60): save every frame from..to as
//     dir\f00000.png..., with dir\frames.csv (frame, time, pointer shown, x, y
//     in output px) for compositing a pointer later
//   --out-width px --out-height px: scale each saved image to this size
//     (render big and save scaled for smooth supersampled edges)
//   --amount --speed --wind --gusts --size --piles n: as the settings sliders
//   --pumpkins n --scarecrows n: amounts 0..100 (-1 hides them)
//   --cobwebs 0|1  --halloween 0|1  --shadow 0|1  --mouse 0|1  --behind 0|1  --paused 0|1
//   --color autumn|golden|crimson|custom  --custom RRGGBB  --mask hex (leaf kinds)
//   --boost n (6): how much faster the leaf piles build up
//   --set t,key,value: change one of the settings above (or boost) at t
//     seconds (repeatable)
//   --clear t: "Clear leaf piles" at t seconds (the leaves blow away)
//   --bg dusk|light|dark: what the scene is drawn over, or
//   --background file.png: an image drawn under the scene (stretched to fit),
//     e.g. a wallpaper with a taskbar and windows the leaves fall in front of
//   --foreground file.png: an image drawn over the scene (windows the scene
//     stays behind, as with "Leaves behind windows")
//   --inset top,left,right: the work area's insets (px), as under a taskbar
//     docked at the top or side: the cobwebs hang from its corners
//   --cover x,y,w,h: a window over the scene: the props and webs step aside
//     there, as on a real desktop (repeatable)
//   --cursor x0,y0,x1,y1,t0,t1: the pointer sweeps from (x0,y0) to (x1,y1)
//     between t0 and t1 seconds (repeatable; elsewhere it is off screen)
//   --ui-scale s  --theme light|dark: the settings window's scale and theme
//     (--settings; otherwise the monitor's and the system's)
// Errors and a summary go to <out>.log (with --frames: dir\frames.log). Exit
// code 0 on success.
#ifdef FALLDESKTOP_DEV_TOOLS

#include "Palette.h"
#include "Scene.h"
#include "SettingsWindow.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

struct Sweep {
    float x0, y0, x1, y1, t0, t1;
};

struct SetCommand {
    float t;
    std::wstring key, value;
};

struct Options {
    std::wstring out;
    bool atlas = false;
    bool settings = false;
    int width = 1600, height = 900;
    int outWidth = 0, outHeight = 0;  // 0: as rendered
    float scale = 1.5f;
    float seconds = 12.0f;
    std::vector<float> shots;
    std::wstring framesDir;
    float from = 0.0f, to = -1.0f, fps = 60.0f;
    float boost = 6.0f;
    std::wstring bg = L"dusk";
    std::wstring background, foreground;
    std::vector<Sweep> sweeps;
    std::vector<D2D1_RECT_F> covers;  // DIP
    float insetTop = 0.0f, insetLeft = 0.0f, insetRight = 0.0f;  // px: a taskbar docked at the top or side
    std::vector<SetCommand> sets;
    std::wstring uiScale, theme;
    Settings s;
};

FILE* g_log = nullptr;

void Log(const wchar_t* fmt, ...) {
    if (!g_log) return;
    va_list args;
    va_start(args, fmt);
    vfwprintf(g_log, fmt, args);
    va_end(args);
    fputwc(L'\n', g_log);
    fflush(g_log);
}

std::vector<float> Floats(const wchar_t* s) {
    std::vector<float> v;
    while (s && *s) {
        wchar_t* end = nullptr;
        const float f = std::wcstof(s, &end);
        if (end == s) break;
        v.push_back(f);
        s = *end == L',' ? end + 1 : end;
    }
    return v;
}

// A setting by its command-line name. False for a name it doesn't know.
bool ApplySetting(Settings& s, const std::wstring& key, const wchar_t* v) {
    auto num = [&] { return v ? static_cast<int>(std::wcstol(v, nullptr, 10)) : 0; };
    if (key == L"amount") s.amount = num();
    else if (key == L"speed") s.speed = num();
    else if (key == L"wind") s.wind = num();
    else if (key == L"gusts") s.gusts = num();
    else if (key == L"size") s.size = num();
    else if (key == L"piles") s.piles = num();
    else if (key == L"pumpkins") {
        s.pumpkins = num() >= 0;
        if (s.pumpkins) s.pumpkinAmount = num();
    } else if (key == L"scarecrows") {
        s.scarecrows = num() >= 0;
        if (s.scarecrows) s.scarecrowAmount = num();
    } else if (key == L"cobwebs") s.cobwebs = num() != 0;
    else if (key == L"halloween") s.halloween = num() != 0;
    else if (key == L"shadow") s.shadow = num() != 0;
    else if (key == L"mouse") s.mouse = num() != 0;
    else if (key == L"behind") s.behindWindows = num() != 0;  // (shows in --settings; the overlay never knows)
    else if (key == L"paused") s.paused = num() != 0;
    else if (key == L"mask") s.leafMask = v ? static_cast<uint32_t>(std::wcstoul(v, nullptr, 16)) : 0;
    else if (key == L"custom") {
        const unsigned long c = v ? std::wcstoul(v, nullptr, 16) : 0;
        s.customColor = RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    } else if (key == L"color") {
        const std::wstring c = v ? v : L"";
        s.color = c == L"golden"    ? LeafColor::Golden
                  : c == L"crimson" ? LeafColor::Crimson
                  : c == L"custom"  ? LeafColor::Custom
                                    : LeafColor::Autumn;
    } else return false;
    return true;
}

bool Parse(int argc, wchar_t** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        const wchar_t* v = i + 1 < argc ? argv[i + 1] : nullptr;
        auto num = [&] { return v ? std::wcstof(v, nullptr) : 0.0f; };
        auto inum = [&] { return v ? static_cast<int>(std::wcstol(v, nullptr, 10)) : 0; };
        bool used = true;
        if (a == L"--render" || a == L"--atlas" || a == L"--settings") {
            o.atlas = a == L"--atlas";
            o.settings = a == L"--settings";
            if (!v) return false;
            o.out = v;
        } else if (a == L"--width") o.width = inum();
        else if (a == L"--height") o.height = inum();
        else if (a == L"--out-width") o.outWidth = inum();
        else if (a == L"--out-height") o.outHeight = inum();
        else if (a == L"--scale") o.scale = num();
        else if (a == L"--seconds") o.seconds = num();
        else if (a == L"--shots") o.shots = Floats(v);
        else if (a == L"--frames") o.framesDir = v ? v : L"";
        else if (a == L"--from") o.from = num();
        else if (a == L"--to") o.to = num();
        else if (a == L"--fps") o.fps = num();
        else if (a == L"--boost") o.boost = num();
        else if (a == L"--clear") o.sets.push_back({num(), L"clear", L"1"});
        else if (a == L"--ui-scale") o.uiScale = v ? v : L"";
        else if (a == L"--theme") o.theme = v ? v : L"";
        else if (a == L"--bg") o.bg = v ? v : L"dusk";
        else if (a == L"--background") o.background = v ? v : L"";
        else if (a == L"--foreground") o.foreground = v ? v : L"";
        else if (a == L"--cursor") {
            const std::vector<float> f = Floats(v);
            if (f.size() != 6) return false;
            o.sweeps.push_back({f[0], f[1], f[2], f[3], f[4], f[5]});
        } else if (a == L"--inset") {
            const std::vector<float> f = Floats(v);
            if (f.size() != 3) return false;
            o.insetTop = f[0];
            o.insetLeft = f[1];
            o.insetRight = f[2];
        } else if (a == L"--cover") {
            const std::vector<float> f = Floats(v);
            if (f.size() != 4) return false;
            o.covers.push_back({f[0], f[1], f[0] + f[2], f[1] + f[3]});
        } else if (a == L"--set") {
            // t,key,value
            const std::wstring arg = v ? v : L"";
            const size_t c1 = arg.find(L','), c2 = c1 == std::wstring::npos ? c1 : arg.find(L',', c1 + 1);
            if (c2 == std::wstring::npos) return false;
            o.sets.push_back({std::wcstof(arg.c_str(), nullptr), arg.substr(c1 + 1, c2 - c1 - 1), arg.substr(c2 + 1)});
        } else if (a.size() > 2 && a[0] == L'-' && a[1] == L'-' && ApplySetting(o.s, a.substr(2), v)) {
        } else used = false;
        if (used) ++i;
    }
    o.s.Sanitize();
    if (!o.framesDir.empty() && o.to < 0.0f) o.to = o.seconds;
    if (!o.framesDir.empty()) o.seconds = std::max(o.seconds, o.to);
    return (!o.out.empty() || !o.framesDir.empty()) && o.width > 16 && o.height > 16 && o.scale > 0.1f &&
           o.fps > 0.0f && o.outWidth >= 0 && o.outHeight >= 0;
}

HRESULT CreateContext(ComPtr<ID2D1DeviceContext>& dc) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    ComPtr<ID3D11Device> d3d;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &d3d, nullptr, nullptr);
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIDevice> dxgi;
    if (FAILED(hr = d3d.As(&dxgi))) return hr;
    ComPtr<ID2D1Factory1> factory;
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()))) return hr;
    ComPtr<ID2D1Device> device;
    if (FAILED(hr = factory->CreateDevice(dxgi.Get(), &device))) return hr;
    return device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
}

HRESULT CreateTarget(ID2D1DeviceContext* dc, UINT w, UINT h, ComPtr<ID2D1Bitmap1>& target) {
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = dc->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, props, &target);
    if (FAILED(hr)) return hr;
    dc->SetTarget(target.Get());
    dc->SetDpi(96.0f, 96.0f);  // DIPs are pixels, as Scene::Draw expects
    return S_OK;
}

HRESULT LoadImage(ID2D1DeviceContext* dc, IWICImagingFactory* wic, const std::wstring& path,
                  ComPtr<ID2D1Bitmap1>& out) {
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                &decoder);
    if (FAILED(hr) || FAILED(hr = decoder->GetFrame(0, &frame)) || FAILED(hr = wic->CreateFormatConverter(&converter)))
        return hr;
    if (FAILED(hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                          0.0, WICBitmapPaletteTypeCustom)))
        return hr;
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    return dc->CreateBitmapFromWicBitmap(converter.Get(), &props, &out);
}

HRESULT SavePng(IWICImagingFactory* wic, ID2D1DeviceContext* dc, ID2D1Bitmap1* target, const std::wstring& path) {
    const D2D1_SIZE_U size = target->GetPixelSize();
    const D2D1_BITMAP_PROPERTIES1 props =
        D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> cpu;
    HRESULT hr = dc->CreateBitmap(size, nullptr, 0, props, &cpu);
    if (FAILED(hr)) return hr;
    const D2D1_POINT_2U origin = {0, 0};
    const D2D1_RECT_U rect = {0, 0, size.width, size.height};
    if (FAILED(hr = cpu->CopyFromBitmap(&origin, target, &rect))) return hr;
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(hr = cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return hr;
    ComPtr<IWICBitmap> bmp;
    hr = wic->CreateBitmapFromMemory(size.width, size.height, GUID_WICPixelFormat32bppPBGRA, mapped.pitch,
                                     mapped.pitch * size.height, mapped.bits, &bmp);
    cpu->Unmap();
    if (FAILED(hr)) return hr;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(hr = wic->CreateStream(&stream))) return hr;
    if (FAILED(hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) return hr;
    if (FAILED(hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) return hr;
    if (FAILED(hr = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache))) return hr;
    if (FAILED(hr = enc->CreateNewFrame(&frame, nullptr))) return hr;
    if (FAILED(hr = frame->Initialize(nullptr))) return hr;
    if (FAILED(hr = frame->WriteSource(bmp.Get(), nullptr))) return hr;
    if (FAILED(hr = frame->Commit())) return hr;
    return enc->Commit();
}

D2D1_COLOR_F Hex(uint32_t c, float a = 1.0f) {
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f, a};
}

// A stand-in desktop: wallpaper, a window to see the leaves over, and the taskbar.
void DrawBackground(ID2D1DeviceContext* dc, const Options& o, float groundY) {
    const float w = static_cast<float>(o.width), h = static_cast<float>(o.height);
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(Hex(0x000000), &brush);
    if (o.bg == L"light" || o.bg == L"dark") {
        dc->Clear(o.bg == L"light" ? Hex(0xE9ECF1) : Hex(0x111418));
        // A window, to see the leaves over light content.
        brush->SetColor(Hex(0xFFFFFF));
        dc->FillRectangle(D2D1::RectF(w * 0.38f, h * 0.18f, w * 0.78f, groundY - h * 0.12f), brush.Get());
    } else {
        const D2D1_GRADIENT_STOP stops[] = {
            {0.0f, Hex(0x1A1430)}, {0.55f, Hex(0x3A2648)}, {0.85f, Hex(0x7A3F3A)}, {1.0f, Hex(0xC0673A)}};
        ComPtr<ID2D1GradientStopCollection> col;
        ComPtr<ID2D1LinearGradientBrush> sky;
        if (SUCCEEDED(dc->CreateGradientStopCollection(stops, 4, &col)) &&
            SUCCEEDED(dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, groundY}),
                                                    col.Get(), &sky)))
            dc->FillRectangle(D2D1::RectF(0, 0, w, groundY), sky.Get());
        else
            dc->Clear(Hex(0x2A2040));
    }
    brush->SetColor(Hex(0x1C1C1C, 0.96f));
    dc->FillRectangle(D2D1::RectF(0, groundY, w, h), brush.Get());
    // Taskbar icons, so the pile's lip has something to hang over.
    const uint32_t icons[] = {0x3A8EE6, 0xF2B233, 0x4CB86B, 0xE5533C, 0x9A6BE0};
    const float icon = 24.0f * o.scale, gap = 14.0f * o.scale;
    float x = w * 0.5f - (5.0f * icon + 4.0f * gap) * 0.5f;
    for (uint32_t c : icons) {
        brush->SetColor(Hex(c));
        const float y = groundY + (h - groundY - icon) * 0.5f;
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + icon, y + icon), 4.0f, 4.0f), brush.Get());
        x += icon + gap;
    }
}

CursorState CursorAt(const Options& o, double t) {
    CursorState c;
    for (const Sweep& s : o.sweeps) {
        if (t < s.t0 || t > s.t1 || s.t1 <= s.t0) continue;
        const float u = static_cast<float>((t - s.t0) / (s.t1 - s.t0));
        c.valid = true;
        c.x = Lerp(s.x0, s.x1, u) * o.scale;
        c.y = Lerp(s.y0, s.y1, u) * o.scale;
        c.vx = (s.x1 - s.x0) * o.scale / (s.t1 - s.t0);
        c.vy = (s.y1 - s.y0) * o.scale / (s.t1 - s.t0);
    }
    return c;
}

std::wstring ShotPath(const std::wstring& out, size_t i) {
    const size_t dot = out.find_last_of(L'.');
    const std::wstring stem = dot == std::wstring::npos ? out : out.substr(0, dot);
    return stem + L"-" + std::to_wstring(i) + L".png";
}

int RenderScene(const Options& o, ID2D1DeviceContext* dc, IWICImagingFactory* wic) {
    LeafAtlas atlas;
    LeafArtOptions art;
    art.shadow = o.s.shadow;
    HRESULT hr = atlas.Build(dc, wic, art);
    if (FAILED(hr)) {
        Log(L"atlas build failed: 0x%08X", static_cast<unsigned>(hr));
        return 3;
    }
    ComPtr<ID2D1Bitmap1> background, foreground;
    if (!o.background.empty() && FAILED(hr = LoadImage(dc, wic, o.background, background))) {
        Log(L"can't load %s: 0x%08X", o.background.c_str(), static_cast<unsigned>(hr));
        return 10;
    }
    if (!o.foreground.empty() && FAILED(hr = LoadImage(dc, wic, o.foreground, foreground))) {
        Log(L"can't load %s: 0x%08X", o.foreground.c_str(), static_cast<unsigned>(hr));
        return 10;
    }
    ComPtr<ID2D1Bitmap1> target, scaled;
    const bool resize = (o.outWidth > 0 && o.outWidth != o.width) || (o.outHeight > 0 && o.outHeight != o.height);
    const UINT outW = static_cast<UINT>(o.outWidth > 0 ? o.outWidth : o.width);
    const UINT outH = static_cast<UINT>(o.outHeight > 0 ? o.outHeight : o.height);
    if (resize && FAILED(hr = CreateTarget(dc, outW, outH, scaled))) {
        Log(L"output target failed: 0x%08X", static_cast<unsigned>(hr));
        return 4;
    }
    if (FAILED(hr = CreateTarget(dc, static_cast<UINT>(o.width), static_cast<UINT>(o.height), target))) {
        Log(L"target failed: 0x%08X", static_cast<unsigned>(hr));
        return 4;
    }
    SceneGeometry geo;
    geo.width = o.width;
    geo.height = o.height;
    geo.dpiScale = o.scale;
    geo.groundY = std::round(static_cast<float>(o.height) - 48.0f * o.scale);
    geo.originScale = o.scale;
    geo.insetTop = o.insetTop;
    geo.insetLeft = o.insetLeft;
    geo.insetRight = o.insetRight;
    Settings settings = o.s;
    auto scene = std::make_unique<Scene>();
    scene->SetPileBoost(o.boost);
    scene->SetScenerySettle(0.0f);
    scene->Configure(geo, MakeSimParams(settings));
    const auto covered = [&](float x, float y) {
        for (const D2D1_RECT_F& r : o.covers)
            if (x >= r.left * o.scale && x < r.right * o.scale && y >= r.top * o.scale && y < r.bottom * o.scale)
                return true;
        return false;
    };
    std::vector<SetCommand> sets = o.sets;
    std::stable_sort(sets.begin(), sets.end(), [](const SetCommand& a, const SetCommand& b) { return a.t < b.t; });
    size_t nextSet = 0;

    FILE* csv = nullptr;
    if (!o.framesDir.empty()) {
        CreateDirectoryW(o.framesDir.c_str(), nullptr);
        _wfopen_s(&csv, (o.framesDir + L"\\frames.csv").c_str(), L"w");
        if (csv) fwprintf(csv, L"frame,time,pointer,x,y\n");
    }
    const float sx = static_cast<float>(outW) / static_cast<float>(o.width);
    const float sy = static_cast<float>(outH) / static_cast<float>(o.height);

    const int frames = std::max(1, static_cast<int>(std::lround(o.seconds * 60.0f)));
    std::vector<float> shots = o.shots;
    std::sort(shots.begin(), shots.end());
    size_t next = 0, saved = 0;
    int frameNo = 0;
    double nextFrame = o.from, nextProbe = 0.0;
    for (int i = 0; i <= frames; ++i) {
        const double t = static_cast<double>(i) / 60.0;
        bool changed = false;
        for (; nextSet < sets.size() && sets[nextSet].t <= t + 1e-6; ++nextSet) {
            const SetCommand& c = sets[nextSet];
            if (c.key == L"boost") scene->SetPileBoost(std::wcstof(c.value.c_str(), nullptr));
            else if (c.key == L"clear") scene->ClearPiles(true);
            else if (ApplySetting(settings, c.key, c.value.c_str())) changed = true;
            else Log(L"unknown setting %s", c.key.c_str());
        }
        if (changed) {
            settings.Sanitize();
            if (settings.shadow != art.shadow) {
                art.shadow = settings.shadow;
                atlas.Build(dc, wic, art);
            }
            scene->SetParams(MakeSimParams(settings));
        }
        const CursorState cursor = CursorAt(o, t);
        if (i > 0) scene->Update(1.0f / 60.0f, t, cursor);
        if (scene->CoverStale() || (!o.covers.empty() && t >= nextProbe)) {
            scene->ProbeCover(covered);
            nextProbe = t + 0.25;
        }
        dc->SetTarget(target.Get());
        dc->BeginDraw();
        if (background) {
            dc->Clear(Hex(0x000000));
            dc->DrawBitmap(background.Get(), D2D1::RectF(0, 0, static_cast<float>(o.width), static_cast<float>(o.height)),
                           1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        } else {
            DrawBackground(dc, o, geo.groundY);
        }
        scene->Draw(dc, atlas);
        if (foreground)
            dc->DrawBitmap(foreground.Get(), D2D1::RectF(0, 0, static_cast<float>(o.width), static_cast<float>(o.height)),
                           1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        if (FAILED(hr = dc->EndDraw())) {
            Log(L"EndDraw failed at frame %d: 0x%08X", i, static_cast<unsigned>(hr));
            return 5;
        }
        std::wstring path;
        if (!o.framesDir.empty()) {
            if (t + 1e-6 >= nextFrame && t <= o.to + 1e-6) {
                wchar_t name[32];
                swprintf_s(name, L"\\f%05d.png", frameNo);
                path = o.framesDir + name;
                if (csv)
                    fwprintf(csv, L"%d,%.4f,%d,%.1f,%.1f\n", frameNo, t, cursor.valid ? 1 : 0, cursor.x * sx,
                             cursor.y * sy);
                ++frameNo;
                nextFrame += 1.0 / o.fps;
            }
        } else {
            const bool last = shots.empty() && i == frames;
            if (last || (next < shots.size() && t + 1e-6 >= shots[next]))
                path = shots.empty() ? o.out : ShotPath(o.out, next++);
        }
        if (path.empty()) continue;
        ID2D1Bitmap1* image = target.Get();
        if (resize) {
            dc->SetTarget(scaled.Get());
            dc->BeginDraw();
            dc->Clear(Hex(0x000000));
            dc->DrawBitmap(target.Get(), D2D1::RectF(0, 0, static_cast<float>(outW), static_cast<float>(outH)), 1.0f,
                           D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
            if (FAILED(hr = dc->EndDraw())) {
                Log(L"resize failed at frame %d: 0x%08X", i, static_cast<unsigned>(hr));
                return 5;
            }
            image = scaled.Get();
        }
        if (FAILED(hr = SavePng(wic, dc, image, path))) {
            Log(L"saving %s failed: 0x%08X", path.c_str(), static_cast<unsigned>(hr));
            return 6;
        }
        ++saved;
        if (o.framesDir.empty())
            Log(L"t=%.2fs saved %s (%zu leaves on screen)", t, path.c_str(), scene->LeafCount());
        if (o.framesDir.empty() && !shots.empty() && next == shots.size()) break;
        if (!o.framesDir.empty() && t >= o.to) break;
    }
    if (csv) fclose(csv);
    if (!o.framesDir.empty()) Log(L"saved %zu frames to %s", saved, o.framesDir.c_str());
    scene->ReleaseDeviceResources();
    return saved ? 0 : 7;
}

// Every sprite on a grid: a row per kind, for each variant the bare greys and
// three autumn colours (with the blush where the design has one). The left
// half sits on a dark background and the right half on a light one.
int RenderAtlas(const Options& o, ID2D1DeviceContext* dc, IWICImagingFactory* wic) {
    LeafAtlas atlas;
    LeafArtOptions art;
    art.shadow = o.s.shadow;
    HRESULT hr = atlas.Build(dc, wic, art);
    if (FAILED(hr)) {
        Log(L"atlas build failed: 0x%08X", static_cast<unsigned>(hr));
        return 3;
    }
    ComPtr<ID2D1DeviceContext3> dc3;
    ComPtr<ID2D1SpriteBatch> batch;
    if (FAILED(hr = dc->QueryInterface(IID_PPV_ARGS(&dc3))) || FAILED(hr = dc3->CreateSpriteBatch(&batch))) {
        Log(L"no sprite batches: 0x%08X", static_cast<unsigned>(hr));
        return 8;
    }
    constexpr int kCell = 84, kColors = 4;
    constexpr float kDiameter = 64.0f;
    const int cols = kMaxVariants * kColors;
    const UINT w = static_cast<UINT>(cols * kCell), h = static_cast<UINT>(kLeafKindCount * kCell);
    ComPtr<ID2D1Bitmap1> target;
    if (FAILED(hr = CreateTarget(dc, w, h, target))) return 4;
    const float genes[kColors - 1] = {0.04f, 0.33f, 0.62f};
    dc->BeginDraw();
    dc->Clear(Hex(0x1E1A26));
    ComPtr<ID2D1SolidColorBrush> light;
    dc->CreateSolidColorBrush(Hex(0xE9ECF1), &light);
    dc->FillRectangle(D2D1::RectF(static_cast<float>(w) * 0.5f, 0, static_cast<float>(w), static_cast<float>(h)),
                      light.Get());
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    auto draw = [&](int index, float cx, float cy, D2D1_COLOR_F color) {
        if (index < 0) return;
        const LeafAtlas::Sprite& sp = atlas.Get(index, kDiameter);
        if (!sp.bitmap) return;
        const float half = kDiameter * sp.extent * 0.5f;
        const D2D1_RECT_F dst = {cx - half, cy - half, cx + half, cy + half};
        batch->Clear();
        if (SUCCEEDED(batch->AddSprites(1, &dst, &sp.src, &color, nullptr)))
            dc3->DrawSpriteBatch(batch.Get(), 0, 1, sp.bitmap, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                 D2D1_SPRITE_OPTIONS_NONE);
    };
    for (int k = 0; k < kLeafKindCount; ++k) {
        const LeafKind kind = static_cast<LeafKind>(k);
        const bool tinted = GetLeafTraits(kind).tinted;
        for (int v = 0; v < kMaxVariants; ++v) {
            const int variant = std::min(v, LeafVariantCount(kind) - 1);
            for (int c = 0; c < kColors; ++c) {
                const float cx = (static_cast<float>(v * kColors + c) + 0.5f) * kCell;
                const float cy = (static_cast<float>(k) + 0.5f) * kCell;
                LeafRgb base{1, 1, 1}, blush{1, 1, 1};
                if (c > 0 && tinted) LeafColors(LeafColor::Autumn, 0, genes[c - 1], base, blush);
                draw(LeafAtlas::SpriteIndex(kind, variant), cx, cy, {base.r, base.g, base.b, 1.0f});
                if (c > 0 && tinted)
                    draw(LeafAtlas::BlushIndex(kind, variant), cx, cy, {blush.r, blush.g, blush.b, 1.0f});
            }
        }
    }
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (FAILED(hr = dc->EndDraw())) return 5;
    if (FAILED(hr = SavePng(wic, dc, target.Get(), o.out))) {
        Log(L"saving failed: 0x%08X", static_cast<unsigned>(hr));
        return 6;
    }
    Log(L"saved %s (%d kinds x %d variants)", o.out.c_str(), kLeafKindCount, kMaxVariants);
    return 0;
}

// The settings window, cloaked the whole time, through its own snapshot hook.
int RenderSettings(const Options& o) {
    SetEnvironmentVariableW(L"FALLDESKTOP_SNAPSHOT", o.out.c_str());
    if (!o.uiScale.empty()) SetEnvironmentVariableW(L"FALLDESKTOP_UI_SCALE", o.uiScale.c_str());
    if (!o.theme.empty()) SetEnvironmentVariableW(L"FALLDESKTOP_THEME", o.theme.c_str());
    DeleteFileW(o.out.c_str());
    auto window = std::make_unique<SettingsWindow>(GetModuleHandleW(nullptr), nullptr);
    window->Show(o.s);
    if (HWND hwnd = FindWindowW(L"falldesktopSettings", nullptr)) {
        BOOL cloak = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    }
    const double end = QpcSeconds() + 6.0;
    while (QpcSeconds() < end && GetFileAttributesW(o.out.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        window->Frame();
        Sleep(10);
    }
    window->Close();
    window.reset();
    const bool saved = GetFileAttributesW(o.out.c_str()) != INVALID_FILE_ATTRIBUTES;
    Log(saved ? L"saved %s" : L"no snapshot written to %s", o.out.c_str());
    return saved ? 0 : 9;
}

}  // namespace

// -1 when the command line doesn't ask for a render.
int DevPreviewMain(int argc, wchar_t** argv) {
    bool asked = false;
    for (int i = 1; i < argc; ++i)
        if (wcscmp(argv[i], L"--render") == 0 || wcscmp(argv[i], L"--atlas") == 0 || wcscmp(argv[i], L"--settings") == 0 ||
            wcscmp(argv[i], L"--frames") == 0)
            asked = true;
    if (!asked) return -1;
    Options o;
    const bool ok = Parse(argc, argv, o);
    if (!o.framesDir.empty()) {
        CreateDirectoryW(o.framesDir.c_str(), nullptr);
        _wfopen_s(&g_log, (o.framesDir + L"\\frames.log").c_str(), L"w, ccs=UTF-8");
    } else if (!o.out.empty()) {
        _wfopen_s(&g_log, (o.out + L".log").c_str(), L"w, ccs=UTF-8");
    }
    if (!ok) {
        Log(L"bad arguments");
        if (g_log) fclose(g_log);
        return 2;
    }
    const HRESULT co = CoInitializeEx(nullptr, o.settings ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    int rc = 1;
    if (o.settings) {
        rc = RenderSettings(o);
    } else {
        ComPtr<ID2D1DeviceContext> dc;
        ComPtr<IWICImagingFactory> wic;
        HRESULT hr = CreateContext(dc);
        if (SUCCEEDED(hr))
            hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        if (FAILED(hr)) Log(L"device setup failed: 0x%08X", static_cast<unsigned>(hr));
        else rc = o.atlas ? RenderAtlas(o, dc.Get(), wic.Get()) : RenderScene(o, dc.Get(), wic.Get());
    }
    if (SUCCEEDED(co)) CoUninitialize();
    if (g_log) fclose(g_log);
    return rc;
}

#endif  // FALLDESKTOP_DEV_TOOLS
