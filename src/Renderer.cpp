#include "Renderer.h"

#include <d3d11_4.h>
#include <dcomp.h>
#include <dxgi1_3.h>

#include <cstdio>

// Per-thread objects built on the shared GpuDevice.
struct MonitorRenderer::Devices {
    std::shared_ptr<GpuDevice> gpu;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<IDCompositionDevice> dcomp;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    ComPtr<IDXGISwapChain2> swapChain;
    ComPtr<ID2D1Bitmap1> backBuffer;
    ComPtr<IDXGIOutput> output;  // this monitor's output, for WaitForVBlank
    ComPtr<IWICImagingFactory> wic;
    HANDLE waitable = nullptr;

    ~Devices() {
        if (waitable) CloseHandle(waitable);
    }
};

namespace {

// Holds Direct2D's device lock while making direct DXGI/D3D calls on the
// shared device.
class D2DLock {
public:
    explicit D2DLock(ID2D1Multithread* m) : m_(m) {
        if (m_) m_->Enter();
    }
    ~D2DLock() {
        if (m_) m_->Leave();
    }
    D2DLock(const D2DLock&) = delete;
    D2DLock& operator=(const D2DLock&) = delete;

private:
    ID2D1Multithread* m_;
};

bool IsDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == D2DERR_RECREATE_TARGET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

// The GPU that drives the primary display, so the overlays never pay for a
// cross-adapter copy (e.g. on desktops that also expose an idle iGPU).
ComPtr<IDXGIAdapter1> DisplayAdapter() {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
    const HMONITOR primary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a, adapter.Reset()) {
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o, output.Reset()) {
            DXGI_OUTPUT_DESC od;
            if (SUCCEEDED(output->GetDesc(&od)) && od.Monitor == primary) return adapter;
        }
    }
    return nullptr;
}

HRESULT CreateD3DDevice(ComPtr<ID3D11Device>& out, bool& software) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = E_FAIL;
    if (ComPtr<IDXGIAdapter1> adapter = DisplayAdapter()) {
        hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &out, nullptr, nullptr);
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &out, nullptr, nullptr);
    }
    software = false;
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &out, nullptr, nullptr);
        software = SUCCEEDED(hr);
    }
    return hr;
}

// Finds the DXGI output that drives `monitor`, on any adapter. Uses a fresh
// factory: a factory's adapter/output list is a snapshot, so the device's own
// factory would not know monitors connected after it was created.
ComPtr<IDXGIOutput> FindOutput(HMONITOR monitor) {
    ComPtr<IDXGIFactory1> f1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f1)))) return nullptr;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT a = 0; f1->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        ComPtr<IDXGIOutput> output;
        for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o) {
            DXGI_OUTPUT_DESC desc;
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) return output;
            output.Reset();
        }
        adapter.Reset();
    }
    return nullptr;
}

}  // namespace

HRESULT GpuDeviceHub::Acquire(std::shared_ptr<GpuDevice>& out) {
    std::lock_guard lock(mutex_);
    if (device_ && !device_->lost) {
        // A device can be removed while every overlay is hidden (driver update
        // overnight), and a software fallback should give way to the GPU again.
        if (FAILED(device_->d3d->GetDeviceRemovedReason()) || (device_->software && DisplayAdapter())) {
            device_->lost = true;
        } else {
            out = device_;
            return S_OK;
        }
    }
    auto g = std::make_shared<GpuDevice>();
    HRESULT hr = CreateD3DDevice(g->d3d, g->software);
    if (FAILED(hr)) return hr;
    // Several threads submit work through the one immediate context.
    ComPtr<ID3D11DeviceContext> ctx;
    g->d3d->GetImmediateContext(&ctx);
    ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(ctx.As(&mt))) mt->SetMultithreadProtected(TRUE);
    if (FAILED(hr = g->d3d.As(&g->dxgi))) return hr;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(hr = g->dxgi->GetAdapter(&adapter))) return hr;
    if (FAILED(hr = adapter->GetParent(IID_PPV_ARGS(&g->factory)))) return hr;
    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &fo,
                                      reinterpret_cast<void**>(g->d2dFactory.GetAddressOf()))))
        return hr;
    if (FAILED(hr = g->d2dFactory.As(&g->lock))) return hr;
    if (FAILED(hr = g->d2dFactory->CreateDevice(g->dxgi.Get(), &g->d2d))) return hr;
    device_ = g;
    out = std::move(g);
    return S_OK;
}

