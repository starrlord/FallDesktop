#include "LeafPile.h"

#include <bit>

#include "Noise.h"
#include "Palette.h"

namespace {

// ---------------------------------------------------------------------------
// Tunables. Lengths and heights are DIPs and are scaled by dpiScale where
// used, so the pile behaves and looks the same at every display scale.
// ---------------------------------------------------------------------------

// Deposition: a landed leaf adds volume ~ d^1.3 (sub-linear in its area, so a
// few big leaves don't dominate). Leaves lie loose and airy: part of it goes
// into a wide, low swell and a good part stays as a small heap, which keeps
// the surface lumpy.
constexpr float kLeafVolume = 2.4f;                    // DIP^2 from a 1-DIP leaf
constexpr float kLeafVolumeExp = 1.3f;
constexpr float kSpreadMin = 0.9f, kSpreadMax = 1.8f;  // swell half-width / leaf diameter
constexpr float kHeapShare = 0.4f;                     // share of the volume left as a heap
constexpr float kHeapMin = 0.45f, kHeapMax = 0.85f;    // heap half-width / leaf diameter
constexpr float kCapKnee = 0.6f;                       // fill level where leaves start to roll on
constexpr float kLeeReach = 36.0f;                     // a crest shelters spots this far downwind
constexpr float kLeeBonus = 0.9f;                      // extra catch in a crest's lee in a stiff breeze
constexpr float kShelterTan = 0.3f;                    // slope of the sheltered zone behind a crest

// Where leaves gather: broad swells plus sparse spots that build real heaps.
// The pattern drifts slowly. Simplex noise is not periodic, so rather than
// wrapping time (the pattern would jump) the sample point travels around a
// circle in noise space: seamless however long the app runs (a lap is ~4 days).
constexpr float kSpotCatch = 3.0f;
constexpr double kFieldSpeed = 0.0012;  // noise units per second
constexpr float kFieldOrbit = 64.0f;    // radius of that circle (noise units)
constexpr float kEndTaper = 140.0f;     // the pile thins out over this distance to an open end

// Settling: leaves catch on each other, so a pile keeps steep, ragged shapes
// and only crests sharper than a small radius tumble and round off; sharp
// hollows fill in, and a faint creep softens the rest. Flux-form diffusion
// whose rate rises with the local curvature: mass conserving, and stable with
// substeps.
constexpr float kCreepBase = 0.3f;    // DIP^2/s everywhere
constexpr float kCreepCrest = 5.0f;   // DIP^2/s more on crests sharper than the limit
constexpr float kCreepFoot = 2.5f;    // DIP^2/s more in hollows sharper than the limit
constexpr float kCrestCurv = 0.045f;  // 1/DIP: tightest crest curvature (varies along x)
constexpr float kFootCurv = 0.15f;    // 1/DIP
constexpr float kCreepCfl = 0.45f;    // stability bound of one explicit substep
constexpr float kDust = 1.0e-4f;      // px; thinner litter is dropped

// Tumbling: fixed-rate relaxation sweeps; each moves half of the excess slope
// between neighbours (thermal-erosion style, mass conserving). Interlocking
// leaves stand steeper than sand or grain.
constexpr float kSweepRate = 240.0f;  // sweeps per second
constexpr int kMaxSweeps = 32;
constexpr float kRelax = 0.5f;
constexpr float kReposeMinDeg = 42.0f, kReposeMaxDeg = 57.0f;

// Wind: the flux of leaves bowling along relaxes toward a capacity that is
// higher on windward slopes and crests and zero in a crest's lee. Leaves are
// light and catch the wind: they move in gentler breezes, are stripped off
// crests first and drop out again soon behind them.
constexpr float kWindTick = 1.0f / 30.0f;
constexpr int kMaxWindTicks = 4;
constexpr float kWindThreshold = 24.0f;  // DIP/s before leaves start to move
constexpr float kFluxScale = 0.009f;     // DIP^2/s per (DIP/s)^1.5 of excess wind
constexpr float kSaturationLen = 7.0f;   // how quickly the flux adapts to the surface
constexpr float kStossGain = 2.5f;
constexpr float kCrestGain = 1.8f;

// Crumbling: leaves slowly break up and blow away (exponential decay, faster
// on tall exposed tops), plus a little scatter so stray leaves eventually go.
constexpr float kCrumbleTau = 300.0f;   // s
constexpr float kPeakCrumble = 0.9f;
constexpr float kTallCrumble = 0.8f;
constexpr float kScatter = 0.003f;      // DIP/s
constexpr float kCrumblePeriod = 0.5f;  // every crumble block is visited this often (s)
constexpr float kOverCapTau = 3.0f;     // leaves above their local cap settle over this time (s)
constexpr float kCapEaseTau = 0.8f;     // a lowered cap eases down with this time constant

// Blowing away (BlowAway): a front crosses the strip with the wind in this
// share of the time; behind it each stretch empties in the rest.
constexpr float kBlowSweep = 0.55f;

// Rendering.
constexpr float kVisibleMin = 0.05f;    // px; a thinner pile shows nothing
constexpr float kRedrawEps = 0.1f;      // px of change that triggers a re-shade
constexpr float kShadowLen = 5.0f;      // the cast shadow fades out over this
constexpr float kShadowAlpha = 0.34f;
constexpr float kAoRadius = 8.0f;       // neighbourhood for concavity / ambient occlusion
constexpr float kPeakRadius = 12.0f;
constexpr int kTopMarginPx = 2;
constexpr float kLayoutQuantum = 8.0f;  // the strip's height above the ground grows in these steps
constexpr float kSlopeDepth = 2.6f;     // how far below the surface its slope still shows
constexpr float kFrontLight = 0.86f;    // the pile's face, relative to its flat top
constexpr float kAoDepth = 7.0f;        // depth where the occlusion is half way
constexpr float kAoStrength = 0.55f;    // how dark the depths of the pile get
constexpr float kTopLight = 0.1f;       // leaves standing out of the surface catch a little more
constexpr float kFormRadius = 10.0f;    // the slope of a mound as a whole is taken over this
constexpr float kFormLight = 0.2f;      // how much brighter its side facing the light is

// Shade work per call, in blocks of LeafPile::kShadeBlock columns: clusters of
// dirty blocks (bridging clean gaps of up to kMaxGap blocks), round robin,
// until the budget is used. While the whole strip moves (blowing away, a
// lowered cap easing down) it is swept left to right in as few equal passes
// of at most kAnimBlocks as possible, so at most one seam shows.
constexpr int kShadeBudget = 40;
constexpr int kMaxGap = 3;
constexpr int kAnimBlocks = 80;

// The litter: leaves scattered over a grid of cells fixed in DIP space, so the
// same leaves lie in the same places whatever the strip's size. Each shows
// while the surface where it lies is above its key, which sits between its
// centre and its top, so it pokes out of the surface: a ragged top edge of
// leaf silhouettes. Leaves lower down lie in front of the ones above them, as
// on a slope facing the viewer, so the front-most leaf at a pixel is the one
// most likely to be showing.
constexpr float kCell = 6.0f;              // DIP
constexpr float kPerCell = 3.6f;           // leaves per cell: about 3 deep at any point
constexpr float kPokeMax = 6.0f;           // a leaf stands at most this far above the surface
constexpr float kMinKey = 0.35f;           // the first leaves show once the pile is this deep
constexpr float kLipCell = 4.0f;           // a leaf may hang over the edge from each stretch this wide
constexpr float kLipHang = 5.0f;           // ... and reach this far below it
constexpr float kMapQuantum = 24.0f;       // the litter map grows in these steps
constexpr float kEdgeShade = 0.68f;        // a leaf just past the edge of one in front of it
constexpr float kBackShade = 0.7f;         // leaves further back, seen through gaps
constexpr float kBackDepth = 1.5f;         // ... this far under the surface and deeper
constexpr float kGeneSpread = 0.55f;       // share of the palette one stretch of the pile draws on
constexpr float kContact = 0.3f;           // shade of the leaves in front on the ones behind
constexpr float kLeafShadowAlpha = 0.32f;  // hanging leaves' shadows on the taskbar

struct Rgb {
    float r, g, b;
};
constexpr Rgb kChink = {0.2f, 0.12f, 0.06f};       // the dark gaps between leaves deep in the pile
constexpr Rgb kShadow = {0.05f, 0.03f, 0.018f};    // warm shadow on the taskbar
// Dead leaves, long since turned brown (base, blush).
constexpr uint32_t kDeadTones[4][2] = {
    {0x6E4326, 0x8A5A34}, {0x845232, 0xA06A3C}, {0x5C3A22, 0x76502E}, {0x93643A, 0xB0844C}};

// Light from the upper left (unit vector in the profile plane, y up).
constexpr float kLx = -0.42f, kLy = 0.907f;

enum Look : uint8_t { kFresh, kUnderside, kDead };
enum Shape : uint8_t { kOval, kRounded, kNarrow, kOak, kMaple };

// Leaf outlines: half-width profiles from base (t = 0) to tip (t = 1) of the
// form c t^a (1 - t)^b, widest at t = a / (a + b), the oak's with rounded
// lobes; the maple's is polar, five pointed lobes around the heart of the leaf.
constexpr int kProfN = 128;
constexpr int kPolarN = 256;
struct Lobe {
    float at, len, half;  // angle from the tip, reach (of the longest), angular half-width
};
constexpr Lobe kMapleLobes[] = {
    {0.0f, 1.0f, 0.62f}, {0.98f, 0.86f, 0.58f}, {-0.98f, 0.86f, 0.58f}, {1.95f, 0.5f, 0.5f}, {-1.95f, 0.5f, 0.5f}};
constexpr float kMapleSinus = 0.34f;

struct ShapeTables {
    float w[4][kProfN + 2];   // half width / the widest, at t = i / kProfN
    float dw[4][kProfN + 2];  // dw/dt
    float r[kPolarN + 2];     // maple outline radius / the longest lobe, by angle from the tip
    float dr[kPolarN + 2];    // dr/dtheta
};

const ShapeTables& Shapes() {
    static const ShapeTables tables = [] {
        ShapeTables t{};
        const float ab[4][2] = {{0.5f, 0.85f}, {0.6f, 0.95f}, {0.75f, 1.05f}, {0.62f, 0.7f}};
        for (int k = 0; k < 4; ++k) {
            const float a = ab[k][0], b = ab[k][1];
            const float peak = a / (a + b);
            const float c = 1.0f / (std::pow(peak, a) * std::pow(1.0f - peak, b));
            for (int i = 0; i <= kProfN + 1; ++i) {
                const float x = std::min(static_cast<float>(i) / kProfN, 1.0f);
                float w = c * std::pow(x, a) * std::pow(1.0f - x, b);
                if (k == kOak) w *= 0.72f + 0.28f * std::pow(std::fabs(std::cos(x * 4.6f * kPi + 0.35f)), 0.6f);
                t.w[k][i] = w;
            }
            for (int i = 0; i <= kProfN + 1; ++i) {
                const int i0 = std::max(i - 1, 0), i1 = std::min(i + 1, kProfN);
                t.dw[k][i] = (t.w[k][i1] - t.w[k][i0]) * static_cast<float>(kProfN) / static_cast<float>(i1 - i0);
            }
        }
        for (int i = 0; i <= kPolarN + 1; ++i) {
            const float th = -kPi + kTau * static_cast<float>(std::min(i, kPolarN)) / kPolarN;
            float r = kMapleSinus;
            for (const Lobe& l : kMapleLobes) {
                const float z = 1.0f - std::fabs(th - l.at) / l.half;
                if (z > 0.0f) r = std::max(r, kMapleSinus + (l.len - kMapleSinus) * std::pow(z, 1.5f));
            }
            // A few teeth along the lobes.
            r += 0.035f * std::fabs(std::sin(th * 11.0f)) * SmoothStep(2.6f, 2.1f, std::fabs(th));
            t.r[i] = r;
        }
        for (int i = 0; i <= kPolarN + 1; ++i) {
            const int i0 = std::max(i - 1, 0), i1 = std::min(i + 1, kPolarN);
            t.dr[i] = (t.r[i1] - t.r[i0]) * static_cast<float>(kPolarN) / (kTau * static_cast<float>(i1 - i0));
        }
        return t;
    }();
    return tables;
}

// A leaf of the litter while the map is built.
struct LitterSpec {
    float x, y;                       // centre (a maple's heart): px from the left, px above the ground line
    float ca, sa;                     // unit axis from base to tip
    float hl, hw;                     // half length and half width (px)
    float rx, ry;                     // half extents of its bounding box (px)
    float key, z;                     // shows while the surface is above key; nearer leaves have lower z
    float bright, curl, blush, stem;  // stem: its length beyond the blade (px), 0 for none
    uint8_t shape, gene, look;
    bool lip;                         // hangs over the edge: drawn below the ground line only
    // Derived for painting (Prepare).
    float invHl, invW, slopeK, curlK, ribK;
};

// Per-leaf constants for SampleLeaf.
void Prepare(LitterSpec& L) {
    const float wMax = L.shape == kMaple ? L.hl : L.hw;
    L.invHl = 1.0f / std::max(L.hl, 0.5f);
    L.invW = 1.0f / std::max(wMax, 0.5f);
    L.slopeK = L.hw * 0.5f * L.invHl;  // turns dw/dt into across per along
    // Rolled about its midrib: the side facing the light is lit, the other in shade.
    L.curlK = 2.0f * L.curl * (-L.sa * kLx + L.ca * kLy) * L.invW;
    L.ribK = 0.3f * SmoothStep(2.5f, 5.0f, L.hl);  // veins show once the leaf is big enough
}

// Per-build constants for SampleLeaf.
struct SampleScale {
    float stemHalf;  // px
    float invRib;    // 1 / half-width of a vein (px)
    float edge;      // width of the margin (px)
};

// Deterministic dice from a hash, so a cell always grows the same leaves.
struct Dice {
    uint32_t s;
    float operator()() {
        s = Hash32(s + 0x9E3779B9u);
        return static_cast<float>(s >> 8) * (1.0f / 16777216.0f);
    }
    float Range(float lo, float hi) { return lo + (hi - lo) * (*this)(); }
};

// One leaf: shape, size, lie and shading (its colour comes from where it
// lies; see BuildLitter). yDip is its height above the ground line (leaves
// near it lie flat), s the display scale.
LitterSpec MakeLeaf(Dice& d, float yDip, float s, bool lip) {
    LitterSpec L{};
    const float pick = d();
    float len, aspect;
    if (pick < 0.16f) {
        L.shape = kMaple;
        len = d.Range(10.5f, 15.0f);
        aspect = 1.0f;
    } else if (pick < 0.28f) {
        L.shape = kOak;
        len = d.Range(9.5f, 13.5f);
        aspect = d.Range(0.5f, 0.6f);
    } else if (pick < 0.38f) {
        L.shape = kNarrow;
        len = d.Range(9.5f, 14.0f);
        aspect = d.Range(0.2f, 0.27f);
    } else if (pick < 0.5f) {
        L.shape = kRounded;
        len = d.Range(7.0f, 9.5f);
        aspect = d.Range(0.62f, 0.78f);
    } else {
        L.shape = kOval;
        len = d.Range(7.0f, 11.5f);
        aspect = d.Range(0.42f, 0.56f);
    }
    if (lip) len *= 0.85f;
    // A maple is sized by its longest lobe, measured from the heart.
    L.hl = (L.shape == kMaple ? 0.62f : 0.5f) * len * s;
    L.hw = L.hl * aspect;
    float ang;
    if (lip) {
        ang = -0.5f * kPi + d.Range(-1.2f, 1.2f);  // hanging down over the edge
    } else {
        const float spread = Lerp(0.45f, kPi, SmoothStep(0.0f, 12.0f, yDip));
        ang = (d() < 0.5f ? 0.0f : kPi) + d.Range(-spread, spread);
    }
    L.ca = std::cos(ang);
    L.sa = std::sin(ang);
    if (d() < 0.12f && L.shape != kMaple) {
        L.curl = d.Range(0.3f, 0.4f);  // rolled up about its midrib
        L.hw *= 0.62f;
    } else {
        L.curl = d.Range(0.02f, 0.07f);
    }
    L.stem = len >= 8.0f && d() < 0.35f ? d.Range(0.18f, 0.32f) * len * 0.5f * s : 0.0f;
    const float back = L.shape == kMaple ? 0.25f * L.hl + L.stem : L.stem;
    if (L.shape == kMaple) {
        L.rx = L.ry = L.hl + L.stem + 1.0f;
    } else {
        L.rx = std::fabs(L.ca) * (L.hl + back) + std::fabs(L.sa) * L.hw + 1.0f;
        L.ry = std::fabs(L.sa) * (L.hl + back) + std::fabs(L.ca) * L.hw + 1.0f;
    }
    const float look = d();
    L.look = look < 0.06f ? kDead : (look < 0.11f ? kUnderside : kFresh);
    L.bright = d.Range(0.86f, 1.1f) * (d() < 0.07f ? 0.72f : 1.0f);  // the odd one lies in shadow
    L.blush = std::pow(d(), 0.7f);
    // It shows once the surface is a little above its centre, its top
    // standing out by up to kPokeMax.
    const float top = std::fabs(L.sa) * L.hl + std::fabs(L.ca) * L.hw;
    L.key = L.y + d.Range(0.2f, 0.8f) * top;  // relative to y for now
    L.key = std::max(L.key, top - kPokeMax * s);
    Prepare(L);
    return L;
}

// atan2 to within about 0.004 rad, plenty for a leaf's outline.
inline float FastAtan2(float y, float x) {
    const float ax = std::fabs(x), ay = std::fabs(y);
    const float mx = std::max(ax, ay);
    if (mx <= 0.0f) return 0.0f;
    const float z = std::min(ax, ay) / mx;
    float a = z * (0.7853982f + (1.0f - z) * (0.2447f + 0.0663f * z));
    if (ay > ax) a = 0.5f * kPi - a;
    if (x < 0.0f) a = kPi - a;
    return y < 0.0f ? -a : a;
}

// Coverage and shading of a leaf at a pixel (dx, dy: px from its centre, y up).
bool SampleLeaf(const ShapeTables& T, const LitterSpec& L, const SampleScale& C, float dx, float dy, float& cov,
                float& lum, float& blush) {
    const float along = dx * L.ca + dy * L.sa;
    const float across = -dx * L.sa + dy * L.ca;
    const float aAcross = std::fabs(across);
    const bool maple = L.shape == kMaple;
    if (along > L.hl + 1.0f) return false;

    // The blade: signed distance to the outline (px, negative inside).
    float dist, t = 0.0f, rho = 0.0f, th = 0.0f, r = 1.0f;
    if (maple) {
        rho = std::sqrt(along * along + across * across);
        if (rho > L.hl + 1.0f && L.stem <= 0.0f) return false;  // past the longest lobe
        th = FastAtan2(across, along);
        float f = (th + kPi) * (kPolarN / kTau);
        const int i = Clamp(static_cast<int>(f), 0, kPolarN - 1);
        f -= static_cast<float>(i);
        r = Lerp(T.r[i], T.r[i + 1], f) * L.hl;
        const float k = Lerp(T.dr[i], T.dr[i + 1], f) * L.hl / std::max(rho, 0.5f);
        dist = (rho - r) / std::sqrt(1.0f + k * k);
    } else {
        t = (along + L.hl) * 0.5f * L.invHl;
        if (t <= 0.0f || t >= 1.0f) {
            const float o = std::fabs(along) - L.hl;
            dist = std::sqrt(o * o + across * across);
        } else {
            float f = t * kProfN;
            const int i = std::min(static_cast<int>(f), kProfN - 1);
            f -= static_cast<float>(i);
            const float* w = T.w[L.shape];
            const float* dw = T.dw[L.shape];
            const float k = (dw[i] + (dw[i + 1] - dw[i]) * f) * L.slopeK;
            const float F = aAcross - (w[i] + (w[i + 1] - w[i]) * f) * L.hw;
            // Clearly outside (dist >= 0.5) or deep inside: no need for the exact distance.
            if (F > 0.0f && F * F >= 0.25f * (1.0f + k * k) && L.stem <= 0.0f) return false;
            dist = F < -3.0f * C.edge ? F : F / std::sqrt(1.0f + k * k);
        }
    }
    if (dist >= 0.5f && L.stem <= 0.0f) return false;
    const float blade = Saturate(0.5f - dist);

    // The stem, from inside the blade out past its base.
    float stem = 0.0f;
    if (L.stem > 0.0f) {
        const float a0 = maple ? -0.2f * L.hl : -L.hl + 0.5f;
        const float a1 = (maple ? -0.3f * L.hl : -L.hl) - L.stem;
        const float du = along - Clamp(along, a1, a0);
        stem = Saturate(0.5f + C.stemHalf - std::sqrt(du * du + across * across));
    }
    cov = std::max(blade, stem);
    if (cov <= 1.0f / 255.0f) return false;
    if (stem > blade) {
        lum = 0.55f * L.bright;
        blush = 0.0f;
        return true;
    }

    float heart2, rib;
    if (maple) {
        heart2 = rho * L.invHl;
        heart2 *= heart2;
        // Veins from the heart to the lobe tips.
        float ang = kPi;
        for (const Lobe& l : kMapleLobes) ang = std::min(ang, std::fabs(th - l.at));
        rib = Saturate(1.0f - ang * rho * C.invRib) * SmoothStep(0.9f, 0.6f, rho / r);
    } else {
        // The heart turns along the midrib, from the base most of the way up.
        const float hu = (t - 0.35f) * (1.0f / 0.45f), hv = across * L.invW * 1.25f;
        heart2 = hu * hu + hv * hv;
        rib = Saturate(1.0f - aAcross * C.invRib) * SmoothStep(0.0f, 0.06f, t) * SmoothStep(0.95f, 0.8f, t);
    }

    lum = L.bright * (1.0f + Clamp(across * L.curlK, -2.0f * L.curl, 2.0f * L.curl)) * (1.0f - L.ribK * rib);
    // The margin: a touch darker, catching the light on the side facing it.
    const float edge = SmoothStep(-C.edge, 0.0f, dist);
    if (edge > 0.0f) {
        float na = along * L.invHl, nc = across * L.invW;
        const float nl = std::sqrt(na * na + nc * nc);
        if (nl > 1e-4f) {
            na /= nl;
            nc /= nl;
        }
        const float nx = na * L.ca - nc * L.sa, ny = na * L.sa + nc * L.ca;
        lum *= 1.0f + edge * (0.1f * (nx * kLx + ny * kLy) - 0.1f);
    }
    blush = L.blush * (1.0f - SmoothStep(0.0625f, 1.0f, heart2));
    return true;
}

inline Rgb Mix(Rgb a, Rgb b, float t) { return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t)}; }

