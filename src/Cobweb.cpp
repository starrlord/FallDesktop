// Cobwebs in the corners of the screen, the spiders that keep them and the
// bats that flit past (see Cobweb.h).
//
// A web is spun in its corner's own frame: u runs along the top edge away from
// the corner and v down the side edge, so a right-hand web is a left-hand one
// mirrored. Three frame threads run round the corner from the top edge to the
// side edge, a rough quarter circle whose two corners are held out by silk too
// fine to see; radials run from the hub out to the frame or straight to an
// edge, with more added further out where they spread; the capture spiral
// winds out across them, round near the hub and following the frame further
// out, turning back where the radials are short. Uneven spacing, the odd
// segment left out or hanging slack, the hub's own little spiral, chords
// across the corner and a stray strand or two make it look spun, not drawn.
//
// The silk is simulated with XPBD: knots are unit point masses and threads
// one-sided distance constraints (silk pulls, never pushes), spun a little
// shorter than they hang so that a web is taut and rings when plucked. Slack
// silk is drawn as a parabola that uses up the length it has spare. A snapped
// thread leaves an end hanging from the knot on either side (a little rope
// that keeps a curl) until it is spun again.
#include "Cobweb.h"

#include "Noise.h"

namespace {

// ---------------------------------------------------------------------------
// Tunables: lengths in DIPs (scaled by dpiScale where used) or shares of a
// web's reach; times in seconds.
// ---------------------------------------------------------------------------

// Layout.
constexpr float kMinReach = 160.0f;   // a web's reach along the top edge from its corner ...
constexpr float kMaxReach = 280.0f;
constexpr float kSkyShare = 0.45f;    // ... at most this share of the height above the ground line
constexpr float kWideShare = 0.36f;   // ... and of the width (the two webs never meet)
constexpr float kSmallest = 34.0f;    // rather no web than a smaller one
constexpr int kMaxRadials = 18;
constexpr int kHubTurns = 2;          // the hub's own little spiral
constexpr int kMaxTurns = 9;          // of the capture spiral
constexpr int kMaxRays = 26;          // radials, with the ones added between them further out
constexpr float kFrameBow = 0.06f;    // of its length: the radials pull the frame in towards the hub
constexpr float kSpiralIn = 0.26f;    // the capture spiral's first and last turns, as shares of a radial
constexpr float kSpiralOut = 0.9f;
constexpr float kSkipped = 0.04f;     // share of the spiral's segments left out
constexpr float kDroopy = 0.06f;      // ... and spun slack, so that they sag
constexpr int kSpanBins = 64;         // a web's extent around its hub, by angle (for catching leaves)

// Silk.
constexpr float kStep = 1.0f / 60.0f;  // longest simulation step (a long frame takes several)
constexpr int kIterations = 4;
constexpr float kDragline = 0.0008f;   // s^2: compliance of 10 DIP of radial and hub silk ...
constexpr float kFrameSilk = 0.00025f; // ... of the frame's, which is stouter ...
constexpr float kCapture = 0.0028f;    // ... and of the capture spiral, which stretches more
constexpr float kTether = 0.0015f;     // s^2: what holds the frame's corners out
constexpr float kTaut = 0.03f;         // radials are spun this much shorter than they hang ...
constexpr float kTautFrame = 0.03f;    // ... the frame too ...
constexpr float kTautSpiral = 0.004f;  // ... and the capture spiral only just taut
constexpr float kTremble = 0.8f;       // a plucked web stills
constexpr float kGravity = 45.0f;      // DIP/s^2 (silk is light: its tension holds it up)
constexpr float kBreeze = 1.2f;        // 1/s: push on a knot per DIP/s of wind
constexpr float kStillAir = 14.0f;     // DIP/s: the air is never quite still
constexpr float kLift = 0.35f;         // the breeze lifts and drops it too (share of its push)
constexpr float kFadeIn = 1.2f, kFadeOut = 0.6f;  // shown and hidden
constexpr float kCoverOut = 0.25f, kCoverIn = 0.7f;  // stepping aside for a window and back

// The pointer.
constexpr float kReach = 11.0f;        // threads this near its path feel it
constexpr float kGrip = 0.5f;          // share of its travel passed to the threads it meets
constexpr float kShove = 0.45f;        // share of the way out of its path they are pushed each frame
constexpr float kTearSpeed = 1100.0f;  // DIP/s: a swipe this fast snaps threads it runs through
constexpr int kMaxTears = 28;          // per web per frame

// Leaves.
constexpr float kCatchShare = 0.18f;   // of leaves blowing through a web, the share that stick
constexpr float kCatchReach = 26.0f;   // a leaf sticks to a knot at most this far away (else: a hole)
constexpr float kLeafWeight = 60.0f;   // DIP/s^2 on the knot a leaf hangs from
constexpr float kLooseSpeed = 70.0f;   // DIP/s: a knot shaken this fast lets its leaf go
constexpr float kGustWind = 150.0f;    // DIP/s: wind that counts as a strong gust
constexpr float kHeldFor = 0.3f;       // a hold not looked at for this long has gone

// Snapped ends and stray strands.
constexpr int kStrandPoints = 5;       // after the knot it hangs from
constexpr size_t kMaxStrands = 40;
constexpr float kStrandGravity = 70.0f;   // DIP/s^2: loose silk hardly falls, it floats
constexpr float kStrandDrag = 0.25f;   // air drag time constant
constexpr float kStrandAir = 0.7f;     // share of the wind they stream in
constexpr float kFlutter = 22.0f;      // DIP/s: the air's little eddies along a loose strand
constexpr float kRecoil = 60.0f;       // DIP/s: a snapped end whips back towards its knot

// Mending.
constexpr float kMendWait = 6.0f;      // before a snapped thread mends itself (plus up to kMendVary)
constexpr float kMendVary = 8.0f;
constexpr float kMendFade = 1.6f;
constexpr float kMendClose = 0.6f;     // time constant of a mended thread drawing its hole closed
constexpr float kSpiderMends = 45.0f;  // with a spider about, a thread is left to her this long

// Spiders.
constexpr float kSpiderFade = 0.8f;
constexpr float kWalk = 24.0f;         // DIP/s
constexpr float kScurry = 130.0f;
constexpr float kClimb = 20.0f;
constexpr float kLower = 12.0f;
constexpr float kSpin = 10.0f;         // crossing a gap, laying silk
constexpr float kShy = 80.0f;          // the pointer this near sends her up and away
constexpr float kBold = 4.0f;          // the pointer must stay away this long before she comes out
constexpr float kSpiderWeight = 160.0f;  // DIP/s^2 on the knots she stands on
constexpr float kRear = 6.5f;          // art DIP from her waist to her spinnerets
constexpr float kStride = 4.4f;        // art DIP a leg moves her each step
constexpr float kSwingWind = 0.02f;    // her dragline swings this far (rad) per DIP/s of wind

// Bats.
constexpr float kBatWait = 40.0f, kBatWaitVary = 80.0f;    // between flights, per scene
constexpr float kBatFirst = 12.0f, kBatFirstVary = 30.0f;  // after Halloween begins
constexpr float kBatShy = 150.0f;      // they swerve away from the pointer within this
constexpr float kBatFade = 0.8f;

// Art.
constexpr int kClear = 2;              // transparent px around every page cell
constexpr int kLineCols = 8;           // columns of the line cells (all alike: they stretch)
constexpr float kCore = 0.82f;         // silk's bright core (peak opacity) ...
constexpr float kUnderlay = 0.6f;      // ... and the dark underlay that keeps it visible on light wallpapers
constexpr float kGlintShare = 0.035f;  // of a dew drop's cycle, the share it catches the light

struct Rgb {
    float r, g, b;
};
constexpr Rgb Hex(uint32_t c) {
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f};
}
D2D1_COLOR_F Color(Rgb c, float a) { return {c.r, c.g, c.b, a}; }

constexpr Rgb kSilk = Hex(0xE4E9F2);    // silver, a touch cool against the warm leaves
constexpr Rgb kDew = Hex(0xF2F6FF);
constexpr Rgb kGlint = Hex(0xFFF5E0);   // the low autumn sun
constexpr Rgb kLeg = Hex(0x1C1512);
constexpr Rgb kMoonlit = Hex(0xC9C2D6);  // the faint light along a spider's edges

float Ease(float t) { return t * t * (3.0f - 2.0f * t); }
float Wrap256(double v) { return static_cast<float>(std::fmod(v, 256.0)); }
float Frac(double v) { return static_cast<float>(v - std::floor(v)); }
float Wrap(float a) {  // to (-pi, pi]
    a = std::fmod(a + kPi, kTau);
    return a < 0.0f ? a + kPi : a - kPi;
}

// Closest points of segments p0-p1 and q0-q1, as parameters along each
// (Ericson, Real-Time Collision Detection, 5.1.9).
void Closest(float p0x, float p0y, float p1x, float p1y, float q0x, float q0y, float q1x, float q1y, float& sp,
             float& tq) {
    const float d1x = p1x - p0x, d1y = p1y - p0y, d2x = q1x - q0x, d2y = q1y - q0y;
    const float rx = p0x - q0x, ry = p0y - q0y;
    const float a = d1x * d1x + d1y * d1y, e = d2x * d2x + d2y * d2y, f = d2x * rx + d2y * ry;
    constexpr float eps = 1e-6f;
    float s = 0.0f, t = 0.0f;
    if (a <= eps && e > eps) {
        t = Saturate(f / e);
    } else if (a > eps) {
        const float c = d1x * rx + d1y * ry;
        if (e <= eps) {
            s = Saturate(-c / a);
        } else {
            const float b = d1x * d2x + d1y * d2y, denom = a * e - b * b;
            s = denom > eps * a * e ? Saturate((b * f - c * e) / denom) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = Saturate(-c / a);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = Saturate((b - c) / a);
            }
        }
    }
    sp = s;
    tq = t;
}

D2D1_MATRIX_3X2_F Placed(float angle, float x, float y) {
    if (angle == 0.0f) return {1, 0, 0, 1, x, y};
    const float c = std::cos(angle), s = std::sin(angle);
    return {c, s, -s, c, x, y};
}

// ---------------------------------------------------------------------------
// The web
// ---------------------------------------------------------------------------

struct Knot {
    float x = 0.0f, y = 0.0f;    // px
    float vx = 0.0f, vy = 0.0f;  // px/s
    float ox = 0.0f, oy = 0.0f;  // where this step started
    float w = 1.0f;              // inverse mass: 0 tied to the screen edge, or parked (no thread left)
    float wa = 0.0f, wb = 0.0f;  // how the breeze's eddies weigh on it (by its place in the web)
    float load = 0.0f;           // px/s^2 dragging it down: a leaf hanging there, the spider
    float pushX = 0.0f, pushY = 0.0f, pushW = 0.0f;  // the pointer's shove this frame ...
    float dragX = 0.0f, dragY = 0.0f;                // ... and the part of it that sets the knot moving
    double heldAt = -100.0;      // when a leaf was last seen hanging here
    unsigned shaken = 0;         // stamp of its last disturbance: leaves caught before it fall
    uint16_t links = 0;          // unbroken threads tied here
    bool anchor = false;         // tied to the screen edge
};

enum class Silk : uint8_t { Frame, Radial, Spiral, Hub, Chord };
enum class State : uint8_t { Whole, Torn, Spinning, Mending };

struct Thread {
    uint16_t a = 0, b = 0;
    Silk silk = Silk::Radial;
    State state = State::Whole;
    bool fromB = false;          // spun from b towards a (Spinning)
    float rest = 0.0f;           // px: its natural length
    float restNow = 0.0f;        // ... easing back to it after mending (the hole closes)
    float compliance = 0.0f;     // s^2
    float lambda = 0.0f;         // XPBD multiplier
    float alpha = 1.0f;          // fades in when it mends by itself
    float progress = 0.0f;       // share spun so far (Spinning)
    double mendAt = 0.0;         // when it mends by itself (Torn)
};

// A snapped end or a stray strand: a little rope hanging from a knot.
struct Strand {
    int knot = 0;
    int thread = -1;             // the snapped thread it was part of (-1: a stray, always there)
    float link = 0.0f;           // px between its points
    float bend = 0.0f;           // px between every other point: its curl
    float alpha = 1.0f;
    bool fading = false;         // its thread has been spun again
    float x[kStrandPoints] = {}, y[kStrandPoints] = {};
    float vx[kStrandPoints] = {}, vy[kStrandPoints] = {};
};

struct Dew {
    int thread = 0;
    float u = 0.5f;              // along it
    float r = 1.0f;              // px
    float phase = 0.0f;          // where in its cycle it catches the light
    float period = 10.0f;        // s between glints
};

enum class Mode : uint8_t { Hub, Lower, Dangle, Climb, Walk, Spin, Hide };

struct Spider {
    bool present = false;        // this web has one (at Halloween)
    Mode mode = Mode::Hub;
    Mode then = Mode::Hub;       // what she does when she gets there (Walk)
    float alpha = 0.0f;
    float scale = 1.0f;          // of the art
    int at = 0, to = 0;          // on the web: between these knots ...
    float t = 0.0f;              // ... this far along
    std::vector<int> path;       // knots still to walk through, the next one last
    float x = 0.0f, y = 0.0f;    // her waist (px)
    float heading = 0.5f * kPi;  // where her head points (y down)
    float drop = 0.0f, dropGoal = 0.0f;  // dragline below the hub (px)
    float swing = 0.0f, swingV = 0.0f;   // its angle from the vertical
    float bob = 0.0f, bobV = 0.0f;       // its stretch (px)
    float timer = 0.0f;          // until her next idea
    float calm = 100.0f;         // since the pointer was last near her
    float gait = 0.0f;           // walk cycle (cycles)
    float pace = 0.0f;           // px/s
    bool hurry = false;
    int task = -1;               // the thread she is mending
    int home = 0, hide = 0;      // the hub and her retreat by the corner (knots)
};

struct Web {
    int corner = 0;              // 0 top left, 1 top right
    uint32_t seed = 0;
    float reach = 0.0f;          // px: its size, from the corner
    float across = 0.0f, down = 0.0f;  // px: where the frame meets the top and side edges
    float hubU = 0.0f, hubV = 0.0f;  // the hub as spun (corner frame, px)
    int hub = 0;
    std::vector<Knot> knots;
    std::vector<Thread> threads;
    std::vector<Strand> strands;
    std::vector<Dew> dew;
    std::vector<int> adjStart, adj;  // threads tied to each knot
    struct Pin {
        int knot = 0;
        float x = 0.0f, y = 0.0f, lambda = 0.0f;
    } pins[2];                   // the frame's corners, held out where they were spun
    float span[kSpanBins] = {};  // px from the hub to the frame, by angle
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;  // bounds (px)
    float vis = 1.0f;            // stepping aside for a window
    bool covered = false, probed = false;
    int held = 0, maxHeld = 3;   // leaves hanging in it
    int torn = 0;                // threads snapped and not yet being spun
    int unsettled = 0;           // threads not whole
    float sheen = 0.0f, sheenBase = 0.0f;  // where the light comes from (rad), and about where
    Spider spider;
};

struct Bat {
    float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;  // px, px/s
    float speed = 0.0f;          // px/s along x (signed)
    float lane = 0.0f;           // px: the height it keeps to
    float wave = 0.0f, waveRate = 2.0f, waveAmp = 0.0f;  // its wavy path
    float flap = 0.0f, flapRate = 7.0f;  // wingbeat (cycles, Hz)
    float glide = 0.0f;          // left gliding, wings out
    float size = 1.0f;           // of the art
    float alpha = 1.0f;
};