MonitorRenderer::MonitorRenderer(HWND notify, GpuDeviceHub& gpu, const MonitorTarget& target,
                                 std::unique_ptr<Scene> scene, const Settings& settings, double clockOrigin)
    : notify_(notify), gpuHub_(gpu), target_(target), scene_(std::move(scene)), clockOrigin_(clockOrigin),
      settings_(settings) {
    if (!scene_) scene_ = std::make_unique<Scene>();
    wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    wchar_t debug[8];
    stats_.enabled = GetEnvironmentVariableW(L"FALLDESKTOP_DEBUG", debug, 8) > 0 && debug[0] == L'1';
#ifdef FALLDESKTOP_DEV_TOOLS
    // Screenshots/tests: build leaf piles faster (FALLDESKTOP_PILE_BOOST=8).
    wchar_t boost[16];
    if (GetEnvironmentVariableW(L"FALLDESKTOP_PILE_BOOST", boost, 16) > 0) scene_->SetPileBoost(static_cast<float>(_wtof(boost)));
#endif
    pendingSettings_ = settings;
    period_ = 1.0 / std::max(target.refreshHz, 24.0f);

    scene_->Configure(GeometryFor(target), MakeSimParams(settings));
}

SceneGeometry MonitorRenderer::GeometryFor(const MonitorTarget& t) const {
    SceneGeometry geo;
    geo.width = t.rect.right - t.rect.left;
    geo.height = t.rect.bottom - t.rect.top;
    geo.dpiScale = static_cast<float>(t.dpi) / 96.0f;
    geo.groundY = static_cast<float>(t.groundY);
    geo.originX = static_cast<float>(t.rect.left);
    geo.insetTop = static_cast<float>(t.workTop);
    geo.insetLeft = static_cast<float>(t.workLeft);
    geo.insetRight = static_cast<float>(t.workRight);
    geo.originScale = t.originScale;
    geo.rakeable = !t.autoHideGround;
    geo.leftOpen = t.leftOpen;
    geo.rightOpen = t.rightOpen;
    geo.additiveGlow = !t.advancedColor;
    return geo;
}

MonitorRenderer::~MonitorRenderer() {
    Stop();
    if (wake_) CloseHandle(wake_);
}

void MonitorRenderer::Start() {
    if (thread_.joinable()) return;
    {
        std::lock_guard lock(mutex_);
        stop_ = false;
    }
    thread_ = std::thread([this] { ThreadMain(); });
}

std::unique_ptr<Scene> MonitorRenderer::Stop() {
    if (thread_.joinable()) {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        SetEvent(wake_);
        thread_.join();
    }
    return std::move(scene_);
}

void MonitorRenderer::ApplySettings(const Settings& settings) {
    {
        std::lock_guard lock(mutex_);
        pendingSettings_ = settings;
        hasSettings_ = true;
    }
    SetEvent(wake_);
}

void MonitorRenderer::SetVisible(bool visible) {
    {
        std::lock_guard lock(mutex_);
        pendingVisible_ = visible;
    }
    SetEvent(wake_);
}

void MonitorRenderer::ClearPiles() {
    {
        std::lock_guard lock(mutex_);
        clearPiles_ = true;
    }
    SetEvent(wake_);
}

void MonitorRenderer::Reconfigure(const MonitorTarget& target) {
    {
        std::lock_guard lock(mutex_);
        pendingTarget_ = target;
        hasTarget_ = true;
    }
    SetEvent(wake_);
}

