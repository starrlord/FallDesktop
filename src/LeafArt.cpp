#include "LeafArt.h"

#include "resource.h"

#include <array>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// Per-kind tables
// ---------------------------------------------------------------------------

constexpr LeafTraits kTraits[kLeafKindCount] = {
    // minDip maxDip rotates twinkles pixelArt tinted weight
    {3.0f, 6.0f, true, false, false, true, 1.4f},     // Fleck
    {38.0f, 70.0f, true, false, false, true, 0.12f},  // Blur
    {12.0f, 21.0f, true, false, false, true, 0.5f},   // Samara
    {9.0f, 13.0f, true, false, false, false, 0.3f},   // Acorn
    {9.0f, 16.0f, true, true, false, true, 1.0f},     // Aspen
    {16.0f, 34.0f, true, false, false, true, 1.1f},   // Maple
    {16.0f, 32.0f, true, false, false, true, 1.0f},   // Oak
    {10.0f, 20.0f, true, false, false, true, 1.0f},   // Birch
    {12.0f, 24.0f, true, false, false, true, 0.9f},   // Ginkgo
    {14.0f, 28.0f, true, false, false, true, 0.9f},   // Sweetgum
    {14.0f, 26.0f, true, false, false, true, 0.8f},   // Willow
    {14.0f, 28.0f, true, false, false, true, 0.9f},   // JapaneseMaple
    {15.0f, 15.0f, false, false, true, true, 1.0f},   // Pixel1..8
    {15.0f, 15.0f, false, false, true, true, 1.0f},
    {15.0f, 15.0f, false, false, true, true, 1.0f},
    {15.0f, 15.0f, false, false, true, true, 1.0f},
    {15.0f, 15.0f, false, false, true, true, 1.0f},
    {15.0f, 15.0f, false, false, true, true, 1.0f},
    {15.0f, 15.0f, false, false, true, true, 1.0f},
    {15.0f, 15.0f, false, false, true, true, 1.0f},
};

constexpr int kVariantCounts[kLeafKindCount] = {
    4, 4, 4, 4, 4,           // fleck, blur, samara, acorn, aspen
    4, 4, 4, 4, 4, 4, 4,     // maple, oak, birch, ginkgo, sweetgum, willow, japanese maple
    1, 1, 1, 1, 1, 1, 1, 1,  // pixel art
};

// Two-tone designs: a blush sprite over the leaf's heart takes the second colour.
constexpr bool kBlush[kLeafKindCount] = {
    false, false, true, false, true,           // fleck, blur, samara, acorn, aspen
    true, true, true, true, true, true, true,  // the tree leaves
    false, false, false, false, false, false, false, false,
};

// The largest atlas level drawn for each kind; the larger levels reuse it. Small
// kinds never need the biggest cells, and out-of-focus leaves hide any upscaling.
constexpr int kTopLevel[kLeafKindCount] = {
    4, 2, 1, 2, 1,         // fleck 32 px, blur 64, samara 90, acorn 64, aspen 90
    0, 0, 1, 0, 0, 0, 0,   // birch 90, the others 128
    0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr bool VariantsFit() {
    for (int n : kVariantCounts)
        if (n < 1 || n > kMaxVariants) return false;
    return true;
}
static_assert(VariantsFit(), "every kind needs 1..kMaxVariants variants");

// Sprite indices: every kind's base sprites in kind order (the pixel-art ones last
// among them), then the blush sprites of the two-tone kinds.
constexpr std::array<int, kLeafKindCount + 1> MakeSpriteOffsets() {
    std::array<int, kLeafKindCount + 1> o{};
    for (int k = 0; k < kLeafKindCount; ++k) o[k + 1] = o[k] + kVariantCounts[k];
    return o;
}
constexpr std::array<int, kLeafKindCount + 1> kSpriteOffset = MakeSpriteOffsets();
constexpr int kBaseTotal = kSpriteOffset[kLeafKindCount];

constexpr std::array<int, kLeafKindCount + 1> MakeBlushOffsets() {
    std::array<int, kLeafKindCount + 1> o{};
    o[0] = kBaseTotal;
    for (int k = 0; k < kLeafKindCount; ++k) o[k + 1] = o[k] + (kBlush[k] ? kVariantCounts[k] : 0);
    return o;
}
constexpr std::array<int, kLeafKindCount + 1> kBlushOffset = MakeBlushOffsets();
constexpr int kSpriteTotal = kBlushOffset[kLeafKindCount];

constexpr bool IsPixel(LeafKind k) { return k >= LeafKind::Pixel1 && k <= LeafKind::Pixel8; }

constexpr int kPixelCount = static_cast<int>(LeafKind::Pixel8) - static_cast<int>(LeafKind::Pixel1) + 1;
constexpr int kFirstPixelSprite = kSpriteOffset[static_cast<int>(LeafKind::Pixel1)];
static_assert(kPixelCount == 8 && IDR_PIXEL8 - IDR_PIXEL_FIRST == 7, "pixel-art resources out of sync");
static_assert(kFirstPixelSprite + kPixelCount == kBaseTotal, "pixel-art sprites must end the base sprites");

constexpr bool IsPixelSprite(int index) { return index >= kFirstPixelSprite && index < kBaseTotal; }

// What a sprite index shows.
struct SpriteRef {
    LeafKind kind = LeafKind::Fleck;
    int variant = 0;
    bool blush = false;
};
constexpr std::array<SpriteRef, kSpriteTotal> MakeSpriteRefs() {
    std::array<SpriteRef, kSpriteTotal> refs{};
    for (int k = 0; k < kLeafKindCount; ++k)
        for (int v = 0; v < kVariantCounts[k]; ++v) {
            refs[static_cast<size_t>(kSpriteOffset[k] + v)] = {static_cast<LeafKind>(k), v, false};
            if (kBlush[k]) refs[static_cast<size_t>(kBlushOffset[k] + v)] = {static_cast<LeafKind>(k), v, true};
        }
    return refs;
}
constexpr std::array<SpriteRef, kSpriteTotal> kSpriteRefs = MakeSpriteRefs();

// ---------------------------------------------------------------------------
// Halo and atlas tuning
// ---------------------------------------------------------------------------

constexpr float kHaloAlpha = 0.4f;  // peak opacity of the dark halo next to the leaf

// How far the halo reaches (device px) around a leaf of radius rpx.
float HaloPx(float rpx) { return Clamp(0.07f * rpx + 0.85f, 1.25f, 4.5f); }

// Atlas levels: the procedural leaf diameter shrinks by sqrt(2) per level, so a sprite
// is never minified by more than that (only the soft blushes, below 8 px). Pixel art
// uses whole-pixel scales.
constexpr int kLevelCount = 13;          // 128 px down to 2 px
constexpr float kTopDiameter = 128.0f;
constexpr int kCellPad = 3;              // per side: 1 px antialiasing + 2 px fully transparent border
constexpr int kClearBorder = 2;
constexpr UINT32 kPageWidth = 1024;
constexpr UINT32 kMaxPageHeight = 4096;
constexpr float kPixelSize = 15.0f;      // pixel-art leaves are 15 x 15
constexpr int kLastBlushLevel = 8;       // 8 px: smaller leaves draw that level's blush, scaled down

float LevelDiameter(int level) { return kTopDiameter * std::exp2(-0.5f * static_cast<float>(level)); }

// Device px per source pixel of a pixel-art leaf at a level: whole numbers when enlarging.
float PixelScale(int level) {
    float k = LevelDiameter(level) / kPixelSize;
    return k >= 1.0f ? std::floor(k) : k;
}

// The Gaussian blur effect (CLSID_D2D1GaussianBlur, here to spare linking dxguid).
constexpr CLSID kGaussianBlur = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};

// ---------------------------------------------------------------------------
// Geometry helpers. Designs are built in a unit-radius space with the tip of
// the leaf up and its stalk pointing down (+y); drawing scales by the radius.
// ---------------------------------------------------------------------------

struct Pt {
    float x, y;
};
constexpr Pt operator+(Pt a, Pt b) { return {a.x + b.x, a.y + b.y}; }
constexpr Pt operator-(Pt a, Pt b) { return {a.x - b.x, a.y - b.y}; }
constexpr Pt operator*(Pt a, float s) { return {a.x * s, a.y * s}; }
inline float Cross(Pt a, Pt b) { return a.x * b.y - a.y * b.x; }
inline float Len(Pt a) { return std::sqrt(a.x * a.x + a.y * a.y); }
inline Pt Unit(Pt a) {
    const float l = Len(a);
    return l > 1e-6f ? a * (1.0f / l) : Pt{0.0f, -1.0f};
}
// The right-hand side of a direction on screen (y down): up becomes right.
constexpr Pt Perp(Pt a) { return {-a.y, a.x}; }
// Direction at an angle from straight up, turning clockwise on screen: 0 up, kPi / 2 right.
inline Pt Dir(float a) { return {std::sin(a), -std::cos(a)}; }
inline Pt Mix(Pt a, Pt b, float t) { return a + (b - a) * t; }
inline Pt Rotate(Pt p, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}
constexpr float Deg(float d) { return d * kPi / 180.0f; }

using Poly = std::vector<Pt>;

// > 0 for outlines running clockwise on screen.
float SignedArea(const Poly& p) {
    float a = 0.0f;
    for (size_t i = 0; i < p.size(); ++i) a += Cross(p[i], p[(i + 1) % p.size()]);
    return 0.5f * a;
}

void Orient(Poly& p, bool clockwise) {
    if ((SignedArea(p) > 0.0f) != clockwise) std::reverse(p.begin(), p.end());
}

// Whether q lies inside the polygon (even-odd rule).
bool Inside(const Poly& p, Pt q) {
    bool in = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if ((p[i].y > q.y) != (p[j].y > q.y) &&
            q.x < p[j].x + (p[i].x - p[j].x) * (q.y - p[j].y) / (p[i].y - p[j].y))
            in = !in;
    return in;
}

// Quadratic Bezier from a through control c to b.
Poly Curve(Pt a, Pt c, Pt b, int n = 12) {
    Poly pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        pts.push_back(Mix(Mix(a, c, t), Mix(c, b, t), t));
    }
    return pts;
}

Poly Ellipse(Pt c, float rx, float ry, float angle = 0.0f, int n = 24) {
    Poly pts;
    pts.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float a = kTau * static_cast<float>(i) / static_cast<float>(n);
        pts.push_back(c + Rotate({rx * std::cos(a), ry * std::sin(a)}, angle));
    }
    return pts;
}

// A frame along an axis: t runs along it, u across it (to the right of the direction).
struct Frame {
    Pt o, d, n;
    Frame(Pt origin, float angle) : o(origin), d(Dir(angle)), n(Perp(Dir(angle))) {}
    Pt At(float t, float u = 0.0f) const { return o + d * t + n * u; }
};

// A midrib: quadratic curve from the base of the blade to its apex.
struct Midrib {
    Pt base, ctrl, apex;
    Pt At(float t) const { return Mix(Mix(base, ctrl, t), Mix(ctrl, apex, t), t); }
    Pt Tangent(float t) const { return Unit((ctrl - base) * (2.0f * (1.0f - t)) + (apex - ctrl) * (2.0f * t)); }
    Pt Right(float t) const { return Perp(Tangent(t)); }
};
Midrib MakeMidrib(Pt base, Pt apex, float bend) {
    return {base, Mix(base, apex, 0.5f) + Perp(Unit(apex - base)) * bend, apex};
}

// ---------------------------------------------------------------------------
// Outlines: nodes joined by smooth curves (centripetal Catmull-Rom) except at
// sharp nodes (lobe tips and big teeth), with optional fine teeth along the way.
// ---------------------------------------------------------------------------

struct Teeth {
    float pitch = 0.0f;  // spacing (0: an entire margin)
    float amp = 0.0f;    // height
    float round = 0.0f;  // 0 sharp serration .. 1 rounded (crenate) teeth
    int pattern = 0;     // n > 1: one big tooth, then n - 1 small ones (doubly serrate)
};

struct Node {
    Pt p{};
    bool sharp = false;
    Teeth teeth;        // along the stretch to the next node
    float lean = 0.0f;  // tooth tips lean towards the next node (+1) or back to this one (-1)
};

Node N(Pt p, bool sharp = false) {
    Node n;
    n.p = p;
    n.sharp = sharp;
    return n;
}
Node N(Pt p, bool sharp, const Teeth& t, float lean) {
    Node n = N(p, sharp);
    n.teeth = t;
    n.lean = lean;
    return n;
}

constexpr float kOutlineStep = 0.011f;  // spacing of traced points on entire margins

Pt CatmullRom(Pt p0, Pt p1, Pt p2, Pt p3, float u) {
    auto knot = [](Pt a, Pt b) { return std::max(std::sqrt(Len(b - a)), 1e-4f); };
    const float t1 = knot(p0, p1), t2 = t1 + knot(p1, p2), t3 = t2 + knot(p2, p3);
    const float t = Lerp(t1, t2, u);
    const Pt a1 = p0 * ((t1 - t) / t1) + p1 * (t / t1);
    const Pt a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
    const Pt a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
    const Pt b1 = a1 * ((t2 - t) / t2) + a2 * (t / t2);
    const Pt b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
    return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
}

// One tooth's height at phase phi (0..1) with its tip at `tip`.
float ToothShape(float phi, float tip, float round) {
    // Serrate: a straight long flank up to the tip, a hollow short one back down.
    const float saw = phi < tip ? phi / tip : std::pow((1.0f - phi) / (1.0f - tip), 1.7f);
    // Crenate: a lopsided bump peaking at the tip, notched between bumps.
    const float w = phi < tip ? 0.5f * phi / tip : 0.5f + 0.5f * (phi - tip) / (1.0f - tip);
    const float bump = std::sqrt(std::max(0.0f, std::sin(kPi * w)));
    return Lerp(saw, bump, round);
}

// Appends the stretch from p1 to p2 (p2 itself excluded). outward: +1 for clockwise outlines.
void TraceStretch(Poly& out, Pt p0, Pt p1, Pt p2, Pt p3, const Node& n, float outward) {
    constexpr int kDense = 48;
    std::array<Pt, kDense + 1> d{};
    std::array<float, kDense + 1> s{};
    d[0] = p1;
    for (int i = 1; i <= kDense; ++i) {
        d[static_cast<size_t>(i)] = CatmullRom(p0, p1, p2, p3, static_cast<float>(i) / kDense);
        s[static_cast<size_t>(i)] = s[static_cast<size_t>(i) - 1] +
                                    Len(d[static_cast<size_t>(i)] - d[static_cast<size_t>(i) - 1]);
    }
    const float total = s[kDense];
    if (total < 1e-5f) return;
    size_t seg = 0;
    // Point and unit tangent at arc length sv (called with non-decreasing sv).
    auto at = [&](float sv, Pt& pos, Pt& tan) {
        while (seg + 1 < kDense && s[seg + 1] < sv) ++seg;
        const float len = s[seg + 1] - s[seg];
        const float f = len > 1e-7f ? Saturate((sv - s[seg]) / len) : 0.0f;
        pos = Mix(d[seg], d[seg + 1], f);
        tan = Unit(d[seg + 1] - d[seg]);
    };
    Pt pos{}, tan{};
    const Teeth& t = n.teeth;
    if (t.pitch <= 0.0f || t.amp <= 0.0f) {
        const int steps = std::max(1, static_cast<int>(std::ceil(total / kOutlineStep)));
        for (int k = 0; k < steps; ++k) {
            at(total * static_cast<float>(k) / static_cast<float>(steps), pos, tan);
            out.push_back(pos);
        }
        return;
    }
    const int teeth = std::max(1, static_cast<int>(std::lround(total / t.pitch)));
    const float tip = 0.5f + 0.32f * n.lean;
    const float ramp = std::min(0.45f, 0.8f / static_cast<float>(teeth));  // teeth grow in from the ends
    // Even samples across each tooth, and one exactly on its tip to keep it crisp.
    constexpr int kPerTooth = 8;
    std::array<float, kPerTooth + 1> phis{};
    size_t samples = 0;
    for (int m = 0; m < kPerTooth; ++m) {
        const float phi = static_cast<float>(m) / kPerTooth;
        if (phi > tip && (samples == 0 || phis[samples - 1] < tip)) phis[samples++] = tip;
        phis[samples++] = phi;
    }
    if (phis[samples - 1] < tip) phis[samples++] = tip;
    for (int k = 0; k < teeth; ++k) {
        const float size = (t.pattern > 1 && k % t.pattern != 0) ? 0.45f : 1.0f;
        for (size_t m = 0; m < samples; ++m) {
            const float phi = phis[m];
            const float sv = total * (static_cast<float>(k) + phi) / static_cast<float>(teeth);
            at(sv, pos, tan);
            const float u = sv / total;
            const float env = SmoothStep(0.0f, ramp, u) * SmoothStep(0.0f, ramp, 1.0f - u);
            const float h = t.amp * size * env * ToothShape(phi, tip, t.round);
            out.push_back(pos - Perp(tan) * (h * outward));
        }
    }
}