// A cell of the sprite page and the page px its art is anchored at.
struct Cell {
    D2D1_RECT_U src{};
    float cx = 0.0f, cy = 0.0f;
};

// A cell anchored at its anchor, scaled by k, turned and placed.
ScenerySprite Put(const Cell& c, float k, float angle, float x, float y, D2D1_COLOR_F col) {
    return {{(static_cast<float>(c.src.left) - c.cx) * k, (static_cast<float>(c.src.top) - c.cy) * k,
             (static_cast<float>(c.src.right) - c.cx) * k, (static_cast<float>(c.src.bottom) - c.cy) * k},
            c.src,
            col,
            Placed(angle, x, y)};
}

// How each kind of silk is drawn: thickness (of the silk cell) and opacity.
struct Look {
    float thick, alpha;
};
constexpr Look kLooks[] = {
    {1.0f, 0.5f},    // frame
    {0.92f, 0.44f},  // radial
    {0.78f, 0.6f},   // capture spiral: finer, a touch brighter
    {0.8f, 0.52f},   // hub
    {0.85f, 0.36f},  // chord
};
constexpr Look kStrandLook = {0.85f, 0.48f};
constexpr Look kDraglineLook = {0.8f, 0.62f};

// The spider's legs, front to back: where they join her (art DIP forward of her
// waist and out to the side), which way they reach (rad from forward) and how far.
constexpr float kHipF[4] = {2.55f, 1.95f, 1.35f, 0.75f};
constexpr float kHipR[4] = {1.3f, 1.55f, 1.55f, 1.3f};
constexpr float kLegAngle[4] = {0.36f, 1.02f, 2.0f, 2.62f};
constexpr float kLegReach[4] = {14.0f, 12.0f, 8.6f, 11.6f};

// Bat wingbeat frames: how far the wings are raised (rad) and spread.
struct Wingbeat {
    float lift, span;
};
constexpr Wingbeat kWingbeat[4] = {{0.85f, 0.9f}, {0.12f, 1.0f}, {-0.55f, 0.92f}, {0.38f, 0.56f}};
// A bat's right wing spread level (art DIP, its body at the origin, y down):
// the shoulder, the wrist, the tips of its three long fingers and the hip; the
// trailing edge scallops between the tips.
constexpr D2D1_POINT_2F kShoulder = {1.4f, -1.7f};
constexpr D2D1_POINT_2F kWrist = {8.0f, -5.0f};
constexpr D2D1_POINT_2F kTips[3] = {{17.6f, -2.4f}, {14.2f, 3.1f}, {9.2f, 4.5f}};
constexpr D2D1_POINT_2F kHip = {2.2f, 3.4f};

// A point of the wing on `side` (-1 left, 1 right) in a frame of the wingbeat.
D2D1_POINT_2F WingPoint(D2D1_POINT_2F p, Wingbeat beat, float side) {
    const float c = std::cos(-beat.lift), sn = std::sin(-beat.lift);
    const float dx = (p.x - kShoulder.x) * beat.span, dy = p.y - kShoulder.y;
    return {side * (kShoulder.x + dx * c - dy * sn), kShoulder.y + dx * sn + dy * c};
}

}  // namespace

// ---------------------------------------------------------------------------

struct Cobwebs::Impl {
    CobwebLayout layout_;          // the work area's (see Cobwebs::Configure)
    float ox_ = 0.0f, oy_ = 0.0f;  // px: the work area's top-left corner in the scene
    bool configured_ = false;
    std::vector<Web> webs_;
    bool shown_ = true, halloween_ = true;
    float fade_ = 0.0f;           // 0 hidden .. 1 shown
    bool coverStale_ = true;
    double time_ = 0.0;
    float wind_ = 0.0f;           // px/s
    float ptrX_ = 0.0f, ptrY_ = 0.0f;
    double ptrAt_ = -100.0;       // when the pointer was last seen
    unsigned stamp_ = 1;          // disturbances and holds, in order
    unsigned strung_ = 1;         // the stamp when the webs were last strung
    std::vector<Bat> bats_;
    double nextBats_ = -1.0;      // when the next bats come (-1: not planned)
    Rng rng_;
    std::vector<int> prev_, queue_;  // path finding

    // Device resources.
    ComPtr<ID2D1Bitmap1> page_;
    float pageScale_ = 0.0f;      // the dpiScale it was drawn for
    double pageRetry_ = 0.0;
    Cell silk_, rod_, drop_, glint_, body_, bat_[4];
    float silkHalf_ = 1.0f, rodHalf_ = 1.0f;  // px: half the height of the line cells
    float dropPx_ = 1.0f, glintPx_ = 1.0f;    // px across, as drawn on the page

    Impl();
    float X(const Web& w, float u) const { return w.corner ? static_cast<float>(layout_.width) - u : u; }
    // From the work area's frame to the scene's, for the sprites from `first` on.
    void Shift(std::vector<ScenerySprite>& out, size_t first) const {
        for (size_t i = first; i < out.size(); ++i) {
            out[i].xf._31 += ox_;
            out[i].xf._32 += oy_;
        }
    }
    float Alpha(const Web& w) const { return Ease(Saturate(fade_)) * Ease(Saturate(w.vis)); }
    bool Walkable(const Thread& t) const { return t.state == State::Whole || t.state == State::Mending; }

    // Strings.
    void Plan(int corner, Rng& rng, uint32_t& seed, float& reach) const;
    void Build(Web& w, int corner);
    void Settle(Web& w);
    void AddStrand(Web& w, int knot, int thread, float ux, float uy, float length, float curl, float vx, float vy);

    // Simulation.
    void UpdateWeb(Web& w, float dt);
    void Step(Web& w, float h, float ax, float lift, float n1, float n2);
    void StepStrands(Web& w, float h, float air);
    void UpdateThreads(Web& w, float dt);
    void Snap(Web& w, int thread, float at, float vx, float vy);
    void Mend(Web& w, int thread, bool spun);
    void Link(Web& w, int knot);
    void Unlink(Web& w, int knot);
    void Shake(Web& w, float x, float y, float radius);
    void Touch(float dt, float x, float y, float vx, float vy);

    // The spider.
    void UpdateSpider(Web& w, float dt);
    void Rest(Web& w);
    void Flee(Web& w);
    void GoTo(Web& w, int target, Mode then, bool hurry);
    bool FindPath(const Web& w, int from, int to, std::vector<int>& path);
    void Walk(Web& w, float dist);
    void Arrive(Web& w);
    bool Repair(Web& w);
    void StartSpin(Web& w);
    void SpinOn(Web& w, float dt);
    void Abandon(Web& w);
    void Place(Web& w, float dt);

    // Bats.
    void UpdateBats(float dt);
    void Launch();

    // Drawing.
    HRESULT BuildPage(ID2D1DeviceContext* dc);
    void Line(std::vector<ScenerySprite>& out, const Cell& cell, float half, float ax, float ay, float bx, float by,
              D2D1_COLOR_F col) const;
    void DrawSilk(std::vector<ScenerySprite>& out, float ax, float ay, float bx, float by, float rest, Look look,
                  float alpha, float lc, float ls, float gx, float gy) const;
    void EmitSpider(const Web& w, float alpha, std::vector<ScenerySprite>& out) const;
};

Cobwebs::Impl::Impl() {
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    rng_.Seed(static_cast<uint64_t>(ticks.QuadPart) ^ reinterpret_cast<uintptr_t>(this), 0xC0B3EB5ull);
    bats_.reserve(8);
}

// ---------------------------------------------------------------------------
// Stringing the webs
// ---------------------------------------------------------------------------

// How big a corner's web is (px); the rest of its look follows from `rng`.
void Cobwebs::Impl::Plan(int corner, Rng& rng, uint32_t& seed, float& reach) const {
    const float s = layout_.dpiScale;
    seed = Hash32(static_cast<uint32_t>(static_cast<int32_t>(std::lround(layout_.originX))) * 0x9E3779B1u +
                  (corner ? 0x6A09E667u : 0xBB67AE85u));
    rng.Seed(seed, 0x2545F4914F6CDD1Dull);
    const float cap = std::min(kSkyShare * layout_.groundY, kWideShare * static_cast<float>(layout_.width));
    reach = std::min(rng.Range(kMinReach, kMaxReach) * s, cap);
}

