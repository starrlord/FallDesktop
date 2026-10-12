#include "Scene.h"

#include "Noise.h"
#include "Palette.h"

#include <atomic>

namespace {

constexpr float kRefArea = 1920.0f * 1080.0f;  // DIP^2 that `density` refers to
constexpr float kBackgroundDepth = 0.2f;        // leaves farther than this pass behind the taskbar
constexpr float kPropFront = 0.78f;             // leaves nearer than this pass in front of the pumpkins and scarecrows
constexpr size_t kMaxParticles = 900;
constexpr size_t kMaxResting = 800;

// Flight.
constexpr float kSink = 38.0f;      // DIP/s a typical leaf sinks at middle depth (speed 50)
constexpr float kGustCalm = 28.0f;  // DIP/s of gust that lifts nothing...
constexpr float kGustLift = 0.8f;   // ...and the lift per DIP/s beyond it, for a typical leaf
constexpr float kTurnLag = 1.25f;   // rad of twice the swing phase: a leaf sinks fastest just after it turns
constexpr float kTiltLead = 0.3f;   // rad: it rocks furthest just before it turns
constexpr float kHoldMax = 90.0f;   // s a web keeps a leaf before the breeze works it loose anyway

// Lying down.
constexpr float kRestMin = 4.5f, kRestMax = 11.0f;  // s a leaf lies on the pile before it is litter
constexpr float kRestFade = 1.8f;   // s of that spent fading into the litter
constexpr float kSettle = 0.45f;    // s to lie down

// Litter.
constexpr float kDebrisSink = 46.0f;  // DIP/s tossed litter flutters down at
constexpr float kDebrisDrag = 0.45f;  // s: how soon the air takes the throw out of it
constexpr float kShedFront = 0.5f;    // of the litter off a prop, the share that comes down in front of it
                                      // (the rest goes down behind it, out of sight)
constexpr float kRakeDwell = 0.035f;  // s a sweep must last before it rakes
constexpr float kRakeShare = 22.0f;   // DIP^2 of raked pile per bit of litter thrown up
constexpr float kClearTime = 2.0f;    // s the piles take to blow away (ClearPiles)
constexpr float kClearRate = 170.0f;  // leaves thrown up per second meanwhile, per 1000 DIP of pile

// How a kind flies.
enum class Style : uint8_t {
    Flutter,  // the falling-leaf pendulum: glides one way, slows, tips and swings back
    Whirl,    // a maple key: spins like a little helicopter
    Drop,     // an acorn: drops nearly straight, tumbling slowly
    Crumb,    // a fleck: tumbles in the turbulence like dust
    Lens,     // out of focus, close to the lens: drifts across in front of everything and fades
};

struct Flight {
    Style style;
    float sink;    // sink speed, relative to a typical leaf
    float swing;   // pendulum amplitude, relative
    float period;  // pendulum period, relative
    float tilt;    // rad it tilts into its swings
    float spin;    // rad/s it may turn in its plane
    float flip;    // turns over this often (1/s) while fluttering
    float tumble;  // spells of tumbling over and over, per minute
    float light;   // how much it answers the wind, gusts and the pointer (1 = a typical leaf)
};

// By LeafKind.
constexpr Flight kFlight[kLeafKindCount] = {
    {Style::Crumb, 0.62f, 0.3f, 0.6f, 0.0f, 5.0f, 0.0f, 0.0f, 1.6f},          // Fleck
    {Style::Lens, 0.3f, 1.5f, 2.4f, 0.22f, 0.12f, 0.0f, 0.0f, 0.8f},         // Blur
    {Style::Whirl, 0.7f, 0.15f, 0.6f, 0.1f, 0.3f, 0.0f, 0.0f, 0.9f},          // Samara
    {Style::Drop, 3.8f, 0.0f, 1.0f, 0.0f, 1.2f, 0.0f, 0.0f, 0.15f},           // Acorn
    {Style::Flutter, 0.92f, 0.8f, 0.8f, 0.5f, 0.2f, 0.08f, 0.6f, 1.25f},      // Aspen
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.55f, 0.15f, 0.06f, 0.5f, 1.0f},      // Maple
    {Style::Flutter, 1.1f, 1.1f, 1.1f, 0.5f, 0.15f, 0.05f, 0.6f, 0.85f},      // Oak
    {Style::Flutter, 0.9f, 0.75f, 0.8f, 0.55f, 0.25f, 0.1f, 0.6f, 1.2f},      // Birch
    {Style::Flutter, 0.95f, 1.25f, 1.05f, 0.65f, 0.1f, 0.06f, 0.4f, 1.1f},    // Ginkgo
    {Style::Flutter, 1.0f, 1.0f, 0.95f, 0.5f, 0.4f, 0.07f, 0.5f, 1.0f},       // Sweetgum
    {Style::Flutter, 1.05f, 0.45f, 0.6f, 0.4f, 1.4f, 0.25f, 1.6f, 1.1f},      // Willow
    {Style::Flutter, 0.9f, 0.9f, 0.85f, 0.6f, 0.2f, 0.08f, 0.5f, 1.25f},      // JapaneseMaple
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.06f, 0.4f, 1.0f},        // Pixel1..8
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.06f, 0.4f, 1.0f},
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.06f, 0.4f, 1.0f},
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.06f, 0.4f, 1.0f},
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.06f, 0.4f, 1.0f},
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.06f, 0.4f, 1.0f},
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.06f, 0.4f, 1.0f},
    {Style::Flutter, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.06f, 0.4f, 1.0f},
};

const Flight& FlightOf(LeafKind k) { return kFlight[Clamp(static_cast<int>(k), 0, kLeafKindCount - 1)]; }

// Nearer leaves are bigger and cross the screen faster (a typical leaf, about
// halfway, at its own size).
float Parallax(float z) { return 0.6f + 0.9f * z; }
constexpr float kLens = 1.6f;  // ...and an out-of-focus leaf is nearer still

// Noise inputs repeat every 256 units; wrapping in double keeps float precision
// for an app that may run for weeks.
float Wrap256(double v) { return static_cast<float>(std::fmod(v, 256.0)); }

// a - b, wrapped into [-pi, pi).
float AngleDelta(float a, float b) {
    float d = std::fmod(a - b + kPi, kTau);
    if (d < 0.0f) d += kTau;
    return d - kPi;
}

float WrapTau(float a) {
    a = std::fmod(a, kTau);
    return a < 0.0f ? a + kTau : a;
}

// The nearest whole half turn: a leaf square to the eye, face or back.
float Square(float face) { return kPi * std::round(face / kPi); }

// Sprite batch colours are straight alpha like every other Direct2D colour
// (measured: (1,1,1,0.5) over black gives 50% white); D2D premultiplies them.
D2D1_COLOR_F Tint(const LeafRgb& c, float a) { return {c.r, c.g, c.b, a}; }

// A leaf's paler, duller underside.
LeafRgb Underside(const LeafRgb& c) {
    const float l = 0.3f * c.r + 0.55f * c.g + 0.15f * c.b;
    return {Saturate(Lerp(c.r, l + 0.18f, 0.38f)), Saturate(Lerp(c.g, l + 0.14f, 0.38f)),
            Saturate(Lerp(c.b, l + 0.06f, 0.38f))};
}

// The sprite squashed in its own frame, by sx along the direction `axis` and
// by sy across it (a leaf turning about that line, seen face on: sx below zero
// shows its back), then turned `angle` in its plane and placed at (x, y).
D2D1_MATRIX_3X2_F SpriteTransform(float angle, float axis, float sx, float sy, float x, float y) {
    const float c = std::cos(angle), s = std::sin(angle);
    float a11 = sx, a12 = 0.0f, a22 = sy;
    if (axis != 0.0f) {
        const float cb = std::cos(axis), sb = std::sin(axis);
        a11 = sx * cb * cb + sy * sb * sb;
        a12 = (sx - sy) * cb * sb;
        a22 = sx * sb * sb + sy * cb * cb;
    }
    // Rotation after the squash; Direct2D's row-vector matrix is the transpose.
    return {c * a11 - s * a12, s * a11 + c * a12, c * a12 - s * a22, s * a12 + c * a22, x, y};
}

}  // namespace

// ---------------------------------------------------------------------------

struct Scene::Leaf {
    // Position in px; velocity with the air in px/s (its own swing comes on top).
    float x = 0, y = 0, vx = 0, vy = 0;
    float moveX = 0;  // px/s it moved sideways last, swing and all
    float z = 0.5f;   // depth: 0 far .. 1 near
    // Random genes, fixed for the leaf's life; Derive() turns them into sizes
    // and speeds so settings changes apply at once.
    float uSize = 0.5f, uFall = 0.5f, uSway = 0.5f, uRhythm = 0.5f, uTau = 0.5f;
    float hue = 0.0f;  // picks its colours (Palette.h)
    uint32_t id = 0;   // tells it apart where it might settle on a prop or stick in a web
    LeafKind kind = LeafKind::Maple;
    int variant = 0, sprite = 0, blush = -1;
    LeafRgb base{1, 1, 1}, heart{1, 1, 1};  // its colour and its blush's (white: its own colours)
    // Derived.
    float diameter = 8.0f, fall = 40.0f, swingAmp = 0.0f, swingRate = 1.0f, tau = 0.5f, alpha = 1.0f;
    // Flight.
    float swing = 0.0f;      // pendulum phase
    float swingGain = 1.0f;  // share of its usual swing: upset by gusts and the pointer, then it finds its rhythm
    float sinkK = 1.0f;      // how much faster than its mean it sinks through the turns
    float tilt = 0.0f;       // rad it tilts into its swings
    float rhythm = 0.0f;     // runs through noise: its swings wander in period and size
    float heading = 0.0f;    // in-plane angle it swings about
    float spin = 0.0f;       // rad/s: a steady turn in its plane
    float twirl = 0.0f;      // rad/s: a turn the pointer gave it (dies away)
    float face = 0.0f;       // turn about its own axis: 0 face on, pi its back
    float faceTo = 0.0f, faceRate = 0.0f;  // a flip: where it ends and how fast (rad/s); tumbling, whirling: the rate
    float wobble = 0.0f;     // the small turn about its axis that goes with the swing
    float quake = 0.0f, quakeRate = 0.0f, quakeAmp = 0.0f;  // aspen shimmer
    float glide = 1.0f;      // tumbling: which way it slides off
    float spell = 0.0f;      // s of tumbling left
    bool tumbling = false, flipping = false, flipDue = false;
    // How it is drawn: squashed in its own frame by sx along `axis` and by sy
    // across it, then turned `angle` in its plane.
    float angle = 0.0f, axis = 0.0f, sx = 1.0f, sy = 1.0f;
    float born = 1.0f;  // fade-in 0..1
    float life = 0.0f;  // out of focus: s left to drift; lying: s until it is litter
    float fade = 1.0f;  // lying: s of its life spent fading into the litter
    bool background = false;
    bool retire = false;  // remove instead of respawning
    bool spent = false;   // already part of the pile's litter (raked or blown up off it)
    bool dead = false;
    // Caught in a cobweb.
    bool held = false;
    WebHold hold;
    float heldFor = 0.0f, holdVx = 0.0f, holdOx = 0.0f, holdOy = 0.0f;
    // Lying on the pile or a prop (in resting_).
    Perch perch;
    bool debris = false;  // tossed litter: it stays where it lands
    float settle = 1.0f;  // 0 just landed .. 1 lying still
    float y0 = 0.0f, a0 = 0.0f, axis0 = 0.0f, sx0 = 1.0f, sy0 = 1.0f;  // pose as it came down
    float aRest = 0.0f, sxRest = 1.0f, syRest = 1.0f;                  // pose lying down
    float lie = 0.0f;     // px its centre lies above the surface
    float slide = 0.0f;   // px/s it slides on as it settles
    float hop = 0.0f, hopV = 0.0f;  // an acorn's bounce: px above its rest, px/s (negative: up)
};