// A closed outline through the nodes.
Poly Trace(const std::vector<Node>& nodes) {
    Poly out;
    const size_t n = nodes.size();
    if (n < 3) return out;
    float area = 0.0f;
    for (size_t i = 0; i < n; ++i) area += Cross(nodes[i].p, nodes[(i + 1) % n].p);
    const float outward = area >= 0.0f ? 1.0f : -1.0f;
    for (size_t i = 0; i < n; ++i) {
        const Node& a = nodes[i];
        const Node& b = nodes[(i + 1) % n];
        const Pt p0 = a.sharp ? a.p * 2.0f - b.p : nodes[(i + n - 1) % n].p;
        const Pt p3 = b.sharp ? b.p * 2.0f - a.p : nodes[(i + 2) % n].p;
        TraceStretch(out, p0, a.p, b.p, p3, a, outward);
    }
    return out;
}

// A band along a polyline, w0 wide at its start tapering to w1 at its end.
Poly Ribbon(const Poly& line, float w0, float w1) {
    const size_t n = line.size();
    Poly band;
    if (n < 2) return band;
    std::vector<float> s(n, 0.0f);
    for (size_t i = 1; i < n; ++i) s[i] = s[i - 1] + Len(line[i] - line[i - 1]);
    const float total = std::max(s[n - 1], 1e-6f);
    band.resize(2 * n);
    for (size_t i = 0; i < n; ++i) {
        const Pt tan = Unit(line[std::min(i + 1, n - 1)] - line[i > 0 ? i - 1 : 0]);
        const float hw = 0.5f * Lerp(w0, w1, s[i] / total);
        band[i] = line[i] - Perp(tan) * hw;
        band[2 * n - 1 - i] = line[i] + Perp(tan) * hw;
    }
    return band;
}

// A bite out of the margin (an insect's, or a tear): the stretch of outline inside the circle
// gives way to the circle's own ragged arc through the leaf.
void Bite(Poly& outline, Pt c, float radius, Rng& rng) {
    const size_t n = outline.size();
    if (n < 3) return;
    auto in = [&](size_t i) { return Len(outline[i % n] - c) < radius; };
    size_t start = n;  // the first point inside after one outside
    for (size_t i = 0; i < n && start == n; ++i)
        if (!in(i) && in(i + 1)) start = (i + 1) % n;
    if (start == n) return;  // nothing inside, or everything
    size_t count = 0;
    while (count < n && in(start + count)) ++count;
    // Where the outline crosses the circle, going in and coming out.
    auto cross = [&](Pt outside, Pt inside) {
        float lo = 0.0f, hi = 1.0f;
        for (int k = 0; k < 12; ++k) {
            const float mid = 0.5f * (lo + hi);
            (Len(Mix(outside, inside, mid) - c) < radius ? hi : lo) = mid;
        }
        return Mix(outside, inside, hi);
    };
    const Pt entry = cross(outline[(start + n - 1) % n], outline[start]);
    const Pt exit = cross(outline[(start + count) % n], outline[(start + count - 1) % n]);
    // Of the two arcs between them, the bite follows the one through the leaf.
    Pt centroid{0.0f, 0.0f};
    for (Pt q : outline) centroid = centroid + q * (1.0f / static_cast<float>(n));
    const float a0 = std::atan2(entry.y - c.y, entry.x - c.x);
    float sweep = std::atan2(exit.y - c.y, exit.x - c.x) - a0;
    sweep -= kTau * std::floor(sweep / kTau);  // 0..tau, turning one way
    const Pt midA = c + Pt{std::cos(a0 + 0.5f * sweep), std::sin(a0 + 0.5f * sweep)} * radius;
    const Pt midB = c + Pt{std::cos(a0 + 0.5f * sweep - kPi), std::sin(a0 + 0.5f * sweep - kPi)} * radius;
    if (Len(midB - centroid) < Len(midA - centroid)) sweep -= kTau;  // the other way round
    const int steps = std::max(4, static_cast<int>(std::fabs(sweep) * radius / 0.012f));
    const float wobble = rng.Range(0.0f, kTau);
    Poly arc = {entry};
    for (int k = 1; k < steps; ++k) {
        const float u = static_cast<float>(k) / static_cast<float>(steps);
        const float a = a0 + sweep * u;
        const float rr = radius * (1.0f + 0.06f * std::sin(9.0f * u * kPi + wobble) * std::sin(kPi * u));
        arc.push_back(c + Pt{std::cos(a), std::sin(a)} * rr);
    }
    arc.push_back(exit);
    Poly out;
    out.reserve(n - count + arc.size());
    for (size_t i = 0; i < n - count; ++i) out.push_back(outline[(start + count + i) % n]);
    out.insert(out.end(), arc.begin(), arc.end());
    outline = std::move(out);
}

// A nibbled hole, counterclockwise so that it cuts through the blade.
Poly Hole(Pt c, float radius, Rng& rng) {
    Poly h;
    const int n = rng.Int(9, 13);
    for (int i = 0; i < n; ++i) {
        const float a = kTau * (static_cast<float>(i) + rng.Range(-0.25f, 0.25f)) / static_cast<float>(n);
        h.push_back(c + Pt{std::cos(a), std::sin(a) * 0.85f} * (radius * rng.Range(0.7f, 1.1f)));
    }
    Orient(h, false);
    return h;
}

// ---------------------------------------------------------------------------
// Leaf plans: everything a design draws, in unit space, independent of the
// drawn size (so a leaf looks the same at every atlas level; only the finest
// detail drops out when small).
// ---------------------------------------------------------------------------

struct Vein {
    Poly line;
    float w0, w1;  // width at its start and its end
    int order;     // 0 main, 1 secondary, 2 fine
};

struct Spot {
    Pt c;
    float rx, ry;
    float alpha;  // darkening; negative lightens
};

struct LeafPlan {
    std::vector<Poly> blade;   // clockwise outlines (a pair of keys has two)
    std::vector<Poly> holes;   // counterclockwise
    std::vector<Poly> stalks;  // centre lines, from the blade outwards
    float stalkW0 = 0.0f, stalkW1 = 0.0f;
    float stalkLum = 0.58f;
    std::vector<Vein> veins;
    std::vector<Poly> facets;  // the shaded side of folds along the main veins
    float facetAlpha = 0.07f;
    std::vector<Poly> shades;  // darker parts inside the blade (a samara's seed)
    float shadeAlpha = 0.0f;
    std::vector<Spot> spots;
    float lum = 0.9f;          // the blade's grey
    Pt light{};                // centre of the gentle highlight
    float lightR = 1.0f;
    float edgeAlpha = 0.2f;
    // Blush: the heart region, softened by a blur, strongest at heartC and fading over heartR.
    std::vector<Poly> heart;
    Pt heartC{};
    float heartR = 1.0f, heartSigma = 0.08f;
    bool blurred = false;      // an out-of-focus leaf
    float blurSigma = 0.0f;
};

void Map(LeafPlan& p, Pt centre, float k) {
    auto f = [&](Pt& q) { q = (q - centre) * k; };
    auto all = [&](std::vector<Poly>& polys) {
        for (Poly& poly : polys)
            for (Pt& q : poly) f(q);
    };
    all(p.blade);
    all(p.holes);
    all(p.stalks);
    all(p.facets);
    all(p.shades);
    all(p.heart);
    for (Vein& v : p.veins) {
        for (Pt& q : v.line) f(q);
        v.w0 *= k;
        v.w1 *= k;
    }
    for (Spot& s : p.spots) {
        f(s.c);
        s.rx *= k;
        s.ry *= k;
    }
    p.stalkW0 *= k;
    p.stalkW1 *= k;
    f(p.light);
    p.lightR *= k;
    f(p.heartC);
    p.heartR *= k;
    p.heartSigma *= k;
    p.blurSigma *= k;
}

// Squeezes one side of the leaf (x > 0 for side +1), as if it curled away.
void Curl(LeafPlan& p, float side, float k) {
    auto f = [&](Pt& q) {
        if (q.x * side > 0.0f) q.x *= k;
    };
    for (auto* polys : {&p.blade, &p.holes, &p.stalks, &p.facets, &p.shades, &p.heart})
        for (Poly& poly : *polys)
            for (Pt& q : poly) f(q);
    for (Vein& v : p.veins)
        for (Pt& q : v.line) f(q);
    for (Spot& s : p.spots) f(s.c);
    f(p.light);
    f(p.heartC);
}

// Centres the design on `pivot` (or on its bounds) and scales it to reach `fit` of the radius.
void Fit(LeafPlan& p, float fit = 0.96f, const Pt* pivot = nullptr) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    auto grow = [&](Pt q, float pad) {
        x0 = std::min(x0, q.x - pad);
        y0 = std::min(y0, q.y - pad);
        x1 = std::max(x1, q.x + pad);
        y1 = std::max(y1, q.y + pad);
    };
    for (const Poly& poly : p.blade)
        for (Pt q : poly) grow(q, 0.0f);
    for (const Poly& poly : p.stalks)
        for (Pt q : poly) grow(q, 0.5f * std::max(p.stalkW0, p.stalkW1));
    const Pt c = pivot ? *pivot : Pt{0.5f * (x0 + x1), 0.5f * (y0 + y1)};
    float reach = 1e-3f;
    auto extend = [&](Pt q, float pad) { reach = std::max(reach, Len(q - c) + pad); };
    for (const Poly& poly : p.blade)
        for (Pt q : poly) extend(q, 0.0f);
    for (const Poly& poly : p.stalks)
        for (Pt q : poly) extend(q, 0.5f * std::max(p.stalkW0, p.stalkW1));
    Map(p, c, fit / reach);
}

// The side of a fold away from the light (from the upper left) is the shaded one.
constexpr Pt kLight = {-0.6f, -0.8f};

// ---------------------------------------------------------------------------
// Palmate leaves: lobes radiating from the junction with the stalk.
// ---------------------------------------------------------------------------

struct FlankPt {
    float t, u;          // along and across the lobe, in lobe lengths (u >= 0: mirrored on the left flank)
    bool sharp = false;  // a big tooth
    bool vein = false;   // a vein runs out to it
    bool bristle = false;
};

struct Lobe {
    float angle, len;
    std::vector<FlankPt> left, right;  // root to tip, the tip itself excluded
    bool roundTip = false;
    bool bristle = false;              // a bristle on the tip
};

constexpr float kBristle = 0.035f;

// Appends a sharp node, with a fine bristle when asked (red oak).
void Point(std::vector<Node>& nodes, Pt p, Pt outward, bool bristle, const Teeth& t, float lean) {
    if (!bristle) {
        nodes.push_back(N(p, true, t, lean));
        return;
    }
    const Pt side = Perp(outward) * 0.006f;
    nodes.push_back(N(p - side, true));
    nodes.push_back(N(p + outward * kBristle, true));
    nodes.push_back(N(p + side, true, t, lean));
}

struct VeinTip {
    Pt p;
    size_t lobe;
    float t;
};

// Appends one lobe (left flank root to tip, the tip, right flank back to the root) in its frame.
void EmitLobe(std::vector<Node>& nodes, std::vector<VeinTip>& tips, const Frame& f, const Lobe& l, size_t index,
              const Teeth& teeth) {
    for (const FlankPt& q : l.left) {
        const Pt p = f.At(q.t * l.len, -q.u * l.len);
        if (q.sharp) Point(nodes, p, Unit(p - f.At(q.t * l.len * 0.8f)), q.bristle, teeth, 1.0f);
        else nodes.push_back(N(p, false, teeth, 1.0f));
        if (q.vein) tips.push_back({p, index, q.t});
    }
    const Pt tip = f.At(l.len);
    if (l.roundTip) nodes.push_back(N(tip, false, teeth, -1.0f));
    else Point(nodes, tip, f.d, l.bristle, teeth, -1.0f);
    for (auto it = l.right.rbegin(); it != l.right.rend(); ++it) {
        const Pt p = f.At(it->t * l.len, it->u * l.len);
        if (it->sharp) Point(nodes, p, Unit(p - f.At(it->t * l.len * 0.8f)), it->bristle, teeth, -1.0f);
        else nodes.push_back(N(p, false, teeth, -1.0f));
        if (it->vein) tips.push_back({p, index, it->t});
    }
    nodes.back().teeth = {};  // the way into the sinus is entire
}

struct Palmate {
    Pt j{0.0f, 0.0f};          // where the stalk joins
    std::vector<Lobe> lobes;   // clockwise (increasing angle)
    std::vector<float> sinus;  // depth of the sinus after each lobe but the last
    Teeth teeth;               // fine teeth along the flanks
    float veinW = 0.05f;       // main veins' width at the junction
    float heartFinger = 0.5f;  // how far up the main veins the blush reaches
};

// Keeps a flank inside its share of the angle to the neighbouring lobe, so that neighbours never cross.
void FitFlank(std::vector<FlankPt>& flank, float halfGap) {
    const float slope = std::tan(0.8f * halfGap);
    for (FlankPt& q : flank) q.u = std::min(q.u, q.t * slope);
}

// The distance from the junction of a lobe's innermost flank point.
float RootReach(const Lobe& l, const std::vector<FlankPt>& flank) {
    if (flank.empty()) return l.len;
    return l.len * std::sqrt(flank.front().t * flank.front().t + flank.front().u * flank.front().u);
}

void BuildPalmate(LeafPlan& plan, Palmate s) {
    std::vector<Node> nodes;
    std::vector<VeinTip> tips;
    nodes.push_back(N(s.j, true));
    const size_t n = s.lobes.size();
    for (size_t i = 0; i < n; ++i) {
        Lobe& l = s.lobes[i];
        FitFlank(l.left, i > 0 ? 0.5f * (l.angle - s.lobes[i - 1].angle) : kPi / 2.0f);
        FitFlank(l.right, i + 1 < n ? 0.5f * (s.lobes[i + 1].angle - l.angle) : kPi / 2.0f);
    }
    for (size_t i = 0; i < n; ++i) {
        const Lobe& l = s.lobes[i];
        EmitLobe(nodes, tips, Frame(s.j, l.angle), l, i, s.teeth);
        if (i + 1 < n) {
            // The sinus stays nearer the junction than the flanks either side of it.
            const Lobe& next = s.lobes[i + 1];
            const float depth = std::min(s.sinus[i], 0.85f * std::min(RootReach(l, l.right), RootReach(next, next.left)));
            nodes.push_back(N(s.j + Dir(0.5f * (l.angle + next.angle)) * depth));
        }
    }
    plan.blade.push_back(Trace(nodes));

    // Main veins to the lobe tips; secondary ones out to the big teeth; fine ones between.
    for (size_t i = 0; i < n; ++i) {
        const Lobe& l = s.lobes[i];
        const Frame f(s.j, l.angle);
        const float w = s.veinW * std::sqrt(l.len);
        plan.veins.push_back({Curve(s.j, f.At(0.5f * l.len, 0.015f * l.len), f.At(0.95f * l.len)), w, 0.15f * w, 0});
        for (float t : {0.32f, 0.5f, 0.68f, 0.84f})
            for (float side : {-1.0f, 1.0f}) {
                const Pt a = f.At(t * l.len);
                const Pt b = f.At((t + 0.13f) * l.len, side * 0.2f * l.len);
                plan.veins.push_back({Curve(a, Mix(a, b, 0.5f) - Perp(f.d) * (side * 0.01f), b), 0.012f, 0.004f, 2});
            }
        // The side of the lobe's fold away from the light.
        const float side = Cross(f.d, kLight) > 0.0f ? 1.0f : -1.0f;  // > 0: the light is on the right
        const float other = side > 0.0f ? (i + 1 < n ? s.lobes[i + 1].angle : l.angle + Deg(50.0f))
                                        : (i > 0 ? s.lobes[i - 1].angle : l.angle - Deg(50.0f));
        plan.facets.push_back({s.j, f.At(0.5f * l.len, 0.015f * l.len), f.At(1.4f * l.len),
                               s.j + Dir(0.5f * (l.angle + other)) * 2.0f});
    }
    for (const VeinTip& tip : tips) {
        const Lobe& l = s.lobes[tip.lobe];
        const Frame f(s.j, l.angle);
        const Pt a = f.At(tip.t * 0.55f * l.len);
        const Pt b = Mix(a, tip.p, 0.93f);
        plan.veins.push_back({Curve(a, Mix(a, b, 0.5f) + f.d * 0.02f, b), 0.026f, 0.007f, 1});
    }

    // The heart: around the junction and a little way up each main vein.
    Poly disc = Ellipse(s.j + Pt{0.0f, -0.06f}, 0.2f, 0.17f);
    plan.heart.push_back(disc);
    for (const Lobe& l : s.lobes) {
        const Frame f(s.j, l.angle);
        const float reach = s.heartFinger * l.len;
        plan.heart.push_back({f.At(0.0f, -0.09f), f.At(0.6f * reach, -0.075f), f.At(reach), f.At(0.6f * reach, 0.075f),
                              f.At(0.0f, 0.09f)});
    }
    plan.heartC = s.j + Pt{0.0f, -0.1f};
    plan.heartR = 0.95f;
    plan.heartSigma = 0.075f;
    plan.light = s.j + Pt{-0.1f, -0.45f};
    plan.lightR = 1.1f;
}