void Cobwebs::Impl::Build(Web& w, int corner) {
    const float s = layout_.dpiScale;
    w = Web{};
    w.corner = corner;
    Rng rng;
    Plan(corner, rng, w.seed, w.reach);
    const float reach = w.reach;
    if (reach < kSmallest * s) return;
    // The frame: three threads round the corner, a rough quarter circle, from
    // the top edge down to its first corner, across to the second and back to
    // the side edge. Silk too fine to see holds its corners out.
    const float cap = std::min(kSkyShare * layout_.groundY, kWideShare * static_cast<float>(layout_.width));
    auto polar = [&](float r, float a) {
        return D2D1_POINT_2F{r * std::cos(a), std::min(r * std::sin(a), 0.94f * cap)};
    };
    const D2D1_POINT_2F v1 = polar(reach * rng.Range(0.96f, 1.06f), rng.Range(0.36f, 0.5f));
    const D2D1_POINT_2F v2 = polar(reach * rng.Range(0.9f, 1.0f), rng.Range(1.06f, 1.2f));
    const D2D1_POINT_2F frame[4] = {
        {v1.x * rng.Range(1.0f, 1.08f), 0.0f}, v1, v2, {0.0f, std::min(v2.y * rng.Range(1.01f, 1.08f), cap)}};
    w.across = frame[0].x;
    w.down = frame[3].y;
    // The hub, about where the largest circle inside it would be centred.
    const float hu = reach * rng.Range(0.36f, 0.42f), hv = std::min(reach, frame[3].y) * rng.Range(0.37f, 0.43f);
    w.hubU = hu;
    w.hubV = hv;
    const float p1 = rng.Range(0.0f, kTau), p2 = rng.Range(0.0f, kTau);
    w.knots.reserve(380);
    w.threads.reserve(720);

    auto knot = [&](float u, float v, bool anchor) {
        Knot k;
        k.x = X(w, u);
        k.y = v;
        k.anchor = anchor;
        k.w = anchor ? 0.0f : 1.0f;
        const float ud = u / s, vd = v / s;
        k.wa = 0.5f + 0.5f * std::sin(0.031f * ud + 0.017f * vd + p1);
        k.wb = 0.5f + 0.5f * std::sin(0.023f * ud - 0.029f * vd + p2);
        w.knots.push_back(k);
        return static_cast<int>(w.knots.size()) - 1;
    };
    // `taut`: how much shorter than it hangs it is spun (negative: slack).
    // Silk stretches in proportion to its length; frame silk is the stoutest.
    auto tie = [&](int a, int b, Silk silk, float taut) {
        Thread t;
        t.a = static_cast<uint16_t>(a);
        t.b = static_cast<uint16_t>(b);
        t.silk = silk;
        const Knot& ka = w.knots[static_cast<size_t>(a)];
        const Knot& kb = w.knots[static_cast<size_t>(b)];
        const float len = std::hypot(kb.x - ka.x, kb.y - ka.y);
        t.rest = t.restNow = std::max(0.5f, len * (1.0f - taut));
        const float per = t.rest / (10.0f * s);
        t.compliance = per * (silk == Silk::Spiral ? kCapture : silk == Silk::Frame ? kFrameSilk : kDragline);
        w.threads.push_back(t);
    };
    int corners[4];
    for (int i = 0; i < 4; ++i) corners[i] = knot(frame[i].x, frame[i].y, i == 0 || i == 3);
    w.pins[0] = {corners[1], w.knots[static_cast<size_t>(corners[1])].x, w.knots[static_cast<size_t>(corners[1])].y};
    w.pins[1] = {corners[2], w.knots[static_cast<size_t>(corners[2])].x, w.knots[static_cast<size_t>(corners[2])].y};

    // Radials: one to each corner of the frame and one into the screen's
    // corner, the rest spread round between them with a hand's unevenness.
    // Each runs out to the first thing it meets: the top edge, the side edge
    // or one of the frame's threads, which it pulls in towards the hub.
    struct Ray {
        float angle, phase, len, eu, ev;
        float q;    // where along its frame thread it ends
        int first;  // the first spiral turn it carries (a secondary radial starts out on the spiral)
        int hit;    // 1 top edge, 2 side edge, 3.. frame thread hit - 3, 0 a corner of the frame
        int end;    // its knot at the far end
    };
    Ray rays[kMaxRays];
    int n = 0;
    auto cast = [&](float angle, int first) {
        Ray& r = rays[n++];
        r.angle = angle;
        r.first = first;
        r.end = -1;
        r.q = 0.0f;
        const float dx = std::cos(angle), dy = std::sin(angle);
        float best = 1e30f;
        r.hit = 0;
        auto meet = [&](D2D1_POINT_2F a, D2D1_POINT_2F b, int hit) {
            const float ex = b.x - a.x, ey = b.y - a.y;
            const float den = dx * ey - dy * ex;
            if (std::fabs(den) < 1e-6f) return;
            const float ax = a.x - hu, ay = a.y - hv;
            const float t = (ax * ey - ay * ex) / den, q = (ax * dy - ay * dx) / den;
            if (t > 0.0f && t < best && q >= 0.0f && q <= 1.0f) {
                best = t;
                r.hit = hit;
                r.q = q;
            }
        };
        meet({0.0f, 0.0f}, frame[0], 1);
        meet(frame[3], {0.0f, 0.0f}, 2);
        for (int i = 0; i < 3; ++i) meet(frame[i], frame[i + 1], 3 + i);
        if (r.hit == 0) {
            best = 0.3f * reach;
            r.hit = 3;
        }
        r.eu = hu + best * dx;
        r.ev = hv + best * dy;
        if (r.hit >= 3) {
            // The radials pull the frame in towards the hub, most between its corners.
            const D2D1_POINT_2F a = frame[r.hit - 3], b = frame[r.hit - 2];
            const float bow = kFrameBow * std::hypot(b.x - a.x, b.y - a.y) * 4.0f * r.q * (1.0f - r.q) *
                              rng.Range(0.8f, 1.2f);
            const float l = std::max(std::hypot(r.eu - hu, r.ev - hv), 1e-3f);
            r.eu -= (r.eu - hu) / l * bow;
            r.ev -= (r.ev - hv) / l * bow;
        }
        r.len = std::max(std::hypot(r.eu - hu, r.ev - hv), 4.0f * s);
    };
    const int primaries = Clamp(static_cast<int>(std::lround(7.0f + reach / s / 28.0f)), 9, kMaxRadials);
    constexpr int kKeys = 5;
    float keys[kKeys + 1];
    for (int i = 0; i < 4; ++i) keys[i] = std::atan2(frame[i].y - hv, frame[i].x - hu);
    keys[4] = std::atan2(-hv, -hu);
    for (int i = 1; i < kKeys; ++i)
        while (keys[i] < keys[i - 1]) keys[i] += kTau;
    keys[kKeys] = keys[0] + kTau;
    {
        int per[kKeys], given = 0, widest = 0;
        for (int a = 0; a < kKeys; ++a) {
            per[a] = static_cast<int>(static_cast<float>(primaries - kKeys) * (keys[a + 1] - keys[a]) / kTau);
            given += per[a];
            if (keys[a + 1] - keys[a] > keys[widest + 1] - keys[widest]) widest = a;
        }
        per[widest] += primaries - kKeys - given;
        for (int a = 0; a < kKeys; ++a) {
            cast(keys[a] + rng.Range(-0.02f, 0.02f), 0);
            const float span = keys[a + 1] - keys[a];
            for (int j = 1; j <= per[a]; ++j) {
                const float share = (static_cast<float>(j) + rng.Range(-0.28f, 0.28f)) / static_cast<float>(per[a] + 1);
                cast(keys[a] + span * share, 0);
            }
        }
    }
    // Where neighbouring radials run long and far apart, the spider added one
    // between them, starting out on the spiral.
    const int turns = Clamp(static_cast<int>(std::lround(reach / s / 30.0f)), 3, kMaxTurns);
    for (int i = 0, primary = n; i < primary && n < kMaxRays; ++i) {
        const Ray& a = rays[i];
        const Ray& b = rays[(i + 1) % primary];
        const float gap = Wrap(b.angle - a.angle);
        if (gap > 0.0f && std::min(a.len, b.len) * gap > 44.0f * s && a.hit != 1 && a.hit != 2 && b.hit != 1 &&
            b.hit != 2)
            cast(a.angle + gap * rng.Range(0.42f, 0.58f), turns / 3 + rng.Int(0, 1));
    }
    for (int i = 0; i < n; ++i) {
        Ray& r = rays[i];
        float d = std::fmod(r.angle - keys[0], kTau);
        if (d < 0.0f) d += kTau;
        r.phase = d / kTau;
    }
    std::sort(rays, rays + n, [](const Ray& a, const Ray& b) { return a.phase < b.phase; });
    float meanLen = 0.0f;
    for (int i = 0; i < n; ++i) meanLen += rays[i].len / static_cast<float>(n);

    // Along each radial: the hub's own little spiral, the capture spiral (round
    // near the hub, following the frame further out) and the radial's end.
    w.hub = knot(hu, hv, false);
    constexpr int kSkip = -2, kCut = -1;  // no knot: the spiral passes over it, or turns back there
    int hubKnots[kHubTurns][kMaxRays];
    std::fill(&hubKnots[0][0], &hubKnots[0][0] + kHubTurns * kMaxRays, kSkip);
    std::vector<int> spiral(static_cast<size_t>(turns * n), kSkip);
    int inner[kMaxRays];
    const float hubIn = 0.022f * reach, hubOut = 0.07f * reach;
    float bunch[kMaxTurns], wobble[kMaxTurns];
    for (int k = 0; k < kMaxTurns; ++k) {
        bunch[k] = k ? rng.Range(-0.2f, 0.2f) : 0.0f;
        wobble[k] = rng.Range(0.0f, kTau);
    }
    for (int i = 0; i < n; ++i) {
        Ray& r = rays[i];
        const float du = (r.eu - hu) / r.len, dv = (r.ev - hv) / r.len;
        int last = r.first == 0 ? w.hub : -1;
        float lastD = 0.0f;
        auto along = [&](float d, float aside) {
            lastD = d;
            return knot(hu + du * d - dv * aside, hv + dv * d + du * aside, false);
        };
        // How far out the capture spiral is, a share u of the way across it.
        auto out = [&](float u) {
            const float g = Lerp(0.3f, 1.0f, std::pow(Saturate(u), 1.3f));
            return Lerp(kSpiralIn, kSpiralOut, u) * std::pow(r.len, g) * std::pow(meanLen, 1.0f - g);
        };
        if (r.first == 0) {
            const float room = 0.5f * out(0.0f);
            for (int j = 0; j < kHubTurns; ++j) {
                const float u = (static_cast<float>(j) + r.phase + rng.Range(-0.15f, 0.15f)) / kHubTurns;
                const float d = std::max(lastD + 0.6f * s, std::min(Lerp(hubIn, hubOut, u), room));
                const int k = along(d, 0.0f);
                hubKnots[j][i] = k;
                tie(last, k, Silk::Radial, kTaut);
                last = k;
            }
        }
        for (int k = r.first; k < turns; ++k) {
            // Turns come closer together here and further apart there, as spun by hand.
            const float uneven = bunch[k] + 0.14f * std::sin(kTau * 2.0f * r.phase + wobble[k]);
            const float u = (static_cast<float>(k) + r.phase + uneven + rng.Range(-0.12f, 0.12f)) /
                            static_cast<float>(turns);
            const float d = std::max(out(u), lastD + 1.0f * s);
            if (d > kSpiralOut * 1.02f * r.len) {  // the spiral turns back before it gets this far out
                for (int q = k; q < turns; ++q) spiral[static_cast<size_t>(q * n + i)] = kCut;
                break;
            }
            const int kn = along(d, rng.Range(-0.6f, 0.6f) * s);
            spiral[static_cast<size_t>(k * n + i)] = kn;
            if (last >= 0) tie(last, kn, Silk::Radial, kTaut);
            last = kn;
        }
        inner[i] = last;
        r.end = -1;
        for (int c = 0; c < 4; ++c)
            if (std::hypot(r.eu - frame[c].x, r.ev - frame[c].y) < 5.0f * s) {
                r.end = corners[c];
                r.hit = 0;
            }
        if (r.end < 0) r.end = knot(r.eu, r.ev, r.hit < 3);
        if (last >= 0) tie(last, r.end, Silk::Radial, kTaut);
    }
    // The spirals wind out from one radial to the next; a few segments were
    // never spun and a few hang slack.
    auto wind = [&](const int* knots, int count, int stride, Silk silk, float skip, float droop) {
        int last = -1;
        bool skipped = false;
        for (int q = 0; q < count; ++q) {
            const int k = knots[(q / n) * stride + q % n];
            if (k == kSkip) continue;
            if (k == kCut) {
                last = -1;
                continue;
            }
            if (last >= 0) {
                if (!skipped && rng.Chance(skip)) {
                    skipped = true;
                } else {
                    skipped = false;
                    tie(last, k, silk, rng.Chance(droop) ? -rng.Range(0.01f, 0.04f) : kTautSpiral);
                }
            }
            last = k;
        }
    };
    wind(&hubKnots[0][0], kHubTurns * n, kMaxRays, Silk::Hub, 0.15f, 0.0f);
    wind(spiral.data(), turns * n, n, Silk::Spiral, kSkipped, kDroopy);

    // The frame's threads, corner to corner through the radials' ends.
    int ends[kMaxRays];
    int m = 0;
    for (int f = 0; f < 3; ++f) {
        int first = m;
        for (int i = 0; i < n; ++i)
            if (rays[i].hit == 3 + f) ends[m++] = i;
        std::sort(ends + first, ends + m, [&](int x, int y) { return rays[x].q < rays[y].q; });
        int last = corners[f];
        for (int k = first; k < m; ++k) {
            tie(last, rays[ends[k]].end, Silk::Frame, kTautFrame);
            last = rays[ends[k]].end;
        }
        tie(last, corners[f + 1], Silk::Frame, kTautFrame);
    }
    for (int k = 0; k < m; ++k) ends[k] = rays[ends[k]].end;
    // Chords across the very corner: older, looser silk.
    const int chords = rng.Int(1, 3);
    for (int c = 0; c < chords; ++c) {
        const float cu = reach * rng.Range(0.03f, 0.15f), cv = reach * rng.Range(0.03f, 0.14f);
        tie(knot(cu, 0.0f, true), knot(0.0f, cv, true), Silk::Chord, -rng.Range(0.01f, 0.05f));
    }

    // Which threads are tied to each knot.
    const size_t count = w.knots.size();
    w.adjStart.assign(count + 1, 0);
    for (const Thread& t : w.threads) {
        ++w.adjStart[static_cast<size_t>(t.a) + 1];
        ++w.adjStart[static_cast<size_t>(t.b) + 1];
    }
    for (size_t k = 0; k < count; ++k) w.adjStart[k + 1] += w.adjStart[k];
    w.adj.assign(static_cast<size_t>(w.adjStart[count]), 0);
    {
        std::vector<int> fill(w.adjStart.begin(), w.adjStart.end() - 1);
        for (size_t i = 0; i < w.threads.size(); ++i) {
            const Thread& t = w.threads[i];
            w.adj[static_cast<size_t>(fill[t.a]++)] = static_cast<int>(i);
            w.adj[static_cast<size_t>(fill[t.b]++)] = static_cast<int>(i);
            ++w.knots[t.a].links;
            ++w.knots[t.b].links;
        }
    }

    // Dew on the capture spiral.
    w.dew.reserve(w.threads.size() / 3);
    for (size_t i = 0; i < w.threads.size(); ++i) {
        if (w.threads[i].silk != Silk::Spiral || !rng.Chance(0.24f)) continue;
        Dew d;
        d.thread = static_cast<int>(i);
        d.u = rng.Range(0.2f, 0.8f);
        d.r = (rng.Chance(0.12f) ? rng.Range(1.25f, 1.6f) : rng.Range(0.7f, 1.15f)) * s;
        d.phase = rng.Float();
        d.period = rng.Range(12.0f, 30.0f);
        w.dew.push_back(d);
    }

    // How far the web reaches from its hub, by angle (corner frame).
    for (int bin = 0; bin < kSpanBins; ++bin) {
        const float ang = kTau * (static_cast<float>(bin) + 0.5f) / static_cast<float>(kSpanBins);
        float before = kTau, after = kTau, lb = 0.0f, la = 0.0f;
        for (int i = 0; i < n; ++i) {
            float d = std::fmod(ang - rays[i].angle, kTau);
            if (d < 0.0f) d += kTau;
            if (d < before) {
                before = d;
                lb = rays[i].len;
            }
            const float e = kTau - d;
            if (e < after) {
                after = e;
                la = rays[i].len;
            }
        }
        w.span[bin] = Lerp(lb, la, before / std::max(before + after, 1e-4f));
    }

    // Strands of stray silk hanging from the frame.
    w.strands.reserve(kMaxStrands);
    const int strays = m > 0 && rng.Chance(0.75f) ? (rng.Chance(0.35f) ? 2 : 1) : 0;
    for (int k = 0; k < strays; ++k)
        AddStrand(w, ends[rng.Int(0, m - 1)], -1, 0.0f, 1.0f, reach * rng.Range(0.07f, 0.16f),
                  rng.Range(0.15f, 0.45f), 0.0f, 0.0f);

    // A spider on every web: her hub, and her retreat up the radial nearest the corner.
    Spider& sp = w.spider;
    sp.present = true;
    sp.scale = Clamp(reach / s / 230.0f, 0.5f, 1.0f);
    sp.home = sp.at = sp.to = w.hub;
    sp.x = w.knots[static_cast<size_t>(w.hub)].x;
    sp.y = w.knots[static_cast<size_t>(w.hub)].y;
    {
        float best = -2.0f;
        for (int i = 0; i < n; ++i) {
            const float c = -(std::cos(rays[i].angle) + std::sin(rays[i].angle));
            if (c > best && inner[i] >= 0) {
                best = c;
                sp.hide = inner[i];
            }
        }
    }
    sp.path.reserve(count);
    sp.timer = rng.Range(2.0f, 10.0f);
    w.maxHeld = Clamp(static_cast<int>(reach / s / 80.0f), 1, 3);
    w.sheen = w.sheenBase = rng.Range(0.4f, 1.2f);

    Settle(w);

    // Bounds, with room for the strands to swing.
    w.left = w.top = 1e30f;
    w.right = w.bottom = -1e30f;
    for (const Knot& k : w.knots) {
        w.left = std::min(w.left, k.x);
        w.right = std::max(w.right, k.x);
        w.top = std::min(w.top, k.y);
        w.bottom = std::max(w.bottom, k.y);
    }
    w.bottom += 0.2f * reach;
    w.left -= 4.0f * s;
    w.right += 4.0f * s;
}

// Lets a newly strung web hang (the frame takes up its sag), so it starts still.
void Cobwebs::Impl::Settle(Web& w) {
    for (int i = 0; i < 150; ++i) {
        Step(w, kStep, 0.0f, 0.0f, 0.0f, 0.0f);
        StepStrands(w, kStep, 0.0f);
    }
    for (Knot& k : w.knots) k.vx = k.vy = 0.0f;
    for (Strand& st : w.strands)
        for (int i = 0; i < kStrandPoints; ++i) st.vx[i] = st.vy[i] = 0.0f;
}

// An end hanging from `knot`, laid out along (ux, uy) and whipped back by the snap.
void Cobwebs::Impl::AddStrand(Web& w, int knot, int thread, float ux, float uy, float length, float curl, float vx,
                              float vy) {
    const float s = layout_.dpiScale;
    if (length < 5.0f * s || w.strands.size() >= kMaxStrands) return;
    Strand st;
    st.knot = knot;
    st.thread = thread;
    st.link = length / static_cast<float>(kStrandPoints);
    st.bend = 2.0f * st.link * std::cos(0.5f * curl);
    const Knot& k = w.knots[static_cast<size_t>(knot)];
    for (int i = 0; i < kStrandPoints; ++i) {
        const float d = st.link * static_cast<float>(i + 1);
        const float kink = (i & 1 ? 0.3f : -0.3f) * st.link;
        st.x[i] = k.x + ux * d - uy * kink;
        st.y[i] = k.y + uy * d + ux * kink;
        const float share = static_cast<float>(i + 1) / static_cast<float>(kStrandPoints);
        st.vx[i] = (0.35f * vx - kRecoil * s * ux) * share;
        st.vy[i] = (0.35f * vy - kRecoil * s * uy) * share;
    }
    w.strands.push_back(st);
}

// ---------------------------------------------------------------------------
// Silk
// ---------------------------------------------------------------------------

// One XPBD step of `h` seconds: gravity, the breeze (ax; eddies n1, n2) and
// the loads, then the threads pull the knots together.
void Cobwebs::Impl::Step(Web& w, float h, float ax, float lift, float n1, float n2) {
    const float damp = std::exp(-h / kTremble);
    const float g = kGravity * layout_.dpiScale;
    for (Knot& k : w.knots) {
        if (k.w <= 0.0f) continue;
        const float e1 = k.wa * n1 + k.wb * n2, e2 = k.wb * n1 - k.wa * n2;
        k.vx = (k.vx + ax * (1.0f + 0.6f * e1) * h) * damp;
        k.vy = (k.vy + (g + k.load + lift * e2) * h) * damp;
        k.ox = k.x;
        k.oy = k.y;
        k.x += k.vx * h;
        k.y += k.vy * h;
    }
    for (Thread& t : w.threads) t.lambda = 0.0f;
    for (Web::Pin& p : w.pins) p.lambda = 0.0f;
    const float inv = 1.0f / (h * h);
    const int n = static_cast<int>(w.threads.size());
    for (int it = 0; it < kIterations; ++it) {
        for (Web::Pin& p : w.pins) {
            Knot& k = w.knots[static_cast<size_t>(p.knot)];
            const float dx = k.x - p.x, dy = k.y - p.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (k.w <= 0.0f || d < 1e-4f) continue;
            const float at = kTether * inv;
            const float dl = (-d - at * p.lambda) / (k.w + at);
            p.lambda += dl;
            k.x += k.w * dl * dx / d;
            k.y += k.w * dl * dy / d;
        }
        const bool back = (it & 1) != 0;  // alternate the order, so no end of the web is favoured
        for (int i = 0; i < n; ++i) {
            Thread& t = w.threads[static_cast<size_t>(back ? n - 1 - i : i)];
            if (!Walkable(t)) continue;
            Knot& a = w.knots[t.a];
            Knot& b = w.knots[t.b];
            const float ws = a.w + b.w;
            if (ws <= 0.0f) continue;
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 <= t.restNow * t.restNow) continue;  // slack: silk only pulls
            const float d = std::sqrt(d2);
            const float at = t.compliance * inv;
            const float dl = (t.restNow - d - at * t.lambda) / (ws + at);
            t.lambda += dl;
            const float kx = dx / d * dl, ky = dy / d * dl;
            a.x -= a.w * kx;
            a.y -= a.w * ky;
            b.x += b.w * kx;
            b.y += b.w * ky;
        }
    }
    const float invH = 1.0f / h;
    for (Knot& k : w.knots) {
        if (k.w <= 0.0f) continue;
        k.vx = (k.x - k.ox) * invH;
        k.vy = (k.y - k.oy) * invH;
    }
}