struct Scene::Particle {
    float x = 0, y = 0, vx = 0, vy = 0;
    float d = 4.0f;    // px across as drawn
    float dep = 4.0f;  // px: the size of leaf its litter is worth to the pile (Deposit)
    float life = 6.0f;
    float fadeIn = 1.0f;   // 0..1: stirred out of the litter
    float noCatch = 0.0f;  // s before it can settle on a prop (knocked off one just now)
    uint32_t id = 0;       // (as a leaf's)
    bool deposit = true;   // adds to the pile or a prop where it comes down...
    float amount = 1.0f;   // ...this many leaves' worth of its size
    LeafKind kind = LeafKind::Fleck;
    int sprite = 0, blush = -1;
    float hue = 0.0f;
    LeafRgb base{1, 1, 1}, heart{1, 1, 1};
    float angle = 0, spin = 0, face = 0, faceRate = 0;
    float sway = 0, swayRate = 0, swayAmp = 0;  // its swing from side to side once it falls freely
};

SimParams MakeSimParams(const Settings& s) {
    SimParams p;
    // Leaves in the air on 1920x1080 DIP (more lie on the piles): A few
    // leaves ~13, Light ~30, Moderate ~48, Heavy ~104, Leaf storm ~260.
    p.density = 31.0f * std::pow(9.3f, static_cast<float>(s.amount) / 100.0f) - 27.5f;
    p.speed = std::pow(2.5f, static_cast<float>(s.speed - 50) / 50.0f);
    p.wind = static_cast<float>(s.wind) * 1.6f;
    p.gusts = static_cast<float>(s.gusts) / 100.0f;
    p.size = std::pow(2.0f, static_cast<float>(s.size - 50) / 50.0f);
    // Up to 56 DIP at 100; the range above it reaches twice that.
    const float piles = static_cast<float>(s.piles);
    p.pileMaxDip = s.piles <= 0 ? 0.0f : piles <= 100.0f ? 6.0f + 0.5f * piles : 56.0f + 0.56f * (piles - 100.0f);
    p.color = s.color;
    p.customColor = s.customColor;
    p.leafMask = s.leafMask;
    p.mouse = s.mouse;
    p.pumpkins = s.pumpkins;
    p.pumpkinAmount = s.pumpkinAmount;
    p.scarecrows = s.scarecrows;
    p.scarecrowAmount = s.scarecrowAmount;
    p.cobwebs = s.cobwebs;
    p.halloween = s.halloween;
    return p;
}

float WindAt(const SimParams& p, double time) {
    // Slow swings over ~30 s, a faster flutter, and occasional gust bursts
    // that blow with the prevailing wind.
    float slow = noise::Fbm1(Wrap256(time * 0.031), 3);
    float fast = noise::Fbm1(Wrap256(time * 0.19 + 91.7), 2);
    float b = noise::Noise1(Wrap256(time * 0.067 + 13.1));
    float burst = SmoothStep(0.15f, 0.75f, b);
    // Bursts blow with the prevailing wind; in calm air their direction
    // wanders slowly (smoothly, so a burst never flips mid-gust).
    float dir = p.wind > 2.0f    ? 1.0f
                : p.wind < -2.0f ? -1.0f
                                 : std::tanh(6.0f * noise::Noise1(Wrap256(time * 0.011 + 57.3)));
    float g = p.gusts;
    return p.wind * (1.0f + 0.4f * g * slow) + g * (34.0f * slow + 10.0f * fast) + dir * g * 110.0f * burst;
}

Scene::Scene() {
    // Full-resolution ticks mixed with an instance count, and a PCG stream of
    // its own, so scenes created together (one per monitor) never share a leaf fall.
    static std::atomic<uint64_t> instances{0};
    const uint64_t n = instances.fetch_add(1, std::memory_order_relaxed);
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    rng_.Seed(static_cast<uint64_t>(ticks.QuadPart) ^ (n * 0xD1B54A32D192ED03ull), 0x9E3779B97F4A7C15ull + 2 * n);
}
Scene::~Scene() = default;

size_t Scene::LeafCount() const { return leaves_.size() + resting_.size(); }

void Scene::Configure(const SceneGeometry& geo, const SimParams& params) {
    bool resized = geo.width != geo_.width || geo.height != geo_.height || geo.dpiScale != geo_.dpiScale ||
                   geo.groundY != geo_.groundY;
    geo_ = geo;
    params_ = params;
    PileParams pp;
    pp.maxHeight = params.pileMaxDip * geo.dpiScale;
    pp.dpiScale = geo.dpiScale;
    pp.leftOpen = geo.leftOpen;
    pp.rightOpen = geo.rightOpen;
    pp.color = params.color;
    pp.customColor = params.customColor;
    pile_.Configure(geo.width, pp);
    scenery_.Configure({geo.width, geo.height, geo.dpiScale, geo.groundY, geo.originX});
    scenery_.SetAdditiveGlow(geo.additiveGlow);
    cobwebs_.Configure(
        {geo.width, geo.height, geo.dpiScale, geo.groundY, geo.originX, geo.insetTop, geo.insetLeft, geo.insetRight});
    if (resized) {
        leaves_.clear();
        resting_.clear();
        particles_.clear();
        pileBitmap_.Reset();
    }
    rakeDwell_ = 0.0f;
    rakeValid_ = false;
    pileUploadAll_ = true;
    LayoutField();
    SetParams(params);
}

// ---------------------------------------------------------------------------
// Air flow
// ---------------------------------------------------------------------------

void Scene::LayoutField() {
    const float s = geo_.dpiScale;
    const float margin = 64.0f * s;
    fieldCell_ = 110.0f * s;
    fieldX0_ = -margin;
    fieldY0_ = -margin;
    fieldCols_ = static_cast<int>(std::ceil((static_cast<float>(geo_.width) + 2.0f * margin) / fieldCell_)) + 1;
    fieldRows_ = static_cast<int>(std::ceil((static_cast<float>(geo_.height) + margin) / fieldCell_)) + 1;
    fieldCols_ = std::max(fieldCols_, 2);
    fieldRows_ = std::max(fieldRows_, 2);
    psi_.assign(static_cast<size_t>(fieldCols_ + 2) * (fieldRows_ + 2), 0.0f);
    fieldU_.assign(static_cast<size_t>(fieldCols_) * fieldRows_, 0.0f);
    fieldV_.assign(fieldU_.size(), 0.0f);
    fieldWind_.assign(static_cast<size_t>(fieldCols_), 0.0f);
    fieldTime_ = -1.0;
    if (fieldSeed_ == 0.0f) fieldSeed_ = rng_.Range(0.0f, 200.0f);
}

void Scene::UpdateField() {
    // The field changes over seconds; 60 updates per second is plenty even
    // on 144-240 Hz displays (leaves interpolate it bilinearly in space).
    if (fieldTime_ >= 0.0 && time_ - fieldTime_ < 1.0 / 60.0 && time_ >= fieldTime_) return;
    fieldTime_ = time_;
    const float s = geo_.dpiScale;

    // Potential: two octaves of simplex noise, features ~420 and ~180 DIP,
    // evolving over ~10 s. Velocity is its curl, so it is divergence-free.
    const float f1 = 1.0f / (420.0f * s), f2 = f1 * 2.3f;
    // Simplex3 is not periodic in time, so wrap rarely: one seam every ~15 h
    // (and the leaves' drag smooths it); float precision is still ample at 4096.
    const float tz = static_cast<float>(std::fmod(time_ * 0.075, 4096.0));
    const int pc = fieldCols_ + 2, pr = fieldRows_ + 2;
    for (int j = 0; j < pr; ++j) {
        const float y = fieldY0_ + static_cast<float>(j - 1) * fieldCell_;
        for (int i = 0; i < pc; ++i) {
            const float x = fieldX0_ + static_cast<float>(i - 1) * fieldCell_;
            psi_[static_cast<size_t>(j) * pc + i] =
                noise::Simplex3(x * f1 + fieldSeed_, y * f1 - fieldSeed_, tz) +
                0.45f * noise::Simplex3(x * f2 + 17.3f, y * f2 - 9.1f, tz * 1.7f + 3.3f);
        }
    }
    double sum2 = 0.0;
    for (int j = 0; j < fieldRows_; ++j) {
        for (int i = 0; i < fieldCols_; ++i) {
            const size_t c = static_cast<size_t>(j + 1) * pc + (i + 1);
            const float u = psi_[c + pc] - psi_[c - pc];  // d(psi)/dy
            const float v = psi_[c - 1] - psi_[c + 1];    // -d(psi)/dx
            fieldU_[static_cast<size_t>(j) * fieldCols_ + i] = u;
            fieldV_[static_cast<size_t>(j) * fieldCols_ + i] = v;
            sum2 += static_cast<double>(u) * u + static_cast<double>(v) * v;
        }
    }
    // Normalize to the target strength with a slow running RMS, so there are
    // still lulls and surges from moment to moment.
    const float rms = static_cast<float>(std::sqrt(sum2 / (2.0 * fieldCols_ * fieldRows_)));
    fieldRms_ = fieldRms_ <= 0.0f ? rms : fieldRms_ + (rms - fieldRms_) * 0.003f;
    const float target = (5.0f + 16.0f * params_.gusts) * s;  // px/s per component
    fieldScale_ = fieldRms_ > 1e-6f ? target / fieldRms_ : 0.0f;

    // Wind gusts travel downwind across the whole desktop: a column feels the
    // gust that the column upwind of it felt a moment ago. Its place on that
    // timeline uses one desktop-wide scale, so monitors of mixed DPI line up.
    const float speed = std::max(std::fabs(params_.wind), 40.0f) * (params_.wind < 0.0f ? -1.0f : 1.0f);
    const float originScale = geo_.originScale > 0.0f ? geo_.originScale : s;
    for (int i = 0; i < fieldCols_; ++i) {
        const float xDip = (geo_.originX + fieldX0_ + static_cast<float>(i) * fieldCell_) / originScale;
        fieldWind_[static_cast<size_t>(i)] = WindAt(params_, time_ - static_cast<double>(xDip / speed)) * s;
    }
}