// A stalk from `from`, heading `angle`, `len` long, bending by `bend`.
void AddStalk(LeafPlan& plan, Pt from, float angle, float len, float bend, float w0, float w1) {
    const Frame f(from, angle);
    plan.stalks.push_back(Curve(f.At(-0.02f), f.At(0.5f * len, bend * len), f.At(len), 10));
    plan.stalkW0 = w0;
    plan.stalkW1 = w1;
}

// ---------------------------------------------------------------------------
// The designs
// ---------------------------------------------------------------------------

// Sugar maple: five pointed lobes (two of them small, at the base), each with a few big
// teeth, between rounded sinuses; three main veins from the stalk. Variant 2 is curled
// on one side, variant 3 is an old leaf with a torn tip and tar spots.
LeafPlan PlanMaple(int variant, Rng& rng) {
    LeafPlan plan;
    auto vary = [&](float v) { return v * rng.Range(0.94f, 1.06f); };
    const float spread = Deg(variant == 1 ? 62.0f : 56.0f) + rng.Range(-0.05f, 0.05f);
    const float sideLen = vary(variant == 1 ? 0.92f : 0.85f);
    auto inner = [&](float k) {  // flank facing the central lobe, root to tip
        return std::vector<FlankPt>{{0.42f, 0.15f},
                                    {0.58f, 0.165f},
                                    {0.7f * k, 0.27f * k, true, true},
                                    {0.75f, 0.14f},
                                    {0.88f, 0.075f}};
    };
    auto outer = [&](float k) {  // flank facing the base
        return std::vector<FlankPt>{{0.4f, 0.17f},
                                    {0.5f, 0.2f},
                                    {0.6f * k, 0.34f * k, true, true},
                                    {0.67f, 0.19f},
                                    {0.8f, 0.14f, true},
                                    {0.87f, 0.075f}};
    };
    const std::vector<FlankPt> central = {{0.38f, 0.16f},
                                          {0.5f, 0.17f},
                                          {0.62f, 0.3f, true, true},
                                          {0.67f, 0.16f},
                                          {0.8f, 0.12f, true},
                                          {0.86f, 0.065f}};
    const std::vector<FlankPt> basalOut = {{0.42f, 0.26f}, {0.62f, 0.27f, true}, {0.78f, 0.12f}};
    const std::vector<FlankPt> basalIn = {{0.5f, 0.2f}, {0.8f, 0.1f}};
    Palmate s;
    const float basal = Deg(118.0f) + rng.Range(-0.08f, 0.08f);
    const float basalLen = vary(0.42f);
    s.lobes = {
        {-basal, basalLen, basalOut, basalIn},
        {-spread, sideLen, outer(vary(1.0f)), inner(vary(1.0f))},
        {rng.Range(-0.04f, 0.04f), 1.0f, central, central},
        {spread, sideLen * rng.Range(0.94f, 1.04f), inner(vary(1.0f)), outer(vary(1.0f))},
        {basal + rng.Range(-0.06f, 0.06f), basalLen * rng.Range(0.9f, 1.1f), basalIn, basalOut},
    };
    s.sinus = {vary(0.24f), vary(0.35f), vary(0.35f), vary(0.24f)};
    BuildPalmate(plan, s);
    AddStalk(plan, s.j, kPi + rng.Range(-0.15f, 0.15f), vary(0.62f), rng.Range(-0.12f, 0.12f), 0.042f, 0.05f);
    plan.lum = 0.9f;
    if (variant == 2) Curl(plan, rng.Chance(0.5f) ? 1.0f : -1.0f, 0.78f);
    if (variant == 3) {
        // An old leaf: a corner of a lobe torn away, tar spots, a darker dry patch.
        Bite(plan.blade[0], Frame(s.j, spread).At(sideLen * 1.02f, 0.05f), 0.16f, rng);
        plan.spots.push_back({Frame(s.j, -0.4f).At(0.45f), 0.045f, 0.04f, 0.55f});
        plan.spots.push_back({Frame(s.j, 0.25f).At(0.62f), 0.03f, 0.028f, 0.5f});
        plan.spots.push_back({Frame(s.j, -1.0f).At(0.7f), 0.2f, 0.16f, 0.16f});
        plan.lum = 0.86f;
    }
    Fit(plan);
    return plan;
}

// Japanese maple: seven pointed lobes spread like a hand round a small palm, each widest in
// the middle and drawn out to a fine tip, finely doubly serrate. Variant 2 is its cousin the
// full-moon maple: nine short lobes round a broad palm.
LeafPlan PlanJapaneseMaple(int variant, Rng& rng) {
    LeafPlan plan;
    const bool moon = variant == 2;
    const int count = moon ? 9 : 7;
    const float fan = Deg(moon ? 142.0f : 130.0f);  // angle of the lowest lobes
    const float slender = variant == 1 ? 0.85f : 1.0f;
    Palmate s;
    for (int i = 0; i < count; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(count - 1) * 2.0f - 1.0f;  // -1..1
        const float a = u * fan * (1.0f - 0.06f * (1.0f - std::fabs(u))) + rng.Range(-0.05f, 0.05f);
        const float len = (1.0f - (moon ? 0.3f : 0.45f) * std::pow(std::fabs(u), 2.0f)) * rng.Range(0.94f, 1.04f);
        const float w = slender * rng.Range(0.95f, 1.08f);
        const std::vector<FlankPt> flank =
            moon ? std::vector<FlankPt>{{0.56f, 0.14f * w}, {0.72f, 0.13f * w}, {0.85f, 0.09f * w}, {0.94f, 0.04f * w}}
                 : std::vector<FlankPt>{{0.42f, 0.14f * w}, {0.6f, 0.175f * w}, {0.77f, 0.125f * w}, {0.9f, 0.055f * w}};
        s.lobes.push_back({a, len, flank, flank});
    }
    for (int i = 0; i + 1 < count; ++i)
        s.sinus.push_back(rng.Range(0.34f, 0.4f) * (variant == 1 ? 0.9f : moon ? 1.4f : 1.0f));
    s.teeth = {0.038f, 0.011f, 0.1f, 2};
    s.veinW = 0.04f;
    s.heartFinger = 0.5f;
    BuildPalmate(plan, s);
    AddStalk(plan, s.j, kPi + rng.Range(-0.2f, 0.2f), rng.Range(0.5f, 0.62f), rng.Range(-0.15f, 0.15f), 0.03f, 0.038f);
    plan.lum = 0.9f;
    if (variant == 3) Curl(plan, rng.Chance(0.5f) ? 1.0f : -1.0f, 0.8f);
    Fit(plan);
    return plan;
}

// Sweetgum: a five (or seven) pointed star of triangular, finely toothed lobes.
LeafPlan PlanSweetgum(int variant, Rng& rng) {
    LeafPlan plan;
    const bool seven = variant == 2;
    const float slender = variant == 1 ? 0.8f : 1.0f;
    std::vector<float> angles = seven ? std::vector<float>{-138.0f, -98.0f, -50.0f, 0.0f, 50.0f, 98.0f, 138.0f}
                                      : std::vector<float>{-122.0f, -66.0f, 0.0f, 66.0f, 122.0f};
    std::vector<float> lens = seven ? std::vector<float>{0.5f, 0.82f, 0.95f, 1.0f, 0.95f, 0.82f, 0.5f}
                                    : std::vector<float>{0.78f, 0.95f, 1.0f, 0.95f, 0.78f};
    Palmate s;
    for (size_t i = 0; i < angles.size(); ++i) {
        const float w = slender * rng.Range(0.94f, 1.06f);
        const std::vector<FlankPt> flank = {
            {0.38f, 0.25f * w}, {0.55f, 0.2f * w}, {0.74f, 0.13f * w}, {0.9f, 0.055f * w}};
        s.lobes.push_back({Deg(angles[i]) + rng.Range(-0.05f, 0.05f), lens[i] * rng.Range(0.94f, 1.05f), flank, flank});
    }
    for (size_t i = 0; i + 1 < angles.size(); ++i) s.sinus.push_back(rng.Range(0.3f, 0.36f) * (seven ? 0.9f : 1.0f));
    s.teeth = {0.04f, 0.009f, 0.35f, 0};
    s.veinW = 0.045f;
    s.heartFinger = 0.45f;
    BuildPalmate(plan, s);
    AddStalk(plan, s.j, kPi + rng.Range(-0.15f, 0.15f), rng.Range(0.55f, 0.68f), rng.Range(-0.12f, 0.12f), 0.035f,
             0.045f);
    plan.lum = 0.91f;
    if (variant == 3) Curl(plan, rng.Chance(0.5f) ? 1.0f : -1.0f, 0.8f);
    Fit(plan);
    return plan;
}

// Oak: round-lobed white oak (variants 0 and 1, an insect has been at the second), red oak
// (2) and pin oak (3) with bristle-tipped lobes. Widest above the middle, lobes reaching
// forward, a wedge-shaped base on a short stalk.
LeafPlan PlanOak(int variant, Rng& rng) {
    LeafPlan plan;
    const bool red = variant >= 2;
    const Pt j{0.0f, 0.6f};
    const Midrib m = MakeMidrib(j, {rng.Range(-0.04f, 0.04f), -0.95f}, rng.Range(-0.07f, 0.07f));
    const float length = Len(m.apex - m.base);
    struct Spec {
        float t, angle, len;  // where on the midrib, degrees from straight ahead, length (midrib lengths)
    };
    std::vector<Spec> lobes;  // base to apex
    switch (variant) {
    case 0: lobes = {{0.12f, 82.0f, 0.16f}, {0.32f, 66.0f, 0.33f}, {0.53f, 54.0f, 0.38f}, {0.74f, 42.0f, 0.3f}}; break;
    case 1:
        lobes = {{0.1f, 84.0f, 0.13f}, {0.27f, 70.0f, 0.27f}, {0.44f, 60.0f, 0.34f}, {0.61f, 50.0f, 0.34f},
                 {0.78f, 40.0f, 0.25f}};
        break;
    case 2: lobes = {{0.16f, 76.0f, 0.27f}, {0.4f, 64.0f, 0.44f}, {0.62f, 55.0f, 0.39f}}; break;
    default: lobes = {{0.16f, 74.0f, 0.3f}, {0.4f, 62.0f, 0.47f}, {0.62f, 53.0f, 0.41f}}; break;  // pin oak
    }
    const float sinusDepth = (red ? (variant == 3 ? 0.05f : 0.085f) : variant == 0 ? 0.075f : 0.12f) * length;
    // Lobe profiles in lobe lengths, root to tip: white oak's rounded fingers, red oak's
    // broad lobes ending in bristle-tipped points (narrower on the pin oak).
    const float w = red ? (variant == 3 ? 0.27f : 0.34f) : 0.34f;
    const std::vector<FlankPt> round = {{0.42f, 0.85f * w}, {0.76f, 1.0f * w}};
    const std::vector<FlankPt> redUpper = {{0.45f, 0.5f * w}, {0.74f, 1.05f * w, true, true, true}, {0.8f, 0.42f * w}};
    const std::vector<FlankPt> redLower = {{0.5f, 0.55f * w}, {0.8f, 1.0f * w, true, true, true}, {0.87f, 0.35f * w}};
    const Teeth none;
    std::vector<Node> nodes;
    std::vector<VeinTip> tips;
    std::vector<std::pair<Frame, float>> frames;  // each lobe's frame and length, for its veins
    auto emit = [&](const Spec& sp, float side) {
        const float t = sp.t + rng.Range(-0.015f, 0.015f);
        const Pt tan = m.Tangent(t);
        const float a = std::atan2(tan.x, -tan.y) + side * Deg(sp.angle + rng.Range(-4.0f, 4.0f));
        Lobe l{a, sp.len * length * rng.Range(0.92f, 1.08f), {}, {}};
        if (red) {
            l.left = side > 0.0f ? redUpper : redLower;  // the left flank faces the apex on the right side
            l.right = side > 0.0f ? redLower : redUpper;
            l.bristle = true;
        } else {
            l.left = l.right = round;
            l.roundTip = true;
        }
        const Frame f(m.At(t), a);
        EmitLobe(nodes, tips, f, l, frames.size(), none);
        frames.push_back({f, l.len});
    };
    auto sinus = [&](float t, float side) {
        nodes.push_back(N(m.At(t) + m.Right(t) * (side * sinusDepth * rng.Range(0.8f, 1.2f))));
    };
    // The terminal lobe, then down the right side, round the base and up the left side.
    const float tEnd = 0.86f;  // where the terminal lobe begins
    if (red) {
        const float tw = 0.1f * length;
        nodes.push_back(N(m.At(tEnd) - m.Right(tEnd) * (0.55f * tw)));
        Point(nodes, m.At(0.91f) - m.Right(0.91f) * (1.0f * tw), Unit(m.Tangent(0.9f) - m.Right(0.9f)), true, none, 0.0f);
        nodes.push_back(N(m.At(0.93f) - m.Right(0.93f) * (0.42f * tw)));
        Point(nodes, m.apex, m.Tangent(1.0f), true, none, 0.0f);
        nodes.push_back(N(m.At(0.93f) + m.Right(0.93f) * (0.42f * tw)));
        Point(nodes, m.At(0.91f) + m.Right(0.91f) * (1.0f * tw), Unit(m.Tangent(0.9f) + m.Right(0.9f)), true, none, 0.0f);
        nodes.push_back(N(m.At(tEnd) + m.Right(tEnd) * (0.55f * tw)));
    } else {
        const float tw = 0.11f * length;
        nodes.push_back(N(m.At(tEnd) - m.Right(tEnd) * (0.75f * tw)));
        nodes.push_back(N(m.At(0.94f) - m.Right(0.94f) * (0.85f * tw)));
        nodes.push_back(N(m.apex + m.Tangent(1.0f) * 0.01f));
        nodes.push_back(N(m.At(0.94f) + m.Right(0.94f) * (0.85f * tw)));
        nodes.push_back(N(m.At(tEnd) + m.Right(tEnd) * (0.75f * tw)));
    }
    const size_t count = lobes.size();
    for (size_t k = count; k-- > 0;) {
        const float above = k + 1 < count ? lobes[k + 1].t : tEnd;
        sinus(Lerp(lobes[k].t, above, 0.55f), 1.0f);
        emit(lobes[k], 1.0f);
    }
    nodes.push_back(N(m.At(0.04f) + m.Right(0.04f) * (0.035f * length)));
    nodes.push_back(N(j, true));
    nodes.push_back(N(m.At(0.04f) - m.Right(0.04f) * (0.035f * length)));
    for (size_t k = 0; k < count; ++k) {
        emit(lobes[k], -1.0f);
        const float above = k + 1 < count ? lobes[k + 1].t : tEnd;
        sinus(Lerp(lobes[k].t, above, 0.55f), -1.0f);
    }
    plan.blade.push_back(Trace(nodes));

    // Veins: the midrib, one out to each lobe, red oak's to its bristle teeth, fine ones between.
    Poly mid;
    for (int i = 0; i <= 14; ++i) mid.push_back(m.At(0.97f * static_cast<float>(i) / 14.0f));
    plan.veins.push_back({mid, 0.055f, 0.012f, 0});
    for (const auto& [f, len] : frames) {
        const Pt end = f.At(len * (red ? 0.95f : 0.84f));
        plan.veins.push_back({Curve(f.o, Mix(f.o, end, 0.5f) + f.n * 0.01f, end), 0.03f, 0.008f, 1});
        for (float t : {0.42f, 0.66f})
            for (float side : {-1.0f, 1.0f}) {
                const Pt a = f.At(t * len);
                const Pt b = f.At((t + 0.16f) * len, side * 0.26f * len);
                plan.veins.push_back({Curve(a, Mix(a, b, 0.5f), b), 0.011f, 0.004f, 2});
            }
    }
    for (const VeinTip& tip : tips) {
        const auto& [f, len] = frames[tip.lobe];
        const Pt a = f.At(tip.t * 0.5f * len);
        plan.veins.push_back({Curve(a, Mix(a, tip.p, 0.5f), Mix(a, tip.p, 0.9f)), 0.018f, 0.005f, 1});
    }
    // The midrib's fold.
    const float side = Cross(m.Tangent(0.5f), kLight) > 0.0f ? 1.0f : -1.0f;
    Poly facet;
    for (int i = -2; i <= 16; ++i) facet.push_back(m.At(static_cast<float>(i) / 14.0f));
    facet.push_back(m.apex + m.Right(1.0f) * (side * 2.0f));
    facet.push_back(m.base + m.Right(0.0f) * (side * 2.0f));
    plan.facets.push_back(facet);
    // The heart: along the midrib and a little way out along the lobes' veins.
    Poly heart;
    for (int i = 0; i <= 10; ++i) heart.push_back(m.At(0.75f * static_cast<float>(i) / 10.0f) + m.Right(0.0f) * 0.13f);
    for (int i = 10; i >= 0; --i) heart.push_back(m.At(0.75f * static_cast<float>(i) / 10.0f) - m.Right(0.0f) * 0.13f);
    plan.heart.push_back(heart);
    for (const auto& [f, len] : frames)
        plan.heart.push_back({f.At(0.0f, -0.06f), f.At(0.45f * len, -0.05f), f.At(0.62f * len), f.At(0.45f * len, 0.05f),
                              f.At(0.0f, 0.06f)});
    plan.heartC = m.At(0.15f);
    plan.heartR = 1.15f;
    plan.heartSigma = 0.07f;
    plan.light = m.At(0.5f) + Pt{-0.12f, 0.0f};
    plan.lightR = 1.2f;
    AddStalk(plan, j, kPi + rng.Range(-0.15f, 0.15f), rng.Range(0.16f, 0.22f), rng.Range(-0.2f, 0.2f), 0.045f, 0.055f);
    plan.lum = red ? 0.88f : 0.9f;
    if (variant == 1) plan.holes.push_back(Hole(m.At(0.57f) + m.Right(0.57f) * 0.12f, 0.055f, rng));
    if (variant == 3) Curl(plan, -1.0f, 0.82f);
    Fit(plan);
    return plan;
}