void MonitorRenderer::SetFrameCap(float fps) {
    {
        std::lock_guard lock(mutex_);
        pendingCap_ = fps;
    }
    SetEvent(wake_);
}

// ---------------------------------------------------------------------------
// Render thread
// ---------------------------------------------------------------------------

void MonitorRenderer::ThreadMain() {
    SetThreadDescription(GetCurrentThread(), L"falldesktop render");
    // The work per frame is tiny; a slightly raised priority keeps frames on
    // time when the machine is busy.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    for (;;) {
        ProcessCommands();
        {
            std::lock_guard lock(mutex_);
            if (stop_) break;
        }

        // Another monitor's thread found the shared device removed.
        if (dev_ && dev_->gpu->lost) ReleaseDevices();
        if (!dev_) {
            HRESULT hr = CreateDevices();
            if (FAILED(hr)) {
                ReleaseDevices();
                if (++failures_ == 5) PostMessageW(notify_, WM_APP_RENDER_FAILED, static_cast<WPARAM>(hr), 0);
                WaitForSingleObject(wake_, 1000);
                continue;
            }
            lastFrame_ = 0.0;
            scene_->RecheckCover();  // new devices (or a scene handed over): look at the windows afresh
        }

        if (!visible_) {
            WaitForSingleObject(wake_, INFINITE);
            lastFrame_ = 0.0;
            continue;
        }

        const bool ownVBlank = !target_.primaryCadence && dev_->output;
        if (ownVBlank) {
            // This monitor refreshes at a different rate than the DWM clock:
            // sample the animation on its own vertical blank (every
            // interval_-th one when the frame rate is capped).
            double before = QpcSeconds();
            HRESULT vb = S_OK;
            for (int i = 0; i < interval_ && SUCCEEDED(vb); ++i) vb = dev_->output->WaitForVBlank();
            if (FAILED(vb) || QpcSeconds() - before < 0.0005) {
                // Returned immediately (display off, mode change): don't spin.
                if (QpcSeconds() - lastFrame_ < period_ * 0.5) {
                    WaitForSingleObject(wake_, static_cast<DWORD>(period_ * 1000.0) + 1);
                    continue;
                }
            }
            WaitForSingleObject(dev_->waitable, 100);
        } else {
            HANDLE handles[2] = {dev_->waitable, wake_};
            DWORD r = WaitForMultipleObjects(2, handles, FALSE, 250);
            if (r == WAIT_TIMEOUT && failures_ > 0) {
                // The last frame failed without presenting, so the latency
                // waitable will never signal again: start over.
                ReleaseDevices();
                continue;
            }
            if (r != WAIT_OBJECT_0) continue;  // commands or timeout (display asleep)
        }

        HRESULT hr = RenderFrame();
        if (FAILED(hr)) {
            if (IsDeviceLost(hr)) {
                dev_->gpu->lost = true;  // every thread rebuilds on a new shared device
                ReleaseDevices();
            } else if (++failures_ > 120) {
                // Persistent non-device errors: rebuild from scratch.
                ReleaseDevices();
                failures_ = 0;
            }
        } else {
            failures_ = 0;
        }
    }

    ReleaseDevices();
    if (SUCCEEDED(hrCo)) CoUninitialize();
}