// A leaf colour lit by k (1: full light). The shade in a pile is warm, lit by
// light bounced off other leaves: blue and green fall off first, so golds
// deepen to amber and brown rather than olive. Light past full pales a colour
// rather than clipping it (clipped, gold would turn lemon).
inline Rgb Lit(float r, float g, float b, float k) {
    if (k <= 1.0f) return {r * k, g * k * (0.78f + 0.22f * k), b * k * (0.5f + 0.5f * k)};
    r *= k;
    g *= k;
    b *= k;
    const float m = std::max(r, std::max(g, b));
    if (m <= 1.0f) return {r, g, b};
    const float inv = 1.0f / m, w = std::min(0.3f, 0.5f * (m - 1.0f));
    return {r * inv + (1.0f - r * inv) * w, g * inv + (1.0f - g * inv) * w, b * inv + (1.0f - b * inv) * w};
}

inline Rgb FromHex(uint32_t c) {
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f};
}

// Bit tests, so they keep working under /fp:fast (which may fold NaN compares).
inline bool IsNan(float v) { return (std::bit_cast<uint32_t>(v) & 0x7FFFFFFFu) > 0x7F800000u; }
inline bool IsFinite(float v) { return (std::bit_cast<uint32_t>(v) & 0x7F800000u) != 0x7F800000u; }