// A simple, unlobed leaf: its outline follows half-width profiles either side of a
// (possibly curved) midrib: w(t) ~ t^p (1 - t)^q, with pinnate veins.
struct Simple {
    Midrib m;
    float width = 0.4f;         // greatest half-width (right side; the left is width * asym)
    float asym = 1.0f;
    float p = 0.5f, q = 1.2f;   // the profile's shape: p small = round base, q large = drawn-out tip
    Teeth teeth;
    float teethFrom = 0.12f;    // where along the midrib the teeth start and end
    float teethTo = 0.96f;
    int pairs = 7;              // secondary veins
    float veinFrom = 0.1f, veinTo = 0.85f;
    float veinReach = 0.16f;    // how far forward a secondary vein runs to the margin
    float veinBow = 1.0f;       // 0: straight secondary veins (birch) .. 1: arching (aspen)
    float midW = 0.05f;
    bool basalPair = false;     // the lowest veins leave from the base (aspen)
};

float SimpleWidth(const Simple& s, float t, float side) {
    if (t <= 0.0f || t >= 1.0f) return 0.0f;
    const float peak = s.p / (s.p + s.q);
    const float norm = std::pow(peak, s.p) * std::pow(1.0f - peak, s.q);
    return s.width * (side < 0.0f ? s.asym : 1.0f) * std::pow(t, s.p) * std::pow(1.0f - t, s.q) / norm;
}

void BuildSimple(LeafPlan& plan, const Simple& s) {
    constexpr int kNodes = 12;
    std::vector<Node> nodes;
    const Midrib& m = s.m;
    auto teethAt = [&](float t) { return t >= s.teethFrom && t <= s.teethTo ? s.teeth : Teeth{}; };
    nodes.push_back(N(m.apex, true, teethAt(0.99f), -1.0f));
    for (int i = kNodes - 1; i >= 1; --i) {
        const float t = static_cast<float>(i) / kNodes;
        nodes.push_back(N(m.At(t) + m.Right(t) * SimpleWidth(s, t, 1.0f), false, teethAt(t), -1.0f));
    }
    nodes.push_back(N(m.base, true));
    for (int i = 1; i < kNodes; ++i) {
        const float t = static_cast<float>(i) / kNodes;
        nodes.push_back(N(m.At(t) - m.Right(t) * SimpleWidth(s, t, -1.0f), false,
                          teethAt(static_cast<float>(i + 1) / kNodes), 1.0f));
    }
    plan.blade.push_back(Trace(nodes));

    Poly mid;
    for (int i = 0; i <= 16; ++i) mid.push_back(m.At(0.98f * static_cast<float>(i) / 16.0f));
    plan.veins.push_back({mid, s.midW, 0.15f * s.midW, 0});
    for (int k = 0; k < s.pairs; ++k) {
        const float t = Lerp(s.veinFrom, s.veinTo, static_cast<float>(k) / static_cast<float>(std::max(1, s.pairs - 1)));
        for (float side : {-1.0f, 1.0f}) {
            const float tt = s.basalPair && k == 0 ? 0.0f : t;
            const float te = std::min(0.97f, tt + s.veinReach * (s.basalPair && k == 0 ? 2.2f : 1.0f));
            const Pt a = m.At(tt);
            const Pt b = m.At(te) + m.Right(te) * (side * 0.9f * SimpleWidth(s, te, side));
            const Pt arch = m.At(Lerp(tt, te, 0.25f)) + m.Right(tt) * (side * 0.6f * SimpleWidth(s, std::max(tt, 0.2f), side));
            plan.veins.push_back({Curve(a, Mix(Mix(a, b, 0.5f), arch, s.veinBow), b), 0.022f, 0.005f, 1});
            // Fine veins between the secondaries.
            const Pt mid1 = Mix(a, b, 0.55f);
            const Pt fine = mid1 + m.Tangent(te) * 0.09f + m.Right(te) * (side * 0.02f);
            plan.veins.push_back({Curve(mid1, Mix(mid1, fine, 0.5f), fine), 0.01f, 0.004f, 2});
        }
    }
    const float side = Cross(m.Tangent(0.5f), kLight) > 0.0f ? 1.0f : -1.0f;
    Poly facet;
    for (int i = -2; i <= 18; ++i) facet.push_back(m.At(static_cast<float>(i) / 16.0f));
    facet.push_back(m.apex + m.Right(1.0f) * (side * 2.0f));
    facet.push_back(m.base + m.Right(0.0f) * (side * 2.0f));
    plan.facets.push_back(facet);

    Poly heart;
    for (int i = 0; i <= 12; ++i) {
        const float t = 0.68f * static_cast<float>(i) / 12.0f;
        heart.push_back(m.At(t) + m.Right(t) * (0.55f * SimpleWidth(s, std::max(t, 0.04f), 1.0f)));
    }
    for (int i = 12; i >= 0; --i) {
        const float t = 0.68f * static_cast<float>(i) / 12.0f;
        heart.push_back(m.At(t) - m.Right(t) * (0.55f * SimpleWidth(s, std::max(t, 0.04f), -1.0f)));
    }
    plan.heart.push_back(heart);
    const float length = Len(m.apex - m.base);
    plan.heartC = m.At(0.12f);
    plan.heartR = 0.75f * length;
    plan.heartSigma = 0.06f * length;
    plan.light = m.At(0.42f) + m.Right(0.4f) * (-side * 0.1f);
    plan.lightR = 0.8f * length;
}

// Birch: ovate to triangular, doubly serrate, with a drawn-out tip and straight parallel veins.
LeafPlan PlanBirch(int variant, Rng& rng) {
    LeafPlan plan;
    Simple s;
    const Pt j{0.0f, 0.55f};
    s.m = MakeMidrib(j, {0.0f, -0.98f}, variant == 2 ? rng.Range(0.08f, 0.12f) : rng.Range(-0.04f, 0.04f));
    s.width = variant == 1 ? 0.6f : variant == 2 ? 0.46f : 0.52f;
    s.p = variant == 1 ? 0.22f : 0.45f;  // variant 1: the silver birch's straight, broad base
    s.q = variant == 1 ? 1.35f : 1.3f;
    s.asym = rng.Range(0.92f, 1.02f);
    s.teeth = {0.055f, 0.04f, 0.0f, 3};
    s.teethFrom = 0.1f;
    s.pairs = 7;
    s.veinFrom = 0.12f;
    s.veinTo = 0.8f;
    s.veinReach = 0.15f;
    s.veinBow = 0.2f;
    BuildSimple(plan, s);
    AddStalk(plan, j, kPi + rng.Range(-0.2f, 0.2f), rng.Range(0.32f, 0.42f), rng.Range(-0.15f, 0.15f), 0.035f, 0.042f);
    plan.lum = 0.91f;
    if (variant == 3) {
        Bite(plan.blade[0], s.m.At(0.5f) + s.m.Right(0.5f) * (SimpleWidth(s, 0.5f, 1.0f) + 0.06f), 0.13f, rng);
        plan.holes.push_back(Hole(s.m.At(0.3f) - s.m.Right(0.3f) * 0.2f, 0.05f, rng));
    }
    Fit(plan);
    return plan;
}

// Quaking aspen: nearly round with a short point, finely round-toothed, on a long flat stalk.
LeafPlan PlanAspen(int variant, Rng& rng) {
    LeafPlan plan;
    Simple s;
    const Pt j{0.0f, 0.3f};
    s.m = MakeMidrib(j, {variant == 2 ? 0.06f : 0.0f, -0.92f}, rng.Range(-0.04f, 0.04f));
    s.width = variant == 1 ? 0.52f : 0.62f;
    s.p = 0.42f;
    s.q = variant == 1 ? 0.75f : 0.6f;
    s.asym = rng.Range(0.94f, 1.04f);
    s.teeth = {0.06f, 0.022f, 0.85f, 0};
    s.teethFrom = 0.15f;
    s.teethTo = 0.9f;
    s.pairs = 3;
    s.veinFrom = 0.0f;
    s.veinTo = 0.5f;
    s.veinReach = 0.26f;
    s.basalPair = true;
    BuildSimple(plan, s);
    AddStalk(plan, j, kPi + rng.Range(-0.2f, 0.2f), rng.Range(0.55f, 0.66f), rng.Range(-0.2f, 0.2f), 0.03f, 0.036f);
    plan.lum = 0.92f;
    if (variant == 3) Bite(plan.blade[0], s.m.At(0.6f) - s.m.Right(0.6f) * (SimpleWidth(s, 0.6f, -1.0f) + 0.07f), 0.15f, rng);
    Fit(plan);
    return plan;
}

// Willow: long, narrow and gently curved, finely serrate, on a short stalk.
LeafPlan PlanWillow(int variant, Rng& rng) {
    LeafPlan plan;
    Simple s;
    const Pt j{0.0f, 0.82f};
    const float bend = variant == 1 ? 0.3f : variant == 2 ? 0.05f : 0.16f;
    s.m = MakeMidrib(j, {0.0f, -0.98f}, bend * (rng.Chance(0.5f) ? 1.0f : -1.0f));
    s.width = variant == 3 ? 0.24f : variant == 2 ? 0.15f : 0.19f;
    s.p = 0.75f;
    s.q = 1.25f;
    s.asym = rng.Range(0.9f, 1.05f);
    s.teeth = {0.045f, 0.008f, 0.2f, 0};
    s.teethFrom = 0.15f;
    s.teethTo = 0.92f;
    s.pairs = 11;
    s.veinFrom = 0.08f;
    s.veinTo = 0.88f;
    s.veinReach = 0.07f;
    s.veinBow = 0.5f;
    s.midW = 0.045f;
    BuildSimple(plan, s);
    AddStalk(plan, j, kPi + rng.Range(-0.15f, 0.15f), 0.12f, 0.0f, 0.035f, 0.04f);
    plan.lum = 0.9f;
    if (variant == 3) Curl(plan, rng.Chance(0.5f) ? 1.0f : -1.0f, 0.6f);
    Fit(plan);
    return plan;
}

// Ginkgo: a fan on a long stalk, notched in the middle of its wavy outer edge, with fine
// veins forking out from the base.
LeafPlan PlanGinkgo(int variant, Rng& rng) {
    LeafPlan plan;
    const Pt j{0.0f, 0.42f};
    const float spread = Deg(rng.Range(60.0f, 68.0f));  // half the fan's angle
    const float notch = variant == 0 ? 0.22f : variant == 1 ? 0.45f : variant == 2 ? 0.0f : 0.3f;
    const float skew = variant == 3 ? 0.12f : rng.Range(-0.04f, 0.04f);  // the notch off centre
    const Teeth wave = {0.11f, 0.02f, 1.0f, 0};
    std::vector<Node> nodes;
    nodes.push_back(N(j, true));
    // Left side up to the corner (sides slightly hollow), outer edge, right side down.
    nodes.push_back(N(j + Dir(-spread) * 0.42f + Dir(-spread + kPi / 2.0f) * 0.03f));
    nodes.push_back(N(j + Dir(-spread - 0.02f) * 0.84f, false, wave, 1.0f));
    const int arc = 9;
    bool notched = notch <= 0.0f;
    for (int i = 0; i <= arc; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(arc);  // 0..1 across the fan
        const float a = Lerp(-spread + 0.1f, spread - 0.1f, u);
        if (notch > 0.0f && std::fabs(a - skew) < 0.12f) continue;
        const float rr = 1.0f + rng.Range(-0.035f, 0.035f) - 0.06f * std::pow(std::fabs(a) / spread, 3.0f);
        if (!notched && a > skew) {
            // The notch: down into it and back out.
            nodes.push_back(N(j + Dir(skew - 0.09f) * 0.98f, false));
            nodes.push_back(N(j + Dir(skew) * (1.0f - notch), true));
            nodes.push_back(N(j + Dir(skew + 0.09f) * 0.98f, false, wave, 1.0f));
            notched = true;
        }
        nodes.push_back(N(j + Dir(a) * rr, false, wave, 1.0f));
    }
    nodes.push_back(N(j + Dir(spread + 0.02f) * 0.84f));
    nodes.push_back(N(j + Dir(spread) * 0.42f + Dir(spread - kPi / 2.0f) * 0.03f));
    plan.blade.push_back(Trace(nodes));

    // Veins fan out from the base, forking once on the way.
    const int count = 11;
    for (int i = 0; i < count; ++i) {
        const float a = Lerp(-spread + 0.12f, spread - 0.12f, (static_cast<float>(i) + 0.5f) / count);
        const Pt fork = j + Dir(a) * 0.45f;
        plan.veins.push_back({Curve(j + Dir(a) * 0.08f, j + Dir(a) * 0.25f, fork), 0.012f, 0.01f, 2});
        for (float d : {-0.055f, 0.055f})
            plan.veins.push_back({Curve(fork, j + Dir(a + 0.5f * d) * 0.7f, j + Dir(a + d) * 0.93f), 0.01f, 0.006f, 2});
    }
    for (float sd : {-1.0f, 1.0f})  // the margins' veins
        plan.veins.push_back({Curve(j, j + Dir(sd * spread) * 0.4f, j + Dir(sd * (spread - 0.03f)) * 0.82f), 0.022f,
                              0.008f, 1});
    plan.facets.push_back({j, j + Dir(skew) * 2.0f, j + Dir(spread + 0.5f) * 2.0f});
    plan.facetAlpha = 0.05f;
    plan.heart.push_back({j + Dir(-spread * 0.8f) * 0.1f, j + Dir(-spread * 0.7f) * 0.5f, j + Dir(0.0f) * 0.56f,
                          j + Dir(spread * 0.7f) * 0.5f, j + Dir(spread * 0.8f) * 0.1f});
    plan.heartC = j + Pt{0.0f, -0.08f};
    plan.heartR = 1.0f;
    plan.heartSigma = 0.08f;
    plan.light = j + Pt{-0.15f, -0.55f};
    plan.lightR = 1.1f;
    AddStalk(plan, j, kPi + rng.Range(-0.2f, 0.2f), rng.Range(0.55f, 0.65f), rng.Range(-0.18f, 0.18f), 0.032f, 0.04f);
    plan.lum = 0.92f;
    if (variant == 3) Bite(plan.blade[0], j + Dir(-spread * 0.5f) * 1.08f, 0.14f, rng);
    Fit(plan);
    return plan;
}