void Scene::SampleField(float x, float y, float& u, float& v, float& wind) const {
    const float fx = (x - fieldX0_) / fieldCell_, fy = (y - fieldY0_) / fieldCell_;
    const int i = Clamp(static_cast<int>(std::floor(fx)), 0, fieldCols_ - 2);
    const int j = Clamp(static_cast<int>(std::floor(fy)), 0, fieldRows_ - 2);
    const float tx = Saturate(fx - static_cast<float>(i)), ty = Saturate(fy - static_cast<float>(j));
    const size_t a = static_cast<size_t>(j) * fieldCols_ + i, b = a + fieldCols_;
    auto bilerp = [&](const std::vector<float>& g) {
        return Lerp(Lerp(g[a], g[a + 1], tx), Lerp(g[b], g[b + 1], tx), ty);
    };
    u = bilerp(fieldU_) * fieldScale_;
    v = bilerp(fieldV_) * fieldScale_;
    wind = Lerp(fieldWind_[static_cast<size_t>(i)], fieldWind_[static_cast<size_t>(i) + 1], tx);
}

// ---------------------------------------------------------------------------
// Settings and population
// ---------------------------------------------------------------------------

void Scene::SetParams(const SimParams& params) {
    params_ = params;

    kinds_.clear();
    kindWeights_.clear();
    debris_.clear();
    kindWeightSum_ = 0.0f;
    for (int k = 0; k < kLeafKindCount; ++k) {
        const LeafKind kind = static_cast<LeafKind>(k);
        if (!(params.leafMask & LeafBit(kind))) continue;
        const LeafTraits& t = GetLeafTraits(kind);
        kinds_.push_back(kind);
        kindWeights_.push_back(std::max(t.weight, 0.0f));
        kindWeightSum_ += kindWeights_.back();
        // Litter thrown up off the piles is small foliage of the kinds that fall (and crumbs).
        if (t.tinted && FlightOf(kind).style == Style::Flutter) debris_.push_back(kind);
    }
    if (kinds_.empty()) {
        kinds_.push_back(LeafKind::Maple);
        kindWeights_.push_back(0.0f);
    }

    PileParams pp = pile_.Params();
    pp.maxHeight = params.pileMaxDip * geo_.dpiScale;
    pp.color = params.color;
    pp.customColor = params.customColor;
    pile_.SetParams(pp);
    scenery_.SetPumpkins(params.pumpkins, params.pumpkinAmount);
    scenery_.SetScarecrows(params.scarecrows, params.scarecrowAmount);
    scenery_.SetHalloween(params.halloween);
    scenery_.SetLeafPalette(params.color, params.customColor);
    scenery_.SetLeafDepth(LeafDepth());
    cobwebs_.SetShown(params.cobwebs);
    cobwebs_.SetHalloween(params.halloween);

    for (Leaf& f : leaves_) {
        if (!(params.leafMask & LeafBit(f.kind))) {
            // Its kind was switched off: a new leaf fades in in its place (one
            // out of focus anywhere).
            const float x = f.x, y = f.y;
            const bool retire = f.retire;
            SpawnLeaf(f, true);
            f.retire = retire;
            if (FlightOf(f.kind).style != Style::Lens) {
                f.x = x;
                f.y = y;
            }
        }
        Derive(f);
    }
    if (params.color != colorNow_ || params.customColor != customNow_) {
        colorNow_ = params.color;
        customNow_ = params.customColor;
        Recolor();
    }
}

// How deep leaves lie on the pumpkins and hats follows the pile setting (none when the piles are off).
float Scene::LeafDepth() const {
    return params_.pileMaxDip > 0.0f ? Clamp(params_.pileMaxDip / 33.5f, 0.4f, 1.4f) : 0.0f;
}

int Scene::TargetCount() const {
    float s = geo_.dpiScale;
    float area = (static_cast<float>(geo_.width) / s) * (static_cast<float>(geo_.height) / s);
    return static_cast<int>(params_.density * area / kRefArea + 0.5f);
}

void Scene::ResizePopulation() {
    int target = TargetCount();
    int live = 0;
    // (A leaf held in a web is out of the fall for now: another takes its place.)
    for (Leaf& f : leaves_) {
        if (f.retire || f.held) continue;
        if (live >= target) f.retire = true;  // leaves when it next lands or exits
        else ++live;
    }
    // Reinstate retiring leaves before creating new ones.
    for (Leaf& f : leaves_) {
        if (live >= target) break;
        if (f.retire && !f.dead && !f.held) {
            f.retire = false;
            ++live;
        }
    }
    // A sky that should fill (at the start, or more leaves wanted) fills at
    // once; a leaf or two short, new ones set off from the top.
    const bool fill = target - live > std::max(2, target / 10);
    while (live < target) {
        leaves_.emplace_back();
        SpawnLeaf(leaves_.back(), fill);
        ++live;
    }
}

// A kind by the weights of the enabled kinds (evenly when they are all zero).
LeafKind Scene::PickKind() {
    if (kindWeightSum_ > 0.0f) {
        float x = rng_.Float() * kindWeightSum_;
        for (size_t i = 0; i < kinds_.size(); ++i) {
            if (x < kindWeights_[i]) return kinds_[i];
            x -= kindWeights_[i];
        }
        for (size_t i = kinds_.size(); i-- > 0;)
            if (kindWeights_[i] > 0.0f) return kinds_[i];
    }
    return kinds_[static_cast<size_t>(rng_.Int(0, static_cast<int>(kinds_.size()) - 1))];
}

void Scene::ChooseLook(Leaf& f) {
    f.kind = PickKind();
    f.variant = rng_.Int(0, LeafVariantCount(f.kind) - 1);
    f.sprite = LeafAtlas::SpriteIndex(f.kind, f.variant);
    f.blush = LeafAtlas::BlushIndex(f.kind, f.variant);
    const LeafTraits& t = GetLeafTraits(f.kind);
    const Flight& m = FlightOf(f.kind);
    const float side = rng_.Chance(0.5f) ? 1.0f : -1.0f;
    // Which way up it swings (stem down, mostly) and which face it shows.
    f.heading = t.rotates ? rng_.Range(-0.8f, 0.8f) + (rng_.Chance(0.2f) ? kPi : 0.0f) : 0.0f;
    f.angle = f.heading;
    f.face = rng_.Chance(0.3f) ? kPi : 0.0f;
    f.axis = 0.0f;
    f.spin = t.rotates ? m.spin * rng_.Range(0.3f, 1.0f) * side : 0.0f;
    f.twirl = 0.0f;
    f.swing = rng_.Range(0.0f, kTau);
    f.swingGain = 1.0f;
    f.sinkK = rng_.Range(0.8f, 1.25f);
    f.tilt = t.rotates ? m.tilt * rng_.Range(0.7f, 1.2f) : 0.0f;
    f.wobble = 0.0f;
    f.quake = rng_.Range(0.0f, kTau);
    f.quakeRate = kTau * rng_.Range(4.0f, 6.0f);
    f.quakeAmp = rng_.Range(0.6f, 1.05f);
    f.tumbling = f.flipping = f.flipDue = false;
    f.faceRate = 0.0f;
    switch (m.style) {
    case Style::Flutter:
        // Most turn over about their midrib, some end over end.
        if (t.rotates) f.axis = rng_.Chance(0.3f) ? 0.5f * kPi + rng_.Range(-0.4f, 0.4f) : rng_.Range(-0.5f, 0.5f);
        break;
    case Style::Whirl:
        f.faceRate = kTau * rng_.Range(2.4f, 3.4f) * side;
        break;
    case Style::Drop:
    case Style::Lens:
        f.face = rng_.Range(0.0f, kTau);
        f.faceRate = rng_.Range(0.5f, 1.4f) * side;
        break;
    case Style::Crumb:
        f.face = rng_.Range(0.0f, kTau);
        f.faceRate = kTau * rng_.Range(0.8f, 3.0f) * side;
        break;
    }
}

void Scene::Derive(Leaf& f) const {
    const float s = geo_.dpiScale;
    const LeafTraits& t = GetLeafTraits(f.kind);
    const Flight& m = FlightOf(f.kind);
    const bool blur = m.style == Style::Lens;
    // Out of focus it is close to the lens: big, but drifting at its own slow pace.
    const float parallax = blur ? 1.0f : Parallax(f.z);
    const float dip = Lerp(t.minDip, t.maxDip, std::pow(f.uSize, 1.3f)) * params_.size;
    f.diameter = std::max(1.6f, dip * (blur ? kLens : parallax) * s);
    f.fall = kSink * s * parallax * params_.speed * m.sink * Lerp(0.8f, 1.2f, f.uFall);
    // Bigger leaves swing wider and slower.
    const float big = std::sqrt(std::max(dip, 1.0f) / 20.0f);
    f.swingAmp = Lerp(18.0f, 44.0f, f.uSway) * m.swing * Clamp(big, 0.6f, 1.5f) * parallax * s;
    f.swingRate = kTau / (Lerp(2.2f, 4.0f, f.uRhythm) * m.period * Clamp(big, 0.8f, 1.25f));
    f.tau = Lerp(0.35f, 0.8f, f.uTau) / m.light;
    f.alpha = blur ? Lerp(0.35f, 0.55f, f.uFall) : Lerp(0.7f, 1.0f, f.z);  // distance veils a leaf a little
}