void MonitorRenderer::ProcessCommands() {
    bool applySettings = false, clear = false, retarget = false;
    Settings settings;
    MonitorTarget target;
    bool visible;
    float cap;
    {
        std::lock_guard lock(mutex_);
        if (hasTarget_) {
            target = pendingTarget_;
            hasTarget_ = false;
            retarget = true;
        }
        if (hasSettings_) {
            settings = pendingSettings_;
            hasSettings_ = false;
            applySettings = true;
        }
        visible = pendingVisible_;
        clear = clearPiles_;
        clearPiles_ = false;
        cap = pendingCap_;
    }
    if (retarget) {
        target_ = target;
        period_ = 1.0 / std::max(target_.refreshHz, 24.0f);
        lastFrame_ = 0.0;
        scene_->Configure(GeometryFor(target_), MakeSimParams(applySettings ? settings : settings_));
    }
    interval_ = cap > 0.0f ? Clamp(static_cast<int>(std::lround(target_.refreshHz / cap)), 1, 4) : 1;
    if (applySettings) {
        // In front of windows or behind them: what covers the props changes at once.
        if (settings.behindWindows != settings_.behindWindows) scene_->RecheckCover();
        settings_ = settings;
        scene_->SetParams(MakeSimParams(settings_));
        if (dev_) AcquireAtlas();  // picks up (or builds) the atlas for the new outline setting
    }
    if (visible != visible_) {
        visible_ = visible;
        lastFrame_ = 0.0;
        // Windows may have come and gone while nothing was drawn: look again before drawing.
        if (visible_) scene_->RecheckCover();
    }
    if (clear) scene_->ClearPiles(true);
}

// A device-lost failure while building this monitor's objects means the shared
// device is dead for every monitor: flag it so the next Acquire makes a new one.
static HRESULT Fail(MonitorRenderer::Devices& dev, HRESULT hr) {
    if (IsDeviceLost(hr) && dev.gpu) dev.gpu->lost = true;
    return hr;
}

HRESULT MonitorRenderer::CreateDevices() {
    // Build in place: if a step fails, ThreadMain's ReleaseDevices tears the
    // partial objects down under the shared device lock.
    dev_ = std::make_unique<Devices>();
    Devices& d = *dev_;
    HRESULT hr = gpuHub_.Acquire(d.gpu);
    if (FAILED(hr)) return hr;
    GpuDevice& gpu = *d.gpu;

    if (FAILED(hr = gpu.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d.dc))) return Fail(d, hr);
    d.dc->SetDpi(96.0f, 96.0f);  // draw in physical pixels
    d.dc->SetUnitMode(D2D1_UNIT_MODE_PIXELS);

    if (FAILED(hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d.wic))))
        return Fail(d, hr);

    const UINT width = static_cast<UINT>(target_.rect.right - target_.rect.left);
    const UINT height = static_cast<UINT>(target_.rect.bottom - target_.rect.top);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGISurface> surface;
    {
        D2DLock lock(gpu.lock.Get());
        ComPtr<IDXGISwapChain1> sc1;
        if (FAILED(hr = gpu.factory->CreateSwapChainForComposition(gpu.d3d.Get(), &desc, nullptr, &sc1))) return Fail(d, hr);
        if (FAILED(hr = sc1.As(&d.swapChain))) return Fail(d, hr);
        d.swapChain->SetMaximumFrameLatency(1);
        d.waitable = d.swapChain->GetFrameLatencyWaitableObject();
        if (FAILED(hr = d.swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)))) return Fail(d, hr);
    }
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    if (FAILED(hr = d.dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &d.backBuffer))) return Fail(d, hr);

    // Each thread composes its own window with its own DirectComposition device.
    if (FAILED(hr = DCompositionCreateDevice(gpu.dxgi.Get(), IID_PPV_ARGS(&d.dcomp)))) return Fail(d, hr);
    if (FAILED(hr = d.dcomp->CreateTargetForHwnd(target_.hwnd, TRUE, &d.target))) return Fail(d, hr);
    if (FAILED(hr = d.dcomp->CreateVisual(&d.visual))) return Fail(d, hr);
    if (FAILED(hr = d.visual->SetContent(d.swapChain.Get()))) return Fail(d, hr);
    if (FAILED(hr = d.target->SetRoot(d.visual.Get()))) return Fail(d, hr);
    if (FAILED(hr = d.dcomp->Commit())) return Fail(d, hr);

    d.output = FindOutput(target_.monitor);

    if (FAILED(hr = AcquireAtlas())) return Fail(d, hr);
    return S_OK;
}