// One maple key: a plump seed at `seed`, its papery wing reaching `len` towards `angle` and
// flaring to the trailing side `trail` (+1: to the right of the wing's direction).
void AddKey(LeafPlan& plan, Pt seed, float angle, float len, float width, float trail, Rng& rng) {
    const Frame f(seed, angle);
    // The wing sweeps round a little towards its trailing side, like a scimitar.
    const float sweep = rng.Range(0.14f, 0.22f) * trail;
    auto at = [&](float t, float u) {
        return seed + Rotate(f.d * (t * len) + f.n * (u * len * trail), sweep * t * std::fabs(t));
    };
    const float bow = rng.Range(-0.015f, 0.015f);
    // Clockwise for trail = +1: round the seed, up the thick leading edge, round the broad
    // tip and back down the trailing edge to the seed.
    std::vector<Node> nodes = {
        N(at(-0.24f, 0.0f), true),  // where the seed hung from its stalk
        N(at(-0.2f, -0.1f)),
        N(at(-0.07f, -0.155f)),
        N(at(0.08f, -0.14f)),
        N(at(0.22f, -0.09f + bow)),
        N(at(0.45f, -0.07f + bow)),
        N(at(0.7f, -0.05f)),
        N(at(0.9f, -0.0f)),
        N(at(0.995f, 0.1f * width + 0.04f)),
        N(at(0.98f, 0.24f * width)),
        N(at(0.88f, 0.34f * width)),
        N(at(0.7f, 0.37f * width)),
        N(at(0.5f, 0.33f * width)),
        N(at(0.32f, 0.25f * width)),
        N(at(0.2f, 0.17f)),
        N(at(0.08f, 0.13f)),
        N(at(-0.1f, 0.12f)),
        N(at(-0.2f, 0.075f)),
    };
    if (trail < 0.0f) std::reverse(nodes.begin(), nodes.end());
    plan.blade.push_back(Trace(nodes));
    // The seed: a darker, plump swelling with a sheen; the wing's thick leading vein and the
    // fine veins fanning from the seed to the trailing edge.
    plan.shades.push_back(Ellipse(at(-0.06f, -0.01f), 0.13f * len, 0.17f * len, angle, 20));
    plan.spots.push_back({at(-0.09f, -0.06f), 0.04f * len, 0.07f * len, -0.32f});
    plan.veins.push_back({Curve(at(0.1f, -0.08f), at(0.5f, -0.065f), at(0.92f, 0.04f)), 0.05f * len, 0.018f * len, 0});
    for (int i = 0; i < 8; ++i) {
        const float u = static_cast<float>(i) / 7.0f;
        const Pt a = at(Lerp(0.12f, 0.55f, u), -0.05f);
        const float te = Lerp(0.3f, 0.96f, u);
        const Pt b = at(te, (0.38f * std::sin(kPi * Lerp(0.2f, 0.9f, u)) - 0.03f) * width);
        plan.veins.push_back({Curve(a, Mix(a, b, 0.55f) + f.d * (0.07f * len), b), 0.014f * len, 0.006f * len, 2});
    }
    // The heart: the seed and the base of the wing.
    plan.heart.push_back(Ellipse(at(0.04f, 0.03f), 0.2f * len, 0.32f * len, angle, 16));
}

// Maple keys: a single samara (variants 0, 1) or a joined pair (2, 3). The sprite's centre is
// the seed (the pair's join), the axis it whirls about.
LeafPlan PlanSamara(int variant, Rng& rng) {
    LeafPlan plan;
    const Pt pivot{0.0f, 0.0f};
    if (variant < 2) {
        const float width = variant == 1 ? 1.15f : 0.95f;
        AddKey(plan, pivot, rng.Range(-0.1f, 0.1f), 1.0f, width, 1.0f, rng);
        AddStalk(plan, pivot + Dir(kPi) * 0.15f, kPi + 0.35f, 0.16f, 0.15f, 0.028f, 0.03f);
    } else {
        // The pair's thick edges face outwards, the curved trailing edges in towards each other.
        const float open = Deg(variant == 2 ? 30.0f : 46.0f);
        const float width = rng.Range(0.95f, 1.08f);
        AddKey(plan, pivot + Dir(-open) * 0.14f, -open, 0.86f, width, 1.0f, rng);
        AddKey(plan, pivot + Dir(open) * 0.14f, open, 0.86f * rng.Range(0.93f, 1.0f), width, -1.0f, rng);
        AddStalk(plan, pivot, kPi + 0.2f, 0.2f, 0.15f, 0.028f, 0.03f);
    }
    plan.shadeAlpha = 0.26f;
    plan.facetAlpha = 0.0f;
    plan.lum = 0.93f;
    plan.edgeAlpha = 0.24f;
    plan.heartC = pivot;
    plan.heartR = 0.7f;
    plan.heartSigma = 0.07f;
    plan.light = {-0.1f, -0.4f};
    plan.lightR = 1.0f;
    Fit(plan, 0.96f, &pivot);
    return plan;
}

// A crumb of leaf: a torn scrap with ragged edges and a bit of vein across it.
LeafPlan PlanFleck(int variant, Rng& rng) {
    LeafPlan plan;
    const int n = variant == 1 ? 5 : rng.Int(5, 7);
    const float elong = variant == 1 ? 0.5f : rng.Range(0.75f, 0.95f);
    const Teeth torn = {0.11f, 0.05f, 0.0f, 2};
    std::vector<Node> nodes;
    for (int i = 0; i < n; ++i) {
        const float a = kTau * (static_cast<float>(i) + rng.Range(-0.25f, 0.25f)) / static_cast<float>(n);
        const float rr = rng.Range(0.7f, 1.0f);
        // Variant 2 keeps a stretch of the leaf's own smooth margin.
        const bool margin = variant == 2 && i < n / 2;
        nodes.push_back(N(Pt{rr * std::sin(a) * elong, -rr * std::cos(a)}, !margin,
                          margin ? Teeth{} : torn, rng.Range(-1.0f, 1.0f)));
    }
    plan.blade.push_back(Trace(nodes));
    const float a = rng.Range(-0.5f, 0.5f);
    plan.veins.push_back({Curve(Dir(a + kPi) * 1.2f, Pt{rng.Range(-0.15f, 0.15f), 0.0f}, Dir(a) * 1.2f), 0.09f, 0.05f, 0});
    if (variant == 3) plan.facets.push_back({Dir(a + kPi) * 2.0f, Dir(a) * 2.0f, Dir(a + kPi / 2.0f) * 2.0f});
    plan.facetAlpha = 0.12f;
    plan.lum = rng.Range(0.84f, 0.92f);
    plan.edgeAlpha = 0.16f;
    plan.light = {0.0f, 0.0f};
    Fit(plan);
    return plan;
}

// An out-of-focus leaf drifting past the lens: a whole leaf, shrunk to leave room for the blur.
LeafPlan PlanBlur(int variant, Rng& rng) {
    LeafPlan plan;
    switch (variant) {
    case 0: plan = PlanMaple(0, rng); break;
    case 1: plan = PlanBirch(2, rng); break;
    case 2: plan = PlanOak(0, rng); break;
    default: plan = PlanSweetgum(1, rng); break;
    }
    Map(plan, {0.0f, 0.0f}, 0.74f);
    plan.blurred = true;
    plan.blurSigma = 0.07f;
    plan.lum = 0.86f;
    return plan;
}

// A path through the figures scaled to DIPs; `tol` (DIPs) drops points closer than that to
// the last one kept, so that small sprites do not pay for detail they cannot show.
ComPtr<ID2D1PathGeometry> MakePath(ID2D1Factory* factory, const std::vector<Poly>& figures, float scale,
                                   bool closed = true, float tol = 0.0f) {
    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return nullptr;
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    std::vector<D2D1_POINT_2F> pts;
    const float tol2 = tol * tol;
    for (const Poly& f : figures) {
        if (f.size() < 2) continue;
        pts.clear();
        for (Pt p : f) {
            const D2D1_POINT_2F q = {p.x * scale, p.y * scale};
            if (!pts.empty() && tol2 > 0.0f) {
                const float dx = q.x - pts.back().x, dy = q.y - pts.back().y;
                if (dx * dx + dy * dy < tol2) continue;
            }
            pts.push_back(q);
        }
        if (pts.size() < 2) continue;
        sink->BeginFigure(pts[0], closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLines(pts.data() + 1, static_cast<UINT32>(pts.size() - 1));
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    }
    if (FAILED(sink->Close())) return nullptr;
    return geo;
}

// Collects a simplified geometry's figures as polygons.
class PolySink final : public ID2D1SimplifiedGeometrySink {
public:
    std::vector<Poly> figures;

    STDMETHOD(QueryInterface)(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = iid == __uuidof(IUnknown) || iid == __uuidof(ID2D1SimplifiedGeometrySink) ? this : nullptr;
        return *out ? S_OK : E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return 1; }  // lives on the stack
    STDMETHOD_(ULONG, Release)() override { return 1; }
    STDMETHOD_(void, SetFillMode)(D2D1_FILL_MODE) override {}
    STDMETHOD_(void, SetSegmentFlags)(D2D1_PATH_SEGMENT) override {}
    STDMETHOD_(void, BeginFigure)(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN) override { figures.push_back({{p.x, p.y}}); }
    STDMETHOD_(void, AddLines)(const D2D1_POINT_2F* pts, UINT32 n) override {
        if (figures.empty()) return;
        for (UINT32 i = 0; i < n; ++i) figures.back().push_back({pts[i].x, pts[i].y});
    }
    STDMETHOD_(void, AddBeziers)(const D2D1_BEZIER_SEGMENT* b, UINT32 n) override {
        if (figures.empty()) return;
        for (UINT32 i = 0; i < n; ++i) figures.back().push_back({b[i].point3.x, b[i].point3.y});
    }
    STDMETHOD_(void, EndFigure)(D2D1_FIGURE_END) override {}
    STDMETHOD(Close)() override { return S_OK; }
};

// Replaces the blade's outlines with the edge of the area they cover: where tight sinuses
// or crowded lobes make the traced outline cross itself, the rim and the halo would
// otherwise follow the loops inside the leaf.
void Clean(LeafPlan& p, ID2D1Factory* factory) {
    std::vector<Poly> cut = p.blade;
    cut.insert(cut.end(), p.holes.begin(), p.holes.end());
    ComPtr<ID2D1PathGeometry> geo = factory ? MakePath(factory, cut, 1.0f) : nullptr;
    PolySink sink;
    if (!geo || FAILED(geo->Outline(nullptr, 0.0015f, &sink)) || sink.figures.empty()) return;
    // The figures wound like the largest are outlines, the others holes. Only the holes the
    // design asked for stay: any other is a pocket shut in by crossing lobes, and is filled.
    size_t largest = 0;
    for (size_t i = 1; i < sink.figures.size(); ++i)
        if (std::fabs(SignedArea(sink.figures[i])) > std::fabs(SignedArea(sink.figures[largest]))) largest = i;
    const bool outerSign = SignedArea(sink.figures[largest]) > 0.0f;
    std::vector<Pt> wanted;
    for (const Poly& h : p.holes) {
        Pt c{0.0f, 0.0f};
        for (Pt q : h) c = c + q * (1.0f / static_cast<float>(h.size()));
        wanted.push_back(c);
    }
    p.blade.clear();
    p.holes.clear();
    for (Poly& f : sink.figures) {
        if (f.size() < 3) continue;
        const bool outer = (SignedArea(f) > 0.0f) == outerSign;
        if (!outer && std::none_of(wanted.begin(), wanted.end(), [&](Pt c) { return Inside(f, c); })) continue;
        Orient(f, outer);
        (outer ? p.blade : p.holes).push_back(std::move(f));
    }
}

LeafPlan MakePlan(LeafKind kind, int variant, ID2D1Factory* factory) {
    Rng rng(0x1EAF5EEDull + static_cast<uint64_t>(kind) * 0x9E3779B97F4A7C15ull + static_cast<uint64_t>(variant) * 7919u);
    LeafPlan plan;
    switch (kind) {
    case LeafKind::Fleck: plan = PlanFleck(variant, rng); break;
    case LeafKind::Blur: plan = PlanBlur(variant, rng); break;
    case LeafKind::Samara: plan = PlanSamara(variant, rng); break;
    case LeafKind::Aspen: plan = PlanAspen(variant, rng); break;
    case LeafKind::Maple: plan = PlanMaple(variant, rng); break;
    case LeafKind::Oak: plan = PlanOak(variant, rng); break;
    case LeafKind::Birch: plan = PlanBirch(variant, rng); break;
    case LeafKind::Ginkgo: plan = PlanGinkgo(variant, rng); break;
    case LeafKind::Sweetgum: plan = PlanSweetgum(variant, rng); break;
    case LeafKind::Willow: plan = PlanWillow(variant, rng); break;
    case LeafKind::JapaneseMaple: plan = PlanJapaneseMaple(variant, rng); break;
    default: return {};
    }
    Clean(plan, factory);
    return plan;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

D2D1_COLOR_F Grey(float l, float a = 1.0f) { return D2D1::ColorF(l, l, l, a); }

D2D1_COLOR_F Rgb(uint32_t c, float a = 1.0f) {
    return D2D1::ColorF(static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
                        static_cast<float>(c & 0xFF) / 255.0f, a);
}

ComPtr<ID2D1RadialGradientBrush> RadialBrush(ID2D1RenderTarget* rt, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
    ComPtr<ID2D1GradientStopCollection> coll;
    ComPtr<ID2D1RadialGradientBrush> brush;
    if (FAILED(rt->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), &coll)) ||
        FAILED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({0, 0}, {0, 0}, 1.0f, 1.0f),
                                             coll.Get(), &brush)))
        return nullptr;
    return brush;
}

ComPtr<ID2D1LinearGradientBrush> LinearBrush(ID2D1RenderTarget* rt, std::initializer_list<D2D1_GRADIENT_STOP> stops) {
    ComPtr<ID2D1GradientStopCollection> coll;
    ComPtr<ID2D1LinearGradientBrush> brush;
    if (FAILED(rt->CreateGradientStopCollection(stops.begin(), static_cast<UINT32>(stops.size()), &coll)) ||
        FAILED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0, 0}, {0, 1}), coll.Get(), &brush)))
        return nullptr;
    return brush;
}

// Brushes reused from sprite to sprite on one target: making gradients for each is slow.
// Gradients are placed with SetCenter / SetRadius before use.
struct Brushes {
    ComPtr<ID2D1SolidColorBrush> ink;
    ComPtr<ID2D1RadialGradientBrush> shade;      // clear in the middle, darkening to the rim
    ComPtr<ID2D1RadialGradientBrush> dark, lit;  // soft spots (their opacity sets the strength)
    ComPtr<ID2D1RadialGradientBrush> heart;      // the blush's falloff from the heart
    ComPtr<ID2D1LinearGradientBrush> nut, cap;   // acorns
    ComPtr<ID2D1RadialGradientBrush> nutShade, gloss;

    HRESULT Create(ID2D1RenderTarget* rt) {
        if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &ink))) return E_FAIL;
        shade = RadialBrush(rt, {{0.0f, Grey(0, 0.0f)}, {0.55f, Grey(0, 0.055f)}, {1.0f, Grey(0, 0.15f)}});
        dark = RadialBrush(rt, {{0.0f, Grey(0, 1.0f)}, {0.55f, Grey(0, 1.0f)}, {1.0f, Grey(0, 0.0f)}});
        lit = RadialBrush(rt, {{0.0f, Grey(1, 1.0f)}, {0.55f, Grey(1, 1.0f)}, {1.0f, Grey(1, 0.0f)}});
        heart = RadialBrush(rt, {{0.0f, Grey(1, 1.0f)}, {0.45f, Grey(1, 0.85f)}, {1.0f, Grey(1, 0.3f)}});
        nut = LinearBrush(rt, {{0.0f, Rgb(0xC9A25E)}, {0.3f, Rgb(0xB07A3A)}, {0.75f, Rgb(0x8A5226)}, {1.0f, Rgb(0x5E3517)}});
        cap = LinearBrush(rt, {{0.0f, Rgb(0x8C6A45)}, {0.6f, Rgb(0x6E4F31)}, {1.0f, Rgb(0x523822)}});
        nutShade = RadialBrush(rt, {{0.0f, Rgb(0x000000, 0.0f)}, {0.6f, Rgb(0x000000, 0.06f)}, {1.0f, Rgb(0x1A0A00, 0.45f)}});
        gloss = RadialBrush(rt, {{0.0f, Rgb(0xFFF4E0, 1.0f)}, {1.0f, Rgb(0xFFF4E0, 0.0f)}});
        return shade && dark && lit && heart && nut && cap && nutShade && gloss ? S_OK : E_FAIL;
    }
};