void Scene::ColorOf(LeafKind kind, float gene, LeafRgb& base, LeafRgb& heart) const {
    if (!GetLeafTraits(kind).tinted) {
        base = heart = {1.0f, 1.0f, 1.0f};  // keeps its own colours
        return;
    }
    LeafColors(params_.color, params_.customColor, gene, base, heart);
}

void Scene::Recolor() {
    for (Leaf& f : leaves_) ColorOf(f.kind, f.hue, f.base, f.heart);
    for (Leaf& r : resting_) ColorOf(r.kind, r.hue, r.base, r.heart);
    for (Particle& p : particles_) ColorOf(p.kind, p.hue, p.base, p.heart);
}

void Scene::SpawnLeaf(Leaf& f, bool initial) {
    f = Leaf{};
    f.uSize = rng_.Float();
    f.uFall = rng_.Float();
    f.uSway = rng_.Float();
    f.uRhythm = rng_.Float();
    f.uTau = rng_.Float();
    f.hue = rng_.Float();
    f.id = rng_.Next();
    f.rhythm = rng_.Range(0.0f, 256.0f);
    ChooseLook(f);
    const Flight& m = FlightOf(f.kind);
    const bool blur = m.style == Style::Lens;
    f.z = blur ? 1.0f : std::pow(rng_.Float(), 1.15f);  // more distant leaves than near ones
    f.background = !blur && f.z < kBackgroundDepth;
    Derive(f);
    ColorOf(f.kind, f.hue, f.base, f.heart);

    const float s = geo_.dpiScale;
    f.x = rng_.Range(0.0f, static_cast<float>(geo_.width));
    if (blur) {
        // It fades in somewhere, drifts a while and fades out again.
        f.y = rng_.Range(0.08f, 0.7f) * geo_.groundY;
        f.born = 0.0f;
        f.life = rng_.Range(7.0f, 13.0f);
    } else if (initial) {
        f.y = rng_.Range(-0.05f * static_cast<float>(geo_.height), geo_.groundY - 6.0f * s);
        f.born = 0.0f;
    } else {
        // Enters from off screen (a short sky, like the settings preview's, from just above it).
        f.y = -0.8f * f.diameter - rng_.Range(0.0f, std::min(60.0f * s, 0.12f * geo_.groundY));
        f.born = 1.0f;
    }
    f.vx = f.moveX = windNow_ * (blur ? 1.0f : Parallax(f.z)) * std::min(1.0f, 0.25f + 0.75f * m.light);
    f.vy = f.fall;
}

void Scene::Respawn(Leaf& f) {
    if (f.retire) f.dead = true;
    else SpawnLeaf(f, false);
}

// ---------------------------------------------------------------------------
// Coming to rest
// ---------------------------------------------------------------------------

// Blown into a cobweb: it hangs there, swaying with the threads.
void Scene::Hang(Leaf& f, const WebHold& hold) {
    f.held = true;
    f.hold = hold;
    f.heldFor = 0.0f;
    f.holdVx = f.moveX;
    f.holdOx = f.holdOy = 0.0f;
    float hx, hy;
    if (cobwebs_.HoldPoint(hold, hx, hy)) {
        // From where it struck it eases onto the thread.
        f.holdOx = f.x - hx;
        f.holdOy = f.y - hy;
    }
    f.heading = f.angle;
    f.face += f.wobble;  // (it turns square to the eye as it hangs)
    f.wobble = 0.0f;
    f.tumbling = f.flipping = f.flipDue = false;
}

// Comes down on the pile: it lies there a while (in resting_) before it is
// part of the litter the pile draws, and a new leaf sets off from the top.
void Scene::Land(Leaf& f) {
    const float W = static_cast<float>(geo_.width);
    // A leaf straddling a screen edge feeds the end column.
    const float x = Clamp(f.x, 0.0f, W - 1.0f);
    if (!f.spent) pile_.Deposit(x, f.diameter, pileBoost_);
    if (resting_.size() < kMaxResting) {
        Leaf& r = resting_.emplace_back(f);
        r.x = x;
        LieDown(r, SlopeAt(x, 0.5f * r.diameter));
        const Style style = FlightOf(r.kind).style;
        r.life = style == Style::Crumb  ? rng_.Range(1.2f, 2.6f)
                 : style == Style::Drop ? rng_.Range(8.0f, 15.0f)
                                        : rng_.Range(kRestMin, kRestMax);
        r.fade = std::min(kRestFade, 0.45f * r.life);
        // An acorn bounces.
        if (style == Style::Drop) r.hopV = -std::min(0.45f * std::max(f.vy, 0.0f), 110.0f * geo_.dpiScale);
    }
    Respawn(f);
}

// Comes down on a pumpkin or a hat: it lies there a while, then is part of the
// litter the scenery draws there.
void Scene::LandOnProp(Leaf& f) {
    if (resting_.size() < kMaxResting) {
        Leaf& r = resting_.emplace_back(f);
        LieDown(r, rng_.Range(-0.3f, 0.3f));
        r.life = rng_.Range(2.5f, 6.5f) * (FlightOf(r.kind).style == Style::Crumb ? 0.5f : 1.0f);
        r.fade = std::min(kRestFade, 0.45f * r.life);
    }
    Respawn(f);
}

// A leaf that has just come down lies flat: it turns broadside (its length
// along the surface) and, seen from the front, foreshortens, keeping the face
// it came down on. Acorns roll onto their side.
void Scene::LieDown(Leaf& r, float slope) {
    const LeafTraits& t = GetLeafTraits(r.kind);
    const bool nut = FlightOf(r.kind).style == Style::Drop;
    r.held = false;
    r.settle = 0.0f;
    r.y0 = r.y;
    r.a0 = r.angle;
    r.axis0 = r.axis - Square(r.axis);  // a line: within a quarter turn of none
    r.sx0 = r.sx;
    r.sy0 = r.sy;
    r.slide = 0.3f * r.moveX;
    r.hop = r.hopV = 0.0f;
    if (nut || t.rotates) {
        const float lie = slope + (nut ? 0.0f : rng_.Range(-0.3f, 0.3f));
        const float a = lie + 0.5f * kPi, b = lie - 0.5f * kPi;
        r.aRest = r.a0 + AngleDelta(std::fabs(AngleDelta(a, r.a0)) <= std::fabs(AngleDelta(b, r.a0)) ? a : b, r.a0);
    } else {
        r.aRest = r.a0;
    }
    if (nut) {
        r.sxRest = r.syRest = 1.0f;
        r.lie = 0.3f * r.diameter;
    } else {
        const float flat = rng_.Range(0.34f, 0.6f);
        const float side = r.sx < 0.0f ? -1.0f : 1.0f;
        r.sxRest = side * (t.rotates ? flat : 1.0f);
        r.syRest = t.rotates ? 1.0f : flat;
        r.lie = 0.25f * flat * r.diameter;
    }
}

// The pile's surface angle around x (rad; positive turns clockwise).
float Scene::SlopeAt(float x, float halfWidth) const {
    halfWidth = std::max(halfWidth, 1.0f);
    return -std::atan((pile_.HeightAt(x + halfWidth) - pile_.HeightAt(x - halfWidth)) / (2.0f * halfWidth));
}

// A leaf lying somewhere takes to the air again (knocked off a prop, raked up
// or lifted by a gust). It flies as before until it comes down, and then goes:
// the population is full without it.
void Scene::Unrest(Leaf& r, float vx, float vy) {
    if (r.dead) return;
    Leaf& f = leaves_.emplace_back(r);
    r.dead = true;
    f.spent = f.spent || r.perch.kind < 0;  // off the pile it is in the pile's litter already
    f.perch = Perch{};
    f.retire = true;
    f.vx = f.moveX = vx;
    f.vy = vy;
    f.heading = f.angle;
    f.face = std::acos(Clamp(f.sx, -1.0f, 1.0f));  // carries on from the face it shows
    f.wobble = f.twirl = 0.0f;
    f.swingGain = 0.0f;
    f.tumbling = f.flipping = false;
    f.flipDue = true;
    f.born = 1.0f;
}

// ---------------------------------------------------------------------------
// Litter
// ---------------------------------------------------------------------------

// A bit of litter: a crumb, or a small leaf of a kind that falls (`dPx` across).
Scene::Particle& Scene::NewDebris(float dPx) {
    const float s = geo_.dpiScale;
    Particle& p = particles_.emplace_back();
    const bool crumb = debris_.empty() || rng_.Chance(0.4f);
    p.kind = crumb ? LeafKind::Fleck : debris_[static_cast<size_t>(rng_.Int(0, static_cast<int>(debris_.size()) - 1))];
    const int variant = rng_.Int(0, LeafVariantCount(p.kind) - 1);
    p.sprite = LeafAtlas::SpriteIndex(p.kind, variant);
    p.blush = LeafAtlas::BlushIndex(p.kind, variant);
    p.hue = rng_.Float();
    ColorOf(p.kind, p.hue, p.base, p.heart);
    p.d = p.dep = crumb ? rng_.Range(2.5f, 4.5f) * std::sqrt(params_.size) * s : dPx;
    const bool rotates = GetLeafTraits(p.kind).rotates;
    p.angle = rotates ? rng_.Range(0.0f, kTau) : 0.0f;
    p.spin = rotates ? rng_.Range(-5.0f, 5.0f) : 0.0f;
    p.face = rng_.Range(0.0f, kTau);
    p.faceRate = kTau * rng_.Range(0.5f, 2.0f) * (rng_.Chance(0.5f) ? 1.0f : -1.0f);
    p.sway = rng_.Range(0.0f, kTau);
    p.swayRate = kTau * rng_.Range(0.7f, 1.4f);
    p.swayAmp = rng_.Range(3.0f, 9.0f) * s;
    p.id = rng_.Next();
    return p;
}