// Premultiplied colour (each channel already scaled by a) to BGRA.
inline uint32_t PackPremul(float r, float g, float b, float a) {
    a = Saturate(a);
    auto ch = [a](float v) { return static_cast<uint32_t>(Clamp(v, 0.0f, a) * 255.0f + 0.5f); };
    return (static_cast<uint32_t>(a * 255.0f + 0.5f) << 24) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
}

inline uint8_t ToByte(float v, float scale) {
    return static_cast<uint8_t>(Clamp(v * scale + 0.5f, 0.0f, 255.0f));
}

int RoundPx(float v) { return std::max(1, static_cast<int>(v + 0.5f)); }

}  // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void LeafPile::Configure(int widthPx, const PileParams& params) {
    if (!seeded_) {
        // Each pile gets its own pattern so that monitors don't match.
        uint64_t t = static_cast<uint64_t>(QpcSeconds() * 1.0e7);
        rng_.Seed(t ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)));
        seed_ = rng_.Range(0.0f, 200.0f);
        seeded_ = true;
    }
    widthPx = std::max(widthPx, 0);
    if (widthPx != width_) Resize(widthPx);
    SetParams(params);
}

void LeafPile::Resize(int widthPx) {
    const int oldW = width_;
    std::vector<float> old;
    old.swap(height_);
    width_ = widthPx;
    const size_t n = static_cast<size_t>(widthPx);
    height_.assign(n, 0.0f);
    if (oldW > 0 && widthPx > 0 && old.size() == static_cast<size_t>(oldW)) {
        // Keep the leaves, stretched to the new width.
        float k = static_cast<float>(oldW) / static_cast<float>(widthPx);
        for (int i = 0; i < widthPx; ++i) {
            float src = Clamp((static_cast<float>(i) + 0.5f) * k - 0.5f, 0.0f, static_cast<float>(oldW - 1));
            int j = static_cast<int>(src);
            int j1 = std::min(j + 1, oldW - 1);
            height_[static_cast<size_t>(i)] = Lerp(old[static_cast<size_t>(j)], old[static_cast<size_t>(j1)],
                                                   src - static_cast<float>(j));
        }
    }
    smooth_.assign(n, 0.0f);
    scratch_.assign(n + 1, 0.0f);
    work_.assign(n + 1, 0.0f);
    talus_.assign(n, 1.0f);
    crestInv_.assign(n, 1.0f);
    capF_.assign(n, 1.0f);
    catch_.assign(n, 1.0f);
    rawCatch_.assign(n, 0.0f);
    rawSpot_.assign(n, 0.0f);
    rawCap_.assign(n, 0.0f);
    prefix_.assign(n + 1, 0.0);
    shadedS_.assign(n, -1.0f);
    blockDirty_.assign(static_cast<size_t>((widthPx + kShadeBlock - 1) / kShadeBlock), 0);
    crumbleAge_.assign(static_cast<size_t>((widthPx + kCrumbleBlock - 1) / kCrumbleBlock), 0.0f);
    spans_.clear();
    spans_.reserve(blockDirty_.size());
    dirtyBlocks_ = 0;
    shadeCursor_ = crumbleCursor_ = fieldCursor_ = 0;
    fieldsValid_ = false;
    litterWidth_ = 0;  // the litter is laid out afresh for the new width
    chg0_ = 0;
    chg1_ = widthPx;
    av0_ = 0;
    av1_ = widthPx;
    maxH_ = 0.0f;
    for (float h : height_) maxH_ = std::max(maxH_, h);
    // Force a fresh layout (the pixel buffer must match the new width).
    texHeight_ = groundRow_ = 0;
    pixels_.clear();
    MarkAllDirty();
}

void LeafPile::SetParams(const PileParams& in) {
    PileParams p = in;
    if (IsNan(p.maxHeight) || p.maxHeight <= 0.0f) p.maxHeight = 0.0f;
    p.maxHeight = std::min(p.maxHeight, 4096.0f);
    if (IsNan(p.dpiScale) || p.dpiScale <= 0.25f) p.dpiScale = 1.0f;
    p.dpiScale = std::min(p.dpiScale, 8.0f);
    if (static_cast<int>(p.color) < 0 || p.color >= LeafColor::Count) p.color = LeafColor::Autumn;

    const bool fieldsChanged = !fieldsValid_ || p.dpiScale != params_.dpiScale ||
                               p.leftOpen != params_.leftOpen || p.rightOpen != params_.rightOpen;
    const bool colorsChanged = p.color != params_.color || p.customColor != params_.customColor;
    const float oldMax = params_.maxHeight;
    params_ = p;

    if (colorsChanged) {
        tonesValid_ = false;  // Shade picks the new colours up and re-shades everything
        MarkAllDirty();
    }
    if (fieldsChanged && width_ > 0) {
        ComputeStaticFields();
        RefreshFields(0, width_);
        FieldStatistics();
        MapFields(0, width_);
        NormalizeCatch();
        fieldsValid_ = true;
        av0_ = 0;  // repose angles changed: re-check every slope
        av1_ = width_;
        chg0_ = 0;
        chg1_ = width_;
        MarkAllDirty();
    }
    if (p.maxHeight != oldMax || capHeight_ <= 0.0f) {
        if (p.maxHeight <= 0.0f) {
            // Piling switched off: let what is there blow away gently.
            if (!Empty() && blowLeft_ <= 0.0f) BlowAway(3.0f);
        } else if (p.maxHeight >= capHeight_) {
            capHeight_ = p.maxHeight;  // a lower cap eases down in Update
        }
    }
    UpdateLayout();
}

// Static per-column fields: repose angle and crest roundness.
void LeafPile::ComputeStaticFields() {
    const float s = params_.dpiScale;
    const float inv = 1.0f / s;
    const float wDip = static_cast<float>(width_) * inv;
    for (int i = 0; i < width_; ++i) {
        const size_t u = static_cast<size_t>(i);
        float xd = (static_cast<float>(i) + 0.5f) * inv;
        float taper = EndTaper(xd, wDip);
        // Some stretches are tangled, holding steep faces and tight crests;
        // others slump into softer, rounder heaps.
        float n = 0.6f * noise::Noise1(xd * (1.0f / 55.0f) + seed_ * 0.71f + 91.1f) +
                  0.4f * noise::Noise1(xd * (1.0f / 19.0f) + seed_ * 0.53f + 7.7f);
        float tangle = SmoothStep(-0.4f, 0.4f, n);
        float deg = Lerp(kReposeMinDeg, kReposeMaxDeg, tangle);
        deg -= 12.0f * (1.0f - taper);  // looser near an open end
        talus_[u] = std::tan(deg * (kPi / 180.0f));
        crestInv_[u] = s / (kCrestCurv * Lerp(0.6f, 1.5f, tangle));
    }
}

float LeafPile::EndTaper(float xDip, float widthDip) const {
    float t = 1.0f;
    if (params_.leftOpen) t = std::min(t, SmoothStep(0.0f, kEndTaper, xDip));
    if (params_.rightOpen) t = std::min(t, SmoothStep(0.0f, kEndTaper, widthDip - xDip));
    return t;
}

// Slowly drifting fields: how many leaves each stretch catches, and how high
// they may pile there. Raw noise is evaluated in DIP space, then mapped
// through the strip's own statistics so that every screen (wide or narrow,
// whatever the noise happens to do there) gets a similar mix of thin and deep
// stretches.
void LeafPile::RefreshFields(int x0, int x1) {
    const float inv = 1.0f / params_.dpiScale;
    // Where on its circle the sample point is now (see kFieldOrbit). A field
    // that changes m times as fast rides a circle m times as large.
    const double ang = time_ * (kFieldSpeed / static_cast<double>(kFieldOrbit));
    const float oc = kFieldOrbit * static_cast<float>(std::cos(ang));
    const float os = kFieldOrbit * static_cast<float>(std::sin(ang));
    auto field = [oc, os](float x, float m, float z) { return noise::Simplex3(x, m * oc, z + m * os); };
    x0 = std::max(x0, 0);
    x1 = std::min(x1, width_);
    for (int i = x0; i < x1; ++i) {
        const size_t u = static_cast<size_t>(i);
        float xd = (static_cast<float>(i) + 0.5f) * inv;
        float broad = field(xd * (1.0f / 560.0f) + seed_, 1.0f, 3.7f);
        float mid = field(xd * (1.0f / 150.0f) + seed_ * 1.37f, 1.3f, 9.1f);
        rawCatch_[u] = 0.6f * broad + 0.4f * mid;
        rawSpot_[u] = field(xd * (1.0f / 70.0f) + seed_ * 0.77f, 1.7f, 15.3f);
        rawCap_[u] = field(xd * (1.0f / 330.0f) + seed_ + 11.3f, 0.7f, 8.1f) +
                     0.4f * field(xd * (1.0f / 110.0f) + seed_ * 0.59f + 3.3f, 0.9f, 21.7f);
    }
    MapFields(x0, x1);
}

void LeafPile::MapFields(int x0, int x1) {
    const float inv = 1.0f / params_.dpiScale;
    const float wDip = static_cast<float>(width_) * inv;
    for (int i = x0; i < x1; ++i) {
        const size_t u = static_cast<size_t>(i);
        float taper = EndTaper((static_cast<float>(i) + 0.5f) * inv, wDip);
        float c = SmoothStep(-1.2f, 1.5f, (rawCatch_[u] - fieldStats_[0]) / fieldStats_[1]);
        float spot = SmoothStep(0.8f, 1.8f, (rawSpot_[u] - fieldStats_[2]) / fieldStats_[3]);
        catch_[u] = (0.1f + 1.6f * c * c + kSpotCatch * spot * (0.3f + 0.7f * c)) * (0.35f + 0.65f * taper);
        // The cap follows the noise linearly (no saturation), so even a pile
        // filled to it has uneven tops rather than flat mesas.
        float m = Saturate(0.55f + 0.3f * (rawCap_[u] - fieldStats_[4]) / fieldStats_[5]);
        capF_[u] = (0.45f + 0.55f * m) * (0.15f + 0.85f * taper);
    }
}

// Mean and spread of each raw field over the strip.
void LeafPile::FieldStatistics() {
    if (width_ <= 0) return;
    const std::vector<float>* fields[3] = {&rawCatch_, &rawSpot_, &rawCap_};
    const double n = static_cast<double>(width_);
    for (int f = 0; f < 3; ++f) {
        double sum = 0, sq = 0;
        for (float v : *fields[f]) {
            sum += v;
            sq += static_cast<double>(v) * v;
        }
        double mean = sum / n;
        // A floor on the spread keeps narrow strips from amplifying tiny wiggles.
        fieldStats_[2 * f] = static_cast<float>(mean);
        fieldStats_[2 * f + 1] = std::max(static_cast<float>(std::sqrt(std::max(sq / n - mean * mean, 0.0))), 0.12f);
    }
}

// The factor that makes the catch average 1, so the total leaf fall doesn't
// depend on the pattern.
void LeafPile::NormalizeCatch() {
    double sum = 0;
    for (float c : catch_) sum += c;
    catchScale_ = sum > 0.0 ? static_cast<float>(static_cast<double>(width_) / sum) : 1.0f;
}