void Place(ID2D1RadialGradientBrush* b, D2D1_POINT_2F c, float rx, float ry, float opacity = 1.0f) {
    b->SetCenter(c);
    b->SetRadiusX(rx);
    b->SetRadiusY(ry);
    b->SetOpacity(opacity);
}

// What to draw of a design: the atlas draws every halo on a page first, with MAX
// blending throughout, then every leaf over them.
enum class Phase { All, Halo, Body };

// Where and how a design is drawn.
struct Canvas {
    ID2D1RenderTarget* rt = nullptr;
    ID2D1DeviceContext* dc = nullptr;   // rt as a device context, when it is one: blurs and blend modes
    ID2D1DeviceContext* rec = nullptr;  // records command lists for the blurs (same device), may be null
    ID2D1Factory* factory = nullptr;
    ID2D1StrokeStyle* round = nullptr;  // round caps and joins
    Brushes* brushes = nullptr;         // made on rt
    Brushes* recBrushes = nullptr;      // made on rec
    bool maxBlend = false;              // the backdrop is transparent: halo passes merge with MAX blending
    Phase phase = Phase::All;           // Halo: the caller has set MAX blending already
    float r = 1.0f;                     // design radius (DIPs)
    float px = 1.0f;                    // device px per DIP
    bool shadow = true;
    float Rpx() const { return r * px; }
    bool Halo() const { return shadow && phase != Phase::Body; }
    bool Body() const { return phase != Phase::Halo; }
};

ComPtr<ID2D1StrokeStyle> RoundStroke(ID2D1Factory* factory) {
    ComPtr<ID2D1StrokeStyle> round;
    factory->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                           D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                               nullptr, 0, &round);
    return round;
}

// The dark halo round a silhouette: its outline widened step by step, merged with MAX blending
// on a transparent backdrop so that overlapping parts do not darken each other. Whatever is
// drawn over the silhouette covers the inner half.
void DrawHalo(Canvas& c, ID2D1Geometry* silhouette) {
    if (!silhouette) return;
    const float hPx = HaloPx(c.Rpx());
    const float h = hPx / c.px;
    const int passes = Clamp(static_cast<int>(std::lround(hPx / 0.9f)), 2, 6);
    const bool own = c.maxBlend && c.dc && c.phase == Phase::All;  // else the caller has set the blend
    D2D1_PRIMITIVE_BLEND oldBlend = D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
    if (own) {
        oldBlend = c.dc->GetPrimitiveBlend();
        c.dc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_MAX);
    }
    ID2D1SolidColorBrush* ink = c.brushes->ink.Get();
    for (int j = passes; j >= 1; --j) {
        const float d = h * static_cast<float>(j) / static_cast<float>(passes);
        float a = kHaloAlpha * std::pow(1.0f - static_cast<float>(j - 1) / static_cast<float>(passes), 1.6f);
        if (!c.maxBlend) a *= 0.5f;  // plain blending accumulates the passes
        ink->SetColor(D2D1::ColorF(0, 0, 0, a));
        c.rt->DrawGeometry(silhouette, ink, 2.0f * d, c.round);
    }
    if (own) c.dc->SetPrimitiveBlend(oldBlend);
}

// Veins in order 0..2: darkness and the drawn radius (device px) from which they show.
constexpr float kVeinAlpha[3] = {0.34f, 0.26f, 0.15f};
constexpr float kVeinFrom[3] = {5.0f, 11.0f, 22.0f};
constexpr float kVeinMinPx = 0.75f;  // thinner veins are drawn this wide but fainter

// The blade's insides, drawn into a layer masked by the blade: shaded fill, folds, spots,
// veins and a darker rim. With an opacity mask it is the blush layer (no rim).
void DrawBlade(Canvas& c, const LeafPlan& p, ID2D1Geometry* blade, ID2D1Brush* opacity) {
    ID2D1RenderTarget* rt = c.rt;
    Brushes& b = *c.brushes;
    const float r = c.r, rpx = c.Rpx();
    const D2D1_RECT_F bounds = {-1.2f * r, -1.2f * r, 1.2f * r, 1.2f * r};
    rt->PushLayer(D2D1::LayerParameters(bounds, blade, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1.0f,
                                        opacity, D2D1_LAYER_OPTIONS_NONE),
                  nullptr);
    // A gentle highlight: lightest round the light's centre, darkening towards the rim.
    b.ink->SetColor(Grey(std::min(1.0f, p.lum + 0.05f)));
    rt->FillRectangle(bounds, b.ink.Get());
    Place(b.shade.Get(), {p.light.x * r, p.light.y * r}, p.lightR * r, p.lightR * r);
    rt->FillRectangle(bounds, b.shade.Get());
    // Folds and darker parts fade in from tiny sizes, where they would only muddy the colour.
    const float detail = SmoothStep(3.0f, 9.0f, rpx);
    if (p.facetAlpha > 0.0f && !p.facets.empty())
        if (auto geo = MakePath(c.factory, p.facets, r)) {
            b.ink->SetColor(D2D1::ColorF(0, 0, 0, p.facetAlpha * detail));
            rt->FillGeometry(geo.Get(), b.ink.Get());
        }
    if (p.shadeAlpha > 0.0f && !p.shades.empty())
        if (auto geo = MakePath(c.factory, p.shades, r, true, 0.3f / c.px)) {
            b.ink->SetColor(D2D1::ColorF(0, 0, 0, p.shadeAlpha * Lerp(0.6f, 1.0f, detail)));
            rt->FillGeometry(geo.Get(), b.ink.Get());
        }
    for (const Spot& s : p.spots) {
        const float a = std::fabs(s.alpha) * SmoothStep(6.0f, 14.0f, rpx);
        if (a <= 0.004f) continue;
        ID2D1RadialGradientBrush* spot = s.alpha < 0.0f ? b.lit.Get() : b.dark.Get();
        const D2D1_POINT_2F centre = {s.c.x * r, s.c.y * r};
        Place(spot, centre, s.rx * r, s.ry * r, a);
        rt->FillEllipse(D2D1::Ellipse(centre, s.rx * r, s.ry * r), spot);
    }
    // Veins: one geometry per order so that crossings do not darken twice.
    std::vector<Poly> bands;
    for (int order = 0; order < 3; ++order) {
        const float show = SmoothStep(kVeinFrom[order], kVeinFrom[order] * 1.5f, rpx);
        if (show <= 0.0f) continue;
        bands.clear();
        float widest = 0.0f;
        const float minW = kVeinMinPx / rpx;
        for (const Vein& v : p.veins) {
            if (v.order != order) continue;
            bands.push_back(Ribbon(v.line, std::max(v.w0, minW), std::max(v.w1, minW)));
            widest = std::max(widest, v.w0);
        }
        if (bands.empty()) continue;
        // Veins drawn wider than they are come out fainter, keeping the same weight.
        const float thin = Saturate(widest * rpx / (2.0f * kVeinMinPx));
        if (auto geo = MakePath(c.factory, bands, r, true, 0.35f / c.px)) {
            b.ink->SetColor(D2D1::ColorF(0, 0, 0, kVeinAlpha[order] * show * Lerp(0.55f, 1.0f, thin)));
            rt->FillGeometry(geo.Get(), b.ink.Get());
        }
    }
    // A darker rim (half of it falls outside the layer). The blush has faded out long before it.
    if (!opacity) {
        b.ink->SetColor(D2D1::ColorF(0, 0, 0, p.edgeAlpha * Lerp(0.5f, 1.0f, detail)));
        rt->DrawGeometry(blade, b.ink.Get(), std::max(0.07f * r, 1.6f / c.px), c.round);
    }
    rt->PopLayer();
}

// The blush layer's mask: the heart region, softened by a blur and fading out from its centre.
ComPtr<ID2D1Brush> BlushMask(Canvas& c, const LeafPlan& p) {
    const float r = c.r;
    const D2D1_POINT_2F centre = {p.heartC.x * r, p.heartC.y * r};
    if (c.dc && c.rec && c.recBrushes) {
        ComPtr<ID2D1CommandList> list;
        ComPtr<ID2D1Effect> blur;
        ComPtr<ID2D1Image> image;
        ComPtr<ID2D1ImageBrush> brush;
        if (SUCCEEDED(c.rec->CreateCommandList(&list))) {
            c.rec->SetTarget(list.Get());
            c.rec->BeginDraw();
            c.rec->SetTransform(D2D1::IdentityMatrix());
            if (auto geo = MakePath(c.factory, p.heart, r)) {
                ID2D1RadialGradientBrush* fill = c.recBrushes->heart.Get();
                Place(fill, centre, p.heartR * r, p.heartR * r);
                c.rec->FillGeometry(geo.Get(), fill);
            }
            const HRESULT hr = c.rec->EndDraw();
            c.rec->SetTarget(nullptr);
            if (SUCCEEDED(hr) && SUCCEEDED(list->Close()) && SUCCEEDED(c.dc->CreateEffect(kGaussianBlur, &blur))) {
                blur->SetInput(0, list.Get());
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, std::max(p.heartSigma * r, 0.3f / c.px));
                blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
                blur->GetOutput(&image);
                const D2D1_RECT_F src = {-1.2f * r, -1.2f * r, 1.2f * r, 1.2f * r};
                if (image && SUCCEEDED(c.dc->CreateImageBrush(image.Get(), D2D1::ImageBrushProperties(src), &brush))) {
                    // The brush paints the source rectangle from its own origin: move it back into place.
                    brush->SetTransform(D2D1::Matrix3x2F::Translation(src.left, src.top));
                    return brush;
                }
            }
        }
    }
    // Without effects: an elliptical falloff from the heart.
    ComPtr<ID2D1RadialGradientBrush> fallback =
        RadialBrush(c.rt, {{0.0f, Grey(1, 1.0f)}, {0.5f, Grey(1, 0.7f)}, {1.0f, Grey(1, 0.0f)}});
    if (fallback) Place(fallback.Get(), centre, p.heartR * 0.7f * r, p.heartR * 0.7f * r);
    return fallback;
}

void DrawPlanSharp(Canvas& c, const LeafPlan& p, bool blush) {
    const float r = c.r;
    std::vector<Poly> bands;
    for (const Poly& s : p.stalks) {
        const float minW = 0.8f / c.Rpx();
        bands.push_back(Ribbon(s, std::max(p.stalkW0, minW), std::max(p.stalkW1, minW)));
    }
    if (c.Halo() && !blush) {
        // The soft halo does not need the margin's finest detail.
        std::vector<Poly> silhouette = p.blade;
        silhouette.insert(silhouette.end(), bands.begin(), bands.end());
        DrawHalo(c, MakePath(c.factory, silhouette, r, true, 0.7f / c.px).Get());
    }
    if (!c.Body()) return;
    std::vector<Poly> cut = p.blade;
    cut.insert(cut.end(), p.holes.begin(), p.holes.end());
    ComPtr<ID2D1PathGeometry> blade = MakePath(c.factory, cut, r, true, 0.3f / c.px);
    if (!blade) return;
    if (blush) {
        if (ComPtr<ID2D1Brush> mask = BlushMask(c, p)) DrawBlade(c, p, blade.Get(), mask.Get());
        return;
    }
    if (!bands.empty())
        if (auto stalk = MakePath(c.factory, bands, r)) {
            c.brushes->ink->SetColor(Grey(p.stalkLum));
            c.rt->FillGeometry(stalk.Get(), c.brushes->ink.Get());
        }
    DrawBlade(c, p, blade.Get(), nullptr);
}

// An out-of-focus leaf: the sharp design recorded, then blurred; a softer, wider shadow under it.
void DrawPlanBlurred(Canvas& c, const LeafPlan& p) {
    if (!c.Body()) return;
    const float r = c.r;
    const float sigma = std::max(p.blurSigma * r, 0.4f / c.px);
    if (!c.dc || !c.rec || !c.recBrushes) {
        // No effects: the sharp leaf, faded.
        c.rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                              D2D1::IdentityMatrix(), 0.7f),
                        nullptr);
        Canvas sharp = c;
        sharp.shadow = false;
        DrawPlanSharp(sharp, p, false);
        c.rt->PopLayer();
        return;
    }
    auto record = [&](bool silhouette) -> ComPtr<ID2D1CommandList> {
        ComPtr<ID2D1CommandList> list;
        if (FAILED(c.rec->CreateCommandList(&list))) return nullptr;
        c.rec->SetTarget(list.Get());
        c.rec->BeginDraw();
        c.rec->SetTransform(D2D1::IdentityMatrix());
        if (silhouette) {
            std::vector<Poly> all = p.blade;
            for (const Poly& s : p.stalks) all.push_back(Ribbon(s, p.stalkW0, p.stalkW1));
            if (auto geo = MakePath(c.factory, all, r, true, 0.5f / c.px)) {
                c.recBrushes->ink->SetColor(D2D1::ColorF(0, 0, 0, 1));
                c.rec->FillGeometry(geo.Get(), c.recBrushes->ink.Get());
            }
        } else {
            Canvas inner = c;
            inner.rt = c.rec;
            inner.dc = c.rec;
            inner.rec = nullptr;
            inner.brushes = c.recBrushes;
            inner.recBrushes = nullptr;
            inner.shadow = false;
            inner.maxBlend = false;
            inner.phase = Phase::All;
            DrawPlanSharp(inner, p, false);
        }
        const HRESULT hr = c.rec->EndDraw();
        c.rec->SetTarget(nullptr);
        if (FAILED(hr) || FAILED(list->Close())) return nullptr;
        return list;
    };
    auto draw = [&](ID2D1CommandList* list, float s, float opacity) {
        ComPtr<ID2D1Effect> blur;
        if (!list || FAILED(c.dc->CreateEffect(kGaussianBlur, &blur))) return;
        blur->SetInput(0, list);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, s);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
        c.dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                               D2D1::IdentityMatrix(), opacity),
                        nullptr);
        c.dc->DrawImage(blur.Get());
        c.dc->PopLayer();
    };
    if (c.shadow) draw(record(true).Get(), 1.6f * sigma, 0.22f);
    draw(record(false).Get(), sigma, 0.92f);
}

// ---------------------------------------------------------------------------
// Acorns, in their own colours
// ---------------------------------------------------------------------------

struct AcornShape {
    Pt at;          // where the nut's middle sits
    float scale;
    float tilt;
    float fat;      // nut width
    float len;      // nut length
    float cap;      // how far the cap comes down over the nut
    float capH;     // the cap's height
};

Pt AcornPoint(const AcornShape& a, Pt q) { return a.at + Rotate(q, a.tilt) * a.scale; }

// Outline of the nut and the cap, in the acorn's own frame (cap up), mapped by the shape.
Poly AcornNut(const AcornShape& a) {
    const float w = a.fat, l = a.len;
    Poly p = Trace({N({0.0f, -0.25f}), N({0.82f * w, -0.16f}), N({w, 0.12f}), N({0.78f * w, 0.55f * l}),
                    N({0.3f * w, 0.88f * l}), N({0.0f, 0.93f * l}), N({-0.3f * w, 0.88f * l}),
                    N({-0.78f * w, 0.55f * l}), N({-w, 0.12f}), N({-0.82f * w, -0.16f})});
    for (Pt& q : p) q = AcornPoint(a, q);
    return p;
}

Poly AcornCap(const AcornShape& a) {
    const float w = a.fat * 1.12f, h = a.capH, rim = a.cap;
    Poly p = Trace({N({0.0f, -h}), N({0.62f * w, -0.86f * h}), N({0.98f * w, -0.4f * h}), N({w, rim - 0.02f}),
                    N({0.72f * w, rim + 0.07f}), N({0.0f, rim + 0.1f}), N({-0.72f * w, rim + 0.07f}),
                    N({-w, rim - 0.02f}), N({-0.98f * w, -0.4f * h}), N({-0.62f * w, -0.86f * h})});
    for (Pt& q : p) q = AcornPoint(a, q);
    return p;
}