HRESULT MonitorRenderer::AcquireAtlas() {
    GpuDevice& gpu = *dev_->gpu;
    std::lock_guard lock(gpu.atlasMutex);
    if (!gpu.atlas || gpu.atlasShadow != settings_.shadow) {
        auto atlas = std::make_shared<LeafAtlas>();
        LeafArtOptions opt;
        opt.shadow = settings_.shadow;
        HRESULT hr = atlas->Build(dev_->dc.Get(), dev_->wic.Get(), opt);
        if (FAILED(hr)) return hr;
        gpu.atlas = std::move(atlas);
        gpu.atlasShadow = settings_.shadow;
    }
    atlas_ = gpu.atlas;
    return S_OK;
}

void MonitorRenderer::ReleaseDevices() {
    if (scene_) scene_->ReleaseDeviceResources();
    atlas_.reset();
    if (dev_) {
        if (dev_->dc) dev_->dc->SetTarget(nullptr);
        // Close the frame-latency handle before its swap chain goes away.
        if (dev_->waitable) {
            CloseHandle(dev_->waitable);
            dev_->waitable = nullptr;
        }
        // Swap chain teardown touches the shared immediate context.
        D2DLock lock(dev_->gpu ? dev_->gpu->lock.Get() : nullptr);
        dev_->backBuffer.Reset();
        dev_->visual.Reset();
        dev_->target.Reset();
        dev_->dcomp.Reset();
        dev_->swapChain.Reset();
    }
    dev_.reset();
}

float MonitorRenderer::FrameDelta(double now) {
    if (lastFrame_ <= 0.0) {
        lastFrame_ = now;
        return static_cast<float>(period_);
    }
    double raw = now - lastFrame_;
    lastFrame_ = now;
    if (raw > 0.25 || raw <= 0.0) return static_cast<float>(period_);  // resumed after a pause or stall
    // Frames are displayed on vblanks, so the true frame time is a whole
    // number of refresh periods. Snapping removes the wake-up jitter that
    // would otherwise show as micro-stutter, and refines the period estimate.
    double frames = std::round(raw / period_);
    if (frames >= 1.0 && std::abs(raw - frames * period_) < 0.2 * period_) {
        period_ += (raw / frames - period_) * 0.02;
        return static_cast<float>(frames * period_);
    }
    return static_cast<float>(raw);
}

CursorState MonitorRenderer::ReadCursor(double now) {
    CursorState c;
    CURSORINFO ci{sizeof(ci)};
    if (!GetCursorInfo(&ci)) {
        lastCursorTime_ = 0.0;
        return c;
    }
    const POINT p = ci.ptScreenPos;
    double dt = now - lastCursorTime_;
    if (lastCursorTime_ > 0.0 && dt > 0.0 && dt < 0.25) {
        float vx = static_cast<float>((p.x - lastCursor_.x) / dt);
        float vy = static_cast<float>((p.y - lastCursor_.y) / dt);
        float k = ApproachFactor(static_cast<float>(dt), 0.03f);
        cursorVx_ += (vx - cursorVx_) * k;
        cursorVy_ += (vy - cursorVy_) * k;
    } else {
        cursorVx_ = cursorVy_ = 0.0f;
    }
    lastCursor_ = p;
    lastCursorTime_ = now;
    // A pointer hidden while typing, by a video player, or after touch/pen
    // input leaves no invisible "umbrella" in the falling leaves.
    if (!(ci.flags & CURSOR_SHOWING) || (ci.flags & CURSOR_SUPPRESSED)) return c;

    const RECT& r = target_.rect;
    const LONG margin = 120;
    if (p.x < r.left - margin || p.x >= r.right + margin || p.y < r.top - margin || p.y >= r.bottom + margin)
        return c;
    // Behind windows, only leaves in sight can be played with: over the desktop,
    // or the taskbar the piles rest on.
    if (settings_.behindWindows) {
        HWND under = WindowFromPoint(p);
        if (under && !IsShellWindow(GetAncestor(under, GA_ROOT))) return c;
    }
    c.valid = true;
    c.x = static_cast<float>(p.x - r.left);
    c.y = static_cast<float>(p.y - r.top);
    c.vx = cursorVx_;
    c.vy = cursorVy_;
    return c;
}