// Litter lying on the pile is thrown up again.
void Scene::TossDebris(Leaf& r, float vx, float vy) {
    r.dead = true;
    if (particles_.size() >= kMaxParticles) return;
    Particle& p = particles_.emplace_back();
    p.kind = r.kind;
    p.sprite = r.sprite;
    p.blush = r.blush;
    p.hue = r.hue;
    p.base = r.base;
    p.heart = r.heart;
    p.x = r.x;
    p.y = r.y;
    p.vx = vx;
    p.vy = vy;
    p.d = p.dep = r.diameter;
    p.angle = r.angle;
    p.spin = GetLeafTraits(r.kind).rotates ? rng_.Range(-5.0f, 5.0f) : 0.0f;
    p.face = std::acos(Clamp(r.sx, -1.0f, 1.0f));
    p.faceRate = kTau * rng_.Range(0.5f, 2.0f) * (rng_.Chance(0.5f) ? 1.0f : -1.0f);
    p.sway = rng_.Range(0.0f, kTau);
    p.swayRate = kTau * rng_.Range(0.7f, 1.4f);
    p.swayAmp = rng_.Range(3.0f, 9.0f) * geo_.dpiScale;
    p.deposit = false;  // it is in the pile already
    p.id = r.id;
}

// Litter that came down on the pile lies there a moment, as a leaf would.
void Scene::RestDebris(const Particle& p) {
    if (resting_.size() >= kMaxResting) return;
    Leaf& r = resting_.emplace_back();
    r.debris = true;
    r.kind = p.kind;
    r.sprite = p.sprite;
    r.blush = p.blush;
    r.hue = p.hue;
    r.base = p.base;
    r.heart = p.heart;
    r.id = p.id;
    r.x = Clamp(p.x, 0.0f, static_cast<float>(geo_.width) - 1.0f);
    r.y = p.y;
    r.moveX = p.vx;
    r.z = 1.0f;
    r.diameter = p.d;
    r.alpha = p.fadeIn;
    r.angle = p.angle;
    r.sx = std::cos(p.face);
    LieDown(r, SlopeAt(r.x, 0.5f * p.d));
    r.life = rng_.Range(1.5f, 4.0f);
    r.fade = 0.45f * r.life;
}

// Leaves knocked or blown off the props tumble down onto the pile, where they
// add the litter they took away: the share of it that came down in front.
void Scene::SpawnClumps() {
    const float s = geo_.dpiScale;
    for (const LeafClump& c : clumps_) {
        if (particles_.size() >= kMaxParticles) break;
        Particle& p = NewDebris(std::max(c.d, rng_.Range(5.0f, 8.0f) * std::sqrt(params_.size) * s));
        p.dep = c.d;
        p.x = c.x;
        p.y = c.y;
        p.vx = c.vx;
        p.vy = c.vy;
        p.noCatch = 0.15f;
        p.amount = kShedFront;
    }
    clumps_.clear();
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void Scene::Update(float dt, double time, const CursorState& cursor) {
    dt = Clamp(dt, 0.0f, 0.1f);
    time_ = time;
    UpdateField();
    windNow_ = fieldWind_.empty() ? WindAt(params_, time) * geo_.dpiScale : fieldWind_[fieldWind_.size() / 2];
    cobwebs_.Update(dt, time, windNow_);
    if (params_.mouse && cursor.valid) cobwebs_.Touch(dt, cursor.x, cursor.y, cursor.vx, cursor.vy);
    ResizePopulation();
    scenery_.Update(dt, time, windNow_, pile_, clumps_);
    UpdateLeaves(dt, cursor);
    UpdateResting(dt);
    // (A pointer resting on a bare screen edge to bring up an auto-hide taskbar
    // leaves the props alone, as it leaves the pile.)
    if (params_.mouse && cursor.valid && (geo_.rakeable || cursor.y < geo_.groundY - 2.0f * geo_.dpiScale))
        scenery_.Touch(dt, cursor.x, cursor.y, cursor.vx, cursor.vy, clumps_);
    SpawnClumps();
    UpdateRake(dt, cursor);
    UpdateStirs(dt);
    UpdateClearing(dt);
    UpdateParticles(dt);
    pile_.Update(dt, windNow_);
    Prune();
}

void Scene::UpdateLeaves(float dt, const CursorState& cursor) {
    const float s = geo_.dpiScale;
    const float W = static_cast<float>(geo_.width);
    const float margin = 48.0f * s;
    const bool pileOn = pile_.Enabled();
    const bool propsOn = scenery_.Active();
    const bool websOn = cobwebs_.Active();
    const float steady = params_.wind * s;             // px/s: the wind without its gusts
    const float pace = std::pow(params_.speed, 0.7f);  // swings quicken with the speed setting
    const float untwirl = std::exp(-dt / 1.2f);

    // The pointer: the air it moves carries leaves along (light ones most), a
    // gentle push keeps them from sitting on it, and a fast pass sets them
    // turning and throws them out of their rhythm.
    const bool useCursor = params_.mouse && cursor.valid;
    const float radius = 85.0f * s;
    const float radius2 = radius * radius;
    float cvx = cursor.vx, cvy = cursor.vy;
    const float cspeed = std::sqrt(cvx * cvx + cvy * cvy);
    const float maxCarry = 900.0f * s;
    if (cspeed > maxCarry) {
        cvx *= maxCarry / cspeed;
        cvy *= maxCarry / cspeed;
    }

    for (Leaf& f : leaves_) {
        if (f.dead) continue;
        const Flight& m = FlightOf(f.kind);
        const LeafTraits& t = GetLeafTraits(f.kind);
        const bool blur = m.style == Style::Lens;
        if (f.born < 1.0f) f.born = std::min(1.0f, f.born + dt / (blur ? 2.5f : 1.2f));
        if (f.held) {
            UpdateHeld(f, dt);
            continue;
        }

        // The air: wind and turbulence carry nearer leaves faster across the
        // screen (parallax); light leaves feel more of the turbulence and a
        // gust lifts them. Acorns hardly notice any of it.
        const float parallax = blur ? 1.0f : Parallax(f.z);
        float tu, tv, wind;
        SampleField(f.x, f.y, tu, tv, wind);
        const float gust = std::max(0.0f, std::fabs(wind - steady) - kGustCalm * s);
        const float follow = std::min(1.0f, 0.25f + 0.75f * m.light);
        const float targetVx = (wind * follow + tu * m.light) * parallax;
        const float targetVy = f.fall + (tv - kGustLift * gust) * m.light * parallax;
        const float k = ApproachFactor(dt, f.tau);
        const float dvx = targetVx - f.vx;
        f.vx += dvx * k;
        f.vy += (targetVy - f.vy) * k;
        // How hard the air tugs at it: a gust upsets its rhythm and turns it over.
        float upset = std::fabs(dvx) / (90.0f * s);

        if (useCursor && !blur) {
            const float dx = f.x - cursor.x, dy = f.y - cursor.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < radius2) {
                const float d = std::sqrt(d2) + 0.001f;
                float w = 1.0f - d / radius;
                w *= w;
                const float carry = ApproachFactor(dt, 0.11f / m.light) * w;
                f.vx += (cvx * 0.85f - f.vx) * carry;
                f.vy += (cvy * 0.85f - f.vy) * carry;
                const float push = 300.0f * s * w * dt * m.light;
                f.vx += dx / d * push;
                f.vy += dy / d * push;
                const float swirl = w * m.light * Saturate(cspeed / (500.0f * s));
                if (swirl > 0.0f) {
                    // Turned the way the air sweeps past it.
                    const float turn = 0.9f * (dx * cvy - dy * cvx) / (d * radius);
                    f.twirl += (turn - f.twirl) * Saturate(8.0f * swirl * dt);
                    f.swingGain = std::max(0.0f, f.swingGain - 3.0f * swirl * dt);
                    upset += 6.0f * swirl;
                }
            }
        }
        f.twirl *= untwirl;

        // Its own motion through the air.
        float swingVx = 0.0f, swingVy = 0.0f;
        const float rate = f.swingRate * pace;
        switch (m.style) {
        case Style::Flutter: {
            f.rhythm += 0.17f * dt;
            if (f.rhythm >= 256.0f) f.rhythm -= 256.0f;
            f.swingGain = std::min(1.0f, f.swingGain + 0.4f * dt);  // finds its rhythm again
            if (!f.tumbling && m.tumble > 0.0f && rng_.Chance(m.tumble / 60.0f * dt * (1.0f + 3.0f * upset))) {
                // Over and over for a while, sliding off to one side.
                f.tumbling = true;
                f.flipping = f.flipDue = false;
                f.spell = rng_.Range(1.2f, 4.0f);
                f.glide = std::cos(f.swing) >= 0.0f ? 1.0f : -1.0f;
                f.faceRate = f.glide * kTau * rng_.Range(0.6f, 1.3f);
            }
            if (f.tumbling) {
                f.face = WrapTau(f.face + f.faceRate * dt);
                if (t.rotates) f.heading += (0.3f * f.faceRate + f.spin + f.twirl) * dt;
                f.angle = f.heading;
                f.wobble = 0.0f;
                swingVx = f.glide * 0.6f * f.fall;
                swingVy = 0.25f * f.fall;
                if ((f.spell -= dt) <= 0.0f) {
                    // It catches the air and flutters again, gliding on the way it was going.
                    f.tumbling = false;
                    f.swing = f.glide > 0.0f ? 0.0f : kPi;
                    f.swingGain = 0.25f;
                }
                break;
            }
            // The pendulum: it glides one way, slows, tips and swings back,
            // its period and reach wandering from swing to swing.
            const float n1 = noise::Noise1(f.rhythm + 37.0f * f.uRhythm);
            const float n2 = noise::Noise1(1.37f * f.rhythm + 71.0f * f.uSway + 9.0f);
            const float w = rate * (1.0f + 0.22f * n1);
            const float gain = f.swingGain * (0.72f + 0.38f * n2);
            const float before = std::cos(f.swing);
            f.swing += w * dt;
            if (f.swing >= kTau) f.swing -= kTau;
            const float c = std::cos(f.swing), ac = std::fabs(c);
            // (|cos|^0.75: the glides keep their speed a little longer and the turns come quicker.)
            swingVx = f.swingAmp * gain * w * std::copysign(std::sqrt(ac * std::sqrt(ac)), c);
            // Like a pendulum bob it swoops down out of each turn, sinking
            // fastest just after it, and levels out (or rises a little as it
            // pitches up) towards the next.
            swingVy = -f.fall * f.sinkK * gain * std::cos(2.0f * f.swing - kTurnLag);
            // Now and then it turns over, as it stalls at the end of a swing.
            if (!f.flipping && !f.flipDue && rng_.Chance(m.flip * dt * (1.0f + 2.0f * upset))) f.flipDue = true;
            if (f.flipDue && before * c <= 0.0f) {
                const float dir = rng_.Chance(0.5f) ? 1.0f : -1.0f;
                f.flipDue = false;
                f.flipping = true;
                f.faceTo = Square(f.face) + dir * kPi;
                f.faceRate = dir * kPi / rng_.Range(0.45f, 0.8f);
            }
            if (f.flipping) {
                const float step = f.faceRate * dt;
                if (std::fabs(f.faceTo - f.face) <= std::fabs(step)) {
                    f.face = WrapTau(f.faceTo);
                    f.flipping = false;
                } else {
                    f.face += step;
                }
            } else {
                f.face += (Square(f.face) - f.face) * ApproachFactor(dt, 0.5f);  // square to the eye again
            }
            if (t.rotates) {
                // It rocks like a cradle, following its arc: the leading edge
                // dips as it swoops into a swing and lifts as it climbs to the turn.
                f.heading += (f.spin + f.twirl) * dt;
                f.angle = f.heading - f.tilt * gain * std::sin(f.swing + kTiltLead);
            }
            f.wobble = 0.38f * gain * std::sin(f.swing);
            break;
        }
        case Style::Whirl:
            // Spins steadily, the wing sweeping round an upright axis, on a slow small helix.
            f.face = WrapTau(f.face + f.faceRate * dt);
            f.swing += 1.6f * rate * dt;
            if (f.swing >= kTau) f.swing -= kTau;
            swingVx = f.swingAmp * 1.6f * rate * std::cos(f.swing);
            f.heading += (f.spin + f.twirl) * dt;
            f.angle = f.heading + f.tilt * std::sin(f.swing);
            f.axis = -f.angle;
            break;
        case Style::Drop:
        case Style::Crumb:
            f.face = WrapTau(f.face + f.faceRate * dt);
            f.heading += (f.spin + f.twirl) * dt;
            f.angle = f.heading;
            if (m.style == Style::Crumb) {
                f.swing += 2.0f * rate * dt;
                if (f.swing >= kTau) f.swing -= kTau;
                swingVx = f.swingAmp * 2.0f * rate * std::cos(f.swing);
            }
            break;
        case Style::Lens:
            // Slow, wide swings; it breathes as it turns a little about its axis.
            f.life -= dt;
            f.face = WrapTau(f.face + f.faceRate * dt);
            f.swing += rate * dt;
            if (f.swing >= kTau) f.swing -= kTau;
            swingVx = f.swingAmp * rate * std::cos(f.swing);
            swingVy = -0.5f * f.fall * std::cos(2.0f * f.swing - kTurnLag);
            f.heading += f.spin * dt;
            f.angle = f.heading - f.tilt * std::sin(f.swing + kTiltLead);
            break;
        }

        // How it shows now.
        float yaw = f.face + f.wobble;
        if (t.twinkles) {
            // Quaking: it shimmers, harder now and then.
            f.quake += f.quakeRate * dt;
            if (f.quake >= kTau) f.quake -= kTau;
            const float burst = 0.6f + 0.4f * std::sin(9.0f * f.rhythm + 6.0f * f.uSway);
            yaw += f.quakeAmp * burst * std::sin(f.quake);
        }
        f.sx = m.style == Style::Drop ? 0.86f + 0.14f * std::cos(yaw)  // a nut: no back to show
               : blur                 ? std::cos(0.55f * std::sin(f.face))
                                      : std::cos(yaw);
        f.sy = std::min(1.0f, f.sy + 3.0f * dt);  // (from lying flat)

        const float x0 = f.x, y0 = f.y;
        f.moveX = f.vx + swingVx;
        f.x += f.moveX * dt;
        f.y += (f.vy + swingVy) * dt;

        const float r = 0.5f * f.diameter;
        if (blur) {
            // Never lands: it drifts until it has faded, or out of sight.
            if (f.life <= 0.0f || f.x < -2.0f * r || f.x > W + 2.0f * r || f.y < -2.0f * r ||
                f.y > geo_.groundY + r)
                Respawn(f);
            continue;
        }
        // Blown into a cobweb, it may stick there.
        if (websOn && !f.background && (m.style == Style::Flutter || m.style == Style::Whirl)) {
            WebHold hold;
            if (cobwebs_.Catch(x0, y0, f.x, f.y, r, f.id, hold)) {
                Hang(f, hold);
                continue;
            }
        }
        if (f.x < -margin) f.x += W + 2.0f * margin;
        else if (f.x > W + margin) f.x -= W + 2.0f * margin;

        // Leaves at the props' depth settle on the pumpkins and hats they come down on.
        if (propsOn && !f.background && f.z < kPropFront && m.style != Style::Drop && f.y > y0 &&
            f.y + r >= scenery_.Top() && scenery_.Catch(f.x, y0, f.y, r, pileBoost_, f.id, &f.perch)) {
            LandOnProp(f);
            continue;
        }
        if (pileOn && !f.background && f.x > -r && f.x < W + r && f.y > y0) {
            const float surface = geo_.groundY - pile_.HeightAt(Clamp(f.x, 0.0f, W - 1.0f));
            if (f.y + 0.5f * r >= surface) {
                Land(f);
                continue;
            }
        }
        // Gone below the ground line (behind the taskbar) or blown far off the top.
        if (f.y - 2.0f * r > geo_.groundY || f.y < -static_cast<float>(geo_.height)) Respawn(f);
    }
}