void DrawAcornBody(Canvas& c, const AcornShape& a) {
    ID2D1RenderTarget* rt = c.rt;
    Brushes& b = *c.brushes;
    const float r = c.r, rpx = c.Rpx() * a.scale;
    const float s = a.scale * r;
    auto P = [&](Pt q) {
        const Pt m = AcornPoint(a, q);
        return D2D1_POINT_2F{m.x * r, m.y * r};
    };
    const float tol = 0.3f / c.px;
    ComPtr<ID2D1PathGeometry> nut = MakePath(c.factory, {AcornNut(a)}, r, true, tol);
    ComPtr<ID2D1PathGeometry> cap = MakePath(c.factory, {AcornCap(a)}, r, true, tol);
    if (!nut || !cap) return;

    // The nut: glossy tan near the cap, deepening to chestnut, darker round the sides.
    b.nut->SetStartPoint(P({0.0f, 0.0f}));
    b.nut->SetEndPoint(P({0.0f, a.len}));
    rt->FillGeometry(nut.Get(), b.nut.Get());
    rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nut.Get()), nullptr);
    Place(b.nutShade.Get(), P({-0.12f * a.fat, 0.3f * a.len}), 1.15f * a.fat * s, 0.75f * a.len * s);
    rt->FillRectangle(D2D1::RectF(-2 * r, -2 * r, 2 * r, 2 * r), b.nutShade.Get());
    if (rpx >= 10.0f) {
        // Faint lengthwise stripes.
        std::vector<Poly> stripes;
        for (float u : {-0.55f, -0.2f, 0.2f, 0.55f})
            stripes.push_back({AcornPoint(a, {u * a.fat, 0.05f}), AcornPoint(a, {u * a.fat * 0.75f, 0.45f * a.len}),
                               AcornPoint(a, {u * a.fat * 0.3f, 0.85f * a.len})});
        if (auto geo = MakePath(c.factory, stripes, r, false)) {
            b.ink->SetColor(Rgb(0x3A1E0A, 0.1f * SmoothStep(10.0f, 20.0f, rpx)));
            rt->DrawGeometry(geo.Get(), b.ink.Get(), std::max(0.03f * s, 0.6f / c.px), c.round);
        }
    }
    // The gloss: a soft long highlight and a bright glint.
    const D2D1_POINT_2F hi = P({-0.45f * a.fat, 0.3f * a.len});
    Place(b.gloss.Get(), hi, 0.2f * a.fat * s, 0.32f * a.len * s, 0.55f);
    rt->FillEllipse(D2D1::Ellipse(hi, 0.2f * a.fat * s, 0.32f * a.len * s), b.gloss.Get());
    const D2D1_POINT_2F glint = P({-0.42f * a.fat, 0.16f * a.len});
    Place(b.lit.Get(), glint, 0.07f * s, 0.07f * s, 0.8f);
    rt->FillEllipse(D2D1::Ellipse(glint, 0.07f * s, 0.07f * s), b.lit.Get());
    rt->PopLayer();
    // The dark point at the tip.
    b.ink->SetColor(Rgb(0x3B2210));
    rt->FillEllipse(D2D1::Ellipse(P({0.0f, 0.9f * a.len}), std::max(0.05f * s, 0.6f / c.px),
                                  std::max(0.045f * s, 0.6f / c.px)),
                    b.ink.Get());

    // The cap: warm grey-brown, lighter on top, its rim catching the light.
    b.cap->SetStartPoint(P({-0.5f * a.fat, -a.capH}));
    b.cap->SetEndPoint(P({0.5f * a.fat, a.cap + 0.1f}));
    rt->FillGeometry(cap.Get(), b.cap.Get());
    if (rpx >= 7.0f) {
        // Overlapping scales: rows of little chevrons, each with a lit edge above a dark one.
        std::vector<Poly> marks, lights;
        const int rows = 4;
        for (int row = 0; row < rows; ++row) {
            const float y = Lerp(a.cap + 0.02f, -0.8f * a.capH, (static_cast<float>(row) + 0.3f) / rows);
            const float half =
                a.fat * 1.1f *
                std::sqrt(std::max(0.05f, 1.0f - std::pow((y - a.cap) / (a.capH + a.cap + 0.1f), 2.0f)));
            const int cols = std::max(3, static_cast<int>(half * 2.0f / 0.13f));
            for (int k = 0; k <= cols; ++k) {
                const float x = Lerp(-half, half, (static_cast<float>(k) + (row % 2 ? 0.5f : 0.0f)) / cols);
                constexpr float sw = 0.075f, sh = 0.06f;
                marks.push_back({AcornPoint(a, {x - sw, y - sh}), AcornPoint(a, {x, y}), AcornPoint(a, {x + sw, y - sh})});
                lights.push_back(Ellipse(AcornPoint(a, {x, y - 0.05f}), 0.022f * a.scale, 0.016f * a.scale, a.tilt, 6));
            }
        }
        const float show = SmoothStep(7.0f, 14.0f, rpx);
        rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), cap.Get()), nullptr);
        if (auto geo = MakePath(c.factory, marks, r, false)) {
            b.ink->SetColor(Rgb(0x2E1D0F, 0.55f * show));
            rt->DrawGeometry(geo.Get(), b.ink.Get(), std::max(0.022f * s, 0.55f / c.px), c.round);
        }
        if (auto geo = MakePath(c.factory, lights, r)) {
            b.ink->SetColor(Rgb(0xC9A87C, 0.35f * show));
            rt->FillGeometry(geo.Get(), b.ink.Get());
        }
        rt->PopLayer();
    }
    // The rim.
    if (auto rim = MakePath(c.factory,
                            {Curve(AcornPoint(a, {-a.fat * 1.08f, a.cap - 0.01f}), AcornPoint(a, {0.0f, a.cap + 0.17f}),
                                   AcornPoint(a, {a.fat * 1.08f, a.cap - 0.01f}))},
                            r, false)) {
        b.ink->SetColor(Rgb(0xB8986C, 0.7f));
        rt->DrawGeometry(rim.Get(), b.ink.Get(), std::max(0.035f * s, 0.7f / c.px), c.round);
    }
}

void DrawAcorn(Canvas& c, int variant) {
    std::vector<AcornShape> acorns;
    std::vector<Poly> stalks;
    switch (variant) {
    case 1: acorns.push_back({{0.0f, 0.1f}, 0.95f, Deg(7.0f), 0.42f, 0.66f, 0.12f, 0.42f}); break;  // round, deep cap
    case 2: acorns.push_back({{0.0f, 0.02f}, 0.92f, Deg(-12.0f), 0.3f, 0.92f, -0.05f, 0.24f}); break;  // slender, shallow cap
    case 3:  // a pair, on one short stalk
        acorns.push_back({{-0.27f, 0.22f}, 0.66f, Deg(-26.0f), 0.36f, 0.78f, 0.04f, 0.36f});
        acorns.push_back({{0.27f, 0.27f}, 0.64f, Deg(22.0f), 0.36f, 0.76f, 0.04f, 0.36f});
        stalks.push_back(Curve({0.0f, -0.3f}, {0.02f, -0.42f}, {0.06f, -0.56f}, 8));
        break;
    default: acorns.push_back({{0.0f, 0.06f}, 0.95f, Deg(-8.0f), 0.36f, 0.78f, 0.04f, 0.36f}); break;
    }
    for (const AcornShape& a : acorns) {
        const Pt top = AcornPoint(a, {0.0f, -a.capH + 0.02f});
        const Pt end = variant == 3 ? Pt{0.0f, -0.3f} : AcornPoint(a, {0.06f, -a.capH - 0.2f});
        stalks.push_back(Curve(top, Mix(top, end, 0.5f) + Pt{0.02f, -0.02f}, end, 8));
    }
    const float r = c.r;
    std::vector<Poly> bands;
    for (const Poly& s : stalks) bands.push_back(Ribbon(s, 0.065f, 0.05f));
    if (c.Halo()) {
        std::vector<Poly> parts = bands;
        for (const AcornShape& a : acorns) {
            parts.push_back(AcornNut(a));
            parts.push_back(AcornCap(a));
        }
        DrawHalo(c, MakePath(c.factory, parts, r, true, 0.7f / c.px).Get());
    }
    if (!c.Body()) return;
    if (auto stalk = MakePath(c.factory, bands, r)) {
        c.brushes->ink->SetColor(Rgb(0x4A3220));
        c.rt->FillGeometry(stalk.Get(), c.brushes->ink.Get());
    }
    for (const AcornShape& a : acorns) DrawAcornBody(c, a);
}

// Draws a design (its halo, its body or both, as the canvas says) centred on the origin of
// the current transform.
void DrawDesign(Canvas& c, LeafKind kind, int variant, const LeafPlan* plan, bool blush) {
    if (kind == LeafKind::Acorn) {
        if (!blush) DrawAcorn(c, Clamp(variant, 0, kVariantCounts[static_cast<int>(kind)] - 1));
        return;
    }
    LeafPlan local;
    if (!plan) {
        local = MakePlan(kind, Clamp(variant, 0, LeafVariantCount(kind) - 1), c.factory);
        plan = &local;
    }
    if (plan->blade.empty()) return;
    if (plan->blurred) {
        if (!blush) DrawPlanBlurred(c, *plan);
        return;
    }
    DrawPlanSharp(c, *plan, blush);
}

// ---------------------------------------------------------------------------
// Pixel-art leaves
// ---------------------------------------------------------------------------

// Halo for a pixel-art leaf at its source resolution: the sprite's alpha grown by one pixel,
// in black, and cleared again under the art itself so its translucent pixels are not greyed.
// Scaled up with the art it stays pixel-crisp.
ComPtr<ID2D1Bitmap> MakePixelHalo(ID2D1RenderTarget* rt, ID2D1Bitmap* art) {
    const D2D1_SIZE_U size = art->GetPixelSize();
    const D2D1_SIZE_U haloSize = {size.width + 2, size.height + 2};
    const D2D1_SIZE_F dips = {static_cast<float>(haloSize.width), static_cast<float>(haloSize.height)};
    const D2D1_PIXEL_FORMAT format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    ComPtr<ID2D1BitmapRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> ink;
    if (FAILED(rt->CreateCompatibleRenderTarget(&dips, &haloSize, &format, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                                                &target)) ||
        FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &ink)))
        return nullptr;
    ComPtr<ID2D1DeviceContext> maxBlend;
    target.As(&maxBlend);

    static constexpr struct {
        float dx, dy, weight;
    } kTaps[] = {{0, 0, 1.0f},    {-1, 0, 0.85f},  {1, 0, 0.85f},  {0, -1, 0.85f}, {0, 1, 0.85f},
                 {-1, -1, 0.55f}, {1, -1, 0.55f}, {-1, 1, 0.55f}, {1, 1, 0.55f}};
    target->BeginDraw();
    target->Clear(D2D1::ColorF(0, 0, 0, 0));
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);  // required by FillOpacityMask
    if (maxBlend) maxBlend->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_MAX);
    for (const auto& tap : kTaps) {
        // Whole-pixel offsets at 1:1 copy the alpha exactly.
        ink->SetColor(D2D1::ColorF(0, 0, 0, kHaloAlpha * tap.weight * (maxBlend ? 1.0f : 0.45f)));
        D2D1_RECT_F rc = {1.0f + tap.dx, 1.0f + tap.dy, 1.0f + tap.dx + static_cast<float>(size.width),
                          1.0f + tap.dy + static_cast<float>(size.height)};
        target->FillOpacityMask(art, ink.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &rc, nullptr);
    }
    if (maxBlend) {
        maxBlend->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        // DrawImage sizes bitmaps by their DPI: scale so that one art pixel covers one halo pixel.
        float dpiX = 96.0f, dpiY = 96.0f;
        art->GetDpi(&dpiX, &dpiY);
        maxBlend->SetTransform(D2D1::Matrix3x2F::Scale(dpiX / 96.0f, dpiY / 96.0f) *
                               D2D1::Matrix3x2F::Translation(1.0f, 1.0f));
        const D2D1_POINT_2F origin = {0.0f, 0.0f};
        maxBlend->DrawImage(art, &origin, nullptr, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
                            D2D1_COMPOSITE_MODE_DESTINATION_OUT);
        maxBlend->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    ComPtr<ID2D1Bitmap> bitmap;
    if (FAILED(target->EndDraw()) || FAILED(target->GetBitmap(&bitmap))) return nullptr;
    return bitmap;
}

// Moves `rc` so that its top-left corner sits on a device pixel corner (when the
// transform has no rotation), so that whole-pixel scaling stays crisp.
void SnapToPixels(ID2D1RenderTarget* rt, D2D1_RECT_F& rc) {
    D2D1_MATRIX_3X2_F m;
    rt->GetTransform(&m);
    if (m._12 != 0.0f || m._21 != 0.0f || m._11 == 0.0f || m._22 == 0.0f) return;
    float dpiX = 96.0f, dpiY = 96.0f;
    rt->GetDpi(&dpiX, &dpiY);
    const float sx = m._11 * dpiX / 96.0f, sy = m._22 * dpiY / 96.0f;
    const float x = (m._11 * rc.left + m._31) * dpiX / 96.0f;
    const float y = (m._22 * rc.top + m._32) * dpiY / 96.0f;
    const float dx = (std::round(x) - x) / sx, dy = (std::round(y) - y) / sy;
    rc = {rc.left + dx, rc.top + dy, rc.right + dx, rc.bottom + dy};
}