void LeafPile::UpdateLayout() {
    int ground = 0, tex = 0;
    if (width_ > 0 && (Enabled() || !Empty())) {
        const float s = params_.dpiScale;
        // The rows above the ground come in quanta, so dragging the height
        // slider only now and then reallocates the strip (and with it the
        // renderer's bitmap): grow to the next quantum, and shrink only once
        // more than a quantum beyond that is spare. Leaves stand out of the
        // surface by up to kPokeMax.
        const int q = RoundPx(kLayoutQuantum * s);
        const int need =
            static_cast<int>(std::ceil(std::max(params_.maxHeight, capHeight_) + kPokeMax * s)) + kTopMarginPx;
        const int spare = groundRow_ - need;
        ground = spare >= 0 && spare < 2 * q ? groundRow_ : (need + q - 1) / q * q;
        tex = ground + static_cast<int>(std::ceil((kLipHang + kShadowLen) * s)) + 2;
    }
    if (ground == groundRow_ && tex == texHeight_) return;
    // While a lowered cap eases down or the pile blows away, keep the current
    // strip as long as everything still fits in it, so it is not reallocated
    // every frame along the way. Growth is never deferred.
    const bool fits = ground <= groundRow_ && tex - ground <= texHeight_ - groundRow_;
    if (fits && (blowLeft_ > 0.0f || (Enabled() && capHeight_ > params_.maxHeight))) return;
    groundRow_ = ground;
    texHeight_ = tex;
    pixels_.assign(static_cast<size_t>(width_) * static_cast<size_t>(tex), 0u);
    if (tex == 0) {
        // Nothing to draw: let the litter go too (it is rebuilt when needed).
        std::vector<LitterPx>().swap(litter_);
        std::vector<Leaf>().swap(leaves_);
        std::vector<uint8_t>().swap(present_);
        litterWidth_ = 0;
    }
    MarkAllDirty();
}

// ---------------------------------------------------------------------------
// The litter
// ---------------------------------------------------------------------------

bool LeafPile::LitterReady() const {
    return !litter_.empty() && litterWidth_ == width_ && litterScale_ == params_.dpiScale &&
           mapAbove_ >= groundRow_ && mapBelow_ >= texHeight_ - groundRow_;
}

// Lays out the leaves and paints them into a map of which leaf is in front at
// each pixel, which lies behind it, and how they are shaded there. Leaves are
// fixed in DIP space (a grid of cells, each with its own hashed leaves), so a
// rebuilt map has the same leaves where the old one had them. The map covers
// whole quanta of height, so it is rebuilt only now and then as the cap grows.
void LeafPile::BuildLitter() {
    const int W = width_;
    const float s = params_.dpiScale;
    const int q = RoundPx(kMapQuantum * s);
    mapAbove_ = std::max(q, (groundRow_ + q - 1) / q * q);
    mapBelow_ = std::max(texHeight_ - groundRow_, 0);
    const int mapRows = mapAbove_ + mapBelow_;
    litterWidth_ = W;
    litterScale_ = s;

    // 1. The leaves: lying in the pile, and hanging over the edge.
    std::vector<LitterSpec> specs;
    const uint32_t seedBits = Hash32(std::bit_cast<uint32_t>(seed_) ^ 0x6C8E9CF5u);
    const float wDip = static_cast<float>(W) / s;
    // The highest cap this map serves (see UpdateLayout): leaves keyed above it never show.
    const float keyMax = static_cast<float>(mapAbove_ - kTopMarginPx) - kPokeMax * s;
    const int i0 = static_cast<int>(std::floor(-14.0f / kCell));
    const int i1 = static_cast<int>(std::ceil((wDip + 14.0f) / kCell));
    const int j1 = static_cast<int>(std::ceil(keyMax / s / kCell)) + 1;
    // Leaves lie near the tree they fell from: each stretch of the pile
    // favours a run of neighbouring colours in the palette (over the strip
    // every colour comes up about as often).
    const float seedF = seed_;
    auto gene = [seedF](Dice& d, float xDip) {
        float g = 1.7f * noise::Noise1(xDip * (1.0f / 110.0f) + seedF * 0.37f + 5.1f) + kGeneSpread * (d() - 0.5f);
        g -= std::floor(g);
        return static_cast<uint8_t>(std::min(g * 256.0f, 255.0f));
    };
    specs.reserve(static_cast<size_t>((i1 - i0) * (j1 + 1)) * 4 + static_cast<size_t>(wDip / kLipCell) + 8);
    for (int j = -1; j < j1; ++j) {
        for (int i = i0; i < i1; ++i) {
            Dice d{Hash32(static_cast<uint32_t>(i) * 0x9E3779B1u ^
                          Hash32(static_cast<uint32_t>(j) * 0x85EBCA77u ^ seedBits))};
            const int n = static_cast<int>(kPerCell + d());
            for (int k = 0; k < n; ++k) {
                const float xd = (static_cast<float>(i) + d()) * kCell, yd = (static_cast<float>(j) + d()) * kCell;
                LitterSpec L = MakeLeaf(d, yd, s, false);
                L.gene = gene(d, xd);
                L.x = xd * s;
                L.y = yd * s;
                L.key += L.y;
                // The first leaves show at different depths, not all at once.
                if (L.key < kMinKey * s) L.key = (kMinKey + d.Range(0.0f, 1.4f)) * s;
                L.z = L.key;
                if (L.key > keyMax || L.y + L.ry <= 0.0f) continue;  // never shows
                if (L.x + L.rx < 0.0f || L.x - L.rx > static_cast<float>(W)) continue;
                specs.push_back(L);
            }
        }
    }
    if (mapBelow_ > 0) {
        const int n = static_cast<int>(std::ceil(wDip / kLipCell)) + 2;
        for (int i = -1; i < n; ++i) {
            Dice d{Hash32(static_cast<uint32_t>(i) * 0x27D4EB2Fu ^ seedBits ^ 0xB5297A4Du)};
            if (d() > 0.6f) continue;
            const float xd = (static_cast<float>(i) + d()) * kLipCell;
            LitterSpec L = MakeLeaf(d, 0.0f, s, true);
            L.gene = gene(d, xd);
            L.x = xd * s;
            L.y = std::max(d.Range(-0.5f, 1.8f) * s, L.ry - kLipHang * s);
            // Hangs over once the pile behind it is this deep; in front of the rest.
            L.key = d.Range(2.0f, 8.0f) * s;
            L.z = -1.0e6f + d();
            L.lip = true;
            if (L.x + L.rx < 0.0f || L.x - L.rx > static_cast<float>(W)) continue;
            specs.push_back(L);
        }
    }

    // 2. Records in order of their centre columns, and each tile's first leaf.
    std::sort(specs.begin(), specs.end(), [](const LitterSpec& a, const LitterSpec& b) { return a.x < b.x; });
    const size_t count = specs.size();
    leaves_.resize(count);
    float reach = 0.0f, poke = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const LitterSpec& L = specs[i];
        Leaf& f = leaves_[i];
        f.key = L.key;
        f.cx = static_cast<uint16_t>(Clamp(static_cast<int>(std::floor(L.x)), 0, std::max(W - 1, 0)));
        f.gene = L.gene;
        f.look = L.look & 3u;
        // Where it lies: about the middle half of its width (sampled at the ends and the centre).
        f.span = static_cast<uint8_t>(Clamp(static_cast<int>(0.5f * (L.rx - 1.0f)), 0, 63));
        reach = std::max(reach, L.rx);
        if (!L.lip) poke = std::max(poke, L.y + L.ry - L.key);
    }
    reachPx_ = static_cast<int>(std::ceil(reach)) + 1;
    pokePx_ = static_cast<int>(std::ceil(poke)) + 1;
    const int tiles = (W >> kTileShift) + 1;
    tileBase_.assign(static_cast<size_t>(tiles), 0u);
    {
        size_t i = 0;
        for (int t = 0; t < tiles; ++t) {
            const int from = (t << kTileShift) - reachPx_;
            while (i < count && static_cast<int>(leaves_[i].cx) < from) ++i;
            tileBase_[static_cast<size_t>(t)] = static_cast<uint32_t>(i);
        }
    }
    present_.assign(count, 0);

    // 3. Paint them into the map, front to back: the first leaf to reach a
    //    pixel is its top, the second the one behind; the rest are hidden.
    //    One 64-px tile at a time (its rows stay in cache), with the leaves
    //    that reach into it in order of depth.
    litter_.assign(static_cast<size_t>(W) * static_cast<size_t>(mapRows), LitterPx{kNoLeaf, kNoLeaf, 0, 0, 0, 0});
    const ShapeTables& shapes = Shapes();
    const SampleScale scale = {0.32f * std::sqrt(s), 1.0f / (0.55f * std::max(1.0f, 0.75f * s)),
                               1.3f * std::max(1.0f, 0.7f * s)};
    const float above = static_cast<float>(mapAbove_);
    std::vector<uint32_t> order;
    order.reserve(1024);
    for (int t = 0; t < tiles; ++t) {
        const int tx0 = t << kTileShift, tx1 = std::min(W, tx0 + (1 << kTileShift));
        if (tx1 <= tx0) break;
        const uint32_t tileBase = tileBase_[static_cast<size_t>(t)];
        order.clear();
        for (size_t i = tileBase; i < count && static_cast<int>(leaves_[i].cx) < tx1 + reachPx_; ++i) {
            const LitterSpec& L = specs[i];
            if (L.x + L.rx >= static_cast<float>(tx0) && L.x - L.rx < static_cast<float>(tx1))
                order.push_back(static_cast<uint32_t>(i));
        }
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return specs[a].z < specs[b].z; });
        for (uint32_t id : order) {
            const LitterSpec& L = specs[id];
            const uint32_t local = id - tileBase;
            if (local >= kNoLeaf) continue;  // cannot happen: every leaf here starts at or after the base
            // Leaves in the pile end at the ground line; the ones hanging over show only below it.
            const int rowLo = L.lip ? mapAbove_ : 0, rowHi = L.lip ? mapRows : mapAbove_;
            const int m0 = std::max(rowLo, static_cast<int>(std::floor(above - (L.y + L.ry))));
            const int m1 = std::min(rowHi - 1, static_cast<int>(std::ceil(above - (L.y - L.ry))));
            const int xa = std::max(tx0, static_cast<int>(std::floor(L.x - L.rx)));
            const int xb = std::min(tx1 - 1, static_cast<int>(std::ceil(L.x + L.rx)));
            // Each row is scanned only where it crosses the leaf's (rotated) box.
            const float aHi = L.hl + 1.0f;
            const float aLo =
                -(L.shape == kMaple ? std::max(0.36f * L.hl, 0.3f * L.hl + L.stem) : L.hl + L.stem) - 1.0f;
            const float bw = (L.shape == kMaple ? L.hl : std::max(L.hw, 0.5f)) + 1.0f;
            for (int m = m0; m <= m1; ++m) {
                const float dy = above - static_cast<float>(m) - 0.5f - L.y;
                float lo = -L.rx, hi = L.rx;
                if (std::fabs(L.ca) > 1e-4f) {
                    const float u0 = (aLo - dy * L.sa) / L.ca, u1 = (aHi - dy * L.sa) / L.ca;
                    lo = std::max(lo, std::min(u0, u1));
                    hi = std::min(hi, std::max(u0, u1));
                } else if (dy * L.sa < aLo || dy * L.sa > aHi) {
                    continue;
                }
                if (std::fabs(L.sa) > 1e-4f) {
                    const float v0 = (dy * L.ca - bw) / L.sa, v1 = (dy * L.ca + bw) / L.sa;
                    lo = std::max(lo, std::min(v0, v1));
                    hi = std::min(hi, std::max(v0, v1));
                } else if (std::fabs(dy * L.ca) > bw) {
                    continue;
                }
                const int x0 = std::max(xa, static_cast<int>(std::ceil(L.x + lo - 0.5f)));
                const int x1 = std::min(xb, static_cast<int>(std::floor(L.x + hi - 0.5f)));
                LitterPx* row = litter_.data() + static_cast<size_t>(m) * static_cast<size_t>(W);
                for (int x = x0; x <= x1; ++x) {
                    LitterPx& p = row[x];
                    if (p.behind != kNoLeaf) continue;
                    float cov, lum, blush;
                    if (!SampleLeaf(shapes, L, scale, static_cast<float>(x) + 0.5f - L.x, dy, cov, lum, blush))
                        continue;
                    if (p.top == kNoLeaf) {
                        p.top = static_cast<uint16_t>(local);
                        p.cov = ToByte(cov, 255.0f);
                        p.lum = ToByte(lum, 128.0f);
                        p.blush = ToByte(blush, 255.0f);
                    } else {
                        p.behind = static_cast<uint16_t>(local);
                        p.behindLum = ToByte(lum, 128.0f);
                    }
                }
            }
        }
    }

    // 4. Contact shadows: a leaf is shaded just past the edge of one lying in
    //    front of it, on the side away from the light.
    const int K = std::max(1, RoundPx(2.0f * s));
    for (int m = 1; m < mapAbove_; ++m) {
        LitterPx* row = litter_.data() + static_cast<size_t>(m) * static_cast<size_t>(W);
        for (int x = 0; x < W; ++x) {
            LitterPx& p = row[x];
            if (p.top == kNoLeaf) continue;
            const uint32_t g = tileBase_[static_cast<size_t>(x >> kTileShift)] + p.top;
            const float key = leaves_[g].key;
            float sh = 0.0f;
            for (int k = 1; k <= K && m - k >= 0; ++k) {
                const int xx = x - static_cast<int>(0.46f * static_cast<float>(k) + 0.5f);
                if (xx < 0) break;
                const LitterPx& o =
                    litter_[static_cast<size_t>(m - k) * static_cast<size_t>(W) + static_cast<size_t>(xx)];
                if (o.top == kNoLeaf) continue;
                const uint32_t go = tileBase_[static_cast<size_t>(xx >> kTileShift)] + o.top;
                if (go != g && leaves_[go].key < key) {
                    sh = 1.0f - static_cast<float>(k - 1) / static_cast<float>(K);
                    break;
                }
            }
            if (sh > 0.0f) p.lum = ToByte(static_cast<float>(p.lum) * (1.0f - kContact * sh), 1.0f);
        }
    }
}