// Snapped ends and strays: light ropes that stream in the air and keep a curl.
void Cobwebs::Impl::StepStrands(Web& w, float h, float air) {
    if (w.strands.empty()) return;
    const float s = layout_.dpiScale;
    const float drag = std::exp(-h / kStrandDrag);
    const float fall = kStrandGravity * s * h;
    const float flow = kStrandAir * air;
    const float invH = 1.0f / h;
    const float wave = static_cast<float>(std::fmod(time_ * 2.3, static_cast<double>(kTau)));
    for (Strand& st : w.strands) {
        const Knot& k = w.knots[static_cast<size_t>(st.knot)];
        const float phase = 0.77f * static_cast<float>(st.knot);
        float ox[kStrandPoints], oy[kStrandPoints];
        for (int i = 0; i < kStrandPoints; ++i) {
            // The air it drifts with: the wind, and eddies that ripple along it.
            const float eddy = kFlutter * s * std::sin(wave + 1.9f * static_cast<float>(i) + phase) *
                               static_cast<float>(i + 1) / static_cast<float>(kStrandPoints);
            st.vx[i] = flow + eddy + (st.vx[i] - flow - eddy) * drag;
            st.vy[i] = st.vy[i] * drag + fall;
            ox[i] = st.x[i];
            oy[i] = st.y[i];
            st.x[i] += st.vx[i] * h;
            st.y[i] += st.vy[i] * h;
        }
        for (int it = 0; it < 3; ++it) {
            // Links: the knot holds the first point.
            for (int i = 0; i < kStrandPoints; ++i) {
                const float qx = i ? st.x[i - 1] : k.x, qy = i ? st.y[i - 1] : k.y;
                const float dx = st.x[i] - qx, dy = st.y[i] - qy;
                const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
                const float c = (d - st.link) / d;
                if (i == 0) {
                    st.x[0] -= dx * c;
                    st.y[0] -= dy * c;
                } else {
                    st.x[i] -= 0.5f * dx * c;
                    st.y[i] -= 0.5f * dy * c;
                    st.x[i - 1] += 0.5f * dx * c;
                    st.y[i - 1] += 0.5f * dy * c;
                }
            }
            // Curl: every other point keeps its distance (softly, either way round).
            for (int i = 1; i < kStrandPoints; ++i) {
                const bool fixed = i < 2;
                const float qx = fixed ? k.x : st.x[i - 2], qy = fixed ? k.y : st.y[i - 2];
                const float dx = st.x[i] - qx, dy = st.y[i] - qy;
                const float d = std::sqrt(dx * dx + dy * dy) + 1e-6f;
                const float c = 0.35f * (d - st.bend) / d;
                if (fixed) {
                    st.x[i] -= dx * c;
                    st.y[i] -= dy * c;
                } else {
                    st.x[i] -= 0.5f * dx * c;
                    st.y[i] -= 0.5f * dy * c;
                    st.x[i - 2] += 0.5f * dx * c;
                    st.y[i - 2] += 0.5f * dy * c;
                }
            }
        }
        for (int i = 0; i < kStrandPoints; ++i) {
            st.vx[i] = (st.x[i] - ox[i]) * invH;
            st.vy[i] = (st.y[i] - oy[i]) * invH;
        }
    }
}

void Cobwebs::Impl::UpdateWeb(Web& w, float dt) {
    const float s = layout_.dpiScale;
    // Leaves hanging in it weigh on their knots.
    int held = 0;
    for (Knot& k : w.knots) {
        const bool leaf = time_ - k.heldAt < kHeldFor;
        held += leaf ? 1 : 0;
        k.load = leaf ? kLeafWeight * s : 0.0f;
    }
    w.held = held;
    UpdateSpider(w, dt);
    UpdateThreads(w, dt);

    // The breeze: the wind, the air's own stirring, and eddies across the web.
    const double seed = static_cast<double>(w.seed & 0xFFu);
    const float n0 = noise::Fbm1(Wrap256(time_ * 0.27 + seed), 2);
    const float n1 = noise::Noise1(Wrap256(time_ * 0.83 + seed * 1.37));
    const float n2 = noise::Noise1(Wrap256(time_ * 1.71 + seed * 0.71));
    const float air = wind_ + kStillAir * s * n0;
    const float ax = kBreeze * air;
    const float lift = kLift * std::fabs(ax) + 0.5f * kBreeze * kStillAir * s;
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / kStep - 0.01f)));
    const float h = dt / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        Step(w, h, ax, lift, n1, n2);
        StepStrands(w, h, air);
    }

    // A knot shaken hard, or a strong gust, lets a leaf go.
    if (held > 0) {
        const float loose = kLooseSpeed * s;
        const float gust = std::fabs(wind_) / (kGustWind * s) - 1.0f;
        for (Knot& k : w.knots) {
            if (time_ - k.heldAt >= kHeldFor) continue;
            if (k.vx * k.vx + k.vy * k.vy > loose * loose || (gust > 0.0f && rng_.Chance(0.7f * gust * dt)))
                k.shaken = ++stamp_;
        }
    }
    // The light on the silk wanders slowly.
    w.sheen = w.sheenBase + 0.7f * noise::Fbm1(Wrap256(time_ * 0.03 + seed * 3.1), 2);
}

// Snapped threads mend; mended ones close their holes; spun-up ends fade.
void Cobwebs::Impl::UpdateThreads(Web& w, float dt) {
    if (w.unsettled > 0) {
        int unsettled = 0, torn = 0;
        for (size_t i = 0; i < w.threads.size(); ++i) {
            Thread& t = w.threads[i];
            switch (t.state) {
            case State::Whole:
                break;
            case State::Torn:
                if (time_ >= t.mendAt) Mend(w, static_cast<int>(i), false);
                else ++torn;
                ++unsettled;
                break;
            case State::Spinning:
                ++unsettled;
                break;
            case State::Mending:
                t.alpha = std::min(1.0f, t.alpha + dt / kMendFade);
                t.restNow += (t.rest - t.restNow) * ApproachFactor(dt, kMendClose);
                if (t.alpha >= 1.0f && t.restNow - t.rest < 0.05f) {
                    t.restNow = t.rest;
                    t.state = State::Whole;
                } else {
                    ++unsettled;
                }
                break;
            }
        }
        w.unsettled = unsettled;
        w.torn = torn;
    }
    for (size_t i = 0; i < w.strands.size();) {
        Strand& st = w.strands[i];
        if (st.fading && (st.alpha -= dt / 0.9f) <= 0.0f) {
            st = w.strands.back();
            w.strands.pop_back();
            continue;
        }
        ++i;
    }
}

void Cobwebs::Impl::Link(Web& w, int knot) {
    Knot& k = w.knots[static_cast<size_t>(knot)];
    ++k.links;
    if (!k.anchor && k.w <= 0.0f) {
        k.w = 1.0f;
        k.vx = k.vy = 0.0f;
    }
}

void Cobwebs::Impl::Unlink(Web& w, int knot) {
    Knot& k = w.knots[static_cast<size_t>(knot)];
    if (k.links > 0) --k.links;
    if (k.links == 0 && !k.anchor) {
        k.w = 0.0f;  // nothing holds it now: parked where it is, and what hung from it drifts away
        k.vx = k.vy = 0.0f;
        for (Strand& st : w.strands)
            if (st.knot == knot) st.fading = true;
    }
}

// Leaves caught within `radius` of (x, y) shake loose.
void Cobwebs::Impl::Shake(Web& w, float x, float y, float radius) {
    const unsigned now = ++stamp_;
    const float r2 = radius * radius;
    for (Knot& k : w.knots)
        if ((k.x - x) * (k.x - x) + (k.y - y) * (k.y - y) < r2) k.shaken = now;
}

// A thread snaps `at` (a share of the way from a to b); its two ends spring
// back and hang from their knots.
void Cobwebs::Impl::Snap(Web& w, int thread, float at, float vx, float vy) {
    Thread& t = w.threads[static_cast<size_t>(thread)];
    if (!Walkable(t)) return;
    if (t.state == State::Whole) ++w.unsettled;
    t.state = State::Torn;
    ++w.torn;
    t.alpha = 0.0f;
    const bool keeper = w.spider.present && halloween_;
    t.mendAt = time_ + static_cast<double>(keeper ? kSpiderMends : kMendWait + rng_.Range(0.0f, kMendVary));
    const Knot& a = w.knots[t.a];
    const Knot& b = w.knots[t.b];
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::max(std::sqrt(dx * dx + dy * dy), 0.01f);
    const float ux = dx / len, uy = dy / len;
    const float mx = Lerp(a.x, b.x, at), my = Lerp(a.y, b.y, at);
    Unlink(w, t.a);
    Unlink(w, t.b);
    // (A knot with nothing left tied to it is a speck of silk: no end hangs there.)
    if (a.w > 0.0f || a.anchor)
        AddStrand(w, t.a, thread, ux, uy, at * t.rest * 0.85f, rng_.Range(0.5f, 1.2f), vx, vy);
    if (b.w > 0.0f || b.anchor)
        AddStrand(w, t.b, thread, -ux, -uy, (1.0f - at) * t.rest * 0.85f, rng_.Range(0.5f, 1.2f), vx, vy);
    Shake(w, mx, my, 30.0f * layout_.dpiScale);
}

// A snapped (or half-spun) thread is whole again: by the spider (`spun`) or
// fading back in by itself.
void Cobwebs::Impl::Mend(Web& w, int thread, bool spun) {
    Thread& t = w.threads[static_cast<size_t>(thread)];
    if (t.state == State::Torn && w.torn > 0) --w.torn;
    if (t.state == State::Whole) ++w.unsettled;
    const Knot& a = w.knots[t.a];
    const Knot& b = w.knots[t.b];
    t.restNow = std::max(t.rest, std::hypot(b.x - a.x, b.y - a.y));
    t.alpha = spun ? 1.0f : 0.0f;
    t.state = State::Mending;
    Link(w, t.a);
    Link(w, t.b);
    for (Strand& st : w.strands)
        if (st.thread == thread) st.fading = true;
}

void Cobwebs::Impl::Touch(float dt, float x, float y, float vx, float vy) {
    ptrX_ = x;
    ptrY_ = y;
    ptrAt_ = time_;
    if (fade_ <= 0.05f || dt <= 0.0f) return;
    const float s = layout_.dpiScale;
    const float R = kReach * s;
    // The stretch it swept this frame (a long jump counts only its end).
    float px0 = x - vx * dt, py0 = y - vy * dt;
    {
        const float jx = x - px0, jy = y - py0, j = std::sqrt(jx * jx + jy * jy), most = 120.0f * s;
        if (j > most) {
            px0 = x - jx * most / j;
            py0 = y - jy * most / j;
        }
    }
    const float speed = std::sqrt(vx * vx + vy * vy);
    const float tear = speed > kTearSpeed * s ? SmoothStep(kTearSpeed * s, 1.8f * kTearSpeed * s, speed) : -1.0f;
    const float bl = std::min(px0, x) - R, br = std::max(px0, x) + R;
    const float bt = std::min(py0, y) - R, bb = std::max(py0, y) + R;
    for (Web& w : webs_) {
        if (Alpha(w) < 0.3f || br < w.left || bl > w.right || bb < w.top || bt > w.bottom) continue;
        for (Knot& k : w.knots) k.pushX = k.pushY = k.pushW = k.dragX = k.dragY = 0.0f;
        bool touched = false;
        int tears = 0;
        for (size_t i = 0; i < w.threads.size(); ++i) {
            Thread& t = w.threads[i];
            if (!Walkable(t)) continue;
            Knot& a = w.knots[t.a];
            Knot& b = w.knots[t.b];
            if (std::max(a.x, b.x) < bl || std::min(a.x, b.x) > br || std::max(a.y, b.y) < bt ||
                std::min(a.y, b.y) > bb)
                continue;
            float sp, st;
            Closest(px0, py0, x, y, a.x, a.y, b.x, b.y, sp, st);
            const float cx = Lerp(px0, x, sp), cy = Lerp(py0, y, sp);
            float dx = Lerp(a.x, b.x, st) - cx, dy = Lerp(a.y, b.y, st) - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d >= R) continue;
            touched = true;
            // A fast swipe snaps the threads it runs through ...
            if (tear >= 0.0f && d < 0.5f * R && tears < kMaxTears && rng_.Chance(0.3f + 0.7f * tear)) {
                Snap(w, static_cast<int>(i), Clamp(st, 0.15f, 0.85f), vx, vy);
                ++tears;
                continue;
            }
            // ... otherwise they are carried along a little and pushed out of its way.
            const float wgt = 1.0f - d / R;
            if (d > 1e-3f) {
                dx /= d;
                dy /= d;
            } else {
                dx = -vy / (speed + 1e-3f);
                dy = vx / (speed + 1e-3f);
            }
            const float out = kShove * (R - d);
            const float gx = vx * dt * kGrip * wgt, gy = vy * dt * kGrip * wgt;
            const float wa = (1.0f - st) * wgt, wb = st * wgt;
            a.pushX += (gx + dx * out) * wa;
            a.pushY += (gy + dy * out) * wa;
            a.dragX += gx * wa;
            a.dragY += gy * wa;
            a.pushW += wa;
            b.pushX += (gx + dx * out) * wb;
            b.pushY += (gy + dy * out) * wb;
            b.dragX += gx * wb;
            b.dragY += gy * wb;
            b.pushW += wb;
        }
        if (!touched) continue;
        const unsigned now = ++stamp_;
        const float inv = 1.0f / dt;
        for (Knot& k : w.knots) {
            if (k.pushW <= 0.0f) continue;
            k.shaken = now;
            if (k.w <= 0.0f) continue;
            // Moved out of the way; carried along, it keeps going a little.
            const float share = 1.0f / std::max(k.pushW, 1.0f);
            k.x += k.pushX * share;
            k.y += k.pushY * share;
            k.vx += 0.5f * k.dragX * share * inv;
            k.vy += 0.5f * k.dragY * share * inv;
        }
        // Leaves the pointer brushes shake loose too.
        Shake(w, x, y, R + 8.0f * s);
    }
}

// ---------------------------------------------------------------------------
// The spider
// ---------------------------------------------------------------------------