// Draws a pixel-art leaf (and its halo, if given) centred on the origin, fitting radius r.
// Enlargements use whole-pixel nearest-neighbour scaling.
void DrawPixel(ID2D1RenderTarget* rt, ID2D1Bitmap* art, ID2D1Bitmap* halo, float r, float pxScale) {
    const D2D1_SIZE_U size = art->GetPixelSize();
    if (size.width == 0 || size.height == 0) return;
    const float srcPx = static_cast<float>(std::max(size.width, size.height));
    float k = 2.0f * r * pxScale / srcPx;  // device px per source pixel
    const bool crisp = k >= 1.0f;
    if (crisp) k = std::floor(k + 1e-3f);
    const float px = k / pxScale;  // one source pixel in local units
    D2D1_RECT_F dst = {-0.5f * px * static_cast<float>(size.width), -0.5f * px * static_cast<float>(size.height),
                       0.5f * px * static_cast<float>(size.width), 0.5f * px * static_cast<float>(size.height)};
    if (crisp) SnapToPixels(rt, dst);

    ComPtr<ID2D1DeviceContext> dc;
    if (!crisp) rt->QueryInterface(IID_PPV_ARGS(&dc));
    auto draw = [&](ID2D1Bitmap* bmp, const D2D1_RECT_F& rc) {
        if (crisp) rt->DrawBitmap(bmp, rc, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        else if (dc) dc->DrawBitmap(bmp, &rc, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
        else rt->DrawBitmap(bmp, rc, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    if (halo) {
        const D2D1_SIZE_U hs = halo->GetPixelSize();
        const float gx = 0.5f * px * static_cast<float>(hs.width - size.width);
        const float gy = 0.5f * px * static_cast<float>(hs.height - size.height);
        draw(halo, {dst.left - gx, dst.top - gy, dst.right + gx, dst.bottom + gy});
    }
    draw(art, dst);
}

// Restores a device context's target and drawing state when the atlas has to borrow it.
class ContextStateGuard {
public:
    explicit ContextStateGuard(ID2D1DeviceContext* dc) : dc_(dc) {
        if (!dc_) return;
        dc_->GetTarget(&target_);
        dc_->GetTransform(&transform_);
        dc_->GetDpi(&dpiX_, &dpiY_);
        unit_ = dc_->GetUnitMode();
        antialias_ = dc_->GetAntialiasMode();
        blend_ = dc_->GetPrimitiveBlend();
    }
    ~ContextStateGuard() {
        if (!dc_) return;
        dc_->SetTarget(target_.Get());
        dc_->SetTransform(transform_);
        dc_->SetDpi(dpiX_, dpiY_);
        dc_->SetUnitMode(unit_);
        dc_->SetAntialiasMode(antialias_);
        dc_->SetPrimitiveBlend(blend_);
    }
    ContextStateGuard(const ContextStateGuard&) = delete;
    ContextStateGuard& operator=(const ContextStateGuard&) = delete;

private:
    ID2D1DeviceContext* dc_;
    ComPtr<ID2D1Image> target_;
    D2D1_MATRIX_3X2_F transform_{};
    float dpiX_ = 96.0f, dpiY_ = 96.0f;
    D2D1_UNIT_MODE unit_ = D2D1_UNIT_MODE_DIPS;
    D2D1_ANTIALIAS_MODE antialias_ = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    D2D1_PRIMITIVE_BLEND blend_ = D2D1_PRIMITIVE_BLEND_SOURCE_OVER;
};

// A second context on dc's device, for recording the command lists that blurs work on.
ComPtr<ID2D1DeviceContext> MakeRecorder(ID2D1DeviceContext* dc) {
    ComPtr<ID2D1Device> device;
    ComPtr<ID2D1DeviceContext> rec;
    if (dc) dc->GetDevice(&device);
    if (device && SUCCEEDED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rec))) {
        rec->SetUnitMode(D2D1_UNIT_MODE_DIPS);
        rec->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    return rec;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const LeafTraits& GetLeafTraits(LeafKind kind) {
    int k = static_cast<int>(kind);
    return kTraits[(k >= 0 && k < kLeafKindCount) ? k : 0];
}

int LeafVariantCount(LeafKind kind) {
    int k = static_cast<int>(kind);
    return (k >= 0 && k < kLeafKindCount) ? kVariantCounts[k] : 1;
}

HRESULT LoadPixelLeaf(IWICImagingFactory* wic, int index, IWICBitmapSource** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!wic || index < 0 || index >= kPixelCount) return E_INVALIDARG;

    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(module, MAKEINTRESOURCEW(IDR_PIXEL_FIRST + index), RT_RCDATA);
    if (!res) return HRESULT_FROM_WIN32(GetLastError());
    HGLOBAL handle = LoadResource(module, res);
    DWORD size = SizeofResource(module, res);
    void* data = handle ? LockResource(handle) : nullptr;
    if (!data || size == 0) return E_FAIL;

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    ComPtr<IWICBitmap> bitmap;
    HRESULT hr = wic->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(static_cast<BYTE*>(data), size);
    if (SUCCEEDED(hr)) hr = wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeMedianCut);
    // Decode now so the result does not depend on the stream.
    if (SUCCEEDED(hr)) hr = wic->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &bitmap);
    if (SUCCEEDED(hr)) *out = bitmap.Detach();
    return hr;
}

void DrawLeafDesign(ID2D1RenderTarget* rt, LeafKind kind, int variant, float cx, float cy, float r,
                    const LeafArtOptions& opt, ID2D1Bitmap* pixel, bool blush) {
    const int k = static_cast<int>(kind);
    if (!rt || !(r > 0.0f) || k < 0 || k >= kLeafKindCount || (blush && !kBlush[k])) return;
    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    float dpiX = 96.0f, dpiY = 96.0f;
    rt->GetDpi(&dpiX, &dpiY);
    const float scale = std::sqrt(std::fabs(old._11 * old._22 - old._12 * old._21));
    const float pxScale = std::max(scale * dpiX / 96.0f, 1e-3f);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(cx, cy) * old);

    if (IsPixel(kind)) {
        if (pixel) {
            ComPtr<ID2D1Bitmap> halo;
            if (opt.shadow) halo = MakePixelHalo(rt, pixel);
            DrawPixel(rt, pixel, halo.Get(), r, pxScale);
        }
    } else {
        ComPtr<ID2D1Factory> factory;
        rt->GetFactory(&factory);
        ComPtr<ID2D1DeviceContext> dc;
        rt->QueryInterface(IID_PPV_ARGS(&dc));
        ComPtr<ID2D1DeviceContext> rec = MakeRecorder(dc.Get());
        ComPtr<ID2D1StrokeStyle> round = factory ? RoundStroke(factory.Get()) : nullptr;
        Brushes brushes, recBrushes;
        Canvas c;
        c.rt = rt;
        c.dc = dc.Get();
        c.rec = rec.Get();
        c.factory = factory.Get();
        c.round = round.Get();
        c.brushes = &brushes;
        c.recBrushes = rec && SUCCEEDED(recBrushes.Create(rec.Get())) ? &recBrushes : nullptr;
        c.r = r;
        c.px = pxScale;
        c.shadow = opt.shadow && !blush;
        if (!factory || FAILED(brushes.Create(rt))) {
            // Nothing to draw with.
        } else if (c.shadow && dc) {
            // The halo passes merge with MAX blending, which needs a transparent
            // backdrop: draw the leaf into a layer and composite that.
            const float reach = 1.1f * r + (HaloPx(r * pxScale) + 2.0f) / pxScale;
            dc->PushLayer(D2D1::LayerParameters1(D2D1::RectF(-reach, -reach, reach, reach)), nullptr);
            c.maxBlend = true;
            DrawDesign(c, kind, variant, nullptr, false);
            dc->PopLayer();
        } else {
            DrawDesign(c, kind, variant, nullptr, blush);
        }
    }
    rt->SetTransform(old);
}

// ---------------------------------------------------------------------------
// LeafAtlas
// ---------------------------------------------------------------------------

int LeafAtlas::SpriteIndex(LeafKind kind, int variant) {
    int k = static_cast<int>(kind);
    if (k < 0 || k >= kLeafKindCount) return 0;
    return kSpriteOffset[k] + Clamp(variant, 0, kVariantCounts[k] - 1);
}

int LeafAtlas::BlushIndex(LeafKind kind, int variant) {
    int k = static_cast<int>(kind);
    if (k < 0 || k >= kLeafKindCount || !kBlush[k]) return -1;
    return kBlushOffset[k] + Clamp(variant, 0, kVariantCounts[k] - 1);
}

int LeafAtlas::SpriteCount() { return kSpriteTotal; }

void LeafAtlas::Reset() {
    levels_.clear();
    pages_.clear();
    diameter_.clear();
    pixelDiameter_.clear();
    device_.Reset();
}

const LeafAtlas::Sprite& LeafAtlas::Get(int index, float diameterPx) const {
    static const Sprite kNone{};
    if (levels_.empty() || index < 0 || index >= kSpriteTotal) return kNone;
    const std::vector<float>& diam = IsPixelSprite(index) ? pixelDiameter_ : diameter_;
    const int last = static_cast<int>(levels_.size()) - 1;
    const float d = diameterPx > 1.0f ? diameterPx : 1.0f;  // also catches NaN
    // Procedural level l is drawn at kTopDiameter / sqrt(2)^l: estimate the level, then settle on
    // the smallest one that is still at least as large as the target.
    int l = std::min(last, static_cast<int>(2.0f * std::log2(std::max(kTopDiameter / d, 1.0f))));
    while (l > 0 && diam[static_cast<size_t>(l)] < d) --l;
    while (l < last && diam[static_cast<size_t>(l) + 1] >= d) ++l;
    return levels_[static_cast<size_t>(l)].sprites[static_cast<size_t>(index)];
}

HRESULT LeafAtlas::Build(ID2D1DeviceContext* dc, IWICImagingFactory* wic, const LeafArtOptions& opt) {
    if (!dc) return E_INVALIDARG;
    // All or nothing: a failed rebuild keeps the previous atlas, unless that
    // one belongs to another device (then it is useless anyway).
    ComPtr<ID2D1Device> device;
    dc->GetDevice(&device);
    if (device != device_) Reset();

    // Draw with a private context on the same device so the caller's target and state stay
    // untouched. Should that fail, borrow the caller's context and restore it afterwards.
    ComPtr<ID2D1DeviceContext> rdc;
    if (device) device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &rdc);
    const bool borrowed = !rdc;
    if (borrowed) rdc = dc;
    ContextStateGuard guard(borrowed ? dc : nullptr);
    rdc->SetDpi(96.0f, 96.0f);
    rdc->SetUnitMode(D2D1_UNIT_MODE_DIPS);
    rdc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    rdc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ComPtr<ID2D1DeviceContext> rec = MakeRecorder(rdc.Get());
    ComPtr<ID2D1Factory> factory;
    rdc->GetFactory(&factory);
    if (!factory) return E_FAIL;
    ComPtr<ID2D1StrokeStyle> round = RoundStroke(factory.Get());
    Brushes brushes, recBrushes;
    HRESULT hr = brushes.Create(rdc.Get());
    if (FAILED(hr)) return hr;
    const bool recorder = rec && SUCCEEDED(recBrushes.Create(rec.Get()));

    ComPtr<ID2D1Bitmap1> pixel[kPixelCount];
    ComPtr<ID2D1Bitmap> pixelHalo[kPixelCount];
    if (wic) {
        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        for (int i = 0; i < kPixelCount; ++i) {
            ComPtr<IWICBitmapSource> src;
            if (FAILED(LoadPixelLeaf(wic, i, &src)) ||
                FAILED(rdc->CreateBitmapFromWicBitmap(src.Get(), &props, &pixel[i])))
                continue;
            if (opt.shadow) pixelHalo[i] = MakePixelHalo(rdc.Get(), pixel[i].Get());
        }
    }

    // Every design's plan, made once for all levels.
    std::vector<LeafPlan> plans(static_cast<size_t>(kFirstPixelSprite));
    for (int i = 0; i < kFirstPixelSprite; ++i) {
        const SpriteRef& ref = kSpriteRefs[static_cast<size_t>(i)];
        if (ref.kind != LeafKind::Acorn) plans[static_cast<size_t>(i)] = MakePlan(ref.kind, ref.variant, factory.Get());
    }

    // Plan one cell per sprite and level. Pixel-art levels that repeat a whole-pixel scale
    // share a cell, as do the levels above a kind's largest and a blush's levels below 8 px.
    struct Cell {
        int level, sprite;
        float d;  // drawn leaf diameter (px)
        int size;
        size_t page;
        D2D1_RECT_U rc;
    };
    std::vector<Cell> cells;
    std::vector<int> cellOf(static_cast<size_t>(kLevelCount * kSpriteTotal), -1);
    for (int l = 0; l < kLevelCount; ++l) {
        const float dProc = LevelDiameter(l);
        const float k = PixelScale(l);
        const bool reusePixel = l > 0 && k >= 1.0f && k == PixelScale(l - 1);
        for (int i = 0; i < kSpriteTotal; ++i) {
            const SpriteRef& ref = kSpriteRefs[static_cast<size_t>(i)];
            const bool isPixel = IsPixelSprite(i);
            if ((isPixel && reusePixel) || (!isPixel && l < kTopLevel[static_cast<int>(ref.kind)]) ||
                (ref.blush && l > kLastBlushLevel))
                continue;
            const float d = isPixel ? kPixelSize * k : dProc;
            const float halo = !opt.shadow || ref.blush ? 0.0f : isPixel ? k : HaloPx(0.5f * d);
            int size = static_cast<int>(std::ceil(d + 2.0f * (halo + static_cast<float>(kCellPad))));
            // Whole-pixel pixel art: same parity as the art so it lands on the pixel grid.
            if (isPixel && k >= 1.0f && (size & 1) != (static_cast<int>(std::lround(d)) & 1)) ++size;
            cellOf[static_cast<size_t>(l * kSpriteTotal + i)] = static_cast<int>(cells.size());
            cells.push_back({l, i, d, size, 0, {}});
        }
    }
    for (int i = 0; i < kSpriteTotal; ++i) {
        const int top = IsPixelSprite(i) ? 0 : kTopLevel[static_cast<int>(kSpriteRefs[static_cast<size_t>(i)].kind)];
        for (int l = 0; l < kLevelCount; ++l) {
            int& cell = cellOf[static_cast<size_t>(l * kSpriteTotal + i)];
            if (cell >= 0) continue;
            cell = l < top ? cellOf[static_cast<size_t>(top * kSpriteTotal + i)]
                           : cellOf[static_cast<size_t>((l - 1) * kSpriteTotal + i)];
        }
    }
    for (const int cell : cellOf)
        if (cell < 0) return E_UNEXPECTED;

    // Shelf-pack every level onto as few pages as the device allows.
    const UINT32 maxHeight = std::min(kMaxPageHeight, rdc->GetMaximumBitmapSize());
    std::vector<UINT32> pageHeights(1, 0);
    UINT32 x = 0, y = 0, rowHeight = 0;
    for (Cell& c : cells) {
        const UINT32 size = static_cast<UINT32>(c.size);
        if (x + size > kPageWidth) {
            x = 0;
            y += rowHeight;
            rowHeight = 0;
        }
        if (y + size > maxHeight) {
            pageHeights.push_back(0);
            x = y = rowHeight = 0;
        }
        c.page = pageHeights.size() - 1;
        c.rc = {x, y, x + size, y + size};
        x += size;
        rowHeight = std::max(rowHeight, size);
        pageHeights.back() = std::max(pageHeights.back(), y + size);
    }

    std::vector<ComPtr<ID2D1Bitmap1>> pages(pageHeights.size());
    const D2D1_BITMAP_PROPERTIES1 pageProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    for (size_t p = 0; p < pages.size(); ++p) {
        hr = rdc->CreateBitmap(D2D1::SizeU(kPageWidth, pageHeights[p]), nullptr, 0, pageProps, &pages[p]);
        if (FAILED(hr)) return hr;
        rdc->SetTarget(pages[p].Get());
        rdc->BeginDraw();
        rdc->Clear(D2D1::ColorF(0, 0, 0, 0));
        auto drawCells = [&](Phase phase) {
            for (const Cell& c : cells) {
                const SpriteRef& ref = kSpriteRefs[static_cast<size_t>(c.sprite)];
                const bool isPixel = IsPixelSprite(c.sprite);
                if (c.page != p || (phase == Phase::Halo && (isPixel || ref.blush))) continue;
                const float left = static_cast<float>(c.rc.left), top = static_cast<float>(c.rc.top);
                const float size = static_cast<float>(c.size);
                rdc->SetTransform(D2D1::Matrix3x2F::Identity());
                // A transparent border keeps bilinear sampling and the aliased quad edges clean.
                const float b = static_cast<float>(kClearBorder);
                rdc->PushAxisAlignedClip(D2D1::RectF(left + b, top + b, left + size - b, top + size - b),
                                         D2D1_ANTIALIAS_MODE_ALIASED);
                rdc->SetTransform(D2D1::Matrix3x2F::Translation(left + 0.5f * size, top + 0.5f * size));
                if (isPixel) {
                    const size_t pi = static_cast<size_t>(c.sprite - kFirstPixelSprite);
                    if (pixel[pi]) DrawPixel(rdc.Get(), pixel[pi].Get(), pixelHalo[pi].Get(), 0.5f * c.d, 1.0f);
                } else {
                    Canvas canvas;
                    canvas.rt = rdc.Get();
                    canvas.dc = rdc.Get();
                    canvas.rec = rec.Get();
                    canvas.factory = factory.Get();
                    canvas.round = round.Get();
                    canvas.brushes = &brushes;
                    canvas.recBrushes = recorder ? &recBrushes : nullptr;
                    canvas.maxBlend = true;
                    canvas.phase = phase;
                    canvas.r = 0.5f * c.d;
                    canvas.px = 1.0f;
                    canvas.shadow = opt.shadow && !ref.blush;
                    const int base = kSpriteOffset[static_cast<int>(ref.kind)] + ref.variant;
                    DrawDesign(canvas, ref.kind, ref.variant, &plans[static_cast<size_t>(base)], ref.blush);
                }
                rdc->SetTransform(D2D1::Matrix3x2F::Identity());
                rdc->PopAxisAlignedClip();
            }
        };
        // Every halo on the page first: the page starts transparent, so their passes can merge
        // with MAX blending throughout. Then the leaves over them.
        if (opt.shadow) {
            rdc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_MAX);
            drawCells(Phase::Halo);
            rdc->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        }
        drawCells(Phase::Body);
        hr = rdc->EndDraw();
        rdc->SetTarget(nullptr);
        if (FAILED(hr)) return hr;
    }

    std::vector<Level> levels(kLevelCount);
    std::vector<float> diameter(kLevelCount), pixelDiameter(kLevelCount);
    for (int l = 0; l < kLevelCount; ++l) {
        Level& level = levels[static_cast<size_t>(l)];
        level.sprites.resize(kSpriteTotal);
        for (int i = 0; i < kSpriteTotal; ++i) {
            const Cell& c = cells[static_cast<size_t>(cellOf[static_cast<size_t>(l * kSpriteTotal + i)])];
            Sprite& sp = level.sprites[static_cast<size_t>(i)];
            sp.bitmap = pages[c.page].Get();
            sp.src = c.rc;
            sp.extent = static_cast<float>(c.size) / c.d;
        }
        diameter[static_cast<size_t>(l)] = LevelDiameter(l);
        pixelDiameter[static_cast<size_t>(l)] = kPixelSize * PixelScale(l);
    }
    pages_ = std::move(pages);
    levels_ = std::move(levels);
    diameter_ = std::move(diameter);
    pixelDiameter_ = std::move(pixelDiameter);
    device_ = std::move(device);
    return S_OK;
}