// Whether a leaf shows: the surface where it lies (the middle and the ends of
// its middle half) is above its key.
bool LeafPile::Shows(const Leaf& f) const {
    const float S = std::max({smooth_[f.cx], smooth_[static_cast<size_t>(std::max(f.cx - f.span, 0))],
                              smooth_[static_cast<size_t>(std::min(f.cx + f.span, width_ - 1))]});
    return S >= kVisibleMin && S > f.key;
}

// Re-shades a leaf's columns (and where its shadow falls) next time, except
// [skip0, skip1), which is being shaded now.
void LeafPile::MarkLeaf(const Leaf& f, int skip0, int skip1) {
    const int r = 2 * f.span + 2;
    const int a = f.cx - r, b = f.cx + r + 2 + RoundPx(params_.dpiScale);
    if (skip1 <= skip0) {
        MarkDirty(a, b);
        return;
    }
    if (a < skip0) MarkDirty(a, std::min(b, skip0));
    if (b > skip1) MarkDirty(std::max(a, skip1), b);
}

// Leaves lying over columns [x0, x1) that have come or gone since they were
// last shaded: re-shade them whole.
void LeafPile::MarkFlips(int x0, int x1) {
    if (!LitterReady() || x1 <= x0) return;
    const size_t count = leaves_.size();
    const int lo = std::max(x0 - 63, 0), hi = x1 + 63;  // 63: the widest span
    for (size_t i = tileBase_[static_cast<size_t>(lo >> kTileShift)]; i < count && leaves_[i].cx < hi; ++i) {
        const Leaf& f = leaves_[i];
        if (f.cx + f.span < x0 || f.cx - f.span >= x1) continue;
        if (Shows(f) != (present_[i] != 0)) MarkLeaf(f);
    }
}