void Cobwebs::Impl::UpdateSpider(Web& w, float dt) {
    Spider& sp = w.spider;
    if (!sp.present) return;
    const float s = layout_.dpiScale;
    sp.alpha = halloween_ ? std::min(1.0f, sp.alpha + dt / kSpiderFade) : std::max(0.0f, sp.alpha - dt / kSpiderFade);
    if (sp.alpha <= 0.0f) {
        if (!halloween_ && (sp.mode != Mode::Hub || sp.at != sp.home)) Rest(w);  // next time, she starts at the hub
        return;
    }
    const bool threat = halloween_ && time_ - ptrAt_ < 0.25 && std::hypot(ptrX_ - sp.x, ptrY_ - sp.y) < kShy * s;
    sp.calm = threat ? 0.0f : sp.calm + dt;
    const bool chores = halloween_ && w.torn > 0 && sp.calm > 1.5f;
    switch (sp.mode) {
    case Mode::Hub:
        sp.timer -= dt;
        if (threat) Flee(w);
        else if (chores && Repair(w)) {
        } else if (sp.timer <= 0.0f && halloween_) {
            sp.mode = Mode::Lower;
            sp.dropGoal = w.reach * rng_.Range(0.1f, 0.24f);
        }
        break;
    case Mode::Lower:
        if (threat || chores) {
            sp.mode = Mode::Climb;
            sp.hurry = threat;
            break;
        }
        sp.drop = std::min(sp.dropGoal, sp.drop + kLower * s * dt);
        if (sp.drop >= sp.dropGoal) {
            sp.mode = Mode::Dangle;
            sp.timer = rng_.Range(8.0f, 24.0f);
        }
        break;
    case Mode::Dangle:
        sp.timer -= dt;
        if (threat || chores || sp.timer <= 0.0f) {
            sp.mode = Mode::Climb;
            sp.hurry = threat;
        }
        break;
    case Mode::Climb:
        if (threat) sp.hurry = true;
        sp.drop -= (sp.hurry ? kScurry : kClimb) * s * dt;
        if (sp.drop <= 0.0f) {
            sp.drop = 0.0f;
            sp.mode = Mode::Hub;
            sp.timer = rng_.Range(6.0f, 18.0f);
            if (sp.hurry && threat) Flee(w);  // still too close: on to her hiding place
            sp.hurry = false;
        }
        break;
    case Mode::Walk:
        if (threat && sp.then != Mode::Hide) {
            Abandon(w);
            Flee(w);
            break;
        }
        Walk(w, (sp.hurry ? kScurry : kWalk) * s * dt);
        break;
    case Mode::Spin:
        if (threat) {
            Abandon(w);
            Flee(w);
            break;
        }
        SpinOn(w, dt);
        break;
    case Mode::Hide:
        if (sp.calm > kBold && halloween_) GoTo(w, sp.home, Mode::Hub, false);
        break;
    }
    Place(w, dt);
}

// Back at the hub, idle.
void Cobwebs::Impl::Rest(Web& w) {
    Abandon(w);
    Spider& sp = w.spider;
    sp.mode = Mode::Hub;
    sp.at = sp.to = sp.home;
    sp.t = 0.0f;
    sp.drop = sp.bob = sp.bobV = sp.swing = sp.swingV = 0.0f;
    sp.path.clear();
    sp.hurry = false;
    sp.heading = 0.5f * kPi;
    sp.timer = rng_.Range(4.0f, 12.0f);
    const Knot& k = w.knots[static_cast<size_t>(sp.home)];
    sp.x = k.x;
    sp.y = k.y;
}

// Up her dragline, then off to her retreat (unless the pointer is there).
void Cobwebs::Impl::Flee(Web& w) {
    Spider& sp = w.spider;
    if (sp.drop > 0.0f) {
        sp.mode = Mode::Climb;
        sp.hurry = true;
        return;
    }
    const Knot& hide = w.knots[static_cast<size_t>(sp.hide)];
    if (std::hypot(ptrX_ - hide.x, ptrY_ - hide.y) < std::hypot(ptrX_ - sp.x, ptrY_ - sp.y)) {
        sp.mode = Mode::Hide;  // it is between her and her retreat: she keeps still where she is
        if (sp.t > 0.5f) sp.at = sp.to;
        sp.to = sp.at;
        sp.t = 0.0f;
        return;
    }
    GoTo(w, sp.hide, Mode::Hide, true);
}

// Sets off along the threads to `target`, from wherever she is on the web.
void Cobwebs::Impl::GoTo(Web& w, int target, Mode then, bool hurry) {
    Spider& sp = w.spider;
    if (sp.t < 0.5f) {  // turn back to the nearer knot first
        std::swap(sp.at, sp.to);
        sp.t = 1.0f - sp.t;
    }
    FindPath(w, sp.to, target, sp.path);
    sp.mode = Mode::Walk;
    sp.then = then;
    sp.hurry = hurry;
}

// Breadth first along whole threads; `path` holds the knots after `from`, the
// next one last. Cut off: straight there.
bool Cobwebs::Impl::FindPath(const Web& w, int from, int to, std::vector<int>& path) {
    path.clear();
    if (from == to) return true;
    prev_.assign(w.knots.size(), -1);
    queue_.clear();
    prev_[static_cast<size_t>(from)] = from;
    queue_.push_back(from);
    for (size_t q = 0; q < queue_.size() && prev_[static_cast<size_t>(to)] < 0; ++q) {
        const int k = queue_[q];
        for (int e = w.adjStart[static_cast<size_t>(k)]; e < w.adjStart[static_cast<size_t>(k) + 1]; ++e) {
            const Thread& t = w.threads[static_cast<size_t>(w.adj[static_cast<size_t>(e)])];
            if (!Walkable(t)) continue;
            const int o = t.a == k ? t.b : t.a;
            if (prev_[static_cast<size_t>(o)] >= 0) continue;
            prev_[static_cast<size_t>(o)] = k;
            queue_.push_back(o);
        }
    }
    if (prev_[static_cast<size_t>(to)] < 0) {
        path.push_back(to);
        return false;
    }
    for (int k = to; k != from; k = prev_[static_cast<size_t>(k)]) path.push_back(k);
    return true;
}

void Cobwebs::Impl::Walk(Web& w, float dist) {
    Spider& sp = w.spider;
    for (int guard = 0; guard < 16 && dist > 0.0f; ++guard) {
        const Knot& a = w.knots[static_cast<size_t>(sp.at)];
        const Knot& b = w.knots[static_cast<size_t>(sp.to)];
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        const float left = (1.0f - sp.t) * len;
        if (dist < left) {
            sp.t += dist / std::max(len, 1e-3f);
            return;
        }
        dist -= left;
        sp.at = sp.to;
        sp.t = 0.0f;
        if (sp.path.empty()) {
            Arrive(w);
            return;
        }
        sp.to = sp.path.back();
        sp.path.pop_back();
    }
}

void Cobwebs::Impl::Arrive(Web& w) {
    Spider& sp = w.spider;
    sp.to = sp.at;
    sp.t = 0.0f;
    sp.hurry = false;
    switch (sp.then) {
    case Mode::Spin:
        StartSpin(w);
        break;
    case Mode::Hide:
        sp.mode = Mode::Hide;
        break;
    default:
        sp.mode = Mode::Hub;
        sp.timer = rng_.Range(5.0f, 15.0f);
        break;
    }
}

// Picks the nearest snapped thread she can reach an end of and sets off.
bool Cobwebs::Impl::Repair(Web& w) {
    Spider& sp = w.spider;
    if (sp.drop > 0.0f) return false;
    int best = -1, start = -1;
    float bestD = 1e30f;
    for (size_t i = 0; i < w.threads.size(); ++i) {
        const Thread& t = w.threads[i];
        if (t.state != State::Torn) continue;
        for (const int end : {static_cast<int>(t.a), static_cast<int>(t.b)}) {
            const Knot& k = w.knots[static_cast<size_t>(end)];
            if (k.links == 0 && !k.anchor) continue;  // nothing to stand on there
            const float d = std::hypot(k.x - sp.x, k.y - sp.y);
            if (d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
                start = end;
            }
        }
    }
    if (best < 0) return false;
    sp.task = best;
    GoTo(w, start, Mode::Spin, false);
    return true;
}

// At one end of her task: she takes up the old silk and spins across.
void Cobwebs::Impl::StartSpin(Web& w) {
    Spider& sp = w.spider;
    if (sp.task < 0 || w.threads[static_cast<size_t>(sp.task)].state != State::Torn) {
        sp.task = -1;
        if (!Repair(w)) GoTo(w, sp.home, Mode::Hub, false);
        return;
    }
    Thread& t = w.threads[static_cast<size_t>(sp.task)];
    if (t.a != sp.at && t.b != sp.at) {  // (could not get there along the threads: from where she is)
        sp.at = t.a;
    }
    if (w.torn > 0) --w.torn;
    t.state = State::Spinning;
    t.fromB = t.b == sp.at;
    t.progress = 0.0f;
    sp.to = t.fromB ? t.a : t.b;
    sp.t = 0.0f;
    sp.mode = Mode::Spin;
    for (Strand& st : w.strands)
        if (st.thread == sp.task) st.fading = true;
}

void Cobwebs::Impl::SpinOn(Web& w, float dt) {
    Spider& sp = w.spider;
    Thread& t = w.threads[static_cast<size_t>(sp.task)];
    const Knot& a = w.knots[static_cast<size_t>(sp.at)];
    const Knot& b = w.knots[static_cast<size_t>(sp.to)];
    const float len = std::max(std::hypot(b.x - a.x, b.y - a.y), 1.0f);
    sp.t = std::min(1.0f, sp.t + kSpin * layout_.dpiScale * dt / len);
    t.progress = sp.t;
    if (sp.t < 1.0f) return;
    Mend(w, sp.task, true);
    sp.task = -1;
    sp.at = sp.to;
    sp.t = 0.0f;
    if (!Repair(w)) GoTo(w, sp.home, Mode::Hub, false);
}

// Leaves a half-spun thread (it mends itself later).
void Cobwebs::Impl::Abandon(Web& w) {
    Spider& sp = w.spider;
    if (sp.task < 0) return;
    Thread& t = w.threads[static_cast<size_t>(sp.task)];
    if (t.state == State::Spinning) {
        t.state = State::Torn;
        ++w.torn;
        t.progress = 0.0f;
        t.mendAt = std::max(t.mendAt, time_ + (halloween_ ? 10.0 : 2.0));
    }
    sp.task = -1;
    if (sp.mode == Mode::Spin) {
        sp.mode = Mode::Hide;
        if (sp.t > 0.5f) sp.at = sp.to;
        sp.to = sp.at;
        sp.t = 0.0f;
    }
}

// Where she is, which way she faces, her legs' cycle and her weight on the web.
void Cobwebs::Impl::Place(Web& w, float dt) {
    Spider& sp = w.spider;
    const float s = layout_.dpiScale;
    const float k = sp.scale * s;  // px per art DIP
    const float oldX = sp.x, oldY = sp.y;
    const float weight = kSpiderWeight * s * sp.alpha;
    const bool hanging = sp.mode == Mode::Lower || sp.mode == Mode::Dangle || sp.mode == Mode::Climb;
    if (hanging) {
        // A pendulum on her dragline swung by the breeze; the silk stretches and she bobs.
        const float push = Clamp(kSwingWind * wind_ / s, -0.5f, 0.5f) +
                           0.06f * noise::Noise1(Wrap256(time_ * 0.7 + static_cast<double>(w.seed & 0x3Fu)));
        sp.swingV += (-9.0f * (std::sin(sp.swing) - push) - 1.4f * sp.swingV) * dt;
        sp.swing = Clamp(sp.swing + sp.swingV * dt, -0.9f, 0.9f);
        const float rest = 1.2f * s * std::sin(static_cast<float>(std::fmod(time_ * 1.6, static_cast<double>(kTau))));
        sp.bobV += (-30.0f * (sp.bob - rest) - 2.5f * sp.bobV) * dt;
        sp.bob += sp.bobV * dt;
        const Knot& hub = w.knots[static_cast<size_t>(sp.home)];
        const float ux = std::sin(sp.swing), uy = std::cos(sp.swing);
        const float rear = kRear * k * Saturate(sp.drop / (kRear * k));
        const float len = std::max(0.0f, sp.drop + sp.bob * Saturate(sp.drop / (4.0f * s))) + rear;
        sp.x = hub.x + ux * len;
        sp.y = hub.y + uy * len;
        sp.heading = std::atan2(uy, ux);
        w.knots[static_cast<size_t>(sp.home)].load += weight;
        sp.gait += std::fabs(std::hypot(sp.x - oldX, sp.y - oldY)) / (kStride * k) * 0.5f;
    } else {
        const Knot& a = w.knots[static_cast<size_t>(sp.at)];
        const Knot& b = w.knots[static_cast<size_t>(sp.to)];
        sp.x = Lerp(a.x, b.x, sp.t);
        sp.y = Lerp(a.y, b.y, sp.t);
        float goal = sp.heading;
        if ((sp.mode == Mode::Walk || sp.mode == Mode::Spin) && std::hypot(b.x - a.x, b.y - a.y) > 0.5f) {
            goal = std::atan2(b.y - a.y, b.x - a.x);
        } else if (sp.mode == Mode::Hub) {
            goal = 0.5f * kPi + 0.12f * noise::Noise1(Wrap256(time_ * 0.21 + static_cast<double>(w.seed & 0x7Fu)));
        } else if (sp.mode == Mode::Hide) {
            const Knot& hub = w.knots[static_cast<size_t>(sp.home)];
            goal = std::atan2(hub.y - sp.y, hub.x - sp.x);
        }
        const float turn = (sp.hurry ? 16.0f : 6.0f) * dt;
        sp.heading = Wrap(sp.heading + Clamp(Wrap(goal - sp.heading), -turn, turn));
        if (sp.mode != Mode::Hub || sp.t > 0.0f) {
            w.knots[static_cast<size_t>(sp.at)].load += weight * (1.0f - sp.t);
            w.knots[static_cast<size_t>(sp.to)].load += weight * sp.t;
        } else {
            w.knots[static_cast<size_t>(sp.home)].load += weight;
        }
        const float moved = std::hypot(sp.x - oldX, sp.y - oldY);
        if (sp.mode == Mode::Walk || sp.mode == Mode::Spin) sp.gait += moved / (kStride * k);
        if (sp.mode == Mode::Spin) sp.gait += 0.6f * dt;  // (her legs work the silk)
    }
    sp.gait -= std::floor(sp.gait);
    sp.calm = std::min(sp.calm, 1000.0f);
    if (dt > 0.0f) {
        const float pace = std::hypot(sp.x - oldX, sp.y - oldY) / dt;
        sp.pace += (std::min(pace, 400.0f * s) - sp.pace) * ApproachFactor(dt, 0.12f);
    }
}

// ---------------------------------------------------------------------------
// Bats
// ---------------------------------------------------------------------------

void Cobwebs::Impl::UpdateBats(float dt) {
    const float s = layout_.dpiScale;
    const float W = static_cast<float>(layout_.width);
    const bool room = configured_ && W > 300.0f * s && layout_.groundY > 160.0f * s;
    if (halloween_ && room) {
        if (nextBats_ < 0.0) nextBats_ = time_ + static_cast<double>(kBatFirst + rng_.Range(0.0f, kBatFirstVary));
        if (time_ >= nextBats_) {
            Launch();
            nextBats_ = time_ + static_cast<double>(kBatWait + rng_.Range(0.0f, kBatWaitVary));
        }
    } else {
        nextBats_ = -1.0;
    }
    const bool pointer = time_ - ptrAt_ < 0.15;
    const float shy = kBatShy * s;
    for (size_t i = 0; i < bats_.size();) {
        Bat& b = bats_[i];
        b.alpha = halloween_ ? std::min(1.0f, b.alpha + dt / kBatFade) : std::max(0.0f, b.alpha - dt / kBatFade);
        b.wave += b.waveRate * dt;
        float tvx = b.speed;
        float tvy = b.waveAmp * b.waveRate * std::cos(b.wave) + 0.8f * (b.lane - b.y);
        if (pointer) {
            // Swerving away from the pointer.
            const float dx = b.x - ptrX_, dy = b.y - ptrY_;
            const float d = std::sqrt(dx * dx + dy * dy) + 1e-3f;
            if (d < shy) {
                const float push = (1.0f - d / shy) * 520.0f * s;
                tvx += dx / d * push;
                tvy += dy / d * push;
                b.glide = 0.0f;
            }
        }
        const float k = ApproachFactor(dt, 0.28f);
        b.vx += (tvx - b.vx) * k;
        b.vy += (tvy - b.vy) * k;
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        if (b.glide > 0.0f) {
            b.glide -= dt;
            b.y += 6.0f * s * dt;  // (sinking a little)
        } else {
            const float before = b.flap;
            b.flap += b.flapRate * dt;
            // Now and then it glides a moment, wings out (as they pass level on the way down).
            if (std::floor(before + 0.75f) != std::floor(b.flap + 0.75f) && rng_.Chance(0.12f))
                b.glide = rng_.Range(0.25f, 0.6f);
        }
        const bool gone = (b.speed > 0.0f && b.x > W + 60.0f * s) || (b.speed < 0.0f && b.x < -60.0f * s) ||
                          (!halloween_ && b.alpha <= 0.0f);
        if (gone) {
            b = bats_.back();
            bats_.pop_back();
            continue;
        }
        ++i;
    }
}