void MonitorRenderer::RecordFrame(double start, double end) {
    Stats& s = stats_;
    if (s.windowStart == 0.0) {
        s.windowStart = end;
        s.lastPresent = end;
        return;
    }
    double interval = end - s.lastPresent;
    s.lastPresent = end;
    ++s.frames;
    s.sumInterval += interval;
    s.maxInterval = std::max(s.maxInterval, interval);
    s.sumWork += end - start;
    s.maxWork = std::max(s.maxWork, end - start);
    if (interval > period_ * 1.5) ++s.late;
    if (end - s.windowStart < 5.0) return;

    wchar_t path[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, path);
    if (n > 0 && n < MAX_PATH - 16) {
        wcscat_s(path, L"falldesktop.log");
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"a, ccs=UTF-8") == 0 && f) {
            fwprintf(f,
                     L"%s %s: %d frames, %.1f fps, interval avg %.2f ms max %.2f ms, late %d, cpu avg %.2f ms max "
                     L"%.2f ms, refresh %.3f Hz, leaves %zu\n",
                     target_.key.c_str(), target_.primaryCadence ? L"[dwm clock]" : L"[own vblank]", s.frames,
                     s.frames / (end - s.windowStart), 1000.0 * s.sumInterval / s.frames, 1000.0 * s.maxInterval,
                     s.late, 1000.0 * s.sumWork / s.frames, 1000.0 * s.maxWork, 1.0 / period_, scene_->LeafCount());
            fclose(f);
        }
    }
    s = Stats{true, end, end};
}

HRESULT MonitorRenderer::RenderFrame() {
    double now = QpcSeconds();
    float dt = FrameDelta(now);
    scene_->Update(dt, now - clockOrigin_, ReadCursor(now));
    // Window changes ask for a look soon (at most ten times a second while they
    // keep coming); otherwise a routine check now and then.
    const bool soon = coverSoon_.load(std::memory_order_relaxed) && now - lastCoverProbe_ >= 0.1;
    if (now >= nextCoverProbe_ || soon || scene_->CoverStale()) {
        // Pumpkins, scarecrows and cobwebs drawn over windows step aside where
        // one covers them (behind windows, the windows cover them anyway), and
        // so do those on the bare screen edge when an auto-hide taskbar pops up
        // over them.
        coverSoon_.store(false, std::memory_order_relaxed);
        lastCoverProbe_ = now;
        nextCoverProbe_ = now + 0.3;
        const RECT& r = target_.rect;
        const bool behind = settings_.behindWindows;
        const bool taskbarCovers = target_.autoHideGround;
        scene_->ProbeCover([&](float x, float y) {
            if (behind) return false;
            const POINT p = {r.left + static_cast<LONG>(std::lround(x)), r.top + static_cast<LONG>(std::lround(y))};
            const HWND under = WindowFromPoint(p);
            if (!under) return false;
            const HWND root = GetAncestor(under, GA_ROOT);
            return taskbarCovers ? !IsDesktop(root) : !IsShellWindow(root);
        });
    }

    ID2D1DeviceContext* dc = dev_->dc.Get();
    dc->SetTarget(dev_->backBuffer.Get());
    dc->BeginDraw();
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    if (atlas_) scene_->Draw(dc, *atlas_);
    HRESULT hr = dc->EndDraw();
    if (FAILED(hr)) return hr;

    const UINT sync = (!target_.primaryCadence && dev_->output) ? 0 : static_cast<UINT>(interval_);
    {
        D2DLock lock(dev_->gpu->lock.Get());
        hr = dev_->swapChain->Present(sync, 0);
    }
    if (hr == DXGI_STATUS_OCCLUDED) hr = S_OK;
    if (stats_.enabled) RecordFrame(now, QpcSeconds());
    return hr;
}