// The colours of the litter in the current setting: the palette's leaf
// colours (bases and blushes), their paler, duller undersides, and browns.
void LeafPile::RefreshTones() {
    LeafRgb list[24];
    const int n = Clamp(PileColors(params_.color, params_.customColor, list, 24) / 2, 1, kMaxTones);
    const bool custom = params_.color == LeafColor::Custom;
    // In order of hue, so the run of neighbouring tones a stretch of the pile
    // draws on (see the genes in BuildLitter) is a family of colours: reds,
    // oranges and russets, golds and greens.
    int order[kMaxTones];
    float hue[kMaxTones];
    for (int i = 0; i < n; ++i) {
        float h, sat, val;
        palette::ToHsv(list[2 * i], h, sat, val);
        hue[i] = h + 0.1f - std::floor(h + 0.1f);  // the reds together, past the wrap
        order[i] = i;
    }
    std::sort(order, order + n, [&](int a, int b) { return hue[a] < hue[b]; });
    auto pale = [](const LeafRgb& c) {
        const float l = 0.3f * c.r + 0.59f * c.g + 0.11f * c.b;
        const Rgb grey = Mix({c.r, c.g, c.b}, {l, l, l}, 0.2f);
        return Mix(grey, {0.8f, 0.66f, 0.46f}, 0.12f);
    };
    float weight[kMaxTones];
    float total = 0.0f;
    for (int k = 0; k < n; ++k) {
        const int i = order[k];
        const LeafRgb a = list[2 * i], b = list[2 * i + 1];
        tones_[kFresh][k] = {a.r, a.g, a.b, b.r, b.g, b.b};
        const Rgb pa = pale(a), pb = pale(b);
        tones_[kUnderside][k] = {pa.r, pa.g, pa.b, pb.r, pb.g, pb.b};
        weight[k] = 1.0f / (1.0f + 0.12f * static_cast<float>(i));  // the list comes commonest first
        total += weight[k];
    }
    for (int i = 0; i < 4; ++i) {
        Rgb a = FromHex(kDeadTones[i][0]), b = FromHex(kDeadTones[i][1]);
        if (custom) {
            // Long-fallen leaves of the custom colour: dark and dull.
            const Tone& t = tones_[kFresh][i % n];
            a = Mix({t.r * 0.5f, t.g * 0.5f, t.b * 0.5f}, a, 0.35f);
            b = Mix({t.br * 0.58f, t.bg * 0.58f, t.bb * 0.58f}, b, 0.35f);
        }
        tones_[kDead][i] = {a.r, a.g, a.b, b.r, b.g, b.b};
    }
    // Genes to tones, each tone taking a share of the genes by how common it is.
    int k = 0;
    float edge = weight[0];
    for (int g = 0; g < 256; ++g) {
        const float t = (static_cast<float>(g) + 0.5f) / 256.0f * total;
        while (k < n - 1 && t > edge) edge += weight[++k];
        toneOf_[g] = static_cast<uint8_t>(k);
    }
    tonesValid_ = true;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

float LeafPile::HeightAt(float x) const {
    if (width_ <= 0) return 0.0f;
    float f = x - 0.5f;  // column i is centred on x = i + 0.5
    if (IsNan(f) || f <= 0.0f) return smooth_[0];
    if (f >= static_cast<float>(width_ - 1)) return smooth_[static_cast<size_t>(width_ - 1)];
    int i = static_cast<int>(f);
    float t = f - static_cast<float>(i);
    float a = smooth_[static_cast<size_t>(i)], b = smooth_[static_cast<size_t>(i) + 1];
    return a + (b - a) * t;
}

bool LeafPile::Empty() const { return maxH_ < kVisibleMin; }

bool LeafPile::RandomSurfacePoint(Rng& rng, float& x, float& y) const {
    if (width_ <= 0 || Empty()) return false;
    const float minH = 0.6f * params_.dpiScale;
    for (int tries = 0; tries < 24; ++tries) {
        int i = rng.Int(0, width_ - 1);
        if (smooth_[static_cast<size_t>(i)] > minH) {
            x = static_cast<float>(i) + rng.Float();
            y = HeightAt(x);
            return true;
        }
    }
    return false;
}

float LeafPile::WideAverage(int i, int radius) const {
    int a = std::max(i - radius, 0);
    int b = std::min(i + radius + 1, width_);
    if (b <= a) return 0.0f;
    return static_cast<float>((prefix_[static_cast<size_t>(b)] - prefix_[static_cast<size_t>(a)]) /
                              static_cast<double>(b - a));
}

// ---------------------------------------------------------------------------
// Changes
// ---------------------------------------------------------------------------

void LeafPile::Touch(int x0, int x1, bool steep) {
    x0 = std::max(x0, 0);
    x1 = std::min(x1, width_);
    if (x1 <= x0) return;
    chg0_ = std::min(chg0_, x0);
    chg1_ = std::max(chg1_, x1);
    if (steep) {
        av0_ = std::min(av0_, x0);
        av1_ = std::max(av1_, x1);
    }
}

void LeafPile::MarkDirty(int x0, int x1) {
    x0 = std::max(x0, 0);
    x1 = std::min(x1, width_);
    if (x1 <= x0) return;
    for (int b = x0 / kShadeBlock; b <= (x1 - 1) / kShadeBlock; ++b) {
        uint8_t& d = blockDirty_[static_cast<size_t>(b)];
        if (!d) {
            d = 1;
            ++dirtyBlocks_;
        }
    }
}

void LeafPile::MarkAllDirty() { fullDirty_ = true; }

void LeafPile::AddBump(float cx, float r, float volume) {
    if (!(volume > 0.0f) || width_ <= 0) return;
    r = std::max(r, 0.75f);
    int i0 = std::max(0, static_cast<int>(std::floor(cx - r - 0.5f)));
    int i1 = std::min(width_ - 1, static_cast<int>(std::ceil(cx + r - 0.5f)));
    if (i0 > i1) return;
    const float inv = 1.0f / r;
    float sum = 0.0f;
    for (int i = i0; i <= i1; ++i) {
        float u = (static_cast<float>(i) + 0.5f - cx) * inv;
        float w = 1.0f - u * u;
        w = w > 0.0f ? w * w : 0.0f;  // biweight: smooth, compact
        scratch_[static_cast<size_t>(i - i0)] = w;
        sum += w;
    }
    if (sum < 1e-4f) {
        int i = Clamp(static_cast<int>(cx), 0, width_ - 1);
        float& h = height_[static_cast<size_t>(i)];
        h = std::min(h + volume, capHeight_);
        maxH_ = std::max(maxH_, h);
        Touch(i, i + 1);
        return;
    }
    const float k = volume / sum;
    float top = 0.0f;
    for (int i = i0; i <= i1; ++i) {
        float& h = height_[static_cast<size_t>(i)];
        h = std::min(h + scratch_[static_cast<size_t>(i - i0)] * k, capHeight_);
        top = std::max(top, h);
    }
    maxH_ = std::max(maxH_, top);
    Touch(i0, i1 + 1);
}

// Extra catch in the lee of a crest: how far the surface upwind rises above a
// shallow line drawn back from this spot.
float LeafPile::LeeFactor(int i) const {
    float w = Saturate((std::fabs(windDip_) - 8.0f) / 70.0f);
    if (w <= 0.0f) return 1.0f;
    const float s = params_.dpiScale;
    const int dir = windDip_ > 0.0f ? -1 : 1;  // look upwind
    const int reach = static_cast<int>(kLeeReach * s);
    const float h0 = smooth_[static_cast<size_t>(i)];
    float best = 0.0f;
    for (int k = 1; k <= reach; ++k) {
        int j = i + dir * k;
        if (j < 0 || j >= width_) break;
        best = std::max(best, smooth_[static_cast<size_t>(j)] - kShelterTan * static_cast<float>(k) - h0);
    }
    return 1.0f + kLeeBonus * w * SmoothStep(0.0f, 3.0f * s, best);
}

void LeafPile::Deposit(float x, float diameterPx, float amount) {
    if (!Enabled() || width_ <= 0 || IsNan(x) || IsNan(diameterPx) || IsNan(amount)) return;
    if (x < 0.0f || x >= static_cast<float>(width_) || diameterPx <= 0.0f || amount <= 0.0f) return;
    const float s = params_.dpiScale;
    const float dDip = Clamp(diameterPx / s, 0.5f, 64.0f);
    amount = std::min(amount, 64.0f);
    const int i = Clamp(static_cast<int>(x), 0, width_ - 1);
    const size_t u = static_cast<size_t>(i);

    // Stretches that gather more leaves get wider as well as taller swells,
    // so the catch shapes broad heaps rather than spikes.
    const float c = catch_[u] * catchScale_ * LeeFactor(i);
    const float vol = kLeafVolume * std::pow(dDip, kLeafVolumeExp) * amount * s * s * c * rng_.Range(0.7f, 1.3f);
    const float d = dDip * s;
    // More than one leaf's worth (a pile built quickly, as in the settings
    // preview) stands for leaves landing all around: wider, not just taller.
    const float spread = std::sqrt(std::max(amount, 1.0f));
    const float rWide = (d * rng_.Range(kSpreadMin, kSpreadMax) * Clamp(std::sqrt(c), 0.7f, 1.4f) + 1.5f * s) * spread;
    const float rHeap = std::max(s, d * rng_.Range(kHeapMin, kHeapMax) * spread);
    const float cx = x + rng_.Range(-0.15f, 0.15f) * d;

    // Soft cap: near the local cap fewer leaves stay put; the rest roll or
    // blow on to the roomiest spot nearby.
    const float fill = height_[u] / std::max(LocalCap(i), 0.5f);
    const float keep = 1.0f - SmoothStep(kCapKnee, 1.0f, fill);
    AddBump(cx, rWide, vol * keep * (1.0f - kHeapShare));
    AddBump(cx + rng_.Range(-0.3f, 0.3f) * d, rHeap, vol * keep * kHeapShare);
    const float loose = vol * (1.0f - keep);
    if (loose < 1e-3f) return;
    float dir = windDip_ > 8.0f ? 1.0f : (windDip_ < -8.0f ? -1.0f : (rng_.Chance(0.5f) ? 1.0f : -1.0f));
    float bestX = -1.0f, bestRoom = 0.0f;
    for (int k = 0; k < 4; ++k) {
        float side = k < 3 ? dir : -dir;  // mostly downwind
        float xx = cx + side * rng_.Range(1.0f, 4.0f) * rWide;
        if (!(xx >= 0.0f && xx < static_cast<float>(width_))) continue;
        int j = static_cast<int>(xx);
        float room = 0.9f * LocalCap(j) - height_[static_cast<size_t>(j)];
        if (room > bestRoom) {
            bestRoom = room;
            bestX = xx;
        }
    }
    if (bestX >= 0.0f) AddBump(bestX, rWide * 1.2f, loose * SmoothStep(0.0f, 2.0f * s, bestRoom));
}

float LeafPile::Rake(float x0, float x1, float ceiling) {
    if (width_ <= 0 || !IsFinite(x0) || !IsFinite(x1) || IsNan(ceiling) || x1 <= x0) return 0.0f;
    ceiling = std::max(ceiling, 0.0f);
    const float cx = 0.5f * x0 + 0.5f * x1;  // halves first: no overflow
    const float r = 0.5f * x1 - 0.5f * x0;
    // Clamp in float before converting, so huge inputs stay defined.
    const float w = static_cast<float>(width_);
    const int i0 = static_cast<int>(std::floor(Clamp(x0, 0.0f, w)));
    const int i1 = static_cast<int>(std::ceil(Clamp(x1, 0.0f, w)));
    float removed = 0.0f;
    for (int i = i0; i < i1; ++i) {
        // A rounded scoop: the cut is deepest under the middle of the range.
        float dx = std::fabs(static_cast<float>(i) + 0.5f - cx) / std::max(r, 0.5f);
        float lift = dx < 1.0f ? r * (1.0f - std::sqrt(1.0f - dx * dx)) * 0.5f : r;
        float c = ceiling + lift;
        float& h = height_[static_cast<size_t>(i)];
        if (h > c) {
            removed += h - c;
            h = c;
        }
    }
    if (removed > 0.0f) {
        Touch(i0, i1);
        Derive();  // so HeightAt sees the cut right away
    }
    return removed;
}

void LeafPile::Clear() {
    std::fill(height_.begin(), height_.end(), 0.0f);
    std::fill(smooth_.begin(), smooth_.end(), 0.0f);
    std::fill(prefix_.begin(), prefix_.end(), 0.0);
    maxH_ = 0.0f;
    blowRate_ = blowLeft_ = 0.0f;
    chg0_ = av0_ = width_;
    chg1_ = av1_ = 0;
    MarkAllDirty();
    UpdateLayout();
}

void LeafPile::BlowAway(float seconds) {
    if (width_ <= 0 || Empty()) return;
    if (IsNan(seconds) || seconds < 0.05f) seconds = 0.05f;
    seconds = std::min(seconds, 600.0f);
    float top = 0.0f;
    for (float h : height_) top = std::max(top, h);
    blowTotal_ = blowLeft_ = seconds;
    blowRate_ = top / ((1.0f - kBlowSweep) * seconds);
    blowDir_ = windDip_ < 0.0f ? -1 : 1;
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void LeafPile::Update(float dt, float wind) {
    if (width_ <= 0 || IsNan(dt) || dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    if (IsNan(wind) || std::fabs(wind) > 1.0e5f) wind = 0.0f;
    const float s = params_.dpiScale;
    time_ += dt;
    windDip_ = wind / s;
    if (unshadedUpdates_ < 1000) ++unshadedUpdates_;

    if (blowLeft_ > 0.0f) {
        // Blowing away: a front sweeps across with the wind, and behind it
        // every stretch empties at the same pace; all gone on time.
        blowLeft_ -= dt;
        const bool done = blowLeft_ <= 0.0f;
        const float front = Saturate((1.0f - blowLeft_ / blowTotal_) / kBlowSweep);
        const float m = blowRate_ * dt;
        const float inv = 1.0f / static_cast<float>(width_);
        for (int i = 0; i < width_; ++i) {
            float& h = height_[static_cast<size_t>(i)];
            const float along = static_cast<float>(blowDir_ > 0 ? i : width_ - 1 - i) * inv;
            if (done) h = 0.0f;
            else if (along <= front) h = std::max(0.0f, h - m);
        }
        if (done) blowRate_ = blowLeft_ = 0.0f;
        Touch(0, width_, false);
    } else if (Enabled() && capHeight_ > params_.maxHeight) {
        // The cap was lowered: ease it down and squeeze the leaves above a
        // knee in proportion, so heaps keep their shapes while they sink.
        const float oldCap = capHeight_;
        capHeight_ = Lerp(capHeight_, params_.maxHeight, ApproachFactor(dt, kCapEaseTau));
        if (capHeight_ - params_.maxHeight < 0.05f) capHeight_ = params_.maxHeight;
        const float knee = 0.6f * params_.maxHeight;
        const float r = (capHeight_ - knee) / std::max(oldCap - knee, 1e-3f);
        int lo = width_, hi = -1;
        for (int i = 0; i < width_; ++i) {
            float& h = height_[static_cast<size_t>(i)];
            if (h > knee) {
                h = std::min(knee + (h - knee) * r, capHeight_);
                lo = std::min(lo, i);
                hi = i;
            }
        }
        Touch(lo, hi + 1, false);
    }

    // Refresh a slice of the slowly drifting fields every frame.
    if (fieldsValid_) {
        int chunk = std::max(64, width_ / 64);
        RefreshFields(fieldCursor_, fieldCursor_ + chunk);
        fieldCursor_ += chunk;
        if (fieldCursor_ >= width_) {
            fieldCursor_ = 0;
            FieldStatistics();
            NormalizeCatch();
        }
    }

    windDebt_ += dt;
    for (int n = 0; windDebt_ >= kWindTick; ++n) {
        if (n == kMaxWindTicks) {
            windDebt_ = 0.0f;
            break;
        }
        windDebt_ -= kWindTick;
        WindStep(kWindTick, wind);
    }

    if (blowLeft_ <= 0.0f) CrumbleBlocks(dt);

    sweepDebt_ += dt * kSweepRate;
    int sweeps = static_cast<int>(sweepDebt_);
    sweepDebt_ -= static_cast<float>(sweeps);
    Relax(std::min(sweeps, kMaxSweeps));

    Creep(dt);

    Derive();
    UpdateLayout();
}

// Angle of repose: alternate-direction Gauss-Seidel sweeps over the columns
// that may be unstable. Each over-steep pair trades half of its excess
// (thermal-erosion style), so the total is conserved; open ends shed leaves.
void LeafPile::Relax(int sweeps) {
    const int W = width_;
    if (W < 2) return;
    float* h = height_.data();
    const float* T = talus_.data();
    int touched0 = W, touched1 = -1;
    for (int n = 0; n < sweeps && av1_ > av0_; ++n) {
        const int a = std::max(av0_ - 1, 0), b = std::min(av1_ + 1, W);
        int lo = W, hi = -1;
        auto pair = [&](int i) {
            float d = h[i] - h[i + 1];
            float t = 0.5f * (T[i] + T[i + 1]);
            float m;
            if (d > t) {
                m = kRelax * 0.5f * (d - t);
                h[i] -= m;
                h[i + 1] += m;
            } else if (-d > t) {
                m = kRelax * 0.5f * (-d - t);
                h[i] += m;
                h[i + 1] -= m;
            } else {
                return false;
            }
            if (m > 1e-5f) {
                lo = std::min(lo, i);
                hi = std::max(hi, i);
                return true;
            }
            return false;
        };
        if ((sweepParity_++ & 1u) == 0) {
            int last = -2;
            for (int i = a; i < W - 1; ++i) {
                if (i >= b - 1 && last != i - 1) break;  // past the range: only follow a running cascade
                if (pair(i)) last = i;
            }
        } else {
            int last = W + 2;
            for (int i = b - 2; i >= 0; --i) {
                if (i < a && last != i + 1) break;
                if (pair(i)) last = i;
            }
        }
        // Open ends: leaves steeper than repose against the drop slide off.
        if (params_.leftOpen && a == 0 && h[0] > T[0]) {
            h[0] -= kRelax * (h[0] - T[0]);
            lo = std::min(lo, 0);
            hi = std::max(hi, 0);
        }
        if (params_.rightOpen && b == W && h[W - 1] > T[W - 1]) {
            h[W - 1] -= kRelax * (h[W - 1] - T[W - 1]);
            lo = std::min(lo, W - 2);
            hi = std::max(hi, W - 2);
        }
        if (hi < lo) {
            av0_ = W;
            av1_ = 0;
        } else {
            av0_ = lo - 1;
            av1_ = hi + 3;
            touched0 = std::min(touched0, lo);
            touched1 = std::max(touched1, hi + 2);
        }
    }
    if (touched1 > touched0) {
        chg0_ = std::min(chg0_, std::max(touched0, 0));
        chg1_ = std::max(chg1_, std::min(touched1, W));
    }
}

// Settling creep over the leafy span. The flux between neighbours is
// D * (h[i-1] - h[i]); D rises where either column's curvature is sharper than
// the crest (convex) or foot (concave) limit, so peaks round off to a minimum
// radius and kinks soften while broad shapes and straight faces stay put.
// Explicit substeps keep D * dt <= kCreepCfl px^2, which is monotone: no new
// extrema, no negative heights, no overshoot of the cap.
void LeafPile::Creep(float dt) {
    const int W = width_;
    if (W < 3 || maxH_ <= 0.0f) return;
    float* z = height_.data();
    int first = 0, last = W - 1;
    while (first < W && z[first] <= 0.0f) ++first;
    if (first >= W) return;
    while (z[last] <= 0.0f) --last;

    const float s = params_.dpiScale;
    const float s2 = s * s;
    const float dMax = (kCreepBase + kCreepCrest + kCreepFoot) * s2;  // px^2/s
    const int steps = std::max(1, static_cast<int>(std::ceil(dt * dMax / kCreepCfl)));
    const float step = dt / static_cast<float>(steps);
    const float gBase = kCreepBase * s2 * step, gCrest = kCreepCrest * s2 * step, gFoot = kCreepFoot * s2 * step;
    const float invFoot = s / kFootCurv;
    // Each substep spreads leaves by at most one column.
    const int x0 = std::max(0, first - steps - 1), x1 = std::min(W, last + steps + 2);
    const bool openL = params_.leftOpen, openR = params_.rightOpen;
    float* K = work_.data();
    float* F = scratch_.data();  // F[k]: flow from column k-1 into column k
    const float* ic = crestInv_.data();
    for (int n = 0; n < steps; ++n) {
        // Beyond an open end the ground drops away; a closed end mirrors.
        const float gl = openL ? 0.0f : z[0];
        const float gr = openR ? 0.0f : z[W - 1];
        for (int i = x0; i < x1; ++i) {
            const float a = i > 0 ? z[i - 1] : gl;
            const float b = i + 1 < W ? z[i + 1] : gr;
            K[i] = a + b - 2.0f * z[i];
        }
        for (int k = x0; k <= x1; ++k) {
            const float diff = (k > 0 ? z[k - 1] : gl) - (k < W ? z[k] : gr);
            if (diff == 0.0f) {
                F[k] = 0.0f;
                continue;
            }
            const int il = std::max(k - 1, x0), ir = std::min(k, x1 - 1);
            const float sharp = std::max(-K[il] * ic[il], -K[ir] * ic[ir]);
            const float hollow = std::max(K[il], K[ir]) * invFoot;
            F[k] = (gBase + gCrest * Saturate(2.0f * (sharp - 1.0f)) + gFoot * Saturate(hollow - 1.0f)) * diff;
        }
        for (int i = x0; i < x1; ++i) {
            const float v = z[i] + F[i] - F[i + 1];
            z[i] = v > kDust ? v : 0.0f;
        }
    }
    Touch(x0, x1, false);  // diffusion never steepens beyond repose
}

// Wind: walking downwind, the moving flux relaxes toward the surface's
// carrying capacity; where it rises the surface is stripped, where it falls
// (in the lee of crests) leaves drop out. Heaps grow gentle windward ramps,
// steep lee faces, and creep downwind.
void LeafPile::WindStep(float dt, float windPx) {
    const float s = params_.dpiScale;
    const float u = std::fabs(windPx) / s;
    if (u <= kWindThreshold || Empty()) return;
    const int W = width_;
    const float Q = kFluxScale * std::pow(u - kWindThreshold, 1.5f) * s * s;  // px^2/s
    const float alpha = 1.0f - std::exp(-1.0f / (kSaturationLen * s));
    const float availH = 1.5f * s;
    const float crestRef = 0.3f * std::max(capHeight_, 4.0f * s);
    const int crestR = RoundPx(kPeakRadius * s);
    const int dir = windPx > 0.0f ? 1 : -1;
    const bool openUpwind = dir > 0 ? params_.leftOpen : params_.rightOpen;
    int i = dir > 0 ? 0 : W - 1;
    const int end = dir > 0 ? W : -1;

    float q = 0.0f;
    float shelter = -1.0e9f;
    float prev = height_[static_cast<size_t>(i)];
    bool first = true;
    int lo = W, hi = -1;
    for (; i != end; i += dir) {
        float& hr = height_[static_cast<size_t>(i)];
        const float h = hr;
        shelter -= kShelterTan;
        float qs = 0.0f;
        if (shelter <= h + 0.25f * s) {
            float rise = Clamp(h - prev, 0.0f, 1.0f);
            float crest = Saturate((smooth_[static_cast<size_t>(i)] - WideAverage(i, crestR)) / crestRef);
            qs = Q * (1.0f + kStossGain * rise + kCrestGain * crest) * SmoothStep(0.0f, availH, h);
        }
        shelter = std::max(shelter, h);
        if (first) {
            // A closed end receives what the neighbouring screen sends: assume equilibrium.
            if (!openUpwind) q = qs;
            first = false;
        }
        float qn = q + (qs - q) * alpha;
        float dh = (q - qn) * dt;
        if (h + dh < 0.0f) {
            dh = -h;
            qn = q + h / dt;
        }
        if (dh != 0.0f) {
            hr = std::min(h + dh, capHeight_);
            lo = std::min(lo, i);
            hi = std::max(hi, i);
        }
        q = qn;
        prev = h;
    }
    Touch(lo, hi + 1);
}

// Slow crumbling and settling, applied one block per call in rotation so that
// the changes (and the re-shading they cause) stay local. Each block crumbles
// by the time that has passed since its last visit, so the rate is frame-rate
// free.
void LeafPile::CrumbleBlocks(float dt) {
    const int nb = static_cast<int>(crumbleAge_.size());
    if (nb == 0) return;
    for (float& a : crumbleAge_) a += dt;
    crumbleDebt_ += dt * static_cast<float>(nb) / kCrumblePeriod;
    int n = std::min(static_cast<int>(crumbleDebt_), nb);
    crumbleDebt_ = std::min(crumbleDebt_ - static_cast<float>(n), 1.0f);
    const float s = params_.dpiScale;
    const int peakR = RoundPx(kPeakRadius * s);
    const float scatter = kScatter * s;
    for (; n > 0; --n) {
        const int b = crumbleCursor_;
        crumbleCursor_ = (crumbleCursor_ + 1) % nb;
        const float age = crumbleAge_[static_cast<size_t>(b)];
        crumbleAge_[static_cast<size_t>(b)] = 0.0f;
        const int x0 = b * kCrumbleBlock, x1 = std::min(width_, x0 + kCrumbleBlock);
        bool any = false;
        for (int i = x0; i < x1; ++i) {
            float& h = height_[static_cast<size_t>(i)];
            if (h <= 0.0f) continue;
            const float cap = std::max(LocalCap(i), 1.0f);
            float expo = std::max(0.0f, smooth_[static_cast<size_t>(i)] - WideAverage(i, peakR)) / (0.25f * cap + s);
            float fill = h / cap;
            float rate = (1.0f + kPeakCrumble * std::min(expo, 2.0f) + kTallCrumble * fill * fill) / kCrumbleTau;
            float v = h * std::exp(-rate * age) - scatter * age;
            if (v > cap) v = cap + (v - cap) * std::exp(-age / kOverCapTau);
            h = std::max(v, 0.0f);
            any = true;
        }
        if (any) Touch(x0, x1, false);
    }
}

// Light blur of the changed range into smooth_, neighbourhood sums, and the
// shading dirty set (columns whose visible height moved noticeably).
void LeafPile::Derive() {
    if (chg1_ <= chg0_ || width_ <= 0) return;
    const int W = width_;
    const float s = params_.dpiScale;
    const int R = RoundPx(1.0f * s);
    const float norm = 1.0f / static_cast<float>((R + 1) * (R + 1));
    const int a = std::max(0, chg0_ - R), b = std::min(W, chg1_ + R);
    const bool wasEmpty = Empty();
    for (int i = a; i < b; ++i) {
        float sum = 0.0f;
        for (int k = -R; k <= R; ++k) {
            int j = Clamp(i + k, 0, W - 1);
            sum += static_cast<float>(R + 1 - std::abs(k)) * height_[static_cast<size_t>(j)];
        }
        smooth_[static_cast<size_t>(i)] = sum * norm;
    }
    for (int i = a; i < W; ++i)
        prefix_[static_cast<size_t>(i) + 1] = prefix_[static_cast<size_t>(i)] + smooth_[static_cast<size_t>(i)];

    // The light at a column comes from the surface up to the AO and form
    // radii away; a leaf that comes or goes re-shades its own columns.
    const int dep = std::max(RoundPx(kAoRadius * s), RoundPx(kFormRadius * s)) + 3;
    int run0 = -1, run1 = -1;
    for (int i = a; i < b; ++i) {
        const size_t u = static_cast<size_t>(i);
        float now = smooth_[u], was = shadedS_[u];
        bool moved = std::fabs(now - was) > kRedrawEps || ((now >= kVisibleMin) != (was >= kVisibleMin));
        if (moved) {
            if (run0 < 0) run0 = i;
            run1 = i;
        } else if (run0 >= 0 && i - run1 > kShadeBlock) {
            MarkDirty(run0 - dep, run1 + 1 + dep);
            MarkFlips(run0, run1 + 1);
            run0 = -1;
        }
    }
    if (run0 >= 0) {
        MarkDirty(run0 - dep, run1 + 1 + dep);
        MarkFlips(run0, run1 + 1);
    }

    float top = 0.0f;
    for (float h : height_) top = std::max(top, h);
    maxH_ = top;
    if (!wasEmpty && Empty()) MarkAllDirty();  // clear the strip in one go
    chg0_ = W;
    chg1_ = 0;
}

// ---------------------------------------------------------------------------
// Shading
// ---------------------------------------------------------------------------

int LeafPile::Shade(int* x0, int* x1, int maxSpans) {
    if (!x0 || !x1 || maxSpans < 1 || width_ <= 0 || texHeight_ <= 0 || pixels_.empty()) return 0;
    if (!tonesValid_) {
        RefreshTones();
        fullDirty_ = true;
    }
    if (!LitterReady()) {
        BuildLitter();
        fullDirty_ = true;
    }
    if (fullDirty_) {
        ShadeRange(0, width_);
        std::fill(blockDirty_.begin(), blockDirty_.end(), static_cast<uint8_t>(0));
        dirtyBlocks_ = 0;
        fullDirty_ = false;
        unshadedUpdates_ = 0;
        x0[0] = 0;
        x1[0] = width_;
        return 1;
    }
    if (dirtyBlocks_ <= 0) {
        unshadedUpdates_ = 0;
        return 0;
    }
    const int nb = static_cast<int>(blockDirty_.size());
    int budget = kShadeBudget;
    if (unshadedUpdates_ > 2) {
        // Not drawn for a while (hidden or paused): catch up in one go so
        // nothing stale shows when drawing resumes.
        budget = nb;
    } else if (blowLeft_ > 0.0f || (Enabled() && capHeight_ > params_.maxHeight)) {
        // The whole strip is moving: sweep it in as few equal passes as
        // possible, left to right, so at most one seam shows.
        const int passes = (nb + kAnimBlocks - 1) / kAnimBlocks;
        budget = std::max(budget, (nb + passes - 1) / passes);
    }
    unshadedUpdates_ = 0;

    // Round robin from where the last call stopped: shade one cluster of dirty
    // blocks after another until the budget is used. Distant changes become
    // separate spans, so the clean columns between them are not re-uploaded.
    spans_.clear();
    int c0 = shadeCursor_ < nb ? shadeCursor_ : 0;
    while (budget > 0 && dirtyBlocks_ > 0) {
        for (int k = 0; k < nb && !blockDirty_[static_cast<size_t>(c0)]; ++k) c0 = c0 + 1 < nb ? c0 + 1 : 0;
        if (!blockDirty_[static_cast<size_t>(c0)]) {
            dirtyBlocks_ = 0;  // the count was off; nothing is dirty
            break;
        }
        int c1 = c0;
        for (int k = c0 + 1; k < nb && k - c0 < budget; ++k) {
            if (blockDirty_[static_cast<size_t>(k)]) c1 = k;
            else if (k - c1 > kMaxGap) break;
        }
        ShadeRange(c0 * kShadeBlock, std::min(width_, (c1 + 1) * kShadeBlock));
        for (int k = c0; k <= c1; ++k) {
            uint8_t& d = blockDirty_[static_cast<size_t>(k)];
            if (d) {
                d = 0;
                --dirtyBlocks_;
            }
        }
        budget -= c1 - c0 + 1;
        spans_.emplace_back(c0, c1 + 1);
        c0 = c1 + 1 < nb ? c1 + 1 : 0;
    }
    shadeCursor_ = c0;
    if (spans_.empty()) return 0;

    // Ascending and disjoint, touching spans joined; then, while there are
    // more than the caller takes, merge the two closest neighbours.
    std::sort(spans_.begin(), spans_.end());
    size_t n = 0;
    for (size_t i = 1; i < spans_.size(); ++i) {
        if (spans_[i].first <= spans_[n].second) spans_[n].second = std::max(spans_[n].second, spans_[i].second);
        else spans_[++n] = spans_[i];
    }
    spans_.resize(n + 1);
    while (spans_.size() > static_cast<size_t>(maxSpans)) {
        size_t best = 0;
        for (size_t i = 1; i + 1 < spans_.size(); ++i)
            if (spans_[i + 1].first - spans_[i].second < spans_[best + 1].first - spans_[best].second) best = i;
        spans_[best].second = spans_[best + 1].second;
        spans_.erase(spans_.begin() + static_cast<std::ptrdiff_t>(best) + 1);
    }
    for (size_t i = 0; i < spans_.size(); ++i) {
        x0[i] = spans_[i].first * kShadeBlock;
        x1[i] = std::min(width_, spans_[i].second * kShadeBlock);
    }
    return static_cast<int>(spans_.size());
}

// Shades columns [x0, x1) of the strip from the litter map: each pixel shows
// its front-most leaf if that leaf is showing, lit by the surface (from the
// upper left near the top, darker deeper in and towards the base), with what
// lies past its edge: the leaf behind, a dark chink, or nothing. Below the
// ground line hang the leaves over the edge, over a soft shadow.
void LeafPile::ShadeRange(int x0, int x1) {
    const int W = width_;
    const float s = params_.dpiScale;
    const float invS = 1.0f / s;
    const int aoR = RoundPx(kAoRadius * s);
    const int slopeK = RoundPx(s);
    const int formR = RoundPx(kFormRadius * s);
    const float formK = 1.4f / static_cast<float>(2 * formR);
    const float topK = 1.0f / (2.0f * s);
    const float shadowLen = kShadowLen * s;
    const int ground = groundRow_;
    const int rows = texHeight_;
    const int rowOff = mapAbove_ - ground;  // map row of strip row 0
    const LitterPx* map = litter_.data();
    const Leaf* leaves = leaves_.data();
    const uint8_t* present = present_.data();
    const size_t stride = static_cast<size_t>(W);

    // Which leaves are showing: those within reach of the range (and of the
    // hanging leaves' shadows) whose key is under the surface where they lie,
    // so a leaf half buried in a slope still shows.
    const int reach = reachPx_;
    const int ox = std::max(1, RoundPx(1.0f * s)), oy = std::max(1, RoundPx(1.6f * s));
    {
        const size_t count = leaves_.size();
        const int lx1 = std::min(x1 + reach, W);
        for (size_t i = tileBase_[static_cast<size_t>(std::max(x0 - ox, 0) >> kTileShift)];
             i < count && leaves_[i].cx < lx1; ++i) {
            const uint8_t on = Shows(leaves_[i]) ? 1 : 0;
            if (on == present_[i]) continue;
            present_[i] = on;
            MarkLeaf(leaves_[i], x0, x1);  // the rest of it, next time
        }
    }
    auto tone = [&](const Leaf& L) -> const Tone& {
        return tones_[L.look][L.look == kDead ? (L.gene & 3) : toneOf_[L.gene]];
    };

    // The highest any showing leaf can reach, per stretch of columns: the
    // surface where it may lie, plus how far a leaf stands out of it.
    const int around = reach + reach / 2;
    float top = 0.0f;
    int topEnd = x0;
    for (int x = x0; x < x1; ++x) {
        uint32_t* px = pixels_.data() + x;
        const uint32_t base = tileBase_[static_cast<size_t>(x >> kTileShift)];
        const float S0 = smooth_[static_cast<size_t>(x)];
        const float S = S0 >= kVisibleMin ? S0 : 0.0f;
        const float wide = WideAverage(x, aoR);
        const float shadowA = kShadowAlpha * SmoothStep(0.3f * s, 8.0f * s, wide);

        if (x >= topEnd) {
            topEnd = std::min(x + kShadeBlock, x1);
            top = 0.0f;
            for (int j = std::max(x - around, 0), j1 = std::min(topEnd + around, W); j < j1; ++j)
                top = std::max(top, smooth_[static_cast<size_t>(j)]);
        }
        if (top < kVisibleMin && shadowA <= 0.002f) {
            for (int r = 0; r < rows; ++r) px[static_cast<size_t>(r) * stride] = 0u;
            shadedS_[static_cast<size_t>(x)] = S0;
            continue;
        }

        // Near the top the litter is lit by the surface's slope (light from
        // the upper left); deeper down it turns into the face of the pile.
        // Depth, concave dips and the base add warm ambient occlusion.
        const float hl = smooth_[static_cast<size_t>(std::max(x - slopeK, 0))];
        const float hr = smooth_[static_cast<size_t>(std::min(x + slopeK, W - 1))];
        const float sx = (hr - hl) / static_cast<float>(2 * slopeK);
        const float slopeLight = Clamp((kLy - sx * kLx) / (kLy * std::sqrt(1.0f + sx * sx)), 0.55f, 1.15f);
        const float thick = SmoothStep(1.0f * s, 10.0f * s, S);  // thin litter stays bright
        // The mound as a whole: the side facing the light brighter, the far side in shade.
        const float fl = smooth_[static_cast<size_t>(std::max(x - formR, 0))];
        const float fr = smooth_[static_cast<size_t>(std::min(x + formR, W - 1))];
        const float form = 1.0f + kFormLight * Clamp((fr - fl) * formK, -1.0f, 1.0f) * thick;
        const float dip = Saturate((wide - S) / (2.0f * s + 0.3f * wide));

        int first = top >= kVisibleMin ? ground - 1 - static_cast<int>(std::floor(top + static_cast<float>(pokePx_)))
                                       : ground;
        first = Clamp(first, 0, ground);
        for (int r = 0; r < first; ++r) px[static_cast<size_t>(r) * stride] = 0u;

        // The pile.
        for (int r = first; r < ground; ++r) {
            const LitterPx& m = map[static_cast<size_t>(r + rowOff) * stride + static_cast<size_t>(x)];
            const float yc = static_cast<float>(ground - r) - 0.5f;  // row centre, px above ground
            const float below = S - yc;
            // Gaps between the showing leaves: under the surface they show the
            // leaves further back in the pile, in shadow; near the top, sky.
            const float backA = S > 0.0f ? Saturate(below - kBackDepth * s) : 0.0f;
            const bool topOn = m.top != kNoLeaf && present[base + m.top];
            const bool behindOn = m.behind != kNoLeaf && present[base + m.behind];
            if (!topOn && !behindOn && backA <= 0.0f) {
                px[static_cast<size_t>(r) * stride] = 0u;
                continue;
            }
            const float d = std::max(below, 0.0f) * invS;  // DIP below the surface
            const float q = d * (1.0f / kSlopeDepth);
            const float g = 1.0f / (1.0f + q * q);
            const float depthT = d / (d + kAoDepth);
            const float baseT = SmoothStep(3.0f * s, 0.0f, yc);
            const float ao = thick * Saturate(0.65f * depthT + dip * (0.35f + 0.5f * depthT) + 0.3f * baseT);
            const float light = Lerp(kFrontLight, slopeLight, g) * (1.0f - kAoStrength * ao) * form *
                                (1.0f + kTopLight * Saturate(-below * topK));

            float cr = 0.0f, cg = 0.0f, cb = 0.0f, a = 0.0f;
            float rest = 1.0f;  // share of the pixel past the front leaf
            if (topOn) {
                const Leaf& L = leaves[base + m.top];
                const Tone& t = tone(L);
                const float bl = static_cast<float>(m.blush) * (1.0f / 255.0f);
                const float k = static_cast<float>(m.lum) * (1.0f / 128.0f) * light;
                const float cov = static_cast<float>(m.cov) * (1.0f / 255.0f);
                const Rgb c = Lit(t.r + (t.br - t.r) * bl, t.g + (t.bg - t.g) * bl, t.b + (t.bb - t.b) * bl, k);
                cr = c.r * cov;
                cg = c.g * cov;
                cb = c.b * cov;
                a = cov;
                rest = 1.0f - cov;
            }
            if (rest > 0.0f) {
                if (behindOn) {
                    // The leaf behind, in the shade of the one in front where there is one.
                    const Leaf& L = leaves[base + m.behind];
                    const Tone& t = tone(L);
                    const float k =
                        static_cast<float>(m.behindLum) * (1.0f / 128.0f) * light * (topOn ? kEdgeShade : 0.9f);
                    const Rgb c =
                        Lit(t.r + 0.25f * (t.br - t.r), t.g + 0.25f * (t.bg - t.g), t.b + 0.25f * (t.bb - t.b), k);
                    cr += c.r * rest;
                    cg += c.g * rest;
                    cb += c.b * rest;
                    a += rest;
                } else if (backA > 0.0f) {
                    // Further back: the leaf the map has here (the front one if
                    // it is not showing, else the one behind it), or a dark chink.
                    const uint16_t id = !topOn ? m.top : m.behind;
                    const float k = light * kBackShade, w = rest * backA;
                    Rgb c;
                    if (id != kNoLeaf) {
                        const Tone& t = tone(leaves[base + id]);
                        const float bl = !topOn ? static_cast<float>(m.blush) * (1.0f / 255.0f) : 0.25f;
                        const float kl = k * static_cast<float>(!topOn ? m.lum : m.behindLum) * (1.0f / 128.0f);
                        c = Lit(t.r + (t.br - t.r) * bl, t.g + (t.bg - t.g) * bl, t.b + (t.bb - t.b) * bl, kl);
                    } else {
                        c = Lit(kChink.r, kChink.g, kChink.b, k);
                    }
                    cr += c.r * w;
                    cg += c.g * w;
                    cb += c.b * w;
                    a += w;
                }
            }
            px[static_cast<size_t>(r) * stride] = a > 0.0f ? PackPremul(cr, cg, cb, a) : 0u;
        }

        // Below the ground line: leaves hanging over the edge, over the
        // pile's soft shadow and their own.
        const float hang = kLipHang * s;
        for (int r = ground; r < rows; ++r) {
            const float yc = static_cast<float>(r - ground) + 0.5f;  // px below ground
            const float t = yc / (shadowLen + 0.4f * hang);
            float sh = t >= 1.0f ? 0.0f : shadowA * (1.0f - t) * (1.0f - t);
            if (x >= ox && r - oy >= ground) {
                const LitterPx& o =
                    map[static_cast<size_t>(r - oy + rowOff) * stride + static_cast<size_t>(x - ox)];
                const uint32_t ob = tileBase_[static_cast<size_t>((x - ox) >> kTileShift)];
                if (o.top != kNoLeaf && present[ob + o.top]) {
                    const float la = kLeafShadowAlpha * static_cast<float>(o.cov) * (1.0f / 255.0f);
                    sh = 1.0f - (1.0f - sh) * (1.0f - la);
                }
            }
            const LitterPx& m = map[static_cast<size_t>(r + rowOff) * stride + static_cast<size_t>(x)];
            const bool topOn = m.top != kNoLeaf && present[base + m.top];
            const bool behindOn = m.behind != kNoLeaf && present[base + m.behind];
            float cr = 0.0f, cg = 0.0f, cb = 0.0f, a = 0.0f;
            if (topOn || behindOn) {
                // Under the pile's edge, the lower the darker.
                const float light = kFrontLight * (0.9f - 0.3f * Saturate(yc / hang)) * (1.0f - 0.25f * thick);
                float rest = 1.0f;
                if (topOn) {
                    const Tone& tn = tone(leaves[base + m.top]);
                    const float bl = static_cast<float>(m.blush) * (1.0f / 255.0f);
                    const float k = static_cast<float>(m.lum) * (1.0f / 128.0f) * light;
                    const float cov = static_cast<float>(m.cov) * (1.0f / 255.0f);
                    const Rgb c =
                        Lit(tn.r + (tn.br - tn.r) * bl, tn.g + (tn.bg - tn.g) * bl, tn.b + (tn.bb - tn.b) * bl, k);
                    cr = c.r * cov;
                    cg = c.g * cov;
                    cb = c.b * cov;
                    a = cov;
                    rest = 1.0f - cov;
                }
                if (rest > 0.0f && behindOn) {
                    const Tone& tb = tone(leaves[base + m.behind]);
                    const float kb =
                        static_cast<float>(m.behindLum) * (1.0f / 128.0f) * light * (topOn ? kEdgeShade : 0.9f);
                    const Rgb c = Lit(tb.r + 0.25f * (tb.br - tb.r), tb.g + 0.25f * (tb.bg - tb.g),
                                      tb.b + 0.25f * (tb.bb - tb.b), kb);
                    cr += c.r * rest;
                    cg += c.g * rest;
                    cb += c.b * rest;
                    a += rest;
                }
            }
            // Leaves over the shadow, premultiplied.
            const float rest = 1.0f - a;
            cr += kShadow.r * sh * rest;
            cg += kShadow.g * sh * rest;
            cb += kShadow.b * sh * rest;
            a += sh * rest;
            px[static_cast<size_t>(r) * stride] = a > 0.0f ? PackPremul(cr, cg, cb, a) : 0u;
        }
        shadedS_[static_cast<size_t>(x)] = S0;
    }
}