// One to three bats across the upper part of the screen.
void Cobwebs::Impl::Launch() {
    const float s = layout_.dpiScale;
    const float W = static_cast<float>(layout_.width);
    const int count = 1 + (rng_.Chance(0.55f) ? 1 : 0) + (rng_.Chance(0.35f) ? 1 : 0);
    const float dir = rng_.Chance(0.5f) ? 1.0f : -1.0f;
    const float lane = layout_.groundY * rng_.Range(0.08f, 0.3f);
    const float speed = rng_.Range(150.0f, 230.0f) * s * dir;
    float lag = 0.0f;
    for (int i = 0; i < count && bats_.size() < 8; ++i) {
        Bat b;
        b.x = dir > 0.0f ? -40.0f * s - lag : W + 40.0f * s + lag;
        b.lane = lane + (i ? rng_.Range(-26.0f, 26.0f) * s : 0.0f);
        b.y = b.lane;
        b.speed = speed * rng_.Range(0.92f, 1.08f);
        b.vx = b.speed;
        b.wave = rng_.Range(0.0f, kTau);
        b.waveRate = rng_.Range(1.6f, 2.6f);
        b.waveAmp = rng_.Range(8.0f, 20.0f) * s;
        b.flap = rng_.Float();
        b.flapRate = rng_.Range(6.5f, 8.5f);
        b.size = rng_.Range(0.8f, 1.05f);
        b.alpha = 1.0f;
        bats_.push_back(b);
        lag += rng_.Range(40.0f, 90.0f) * s;
    }
}

// ---------------------------------------------------------------------------
// The sprite page
// ---------------------------------------------------------------------------

namespace {

// One frame of a bat's wingbeat (art DIP, the body at the origin, y down).
ComPtr<ID2D1PathGeometry> BatWings(ID2D1Factory* factory, Wingbeat beat) {
    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return nullptr;
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    for (int side = -1; side <= 1; side += 2) {
        const float sd = static_cast<float>(side);
        auto quad = [&](D2D1_POINT_2F c, D2D1_POINT_2F p) {
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(WingPoint(c, beat, sd), WingPoint(p, beat, sd)));
        };
        // Deep scallops: each edge between finger tips is drawn in towards the wrist.
        auto scallop = [&](D2D1_POINT_2F a, D2D1_POINT_2F b) {
            const D2D1_POINT_2F mid = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y)};
            quad({Lerp(mid.x, kWrist.x, 0.4f), Lerp(mid.y, kWrist.y, 0.4f)}, b);
        };
        sink->BeginFigure(WingPoint(kShoulder, beat, sd), D2D1_FIGURE_BEGIN_FILLED);
        quad({4.0f, -5.6f}, kWrist);    // the arm, arched up to the wrist
        quad({13.0f, -5.4f}, kTips[0]);  // the long finger, out to the tip
        scallop(kTips[0], kTips[1]);
        scallop(kTips[1], kTips[2]);
        scallop(kTips[2], kHip);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }
    if (FAILED(sink->Close())) return nullptr;
    return geo;
}

ComPtr<ID2D1PathGeometry> Polygon(ID2D1Factory* factory, std::initializer_list<D2D1_POINT_2F> pts) {
    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (pts.size() < 3 || FAILED(factory->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return nullptr;
    sink->BeginFigure(*pts.begin(), D2D1_FIGURE_BEGIN_FILLED);
    for (auto p = pts.begin() + 1; p != pts.end(); ++p) sink->AddLine(*p);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return nullptr;
    return geo;
}

}  // namespace

HRESULT Cobwebs::Impl::BuildPage(ID2D1DeviceContext* dc) {
    const float s = layout_.dpiScale;
    ComPtr<ID2D1Device> device;
    dc->GetDevice(&device);
    if (!device) return E_FAIL;
    // A private context on the same device: the caller is mid-frame.
    ComPtr<ID2D1DeviceContext> rdc;
    HRESULT hr = device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rdc);
    if (FAILED(hr)) return hr;
    ComPtr<ID2D1Factory> factory;
    rdc->GetFactory(&factory);
    if (!factory) return E_FAIL;
    rdc->SetDpi(96.0f, 96.0f);
    rdc->SetUnitMode(D2D1_UNIT_MODE_PIXELS);

    // Bat frames and their extents (art DIP).
    ComPtr<ID2D1PathGeometry> wings[4];
    D2D1_RECT_F batBounds[4];
    for (int f = 0; f < 4; ++f) {
        wings[f] = BatWings(factory.Get(), kWingbeat[f]);
        if (!wings[f] || FAILED(wings[f]->GetBounds(nullptr, &batBounds[f]))) return E_FAIL;
        batBounds[f].top = std::min(batBounds[f].top, -7.0f);  // (the ears)
        batBounds[f].bottom = std::max(batBounds[f].bottom, 5.2f);
    }

    // ---- Plan the cells ------------------------------------------------------
    // Silk: a soft line along the cell, a bright core over a faint dark underlay
    // that keeps it visible over light windows. Every column is alike, so a
    // sprite stretches it to any length; it is drawn 1:1 across.
    const float core = std::max(0.62f, 0.5f * s), under = std::max(0.9f, 0.78f * s);
    const int silkRows = 2 * static_cast<int>(std::ceil(3.0f * under)) + 1;
    // Legs: a solid rod a DIP thick (at least a pixel), tinted dark.
    const float rodW = 0.5f * std::max(1.0f, s);
    const int rodRows = 2 * static_cast<int>(std::ceil(rodW + 1.5f)) + 1;
    dropPx_ = std::ceil(4.0f * s);
    glintPx_ = std::ceil(12.0f * s);
    struct Box {
        Cell* cell;
        int w, h;      // incl. the clear border
        float ax, ay;  // the anchor inside the box
    };
    std::vector<Box> boxes;
    auto box = [&](Cell* cell, float left, float top, float right, float bottom) {
        const int l = static_cast<int>(std::ceil(left)) + kClear, t = static_cast<int>(std::ceil(top)) + kClear;
        const int r = static_cast<int>(std::ceil(right)) + kClear, b = static_cast<int>(std::ceil(bottom)) + kClear;
        boxes.push_back({cell, l + r, t + b, static_cast<float>(l), static_cast<float>(t)});
    };
    box(&silk_, 0.0f, 0.5f * static_cast<float>(silkRows), static_cast<float>(kLineCols),
        0.5f * static_cast<float>(silkRows));
    box(&rod_, 0.0f, 0.5f * static_cast<float>(rodRows), static_cast<float>(kLineCols),
        0.5f * static_cast<float>(rodRows));
    box(&drop_, 0.5f * dropPx_ + 1.0f, 0.5f * dropPx_ + 1.0f, 0.5f * dropPx_ + 1.0f, 0.5f * dropPx_ + 1.0f);
    box(&glint_, 0.5f * glintPx_, 0.5f * glintPx_, 0.5f * glintPx_, 0.5f * glintPx_);
    box(&body_, 3.6f * s, 4.6f * s, 3.6f * s, 7.3f * s);
    for (int f = 0; f < 4; ++f)
        box(&bat_[f], -batBounds[f].left * s + 1.0f, -batBounds[f].top * s + 1.0f, batBounds[f].right * s + 1.0f,
            batBounds[f].bottom * s + 1.0f);
    // Shelf-pack, tallest first.
    std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return a.h > b.h; });
    constexpr UINT32 pageW = 512;
    UINT32 x = 0, y = 0, row = 0;
    for (Box& b : boxes) {
        if (x + static_cast<UINT32>(b.w) > pageW) {
            x = 0;
            y += row;
            row = 0;
        }
        b.cell->src = {x, y, x + static_cast<UINT32>(b.w), y + static_cast<UINT32>(b.h)};
        b.cell->cx = static_cast<float>(x) + b.ax;
        b.cell->cy = static_cast<float>(y) + b.ay;
        x += static_cast<UINT32>(b.w);
        row = std::max(row, static_cast<UINT32>(b.h));
    }
    const UINT32 pageH = y + row;
    if (pageH == 0 || pageH > rdc->GetMaximumBitmapSize()) return E_OUTOFMEMORY;

    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    ComPtr<ID2D1Bitmap1> page;
    if (FAILED(hr = rdc->CreateBitmap(D2D1::SizeU(pageW, pageH), nullptr, 0, props, &page))) return hr;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(hr = rdc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush))) return hr;
    ComPtr<ID2D1StrokeStyle> round;
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                           D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                               nullptr, 0, &round);
    auto radial = [&](D2D1_POINT_2F c, float rx, float ry, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
        ComPtr<ID2D1GradientStopCollection> coll;
        ComPtr<ID2D1RadialGradientBrush> g;
        if (SUCCEEDED(rdc->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), &coll)))
            rdc->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(c, {0, 0}, rx, ry), coll.Get(), &g);
        return g;
    };
    auto stop = [](float at, Rgb c, float a) { return D2D1_GRADIENT_STOP{at, Color(c, a)}; };
    auto fillEllipse = [&](D2D1_POINT_2F c, float rx, float ry, ID2D1Brush* b) {
        rdc->FillEllipse(D2D1::Ellipse(c, rx, ry), b);
    };

    rdc->SetTarget(page.Get());
    rdc->BeginDraw();
    rdc->Clear(D2D1::ColorF(0, 0, 0, 0));
    for (const Box& b : boxes) {
        const D2D1_RECT_U& rc = b.cell->src;
        const float cb = static_cast<float>(kClear);
        rdc->SetTransform(D2D1::Matrix3x2F::Identity());
        rdc->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(rc.left) + cb, static_cast<float>(rc.top) + cb,
                                             static_cast<float>(rc.right) - cb, static_cast<float>(rc.bottom) - cb),
                                 D2D1_ANTIALIAS_MODE_ALIASED);
        const float cx = b.cell->cx, cy = b.cell->cy;
        if (b.cell == &silk_ || b.cell == &rod_) {
            // Row by row: the line's profile across.
            const bool silk = b.cell == &silk_;
            const int rows = silk ? silkRows : rodRows;
            rdc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
            for (int r = 0; r < rows; ++r) {
                const float d = static_cast<float>(r - rows / 2);
                float c = 0.0f, a = 0.0f;
                if (silk) {
                    c = kCore * std::exp(-0.5f * d * d / (core * core));
                    const float u = kUnderlay * std::exp(-0.5f * d * d / (under * under));
                    a = c + u * (1.0f - c);
                } else {
                    const float e = std::fabs(d) / (rodW + 0.5f);
                    c = a = std::exp(-e * e * e * e);
                }
                if (a < 1e-3f) continue;
                brush->SetColor(D2D1::ColorF(c / a, c / a, c / a, a));
                const float top = cy - 0.5f * static_cast<float>(rows) + static_cast<float>(r);
                rdc->FillRectangle(D2D1::RectF(cx, top, cx + static_cast<float>(kLineCols), top + 1.0f), brush.Get());
            }
            rdc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        } else if (b.cell == &drop_) {
            // A dew drop: clear and bright, lit from the upper left, on a faint dark ring.
            const float r = 0.5f * dropPx_;
            const D2D1_POINT_2F c = {cx, cy};
            brush->SetColor(D2D1::ColorF(0, 0, 0, 0.22f));
            fillEllipse(c, r + 0.7f, r + 0.7f, brush.Get());
            if (auto g = radial({cx - 0.25f * r, cy - 0.3f * r}, 1.25f * r, 1.25f * r,
                                {stop(0.0f, kDew, 1.0f), stop(0.35f, kDew, 0.7f), stop(0.75f, kDew, 0.38f),
                                 stop(1.0f, kDew, 0.75f)}))
                fillEllipse(c, r, r, g.Get());
            brush->SetColor(D2D1::ColorF(1, 1, 1, 1));
            fillEllipse({cx - 0.3f * r, cy - 0.32f * r}, 0.3f * r, 0.3f * r, brush.Get());
        } else if (b.cell == &glint_) {
            // A glint: a soft point of light with four fine rays.
            const float r = 0.5f * glintPx_;
            if (auto g = radial({cx, cy}, r, r, {stop(0.0f, {1, 1, 1}, 1.0f), stop(0.3f, {1, 1, 1}, 0.5f),
                                                  stop(1.0f, {1, 1, 1}, 0.0f)})) {
                const float thin = std::max(0.55f, 0.05f * r);
                fillEllipse({cx, cy}, r, thin, g.Get());
                fillEllipse({cx, cy}, thin, r, g.Get());
            }
            if (auto g = radial({cx, cy}, 0.32f * r, 0.32f * r,
                                {stop(0.0f, {1, 1, 1}, 1.0f), stop(1.0f, {1, 1, 1}, 0.0f)}))
                fillEllipse({cx, cy}, 0.32f * r, 0.32f * r, g.Get());
        } else if (b.cell == &body_) {
            // The spider's body, head up, her waist at the anchor (art DIP): a
            // dark glossy abdomen with a faint pale folium, her head and jaws.
            rdc->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(cx, cy));
            const Rgb rim = Hex(0xB9AC9E);
            if (auto g = radial({-0.8f, 2.2f}, 4.2f, 4.2f,
                                {stop(0.0f, Hex(0x3E2E26), 1.0f), stop(0.45f, Hex(0x1C1410), 1.0f),
                                 stop(1.0f, Hex(0x090605), 1.0f)}))
                fillEllipse({0.0f, 3.25f}, 2.75f, 3.35f, g.Get());
            brush->SetColor(Color(Hex(0xD3C2AA), 0.2f));
            for (int i = 0; i < 3; ++i) {
                const float yy = 2.2f + 1.1f * static_cast<float>(i), hw = 1.15f - 0.28f * static_cast<float>(i);
                rdc->DrawLine({-hw, yy}, {0.0f, yy + 0.5f}, brush.Get(), 0.32f, round.Get());
                rdc->DrawLine({hw, yy}, {0.0f, yy + 0.5f}, brush.Get(), 0.32f, round.Get());
            }
            brush->SetColor(Color(Hex(0xD3C2AA), 0.16f));
            for (int i = 0; i < 2; ++i) {
                const float yy = 1.4f + 2.3f * static_cast<float>(i);
                fillEllipse({-0.95f, yy}, 0.24f, 0.24f, brush.Get());
                fillEllipse({0.95f, yy}, 0.24f, 0.24f, brush.Get());
            }
            if (auto g = radial({-0.95f, 1.8f}, 1.05f, 1.6f,
                                {stop(0.0f, {1, 1, 1}, 0.34f), stop(1.0f, {1, 1, 1}, 0.0f)}))
                fillEllipse({-0.95f, 1.8f}, 1.05f, 1.6f, g.Get());
            brush->SetColor(Color(rim, 0.3f));
            rdc->DrawEllipse(D2D1::Ellipse({0.0f, 3.25f}, 2.75f, 3.35f), brush.Get(), 0.45f);
            brush->SetColor(Color(Hex(0x0D0907), 1.0f));
            fillEllipse({0.0f, 6.55f}, 0.38f, 0.32f, brush.Get());  // spinnerets
            fillEllipse({0.0f, 0.1f}, 0.6f, 0.55f, brush.Get());    // her waist
            if (auto g = radial({-0.5f, -2.4f}, 2.6f, 2.6f,
                                {stop(0.0f, Hex(0x4A372B), 1.0f), stop(0.5f, Hex(0x1D1411), 1.0f),
                                 stop(1.0f, Hex(0x0B0807), 1.0f)}))
                fillEllipse({0.0f, -1.75f}, 1.75f, 2.05f, g.Get());
            if (auto g = radial({-0.55f, -2.3f}, 0.6f, 0.75f,
                                {stop(0.0f, {1, 1, 1}, 0.28f), stop(1.0f, {1, 1, 1}, 0.0f)}))
                fillEllipse({-0.55f, -2.3f}, 0.6f, 0.75f, g.Get());
            brush->SetColor(Color(rim, 0.26f));
            rdc->DrawEllipse(D2D1::Ellipse({0.0f, -1.75f}, 1.75f, 2.05f), brush.Get(), 0.4f);
            brush->SetColor(Color(Hex(0x120C0A), 1.0f));
            fillEllipse({-0.5f, -3.8f}, 0.42f, 0.56f, brush.Get());  // jaws
            fillEllipse({0.5f, -3.8f}, 0.42f, 0.56f, brush.Get());
            brush->SetColor(D2D1::ColorF(1, 1, 1, 0.45f));
            fillEllipse({-0.34f, -3.05f}, 0.17f, 0.17f, brush.Get());  // eyes catching the light
            fillEllipse({0.34f, -3.05f}, 0.17f, 0.17f, brush.Get());
        } else {
            // A bat: membrane wings with finger bones, a furry body, pointed ears;
            // a faint moonlit rim keeps it from vanishing into dark wallpapers.
            const int f = static_cast<int>(b.cell - bat_);
            rdc->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(cx, cy));
            ID2D1PathGeometry* wing = wings[f].Get();
            const Wingbeat beat = kWingbeat[f];
            auto ear = [&](float side) {
                return Polygon(factory.Get(), {{side * 0.4f, -4.6f}, {side * 2.0f, -6.9f}, {side * 1.6f, -3.9f}});
            };
            auto earL = ear(-1.0f), earR = ear(1.0f);
            ComPtr<ID2D1EllipseGeometry> torso, head;
            factory->CreateEllipseGeometry(D2D1::Ellipse({0.0f, 0.6f}, 2.15f, 3.5f), &torso);
            factory->CreateEllipseGeometry(D2D1::Ellipse({0.0f, -3.0f}, 1.95f, 1.85f), &head);
            ID2D1Geometry* parts[] = {wing, torso.Get(), head.Get(), earL.Get(), earR.Get()};
            bool ok = true;
            for (ID2D1Geometry* g : parts) ok = ok && g;
            ComPtr<ID2D1GeometryGroup> group;
            ComPtr<ID2D1PathGeometry> outline;
            ComPtr<ID2D1GeometrySink> sink;
            if (ok && SUCCEEDED(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING, parts, 5, &group)) &&
                SUCCEEDED(factory->CreatePathGeometry(&outline)) && SUCCEEDED(outline->Open(&sink))) {
                group->Outline(nullptr, 0.02f, sink.Get());
                sink->Close();
                brush->SetColor(Color(Hex(0x1D1525), 0.97f));
                rdc->FillGeometry(outline.Get(), brush.Get());
                // Finger bones, from the wrist.
                brush->SetColor(Color(Hex(0x4A3D55), 0.6f));
                for (int side = -1; side <= 1; side += 2) {
                    const float sd = static_cast<float>(side);
                    const D2D1_POINT_2F wrist = WingPoint(kWrist, beat, sd);
                    for (const D2D1_POINT_2F tip : kTips)
                        rdc->DrawLine(wrist, WingPoint(tip, beat, sd), brush.Get(), 0.42f, round.Get());
                    rdc->DrawLine(WingPoint(kShoulder, beat, sd), wrist, brush.Get(), 0.65f, round.Get());
                }
                // The body, darker and furred, over the wings.
                brush->SetColor(Color(Hex(0x150F19), 1.0f));
                rdc->FillGeometry(torso.Get(), brush.Get());
                rdc->FillGeometry(head.Get(), brush.Get());
                rdc->FillGeometry(earL.Get(), brush.Get());
                rdc->FillGeometry(earR.Get(), brush.Get());
                // Moonlight along the top edges.
                ComPtr<ID2D1GradientStopCollection> coll;
                ComPtr<ID2D1LinearGradientBrush> moon;
                const D2D1_GRADIENT_STOP stops[] = {stop(0.0f, Hex(0xB4A6C8), 0.5f), stop(1.0f, Hex(0xB4A6C8), 0.0f)};
                const auto line = D2D1::LinearGradientBrushProperties({0.0f, batBounds[f].top}, {0.0f, 3.0f});
                if (SUCCEEDED(rdc->CreateGradientStopCollection(stops, 2, &coll)) &&
                    SUCCEEDED(rdc->CreateLinearGradientBrush(line, coll.Get(), &moon)))
                    rdc->DrawGeometry(outline.Get(), moon.Get(), 0.55f, round.Get());
            }
        }
        rdc->SetTransform(D2D1::Matrix3x2F::Identity());
        rdc->PopAxisAlignedClip();
    }
    hr = rdc->EndDraw();
    rdc->SetTarget(nullptr);
    if (FAILED(hr)) return hr;

    // The line cells are sampled from their middle columns (all alike) and
    // across their full profile, with a clear row either side.
    for (Cell* c : {&silk_, &rod_}) {
        const UINT32 mid = c->src.left + static_cast<UINT32>(kClear) + static_cast<UINT32>(kLineCols) / 2;
        c->src.left = mid - 2;
        c->src.right = mid + 2;
        c->src.top += static_cast<UINT32>(kClear) - 1;
        c->src.bottom -= static_cast<UINT32>(kClear) - 1;
    }
    silkHalf_ = 0.5f * static_cast<float>(silk_.src.bottom - silk_.src.top);
    rodHalf_ = 0.5f * static_cast<float>(rod_.src.bottom - rod_.src.top);
    page_ = page;
    return S_OK;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// A line cell stretched from a to b, `half` px either side.