void Scene::UpdateHeld(Leaf& f, float dt) {
    f.heldFor += dt;
    float hx, hy;
    if (f.heldFor > kHoldMax || !cobwebs_.HoldPoint(f.hold, hx, hy)) {
        // Shaken loose: it falls on, turning over as it goes (and goes when it
        // comes down: another took its place).
        f.held = false;
        f.retire = true;
        f.hold = WebHold{};
        f.vx = f.holdVx;
        f.vy = 0.2f * f.fall;
        f.swingGain = 0.2f;
        f.flipDue = true;
        return;
    }
    const float ease = std::exp(-dt / 0.15f);
    f.holdOx *= ease;
    f.holdOy *= ease;
    const float nx = hx + f.holdOx, ny = hy + f.holdOy;
    if (dt > 0.0f) f.holdVx += ((nx - f.x) / dt - f.holdVx) * ApproachFactor(dt, 0.1f);
    f.x = nx;
    f.y = ny;
    f.moveX = f.holdVx;
    // A slight swing of its angle, trailing as the web moves.
    f.swing += 0.6f * f.swingRate * dt;
    if (f.swing >= kTau) f.swing -= kTau;
    if (GetLeafTraits(f.kind).rotates)
        f.angle = f.heading + 0.1f * std::sin(f.swing) - Clamp(f.holdVx / (300.0f * geo_.dpiScale), -0.5f, 0.5f);
    f.face += (Square(f.face) - f.face) * ApproachFactor(dt, 0.3f);
    f.sx = std::cos(f.face + 0.25f * std::sin(0.7f * f.swing));
}

void Scene::UpdateResting(float dt) {
    const float s = geo_.dpiScale;
    const float W = static_cast<float>(geo_.width);
    const float g = 900.0f * s;
    // A strong gust lifts a leaf off the pile now and then.
    const float gust = std::max(0.0f, std::fabs(windNow_) / s - 55.0f) / 40.0f;
    for (Leaf& r : resting_) {
        if (r.dead) continue;
        if ((r.life -= dt) <= 0.0f) {
            r.dead = true;
            continue;
        }
        const bool nut = FlightOf(r.kind).style == Style::Drop;
        if (r.settle < 1.0f) r.settle = std::min(1.0f, r.settle + dt / (nut ? 2.0f * kSettle : kSettle));
        const float e = SmoothStep(0.0f, 1.0f, r.settle);
        r.angle = Lerp(r.a0, r.aRest, e);
        r.axis = r.axis0 * (1.0f - e);
        r.sx = Lerp(r.sx0, r.sxRest, e);
        r.sy = Lerp(r.sy0, r.syRest, e);
        if (r.perch.kind >= 0) {
            float px, py;
            if (!scenery_.PerchPoint(r.perch, px, py)) {
                // The litter under it is gone (knocked off, blown away): it falls again.
                Unrest(r, 0.0f, 0.0f);
                continue;
            }
            r.x = px;
            r.y = Lerp(r.y0, py - r.lie, e);
            continue;
        }
        if (r.settle < 1.0f) r.x = Clamp(r.x + r.slide * (1.0f - e) * dt, 0.0f, W - 1.0f);
        const float rest = geo_.groundY - pile_.HeightAt(r.x) - r.lie;
        if (nut) {
            // An acorn bounces once or twice before it lies still.
            r.hopV += g * dt;
            r.hop += r.hopV * dt;
            if (r.hop >= 0.0f) {
                r.hop = 0.0f;
                r.hopV = r.hopV > 50.0f * s ? -0.35f * r.hopV : 0.0f;
            }
            r.y = rest + r.hop;
        } else {
            r.y = Lerp(r.y0, rest, e);
        }
        if (gust > 0.0f && !r.debris && !nut && r.settle >= 1.0f && rng_.Chance(0.05f * gust * dt))
            Unrest(r, rng_.Range(0.3f, 0.9f) * windNow_ + rng_.Range(-25.0f, 25.0f) * s,
                   -rng_.Range(30.0f, 90.0f) * s);
    }
}