void Cobwebs::Impl::Line(std::vector<ScenerySprite>& out, const Cell& cell, float half, float ax, float ay, float bx,
                         float by, D2D1_COLOR_F col) const {
    const float dx = bx - ax, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.05f) return;
    const float c = dx / len, sn = dy / len;
    out.push_back({{0.0f, -half, len, half}, cell.src, col, {c, sn, -sn, c, ax, ay}});
}

// A thread from a to b whose natural length is `rest`: taut, a straight line;
// slack, a parabola sagging along (gx, gy). Threads lying across the light
// (lc, ls: twice its angle) catch it.
void Cobwebs::Impl::DrawSilk(std::vector<ScenerySprite>& out, float ax, float ay, float bx, float by, float rest,
                             Look look, float alpha, float lc, float ls, float gx, float gy) const {
    const float dx = bx - ax, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.3f || alpha <= 0.003f) return;
    const float ux = dx / len, uy = dy / len;
    const float m = 0.5f + 0.5f * ((ux * ux - uy * uy) * lc + 2.0f * ux * uy * ls);
    const float m3 = m * m * m;
    const D2D1_COLOR_F col = Color(kSilk, std::min(1.0f, alpha * look.alpha * (0.5f + 1.25f * m3 * m3 * m)));
    const float half = silkHalf_ * look.thick;
    const float slack = rest - len;
    if (slack <= 0.004f * len) {
        Line(out, silk_, half, ax, ay, bx, by, col);
        return;
    }
    // A parabola of sag h over a chord c is about c + 8h^2/(3c) long.
    const float sag = std::min(0.3f * len, std::sqrt(0.375f * len * slack));
    float nx = gx - (gx * ux + gy * uy) * ux, ny = gy - (gx * ux + gy * uy) * uy;
    const float nl = std::sqrt(nx * nx + ny * ny);
    if (nl > 1e-3f) {
        nx /= nl;
        ny /= nl;
    } else {
        nx = -uy;
        ny = ux;
    }
    const int pieces = sag < 0.5f ? 1 : sag < 2.0f ? 2 : sag < 6.0f ? 4 : 6;
    float px = ax, py = ay;
    for (int i = 1; i <= pieces; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(pieces);
        const float off = 4.0f * u * (1.0f - u) * sag;
        const float qx = ax + dx * u + nx * off, qy = ay + dy * u + ny * off;
        Line(out, silk_, half, px, py, qx, qy, col);
        px = qx;
        py = qy;
    }
}

void Cobwebs::Impl::EmitSpider(const Web& w, float alpha, std::vector<ScenerySprite>& out) const {
    const Spider& sp = w.spider;
    const float a = alpha * sp.alpha;
    if (!sp.present || a <= 0.003f) return;
    const float s = layout_.dpiScale;
    const float k = sp.scale * s;  // px per art DIP
    const float fx = std::cos(sp.heading), fy = std::sin(sp.heading);  // forward
    const float rx = -fy, ry = fx;                                     // her right
    auto P = [&](float f, float r) {
        return D2D1_POINT_2F{sp.x + (fx * f + rx * r) * k, sp.y + (fy * f + ry * r) * k};
    };
    const bool hanging = sp.mode == Mode::Lower || sp.mode == Mode::Dangle || sp.mode == Mode::Climb;
    // Her dragline, from the hub to her spinnerets.
    if (hanging && sp.drop > 0.5f) {
        const Knot& hub = w.knots[static_cast<size_t>(sp.home)];
        const D2D1_POINT_2F tail = P(-kRear, 0.0f);
        Line(out, silk_, silkHalf_ * kDraglineLook.thick, hub.x, hub.y, tail.x, tail.y,
             Color(kSilk, a * kDraglineLook.alpha));
    }
    // Legs: each reaches for a foothold and steps with the walk cycle (four at
    // a time, alternating), bending at the knee away from her body.
    const float walking = Saturate(sp.pace / (6.0f * s));
    const float idle = Wrap256(time_ * 0.8);
    const float work = static_cast<float>(std::fmod(time_ * 13.0, static_cast<double>(kTau)));
    const D2D1_COLOR_F leg = Color(kLeg, 0.95f * a), rim = Color(kMoonlit, 0.13f * a);
    for (int side = -1; side <= 1; side += 2) {
        const float sd = static_cast<float>(side);
        for (int j = 0; j < 4; ++j) {
            float ang = kLegAngle[j], reach = kLegReach[j];
            if (hanging) {  // legs drawn in, the front ones reaching down
                ang *= 0.78f;
                reach *= 0.74f;
            } else if (sp.mode == Mode::Hide) {
                reach *= 0.8f;
            }
            const int group = (j + (side > 0 ? 1 : 0)) & 1;
            const float ph = Frac(static_cast<double>(sp.gait) + 0.5 * group);
            float off, lift = 0.0f;
            if (ph < 0.5f) {
                off = kStride * (0.25f - ph);
            } else {
                off = kStride * (ph - 0.75f);
                lift = std::sin(kTau * (ph - 0.5f));  // (0..1..0 over the swing)
            }
            off *= walking;
            reach *= 1.0f - 0.12f * lift * walking;
            // Idle: now and then a leg shifts a little.
            const float which = static_cast<float>(j) * 7.3f + sd * 3.1f + static_cast<float>(w.seed & 31u);
            off += 0.35f * noise::Noise1(idle + which);
            if (sp.mode == Mode::Spin && j == 3) off += 0.7f * std::sin(work + sd);  // working the silk
            const float hipR = sd * kHipR[j] * 0.85f;
            const D2D1_POINT_2F hip = P(kHipF[j], hipR);
            const D2D1_POINT_2F foot = P(kHipF[j] + std::cos(ang) * reach + off, hipR + sd * std::sin(ang) * reach);
            // Thigh and lower leg reach the foothold, bent outward at the knee.
            const float l1 = 0.48f * kLegReach[j] * 1.12f * k, l2 = 0.64f * kLegReach[j] * 1.12f * k;
            float dx = foot.x - hip.x, dy = foot.y - hip.y;
            float d = std::sqrt(dx * dx + dy * dy);
            const float most = 0.98f * (l1 + l2);
            if (d > most) {
                dx *= most / d;
                dy *= most / d;
                d = most;
            }
            d = std::max(d, 1e-3f);
            const float ux = dx / d, uy = dy / d;
            const float cosA = Clamp((l1 * l1 + d * d - l2 * l2) / (2.0f * l1 * d), -1.0f, 1.0f);
            const float sinA = std::sqrt(1.0f - cosA * cosA);
            float px = -uy, py = ux;
            if (px * rx * sd + py * ry * sd < 0.0f) {
                px = -px;
                py = -py;
            }
            const D2D1_POINT_2F knee = {hip.x + l1 * (ux * cosA + px * sinA), hip.y + l1 * (uy * cosA + py * sinA)};
            const D2D1_POINT_2F tip = {hip.x + dx, hip.y + dy};
            // The lower leg arches gently on down to the foot, slimmer at the end.
            const float lx = tip.x - knee.x, ly = tip.y - knee.y, arch = 0.08f * std::sqrt(lx * lx + ly * ly);
            const D2D1_POINT_2F ankle = {knee.x + 0.6f * lx + px * arch, knee.y + 0.6f * ly + py * arch};
            // A faint moonlit edge first, so she doesn't vanish against a dark wallpaper.
            const float edge = 2.4f * rodHalf_ * sp.scale;
            Line(out, rod_, edge * 1.1f, hip.x, hip.y, knee.x, knee.y, rim);
            Line(out, rod_, edge * 0.8f, knee.x, knee.y, ankle.x, ankle.y, rim);
            Line(out, rod_, edge * 0.58f, ankle.x, ankle.y, tip.x, tip.y, rim);
            Line(out, rod_, rodHalf_ * 1.1f * sp.scale, hip.x, hip.y, knee.x, knee.y, leg);
            Line(out, rod_, rodHalf_ * 0.8f * sp.scale, knee.x, knee.y, ankle.x, ankle.y, leg);
            Line(out, rod_, rodHalf_ * 0.58f * sp.scale, ankle.x, ankle.y, tip.x, tip.y, leg);
        }
    }
    // Her feelers.
    for (int side = -1; side <= 1; side += 2) {
        const float sd = static_cast<float>(side);
        const D2D1_POINT_2F p0 = P(3.4f, sd * 0.55f), p1 = P(4.7f, sd * 1.05f);
        Line(out, rod_, rodHalf_ * 0.75f * sp.scale, p0.x, p0.y, p1.x, p1.y, leg);
    }
    out.push_back(Put(body_, sp.scale, sp.heading + 0.5f * kPi, sp.x, sp.y, {1.0f, 1.0f, 1.0f, a}));
}

// ---------------------------------------------------------------------------
// Cobwebs
// ---------------------------------------------------------------------------

Cobwebs::Cobwebs() : impl_(std::make_unique<Impl>()) {}
Cobwebs::~Cobwebs() = default;

void Cobwebs::Configure(const CobwebLayout& scene) {
    Impl& m = *impl_;
    // The webs hang from the corners of the work area, under a taskbar docked at
    // the top or beside one docked at the side. Impl works from its top-left
    // corner; the calls below shift points into and out of that frame.
    const float left = Clamp(scene.insetLeft, 0.0f, 0.3f * static_cast<float>(scene.width));
    const float right = Clamp(scene.insetRight, 0.0f, 0.3f * static_cast<float>(scene.width));
    const float top = Clamp(scene.insetTop, 0.0f, 0.3f * scene.groundY);
    CobwebLayout layout = scene;
    layout.width = std::max(0, scene.width - static_cast<int>(std::lround(left + right)));
    layout.height = std::max(0, scene.height - static_cast<int>(std::lround(top)));
    layout.groundY = std::max(0.0f, scene.groundY - top);
    layout.insetTop = layout.insetLeft = layout.insetRight = 0.0f;
    m.ox_ = left;
    m.oy_ = top;
    if (m.configured_ && layout == m.layout_) return;
    const bool rescaled = !m.configured_ || layout.dpiScale != m.layout_.dpiScale;
    const bool resized = !m.configured_ || layout.width != m.layout_.width;
    m.layout_ = layout;
    m.configured_ = true;
    // Webs whose plan is unchanged stay as they are (leaves, tears and all).
    std::vector<Web> old;
    old.swap(m.webs_);
    bool rebuilt = false;
    for (int corner = 0; corner < 2; ++corner) {
        Rng rng;
        uint32_t seed;
        float reach;
        m.Plan(corner, rng, seed, reach);
        Web* keep = nullptr;
        for (Web& w : old)
            if (w.corner == corner && w.seed == seed && w.reach == reach) keep = &w;
        if (keep && !rescaled && !resized) {
            m.webs_.push_back(std::move(*keep));
            continue;
        }
        rebuilt = true;
        Web w;
        m.Build(w, corner);
        if (!w.knots.empty()) m.webs_.push_back(std::move(w));
    }
    if (rebuilt) m.strung_ = ++m.stamp_;  // holds in the old webs lapse
    m.coverStale_ = true;
    for (Web& w : m.webs_) w.probed = false;
    if (rescaled) {
        m.page_.Reset();
        m.pageScale_ = 0.0f;
    }
    if (resized) m.bats_.clear();
}

void Cobwebs::SetShown(bool show) {
    Impl& m = *impl_;
    if (show && !m.shown_) {
        // Shown again: look at the windows afresh before showing them.
        m.coverStale_ = true;
        for (Web& w : m.webs_) w.probed = false;
    }
    m.shown_ = show;
}

void Cobwebs::SetHalloween(bool on) {
    Impl& m = *impl_;
    if (on == m.halloween_) return;
    m.halloween_ = on;
    if (on) return;
    // No spiders now: what they left half done, and what was waiting for them,
    // mends by itself.
    for (Web& w : m.webs_) {
        m.Abandon(w);
        for (Thread& t : w.threads)
            if (t.state == State::Torn)
                t.mendAt = std::min(t.mendAt, m.time_ + static_cast<double>(0.5f * kMendWait +
                                                                            m.rng_.Range(0.0f, kMendVary)));
    }
}

bool Cobwebs::Active() const {
    const Impl& m = *impl_;
    if (!m.configured_) return false;
    return (!m.webs_.empty() && (m.shown_ || m.fade_ > 0.0f)) || m.halloween_ || !m.bats_.empty();
}

void Cobwebs::Update(float dt, double time, float windPx) {
    Impl& m = *impl_;
    dt = Clamp(dt, 0.0f, 0.1f);
    m.time_ = time;
    m.wind_ = windPx;
    m.fade_ = m.shown_ ? std::min(1.0f, m.fade_ + dt / kFadeIn) : std::max(0.0f, m.fade_ - dt / kFadeOut);
    for (Web& w : m.webs_) {
        w.vis = w.covered ? std::max(0.0f, w.vis - dt / kCoverOut) : std::min(1.0f, w.vis + dt / kCoverIn);
        if (dt > 0.0f && m.Alpha(w) > 0.0f) m.UpdateWeb(w, dt);
    }
    m.UpdateBats(dt);
}

void Cobwebs::Touch(float dt, float x, float y, float vx, float vy) {
    impl_->Touch(Clamp(dt, 0.0f, 0.1f), x - impl_->ox_, y - impl_->oy_, vx, vy);
}

bool Cobwebs::Catch(float x0, float y0, float x1, float y1, float r, uint32_t id, WebHold& hold) {
    Impl& m = *impl_;
    if (m.fade_ < 0.95f) return false;
    x0 -= m.ox_;
    x1 -= m.ox_;
    y0 -= m.oy_;
    y1 -= m.oy_;
    const float s = m.layout_.dpiScale;
    for (size_t i = 0; i < m.webs_.size(); ++i) {
        Web& w = m.webs_[i];
        if (x1 < w.left - r || x1 > w.right + r || y1 < w.top - r || y1 > w.bottom + r) continue;
        if (w.vis < 0.95f || w.held >= w.maxHeld) continue;
        const uint32_t h = Hash32(id ^ w.seed);
        if (HashFloat(h) >= kCatchShare) continue;  // this one blows through
        // How far in it gets before it sticks, as a share of the way out from the hub.
        const float deep = 0.35f + 0.5f * HashFloat(h ^ 0xA511E9B3u);
        const float u = w.corner ? static_cast<float>(m.layout_.width) - x1 : x1;
        const float du = u - w.hubU, dv = y1 - w.hubV;
        float ang = std::atan2(dv, du);
        if (ang < 0.0f) ang += kTau;
        const int bin = Clamp(static_cast<int>(ang / kTau * static_cast<float>(kSpanBins)), 0, kSpanBins - 1);
        if (du * du + dv * dv > deep * deep * w.span[bin] * w.span[bin]) continue;
        // The nearest knot with silk still tied to it (none near: a hole, and through it goes).
        int best = -1;
        float bestD = kCatchReach * s * kCatchReach * s;
        for (size_t k = 0; k < w.knots.size(); ++k) {
            const Knot& kn = w.knots[k];
            if (kn.anchor || kn.links == 0) continue;
            const float d2 = (kn.x - x1) * (kn.x - x1) + (kn.y - y1) * (kn.y - y1);
            if (d2 < bestD) {
                bestD = d2;
                best = static_cast<int>(k);
            }
        }
        if (best < 0) continue;
        Knot& kn = w.knots[static_cast<size_t>(best)];
        float dx = x1 - kn.x, dy = y1 - kn.y;
        const float d = std::sqrt(dx * dx + dy * dy), most = 0.5f * r;
        if (d > most) {
            dx *= most / d;
            dy *= most / d;
        }
        hold.web = static_cast<int>(i);
        hold.knot = best;
        hold.dx = dx;
        hold.dy = dy;
        hold.gen = ++m.stamp_;
        kn.heldAt = m.time_;
        ++w.held;
        // It lands with a little bump.
        kn.vx += 15.0f * (x1 - x0);
        kn.vy += 15.0f * (y1 - y0);
        return true;
    }
    return false;
}

bool Cobwebs::HoldPoint(const WebHold& hold, float& x, float& y) const {
    Impl& m = *impl_;
    if (hold.web < 0 || static_cast<size_t>(hold.web) >= m.webs_.size() || hold.gen <= m.strung_) return false;
    Web& w = m.webs_[static_cast<size_t>(hold.web)];
    if (hold.knot < 0 || static_cast<size_t>(hold.knot) >= w.knots.size() || m.Alpha(w) < 0.6f) return false;
    Knot& k = w.knots[static_cast<size_t>(hold.knot)];
    if (k.shaken >= hold.gen || k.links == 0) return false;
    x = k.x + hold.dx + m.ox_;
    y = k.y + hold.dy + m.oy_;
    k.heldAt = m.time_;  // (it weighs on the knot while it hangs there)
    return true;
}

void Cobwebs::ProbeCover(const std::function<bool(float, float)>& sceneCovered) {
    Impl& m = *impl_;
    auto covered = [&](float x, float y) { return sceneCovered(x + m.ox_, y + m.oy_); };
    m.coverStale_ = false;
    if (!m.shown_ && m.fade_ <= 0.0f) return;  // hidden webs aren't looked at (they are again when shown)
    const float s = m.layout_.dpiScale;
    for (Web& w : m.webs_) {
        // The hub and points along both edges and the frame, out to its ends.
        auto at = [&](float u, float v) { return covered(m.X(w, u), v); };
        const Knot& hub = w.knots[static_cast<size_t>(w.hub)];
        w.covered = covered(hub.x, hub.y) || at(0.5f * w.across, 3.0f * s) || at(0.92f * w.across, 3.0f * s) ||
                    at(3.0f * s, 0.5f * w.down) || at(3.0f * s, 0.92f * w.down) ||
                    at(0.45f * w.across, 0.45f * w.down) || at(4.0f * s, 4.0f * s);
        if (!w.probed) w.vis = w.covered ? 0.0f : 1.0f;
        w.probed = true;
    }
}

bool Cobwebs::CoverStale() const {
    const Impl& m = *impl_;
    if (!m.shown_ && m.fade_ <= 0.0f) return false;
    return m.coverStale_;
}

void Cobwebs::RecheckCover() {
    Impl& m = *impl_;
    m.coverStale_ = true;
    for (Web& w : m.webs_) w.probed = false;
}

bool Cobwebs::Prepare(ID2D1DeviceContext* dc) {
    Impl& m = *impl_;
    if (!Active() || !dc) return false;
    if (m.page_ && m.pageScale_ == m.layout_.dpiScale) return true;
    const double now = QpcSeconds();
    if (now < m.pageRetry_) return false;
    m.page_.Reset();
    if (FAILED(m.BuildPage(dc))) {
        m.page_.Reset();
        m.pageRetry_ = now + 1.0;
        return false;
    }
    m.pageScale_ = m.layout_.dpiScale;
    return true;
}

ID2D1Bitmap1* Cobwebs::Page() const { return impl_->page_.Get(); }

void Cobwebs::EmitWebs(std::vector<ScenerySprite>& out) const {
    const Impl& m = *impl_;
    if (!m.page_) return;
    const size_t first = out.size();
    const float s = m.layout_.dpiScale;
    // Slack silk sags, blown a little sideways by the wind.
    float gx = Clamp(m.wind_ / (180.0f * s), -0.7f, 0.7f), gy = 1.0f;
    {
        const float l = std::sqrt(gx * gx + gy * gy);
        gx /= l;
        gy /= l;
    }
    const float turn = static_cast<float>(std::fmod(m.time_ * 0.3, static_cast<double>(kTau)));
    for (const Web& w : m.webs_) {
        const float alpha = m.Alpha(w);
        if (alpha <= 0.003f) continue;
        const float lc = std::cos(2.0f * w.sheen), ls = std::sin(2.0f * w.sheen);
        for (const Thread& t : w.threads) {
            if (t.state == State::Torn) continue;
            const Knot& a = w.knots[t.a];
            const Knot& b = w.knots[t.b];
            if (t.state == State::Spinning) {
                // Only as far as she has spun it, taut behind her.
                const Knot& p = t.fromB ? b : a;
                const Knot& q = t.fromB ? a : b;
                m.DrawSilk(out, p.x, p.y, Lerp(p.x, q.x, t.progress), Lerp(p.y, q.y, t.progress), 0.0f,
                       kLooks[static_cast<int>(t.silk)], alpha, lc, ls, gx, gy);
                continue;
            }
            const Look look = kLooks[static_cast<int>(t.silk)];
            m.DrawSilk(out, a.x, a.y, b.x, b.y, t.restNow, look, alpha * t.alpha, lc, ls, gx, gy);
        }
        // Loose strands, smoothed: a quadratic B-spline through their points.
        for (const Strand& st : w.strands) {
            const Knot& k = w.knots[static_cast<size_t>(st.knot)];
            float px[kStrandPoints + 1], py[kStrandPoints + 1];
            px[0] = k.x;
            py[0] = k.y;
            for (int i = 0; i < kStrandPoints; ++i) {
                px[i + 1] = st.x[i];
                py[i + 1] = st.y[i];
            }
            const float a = alpha * st.alpha;
            float ax = px[0], ay = py[0];
            for (int i = 1; i < kStrandPoints; ++i) {
                // From the last midpoint round point i to the next (the curve's middle at c).
                const float mx = 0.5f * (px[i] + px[i + 1]), my = 0.5f * (py[i] + py[i + 1]);
                const float cx = 0.25f * ax + 0.5f * px[i] + 0.25f * mx, cy = 0.25f * ay + 0.5f * py[i] + 0.25f * my;
                m.DrawSilk(out, ax, ay, cx, cy, 0.0f, kStrandLook, a, lc, ls, gx, gy);
                m.DrawSilk(out, cx, cy, mx, my, 0.0f, kStrandLook, a, lc, ls, gx, gy);
                ax = mx;
                ay = my;
            }
            m.DrawSilk(out, ax, ay, px[kStrandPoints], py[kStrandPoints], 0.0f, kStrandLook, a, lc, ls, gx, gy);
        }
        // Dew, each drop catching the light now and then.
        for (const Dew& d : w.dew) {
            const Thread& t = w.threads[static_cast<size_t>(d.thread)];
            if (t.state != State::Whole && t.state != State::Mending) continue;
            const Knot& a = w.knots[t.a];
            const Knot& b = w.knots[t.b];
            float x = Lerp(a.x, b.x, d.u), y = Lerp(a.y, b.y, d.u);
            // On a slack thread it hangs where the thread sags.
            const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
            const float slack = t.restNow - len;
            if (slack > 0.004f * len && len > 0.3f) {
                const float sag = std::min(0.3f * len, std::sqrt(0.375f * len * slack)) * 4.0f * d.u * (1.0f - d.u);
                const float ux = dx / len, uy = dy / len;
                float nx = gx - (gx * ux + gy * uy) * ux, ny = gy - (gx * ux + gy * uy) * uy;
                const float nl = std::sqrt(nx * nx + ny * ny);
                if (nl > 1e-3f) {
                    x += nx / nl * sag;
                    y += ny / nl * sag;
                }
            }
            const float ph = Frac(m.time_ / static_cast<double>(d.period) + static_cast<double>(d.phase));
            const float glint = ph < kGlintShare ? std::sin(kPi * ph / kGlintShare) : 0.0f;
            const float a0 = alpha * t.alpha;
            out.push_back(Put(m.drop_, 2.0f * d.r / m.dropPx_, 0.0f, x, y, Color(kDew, a0 * (0.62f + 0.38f * glint))));
            if (glint > 0.02f) {
                const float size = (6.5f + 3.0f * d.r / s) * s * (0.5f + 0.5f * glint);
                out.push_back(Put(m.glint_, size / m.glintPx_, turn + d.phase * kTau, x, y,
                                  Color(kGlint, a0 * 0.9f * glint)));
            }
        }
        m.EmitSpider(w, alpha, out);
    }
    m.Shift(out, first);
}

void Cobwebs::EmitBats(std::vector<ScenerySprite>& out) const {
    const Impl& m = *impl_;
    if (!m.page_) return;
    const size_t first = out.size();
    for (const Bat& b : m.bats_) {
        if (b.alpha <= 0.003f) continue;
        const int f = b.glide > 0.0f ? 1 : static_cast<int>(Frac(static_cast<double>(b.flap)) * 4.0f) & 3;
        const float roll = 0.4f * std::atan(b.vy / std::max(std::fabs(b.vx), 1.0f)) * (b.vx >= 0.0f ? 1.0f : -1.0f);
        out.push_back(Put(m.bat_[f], b.size, roll, b.x, b.y, {1.0f, 1.0f, 1.0f, b.alpha}));
    }
    m.Shift(out, first);
}

void Cobwebs::ReleaseDeviceResources() {
    impl_->page_.Reset();
    impl_->pageScale_ = 0.0f;
}