void Scene::UpdateRake(float dt, const CursorState& cursor) {
    const float s = geo_.dpiScale;
    // Only a deliberate sideways sweep through the leaves rakes them: within
    // ~27 degrees of horizontal and held for a moment, so trips down to the
    // taskbar don't cut notches. A pointer resting on a bare screen edge
    // (auto-hide taskbar) never rakes.
    const float ax = std::fabs(cursor.vx);
    float ceiling = 0.0f;  // cursor height above the ground line
    bool sweep = params_.mouse && cursor.valid && geo_.rakeable && pile_.Enabled() && ax >= 40.0f * s &&
                 std::fabs(cursor.vy) <= 0.5f * ax && cursor.x >= 0.0f && cursor.x < static_cast<float>(geo_.width);
    if (sweep) {
        ceiling = geo_.groundY - cursor.y;
        sweep = ceiling >= -2.0f * s && ceiling < pile_.HeightAt(cursor.x);
    }
    if (!sweep) {
        rakeDwell_ = 0.0f;
        rakeValid_ = false;
        return;
    }
    rakeDwell_ += dt;
    if (!rakeValid_) {
        // The furrow starts here if the sweep lasts.
        rakeX_ = cursor.x;
        rakeCeiling_ = ceiling;
        rakeValid_ = true;
        return;
    }
    if (rakeDwell_ < kRakeDwell) return;

    // Cut the whole span swept since the last cut in steps no wider than the
    // rake, so the furrow is clean at any pointer speed and frame rate.
    const float r = 8.0f * s;
    const float x0 = rakeX_, c0 = rakeCeiling_, span = cursor.x - x0;
    const int n = std::max(1, static_cast<int>(std::ceil(std::fabs(span) / r)));
    float removed = 0.0f;
    for (int k = 0; k <= n; ++k) {
        const float t = static_cast<float>(k) / static_cast<float>(n);
        const float x = x0 + span * t;
        removed += pile_.Rake(x - r, x + r, std::max(0.0f, Lerp(c0, ceiling, t)));
    }
    rakeX_ = cursor.x;
    rakeCeiling_ = ceiling;

    const float vx = Clamp(cursor.vx, -900.0f * s, 900.0f * s);
    const float dir = vx > 0.0f ? 1.0f : -1.0f;
    // The leaves lying in its way fly up with the litter.
    const float lo = std::min(x0, cursor.x) - r, hi = std::max(x0, cursor.x) + r;
    for (Leaf& l : resting_) {
        if (l.dead || l.perch.kind >= 0 || l.x < lo || l.x > hi) continue;
        const float tvx = vx * rng_.Range(0.25f, 0.6f) + dir * rng_.Range(10.0f, 60.0f) * s;
        const float tvy = -rng_.Range(80.0f, 230.0f) * s;
        if (l.debris) TossDebris(l, tvx, tvy);
        else Unrest(l, tvx, tvy);
    }
    // And the raked pile is thrown up as small leaves and crumbs, which come
    // down on it again.
    sprayBudget_ += removed;
    const float perBit = kRakeShare * s * s;
    while (sprayBudget_ > perBit && particles_.size() < kMaxParticles) {
        sprayBudget_ -= perBit;
        const float t = rng_.Float();  // spread along the swept span
        Particle& p = NewDebris(rng_.Range(4.0f, 7.5f) * std::sqrt(params_.size) * s);
        p.x = x0 + span * t + rng_.Range(-r, r);
        p.y = geo_.groundY - std::max(0.0f, Lerp(c0, ceiling, t)) - rng_.Range(0.0f, 4.0f * s);
        p.vx = vx * rng_.Range(0.25f, 0.6f) + dir * rng_.Range(10.0f, 60.0f) * s;
        p.vy = -rng_.Range(60.0f, 220.0f) * s;
    }
    if (particles_.size() >= kMaxParticles) sprayBudget_ = 0.0f;
}

// Now and then the breeze lifts a leaf off the pile and skitters it a little
// way; in a gust, many.
void Scene::UpdateStirs(float dt) {
    if (!pile_.Enabled() || pile_.Empty()) return;
    const float s = geo_.dpiScale;
    const float gust = std::max(0.0f, std::fabs(windNow_) / s - 20.0f) / 60.0f;
    const float perSecond = (0.12f + 1.2f * gust * gust) * static_cast<float>(geo_.width) / (1000.0f * s);
    stirTimer_ -= dt;
    if (stirTimer_ > 0.0f) return;
    stirTimer_ = -std::log(std::max(rng_.Float(), 1e-4f)) / std::max(perSecond, 0.05f);
    float x, h;
    if (particles_.size() >= kMaxParticles || !pile_.RandomSurfacePoint(rng_, x, h) || h < 2.0f * s) return;
    Particle& p = NewDebris(rng_.Range(4.0f, 8.0f) * std::sqrt(params_.size) * s);
    p.x = x;
    p.y = geo_.groundY - h;
    p.vx = windNow_ * rng_.Range(0.5f, 1.0f) + rng_.Range(-20.0f, 20.0f) * s;
    p.vy = -rng_.Range(30.0f, 80.0f) * s * (1.0f + 0.5f * std::min(gust, 2.0f));
    p.deposit = false;  // it is part of the pile already
    p.fadeIn = 0.0f;
}

// While the piles blow away (ClearPiles) the wind whirls their leaves off
// downwind, the deepest stretches throwing up the most.
void Scene::UpdateClearing(float dt) {
    if (clearing_ <= 0.0f) return;
    clearing_ -= dt;
    if (!pile_.Enabled() || pile_.Empty()) {
        clearing_ = clearDebt_ = 0.0f;
        return;
    }
    const float s = geo_.dpiScale;
    const float dir = windNow_ < 0.0f ? -1.0f : 1.0f;
    clearDebt_ += kClearRate * dt * static_cast<float>(geo_.width) / (1000.0f * s);
    for (; clearDebt_ >= 1.0f && particles_.size() < kMaxParticles; clearDebt_ -= 1.0f) {
        float x, h;
        if (!pile_.RandomSurfacePoint(rng_, x, h) || !rng_.Chance(h / (h + 6.0f * s))) continue;
        Particle& p = NewDebris(rng_.Range(6.0f, 11.0f) * std::sqrt(params_.size) * s);
        p.x = x;
        p.y = geo_.groundY - h * rng_.Range(0.6f, 1.0f);
        p.vx = dir * rng_.Range(110.0f, 300.0f) * s + 0.4f * windNow_;
        p.vy = -rng_.Range(50.0f, 210.0f) * s;
        p.deposit = false;  // (the pile they leave is going)
        p.fadeIn = 0.0f;
    }
    clearDebt_ = std::min(clearDebt_, 1.0f);
}

void Scene::UpdateParticles(float dt) {
    const float s = geo_.dpiScale;
    const float W = static_cast<float>(geo_.width);
    const bool propsOn = scenery_.Active();
    const bool pileOn = pile_.Enabled();
    // Litter is light: thrown up, the air soon takes the throw out of it and
    // it flutters down, swinging from side to side.
    const float drag = std::exp(-dt / kDebrisDrag);
    const float sink = kDebrisSink * s * params_.speed;
    const float air = 0.8f * windNow_;
    for (Particle& p : particles_) {
        const float y0 = p.y;
        p.vx = air + (p.vx - air) * drag;
        p.vy = sink + (p.vy - sink) * drag;
        p.sway += p.swayRate * dt;
        if (p.sway >= kTau) p.sway -= kTau;
        const float free = Saturate(1.0f - std::fabs(p.vy - sink) / (120.0f * s));
        p.x += (p.vx + free * p.swayAmp * p.swayRate * std::cos(p.sway)) * dt;
        p.y += p.vy * dt;
        p.angle += p.spin * dt;
        p.face += p.faceRate * dt;
        if (p.face >= kTau) p.face -= kTau;
        else if (p.face < 0.0f) p.face += kTau;
        p.life -= dt;
        if (p.fadeIn < 1.0f) p.fadeIn = std::min(1.0f, p.fadeIn + dt / 0.25f);
        // Litter coming down on a pumpkin or a hat stays there.
        if (p.noCatch > 0.0f) {
            p.noCatch -= dt;
        } else if (propsOn && p.deposit && p.vy > 0.0f && p.y >= scenery_.Top() &&
                   scenery_.Catch(p.x, y0, p.y, 0.5f * p.dep, p.amount, p.id, nullptr)) {
            p.life = 0.0f;
            continue;
        }
        // It comes to rest where it comes down on the pile (litter raked up
        // returns to it); past the ends, or with no pile, it goes on behind the taskbar.
        if (pileOn && p.x >= 0.0f && p.x < W) {
            if (p.vy > 0.0f && p.y >= geo_.groundY - pile_.HeightAt(p.x) - 0.2f * p.d) {
                if (p.deposit) pile_.Deposit(p.x, p.dep, p.amount);
                RestDebris(p);
                p.life = 0.0f;
            }
        } else if (p.y > geo_.groundY) {
            p.life = 0.0f;
        }
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.life <= 0.0f; }),
                     particles_.end());
}

void Scene::Prune() {
    auto dead = [](const Leaf& f) { return f.dead; };
    leaves_.erase(std::remove_if(leaves_.begin(), leaves_.end(), dead), leaves_.end());
    resting_.erase(std::remove_if(resting_.begin(), resting_.end(), dead), resting_.end());
}

void Scene::ClearPiles(bool animated) {
    if (animated) {
        clearing_ = pile_.Enabled() && !pile_.Empty() ? kClearTime : 0.0f;
        pile_.BlowAway(kClearTime);
    } else {
        clearing_ = 0.0f;
        pile_.Clear();
    }
    scenery_.ClearLeaves(animated);
    for (Leaf& r : resting_) {
        r.life = std::min(r.life, 0.5f);
        r.fade = std::min(r.fade, r.life);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void Scene::ReleaseDeviceResources() {
    pileBitmap_.Reset();
    scenery_.ReleaseDeviceResources();
    cobwebs_.ReleaseDeviceResources();
    batch_.Reset();
    dc3_.Reset();
    dcOwner_ = nullptr;
    pileUploadAll_ = true;
    for (Bucket& b : buckets_) b.page = nullptr;
    for (Bucket& b : late_) b.page = nullptr;
}

Scene::Bucket& Scene::BucketFor(ID2D1Bitmap1* page, bool late) {
    std::vector<Bucket>& list = late ? late_ : buckets_;
    for (Bucket& b : list)
        if (b.page == page) return b;
    for (Bucket& b : list)
        if (b.page == nullptr || b.dst.empty()) {
            b.page = page;
            return b;
        }
    Bucket& b = list.emplace_back();
    b.page = page;
    return b;
}

// Queues a leaf sprite and, over it, its blush. Its back (sx * sy below zero)
// is paler.
void Scene::AddLook(const LeafAtlas& atlas, int sprite, int blush, bool tinted, const LeafRgb& base,
                    const LeafRgb& heart, float x, float y, float diameter, float angle, float axis, float sx, float sy,
                    float alpha) {
    if (alpha <= 0.004f || diameter <= 0.5f) return;
    const LeafAtlas::Sprite& sp = atlas.Get(sprite, diameter);
    if (!sp.bitmap) return;
    const bool back = tinted && sx * sy < 0.0f;
    const D2D1_MATRIX_3X2_F xf = SpriteTransform(angle, axis, sx, sy, x, y);
    {
        Bucket& bucket = BucketFor(sp.bitmap, false);
        const float half = diameter * sp.extent * 0.5f;
        bucket.dst.push_back({-half, -half, half, half});
        bucket.src.push_back(sp.src);
        bucket.col.push_back(!tinted ? D2D1_COLOR_F{1, 1, 1, alpha} : Tint(back ? Underside(base) : base, alpha));
        bucket.xf.push_back(xf);
    }
    if (blush < 0) return;
    const LeafAtlas::Sprite& bl = atlas.Get(blush, diameter);
    if (!bl.bitmap) return;
    // On the leaf's own page it follows the leaf in its bucket; on another it
    // waits for the rest (it must not go under its leaf).
    Bucket& bucket = BucketFor(bl.bitmap, bl.bitmap != sp.bitmap);
    const float half = diameter * bl.extent * 0.5f;
    bucket.dst.push_back({-half, -half, half, half});
    bucket.src.push_back(bl.src);
    bucket.col.push_back(!tinted ? D2D1_COLOR_F{1, 1, 1, alpha}
                                 : Tint(back ? Underside(heart) : heart, alpha * (back ? 0.75f : 1.0f)));
    bucket.xf.push_back(xf);
}

void Scene::AddLeaf(const LeafAtlas& atlas, const Leaf& f, float alpha) {
    AddLook(atlas, f.sprite, f.blush, GetLeafTraits(f.kind).tinted, f.base, f.heart, f.x, f.y, f.diameter, f.angle,
            f.axis, f.sx, f.sy, alpha);
}

// Queues pageSprites_ (all on `page`, so they keep their order) and clears them.
void Scene::AddPageSprites(ID2D1Bitmap1* page) {
    if (page && !pageSprites_.empty()) {
        Bucket& bucket = BucketFor(page, false);
        for (const ScenerySprite& t : pageSprites_) {
            if (t.color.a <= 0.004f) continue;
            bucket.dst.push_back(t.dst);
            bucket.src.push_back(t.src);
            bucket.col.push_back(t.color);
            bucket.xf.push_back(t.xf);
        }
    }
    pageSprites_.clear();
}

bool Scene::Queued() const {
    for (const Bucket& b : buckets_)
        if (!b.dst.empty()) return true;
    for (const Bucket& b : late_)
        if (!b.dst.empty()) return true;
    return false;
}

void Scene::FlushSprites(ID2D1DeviceContext* dc) {
    if (dc3_ && batch_) {
        batch_->Clear();
        dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        UINT32 start = 0;
        for (std::vector<Bucket>* list : {&buckets_, &late_}) {
            for (Bucket& b : *list) {
                UINT32 n = static_cast<UINT32>(b.dst.size());
                if (n == 0) continue;
                if (SUCCEEDED(batch_->AddSprites(n, b.dst.data(), b.src.data(), b.col.data(), b.xf.data()))) {
                    dc3_->DrawSpriteBatch(batch_.Get(), start, n, b.page, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                          D2D1_SPRITE_OPTIONS_NONE);
                    start += n;
                }
            }
        }
        dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    } else {
        // Fallback for systems without sprite batches: one DrawBitmap per sprite
        // (no colour tint, opacity only).
        D2D1_MATRIX_3X2_F old;
        dc->GetTransform(&old);
        for (std::vector<Bucket>* list : {&buckets_, &late_}) {
            for (Bucket& b : *list) {
                for (size_t i = 0; i < b.dst.size(); ++i) {
                    dc->SetTransform(b.xf[i]);
                    const D2D1_RECT_U& u = b.src[i];
                    D2D1_RECT_F src = {static_cast<float>(u.left), static_cast<float>(u.top),
                                       static_cast<float>(u.right), static_cast<float>(u.bottom)};
                    dc->DrawBitmap(b.page, b.dst[i], b.col[i].a, D2D1_INTERPOLATION_MODE_LINEAR, &src);
                }
            }
        }
        dc->SetTransform(old);
    }
    for (std::vector<Bucket>* list : {&buckets_, &late_}) {
        for (Bucket& b : *list) {
            b.dst.clear();
            b.src.clear();
            b.col.clear();
            b.xf.clear();
        }
    }
}

// Draws what is queued, clipped to `clip`.
void Scene::FlushClipped(ID2D1DeviceContext* dc, const D2D1_RECT_F& clip) {
    if (!Queued()) return;
    dc->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    FlushSprites(dc);
    dc->PopAxisAlignedClip();
}

void Scene::DrawPile(ID2D1DeviceContext* dc) {
    int w = pile_.TexWidth(), h = pile_.TexHeight();
    if (w <= 0 || h <= 0) return;
    // Columns re-shaded this frame, as a few disjoint spans: upload only those.
    constexpr int kMaxSpans = 8;
    int x0[kMaxSpans], x1[kMaxSpans];
    int spans = pile_.Shade(x0, x1, kMaxSpans);
    if (pileBitmap_) {
        D2D1_SIZE_U size = pileBitmap_->GetPixelSize();
        if (size.width != static_cast<UINT32>(w) || size.height != static_cast<UINT32>(h)) pileBitmap_.Reset();
    }
    if (!pileBitmap_) {
        D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(dc->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(w), static_cast<UINT32>(h)), nullptr, 0, props,
                                    &pileBitmap_)))
            return;
        pileUploadAll_ = true;
    }
    if (pileUploadAll_) {
        x0[0] = 0;
        x1[0] = w;
        spans = 1;
        pileUploadAll_ = false;
    }
    for (int i = 0; i < spans; ++i) {
        if (x1[i] <= x0[i]) continue;
        D2D1_RECT_U r = {static_cast<UINT32>(x0[i]), 0, static_cast<UINT32>(x1[i]), static_cast<UINT32>(h)};
        pileBitmap_->CopyFromMemory(&r, pile_.Pixels() + x0[i], static_cast<UINT32>(w) * 4u);
    }
    if (pile_.Empty() && spans == 0) return;
    float top = geo_.groundY - static_cast<float>(pile_.GroundRow());
    dc->DrawBitmap(pileBitmap_.Get(), D2D1::RectF(0, top, static_cast<float>(w), top + static_cast<float>(h)), 1.0f,
                   D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
}

void Scene::Draw(ID2D1DeviceContext* dc, const LeafAtlas& atlas) {
    if (dc != dcOwner_) {
        ReleaseDeviceResources();
        dcOwner_ = dc;
        if (SUCCEEDED(dc->QueryInterface(IID_PPV_ARGS(&dc3_)))) {
            if (FAILED(dc3_->CreateSpriteBatch(&batch_))) dc3_.Reset();
        }
    }
    if (!atlas.Ready()) return;

    const bool pileOn = pile_.Enabled();
    const D2D1_RECT_F sky = D2D1::RectF(0.0f, 0.0f, static_cast<float>(geo_.width), geo_.groundY);
    const bool props = scenery_.Prepare(dc);
    const bool webs = cobwebs_.Prepare(dc);
    auto blurred = [](const Leaf& f) { return FlightOf(f.kind).style == Style::Lens; };
    // Falling leaves, farther ones first (in depth bands, so a big near leaf is
    // never covered by a small distant one): the distant ones, or the rest.
    auto falling = [&](bool distant, bool everything) {
        constexpr float kBands[] = {0.0f, 0.45f, 0.75f, 2.0f};
        for (int b = 0; b < 3; ++b)
            for (const Leaf& f : leaves_)
                if (!f.held && !blurred(f) && (everything || f.background == distant) && f.z >= kBands[b] &&
                    f.z < kBands[b + 1])
                    AddLeaf(atlas, f, f.alpha * f.born);
    };
    // Falling leaves nearer than the distant ones pass in front of the props
    // and the webs, and with piles land on them: they come later.
    const bool nearLater = pileOn || props || webs;

    // 1. Distant leaves (and, with nothing to pass in front of, every leaf)
    //    pass behind the ground line: clip them to the area above it.
    falling(true, !nearLater);
    FlushClipped(dc, sky);

    // 2. The cobwebs in the corners, and the leaves caught in them.
    if (webs) {
        cobwebs_.EmitWebs(pageSprites_);
        AddPageSprites(cobwebs_.Page());
        FlushClipped(dc, sky);
    }
    for (const Leaf& f : leaves_)
        if (f.held) AddLeaf(atlas, f, f.alpha * f.born);
    FlushClipped(dc, sky);

    // 3. Pumpkins and scarecrows, in front of all that; the pile buries their feet.
    if (props) {
        scenery_.EmitProps(pageSprites_);
        AddPageSprites(scenery_.Page());
        FlushClipped(dc, sky);
    }

    // 4. The pile, and the leaves heaped round the props' feet.
    DrawPile(dc);
    if (props) {
        scenery_.EmitGround(pageSprites_);
        AddPageSprites(scenery_.Page());
        FlushClipped(dc, sky);
    }

    // 5. Leaves lying on the pile and the props, the nearer falling leaves
    //    (with piles they come down on them; without, they pass in front of
    //    the props and on behind the ground line) and tossed litter.
    if (nearLater && !pileOn) {
        falling(false, false);
        FlushClipped(dc, sky);
    }
    // (Lying on the pile a leaf is at the front: whatever its distance veiled clears as it settles.)
    for (const Leaf& r : resting_)
        AddLeaf(atlas, r, Lerp(r.alpha, 1.0f, SmoothStep(0.0f, 1.0f, r.settle)) * SmoothStep(0.0f, r.fade, r.life));
    if (pileOn) falling(false, false);
    for (const Particle& p : particles_)
        AddLook(atlas, p.sprite, p.blush, GetLeafTraits(p.kind).tinted, p.base, p.heart, p.x, p.y, p.d, p.angle,
                0.0f, std::cos(p.face), 1.0f, p.fadeIn * Saturate(p.life / 0.5f));
    FlushSprites(dc);

    // 6. The candles glow over everything near them: pumpkins, leaves and all.
    if (props) {
        scenery_.EmitGlow(pageSprites_);
        AddPageSprites(scenery_.Page());
        FlushClipped(dc, sky);
    }

    // 7. Bats, now and then.
    if (webs) {
        cobwebs_.EmitBats(pageSprites_);
        AddPageSprites(cobwebs_.Page());
        FlushClipped(dc, sky);
    }

    // 8. Leaves out of focus, close to the lens, in front of everything (fading
    //    out before they would reach the taskbar).
    for (const Leaf& f : leaves_) {
        if (!blurred(f)) continue;
        const float low = SmoothStep(geo_.groundY, geo_.groundY - 0.6f * f.diameter, f.y);
        AddLeaf(atlas, f, f.alpha * SmoothStep(0.0f, 1.0f, f.born) * SmoothStep(0.0f, 3.0f, f.life) * low);
    }
    FlushSprites(dc);
}
