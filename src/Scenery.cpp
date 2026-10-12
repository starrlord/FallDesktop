#include "SceneryParts.h"

#include "Noise.h"
#include "Palette.h"

using namespace scenery;

namespace {

// A sprite colour that adds `c` at strength `a` while veiling only `veil` of
// what is behind (Direct2D premultiplies it to (c * a, veil * a)).
D2D1_COLOR_F Glow(Rgb c, float a, float veil) {
    const float k = 1.0f / veil;
    return {c.r * k, c.g * k, c.b * k, veil * a};
}

// Noise inputs repeat every 256 units; wrapping in double keeps float precision.
float Wrap256(double v) { return static_cast<float>(std::fmod(v, 256.0)); }
float Frac(double v) { return static_cast<float>(v - std::floor(v)); }

// One step of a damped spring (semi-implicit Euler; callers keep w * dt small).
void Spring(float& x, float& v, float rest, float w, float zeta, float dt) {
    v += (-(w * w) * (x - rest) - 2.0f * zeta * w * v) * dt;
    x += v * dt;
}

D2D1_MATRIX_3X2_F Placed(float angle, float x, float y) {
    if (angle == 0.0f) return {1, 0, 0, 1, x, y};
    const float c = std::cos(angle), s = std::sin(angle);
    return {c, s, -s, c, x, y};
}

D2D1_RECT_F Around(const Scenery::Sprite& sp) {
    return {static_cast<float>(sp.src.left) - sp.cx, static_cast<float>(sp.src.top) - sp.cy,
            static_cast<float>(sp.src.right) - sp.cx, static_cast<float>(sp.src.bottom) - sp.cy};
}

// A page cell anchored at its sprite's anchor, scaled by (kx, ky), turned and placed.
ScenerySprite Quad(const Scenery::Sprite& sp, float kx, float ky, float angle, float x, float y, D2D1_COLOR_F col) {
    const D2D1_RECT_F r = Around(sp);
    return {{r.left * kx, r.top * ky, r.right * kx, r.bottom * ky}, sp.src, col, Placed(angle, x, y)};
}

// A leaf lying on something seen from a little above: scaled by k, turned by
// `turn` in its own plane, foreshortened to `squash` of its height, then laid
// at `angle`.
ScenerySprite Leaflet(const Scenery::Sprite& sp, float k, float turn, float squash, float angle, float x, float y,
                      D2D1_COLOR_F col) {
    const float c1 = std::cos(turn), s1 = std::sin(turn), c2 = std::cos(angle), s2 = std::sin(angle);
    return {Around(sp), sp.src, col,
            {k * (c1 * c2 - squash * s1 * s2), k * (c1 * s2 + squash * s1 * c2), k * (-s1 * c2 - squash * c1 * s2),
             k * (-s1 * s2 + squash * c1 * c2), x, y}};
}

// Smallest level at least `d` px across (levels halve from the first).
int PickLevel(const float* px, int count, float d) {
    int l = 0;
    while (l + 1 < count && px[l + 1] >= d) ++l;
    return l;
}

float Ease(float t) { return t * t * (3.0f - 2.0f * t); }
float Byte(uint32_t h, int i) { return static_cast<float>((h >> (8 * i)) & 0xFF) / 255.0f; }

// The top of a gourd's outline over x (px about its centre, y up), `n`
// columns from -span to span.
void TopLine(const Gourd& g, float span, float* ys, int n) {
    for (int i = 0; i < n; ++i) ys[i] = -1.0e9f;
    for (int i = 0; i < 160; ++i) {
        const float th = kTau * static_cast<float>(i) / 160.0f;
        for (int j = 0; j <= 30; ++j) {
            const Vec3 p = g.At(th, 0.6f * kPi * static_cast<float>(j) / 30.0f);
            const int b = static_cast<int>((p.x + span) / (2.0f * span) * static_cast<float>(n));
            if (b >= 0 && b < n) ys[b] = std::max(ys[b], p.y);
        }
    }
    // A column no sample fell in takes its neighbours'.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < n; ++i)
            if (ys[i] < -1.0e8f) ys[i] = std::max(i > 0 ? ys[i - 1] : -1.0e9f, i + 1 < n ? ys[i + 1] : -1.0e9f);
}

// Litter landing on cell i of a shelf: half on it, a quarter on each
// neighbour on the same shelf. Near a spot's cap less of it stays; the rest
// slides off (`spill`, px^2), as does what falls past the shelf's ends.
void Deposit(std::vector<Scenery::Cell>& cells, size_t i, float capPx, float area, float cellW, float& spill) {
    static constexpr float kShare[3] = {0.25f, 0.5f, 0.25f};
    for (int d = -1; d <= 1; ++d) {
        const float add = kShare[d + 1] * area / cellW;
        const size_t j = i + static_cast<size_t>(d);  // (wraps for i = 0, d = -1: out of range)
        if (j >= cells.size() || cells[j].seg != cells[i].seg) {
            spill += add * cellW;
            continue;
        }
        Scenery::Cell& c = cells[j];
        const float cap = std::max(c.capF * capPx, 1e-3f);
        const float keep = 1.0f - SmoothStep(kCapKnee, 1.0f, c.load / cap);
        // However big the leaf, a spot never fills past its cap at once.
        const float stick = std::min(add * keep, std::max(0.0f, cap - c.load));
        c.load += stick;
        spill += (add - stick) * cellW;
    }
}

constexpr float kMinGap = 12.0f;     // DIP at least between patches, and between them and scarecrows
constexpr float kCrowShare = 0.56f;  // a scarecrow's half width (straw hands) / S
constexpr float kDrop = 1500.0f;     // DIP/s^2: a prop dropping onto leaves that went from under it
constexpr float kSwapOut = 0.35f;    // s: the props fading out when Halloween is switched...
constexpr float kSwapIn = 0.5f;      // ...and back in with their new looks

}  // namespace

Scenery::Scenery() {
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    rng_.Seed(static_cast<uint64_t>(ticks.QuadPart) ^ reinterpret_cast<uintptr_t>(this), 0x9A7C4ull);
    SetLeafPalette(LeafColor::Autumn, RGB(232, 116, 36));
}
Scenery::~Scenery() = default;

bool Scenery::Active() const {
    return (!pumpkins_.empty() && fadePumpkins_ > 0.0f) || (!crows_.empty() && fadeCrows_ > 0.0f);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void Scenery::Configure(const SceneryLayout& layout) {
    if (configured_ && layout == layout_) return;
    layout_ = layout;
    configured_ = true;
    Relayout(true);
}

void Scenery::SetPumpkins(bool show, int amount) {
    showPumpkins_ = show;
    amount = Clamp(amount, 0, 100);
    if (amount == pumpkinAmount_) return;
    pumpkinAmount_ = amount;
    AmountsChanged();
}

void Scenery::SetScarecrows(bool show, int amount) {
    showCrows_ = show;
    amount = Clamp(amount, 0, 100);
    if (amount == scarecrowAmount_) return;
    scarecrowAmount_ = amount;
    AmountsChanged();
}

// New amounts lay the props out again once they have rested, and only if they
// change the counts (back to what is standing: nothing to do).
void Scenery::AmountsChanged() {
    if (!configured_) return;
    if (CountsFor(pumpkinAmount_, scarecrowAmount_) == built_) {
        relayoutAt_ = -1.0;
        return;
    }
    relayoutAt_ = time_ + settle_;
    if (settle_ <= 0.0f) Relayout(false);
}

// The props fade out (Update), change their looks and fade back in, unless
// none are showing: then they change at once.
void Scenery::SetHalloween(bool on) {
    halloweenWanted_ = on;
    if (fadePumpkins_ <= 0.0f && fadeCrows_ <= 0.0f) ApplyHalloween(on);
}

// The props keep their places, leaves and candles; their looks change, and
// the scarecrows' hats with them (a battered top hat stays), what lies on a
// hat moving to the new one.
void Scenery::ApplyHalloween(bool on) {
    if (on == halloween_) return;
    halloween_ = on;
    bool hats = false;
    for (Scarecrow& m : crows_) {
        if (m.topHat) continue;
        BuildHatShelf(m);
        hats = true;
    }
    if (hats) ++genCrows_;  // leaves perched on the old hats fall
    // Newly carved lanterns are lit one by one.
    if (on) lit_[0] = lit_[1] = 0.0f;
    ++artGen_;
    coverStale_ = true;
}

void Scenery::SetLeafPalette(LeafColor color, COLORREF custom) {
    LeafRgb c[24];
    litterCount_ = PileColors(color, custom, c, 24);
    for (int i = 0; i < litterCount_; ++i) litter_[i] = {c[i].r, c[i].g, c[i].b, 1.0f};
}

void Scenery::RecheckCover() {
    for (Pumpkin& p : pumpkins_) p.probed = false;
    for (Scarecrow& m : crows_) m.probed = false;
    coverStale_ = true;
}

void Scenery::SetAdditiveGlow(bool on) { glowAlpha_ = on ? kGlowAlpha : 1.0f; }

void Scenery::SetLeafDepth(float scale) {
    if (!(scale > 0.0f)) scale = 0.0f;
    if (scale == 0.0f && leafDepth_ > 0.0f) ClearLeaves(true);  // no more litter: what is there blows away
    leafDepth_ = scale;
}

// The deepest litter on a prop `sizePx` tall.
float Scenery::CapPx(float sizePx, float share) const {
    const float s = layout_.dpiScale;
    return (kLitterBase + share * sizePx / s) * s * leafDepth_;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

// How big the props may be here: 1 normally, less where the screen is short;
// 0 where they would be too small (or the screen too narrow) to bother.
float Scenery::SizeScale() const {
    const float s = layout_.dpiScale;
    if (layout_.width <= 0 || !(s > 0.0f) || static_cast<float>(layout_.width) / s < 240.0f) return 0.0f;
    const float k = std::min(1.0f, kSkyShare * layout_.groundY / s / kTallest);
    return k < kMinScale ? 0.0f : k;
}

// How many patches and scarecrows the amounts give on this screen: at the
// usual amount about one patch per kPatchSpacing of width and one scarecrow
// per kScarecrowSpacing, doubling every kAmountDouble steps.
Scenery::Counts Scenery::CountsFor(int pumpkinAmount, int scarecrowAmount) const {
    if (SizeScale() <= 0.0f) return {};
    const float wDip = static_cast<float>(layout_.width) / layout_.dpiScale;
    const float pumpkinMul = std::exp2(static_cast<float>(pumpkinAmount - kUsualAmount) / kAmountDouble);
    const float crowMul = std::exp2(static_cast<float>(scarecrowAmount - kUsualAmount) / kAmountDouble);
    return {Clamp(static_cast<int>(std::lround(wDip / kPatchSpacing * pumpkinMul)), 1, kMaxPatches),
            Clamp(static_cast<int>(std::lround(wDip / kScarecrowSpacing * crowMul)), 1, kMaxScarecrows)};
}

// The same monitor always gets the same scene.
uint32_t Scenery::Seed() const {
    return Hash32(static_cast<uint32_t>(layout_.width)) ^ Hash32(static_cast<uint32_t>(layout_.height) * 7919u + 1u) ^
           Hash32(static_cast<uint32_t>(std::lround(layout_.dpiScale * 100.0f)) + 77u) ^
           Hash32(static_cast<uint32_t>(std::lround(layout_.originX)) + 0x51ED27u);
}

// Lays the props out for the current amounts: everything (`all`), or what the
// counts changed. A new number of scarecrows keeps the pumpkins as they are,
// leaves, candles and all (they only move if the scarecrows need the room).
void Scenery::Relayout(bool all) {
    relayoutAt_ = -1.0;
    const Counts want = CountsFor(pumpkinAmount_, scarecrowAmount_);
    const bool pumpkins = all || want.patches != built_.patches;
    if (!pumpkins && want.crows == built_.crows) return;
    if (pumpkins) LayoutPumpkins(want.patches);
    LayoutScarecrows(want.crows);
    built_ = want;
    ++artGen_;
    coverStale_ = true;
}

// Room kept clear at the screen's ends (DIP).
float Scenery::Margin() const {
    return std::max(70.0f, 0.05f * static_cast<float>(layout_.width) / layout_.dpiScale);
}

// Patches of one to four pumpkins, as many as the screen has room for: the
// biggest and smaller ones nestled either side of it, a little in front. Each
// pumpkin draws its own numbers, so with the same count a monitor always gets
// the same patches and nothing else on the screen changes how they look.
// Where they stand is up to LayoutScarecrows.
void Scenery::LayoutPumpkins(int patches) {
    pumpkins_.clear();
    patches_.clear();
    ++genPumpkins_;
    lit_[0] = 0.0f;
    top_ = layout_.groundY;
    if (patches <= 0) return;
    const float s = layout_.dpiScale;
    const float wDip = static_cast<float>(layout_.width) / s;
    const float hMax = kMaxPumpkin * SizeScale();
    const uint32_t seed = Seed();
    Rng rng(seed, 0x9A7C4ull);
    std::vector<Pumpkin> built;
    int count = 0;
    for (int g = 0; g < patches; ++g) {
        const float pick = rng.Float();
        int n = pick < 0.3f ? 1 : (pick < 0.64f ? 2 : (pick < 0.9f || wDip < 900.0f ? 3 : 4));
        n = std::min(n, kMaxPumpkins - count);
        if (n <= 0) break;
        const float lead = hMax * rng.Range(0.88f, 1.0f);
        const float first = rng.Chance(0.5f) ? 1.0f : -1.0f;
        float xs[4] = {}, reachL = 0.0f, reachR = 0.0f;  // px from the lead's centre
        float left = 1.0e9f, right = -1.0e9f;
        Patch it;
        for (int k = 0; k < n; ++k) {
            Rng own(seed ^ Hash32(static_cast<uint32_t>(g * 8 + k) + 0x3C6EF372u), 0x9A7C4ull);
            const float h = k == 0 ? lead : lead * rng.Range(0.48f, 0.8f);
            Pumpkin p;
            BuildPumpkin(p, h * s, lead * s, own);
            p.patch = g;
            p.member = k;
            if (k > 0) {
                // Either side in turn (the fourth outside the second), tucked in a little.
                const float tuck = p.shape.R * rng.Range(0.12f, 0.3f);
                xs[k] = (k % 2 == 1) == (first > 0.0f) ? reachR + p.shape.R - tuck : reachL - p.shape.R + tuck;
            }
            reachL = std::min(reachL, xs[k] - p.shape.R);
            reachR = std::max(reachR, xs[k] + p.shape.R);
            left = std::min(left, xs[k] - p.halfW + kReach * s);
            right = std::max(right, xs[k] + p.halfW - kReach * s);
            it.pumpkins.push_back({xs[k]});
            built.push_back(std::move(p));
        }
        for (Patch::Member& mb : it.pumpkins) mb.dx = (mb.dx - left) / s;
        it.w = (right - left) / s;
        count += n;
        patches_.push_back(std::move(it));
    }
    const float usable = wDip - 2.0f * Margin();
    float need = -kMinGap;
    size_t fit = 0;
    for (; fit < patches_.size(); ++fit) {
        need += patches_[fit].w + kMinGap;
        if (need > usable && fit > 0) break;
    }
    patches_.resize(fit);
    std::erase_if(built, [fit](const Pumpkin& p) { return p.patch >= static_cast<int>(fit); });
    // Big pumpkins first: smaller ones stand in front.
    std::stable_sort(built.begin(), built.end(), [](const Pumpkin& a, const Pumpkin& b) { return a.H > b.H; });
    pumpkins_ = std::move(built);
    NumberCandles(true);
}

// Where everything stands, and the scarecrows. The patches spread out over
// the screen, sharing the room left over unevenly; the scarecrows go in the
// open stretches between them, the middle ones first, then the screen's ends
// (where the pile thins out), and turn to face the nearest patch. So the
// pumpkins stay put whatever the scarecrows, unless the stretches can't hold
// them all: then the patches close up to make room, keeping their looks,
// leaves and candles.
void Scenery::LayoutScarecrows(int crows) {
    crows_.clear();
    spans_.clear();
    ++genCrows_;
    lit_[1] = 0.0f;
    const float s = layout_.dpiScale;
    if (layout_.width <= 0 || !(s > 0.0f)) return;
    const float wDip = static_cast<float>(layout_.width) / s;
    const float sMax = kMaxScarecrow * SizeScale();
    const uint32_t seed = Seed();
    Rng rng(seed ^ 0x5CA3u, 0x5CA3ull);
    Rng spread(seed ^ 0x6A9Du, 0x6A9Dull);  // the spacing

    std::vector<float> sizes, widths;  // DIP
    for (int k = 0; k < crows; ++k) {
        sizes.push_back(sMax * rng.Range(0.86f, 1.0f));
        widths.push_back(2.0f * kCrowShare * sizes.back());
    }

    // A row of things `ws` wide along the ground, sharing the room left over
    // out between them unevenly (half as much at the ends): left edges.
    const float margin = Margin(), usable = wDip - 2.0f * margin;
    auto row = [&](const std::vector<float>& ws) {
        float need = -kMinGap;
        for (float w : ws) need += w + kMinGap;
        const float room = std::max(0.0f, usable - need);
        std::vector<float> weights(ws.size() + 1);
        float total = 0.0f;
        for (size_t i = 0; i < weights.size(); ++i)
            total += (weights[i] = spread.Range(0.6f, 1.4f) * (i == 0 || i == ws.size() ? 0.5f : 1.0f));
        std::vector<float> lefts(ws.size());
        float x = margin;
        for (size_t i = 0; i < ws.size(); ++i) {
            x += room * weights[i] / total;
            lefts[i] = x;
            x += ws[i] + kMinGap;
        }
        return lefts;
    };
    const int G = static_cast<int>(patches_.size());
    std::vector<float> patchWidths;
    for (const Patch& pt : patches_) patchWidths.push_back(pt.w);
    std::vector<float> patchLeft = row(patchWidths);

    // The open stretches.
    struct Gap {
        float a, b;  // DIP
        std::vector<size_t> items;
        float used = 0.0f;  // DIP taken by its scarecrows
    };
    std::vector<Gap> gaps;
    for (int i = 0; i + 1 < G; ++i)
        gaps.push_back({patchLeft[static_cast<size_t>(i)] + patches_[static_cast<size_t>(i)].w,
                        patchLeft[static_cast<size_t>(i) + 1]});
    std::stable_sort(gaps.begin(), gaps.end(), [wDip](const Gap& p, const Gap& q) {
        return std::fabs(p.a + p.b - wDip) < std::fabs(q.a + q.b - wDip);
    });
    const bool leftFirst = rng.Chance(0.5f);
    if (G == 0) {
        gaps.push_back({margin, wDip - margin});
    } else {
        const Gap ends[2] = {{margin, patchLeft.front()}, {patchLeft.back() + patches_.back().w, wDip - margin}};
        gaps.push_back(ends[leftFirst ? 0 : 1]);
        gaps.push_back(ends[leftFirst ? 1 : 0]);
    }
    // Room left in a gap for one more scarecrow, clear of its neighbours.
    auto room = [](const Gap& g) { return g.b - g.a - g.used - kMinGap * static_cast<float>(g.items.size() + 2); };
    // Each in turn to the next gap with room for it, so they spread out.
    size_t next = 0, fitted = 0;
    for (size_t k = 0; k < widths.size(); ++k)
        for (size_t tries = 0; tries < gaps.size(); ++tries) {
            Gap& g = gaps[(next + tries) % gaps.size()];
            if (room(g) < widths[k]) continue;
            g.items.push_back(k);
            g.used += widths[k];
            next = (next + tries + 1) % gaps.size();
            ++fitted;
            break;
        }

    std::vector<std::pair<float, size_t>> at;  // left edge (DIP) of each scarecrow placed
    if (fitted == widths.size()) {
        // In each gap, share the room left over out between its scarecrows.
        for (const Gap& g : gaps) {
            if (g.items.empty()) continue;
            const float spare = std::max(0.0f, room(g) + kMinGap);
            std::vector<float> weights(g.items.size() + 1);
            float total = 0.0f;
            for (float& w : weights) total += (w = rng.Range(0.6f, 1.4f));
            float x = g.a + kMinGap;
            for (size_t i = 0; i < g.items.size(); ++i) {
                x += spare * weights[i] / total;
                at.push_back({x, g.items[i]});
                x += widths[g.items[i]] + kMinGap;
            }
        }
    } else {
        // The patches close up: scarecrows between them (from the middle
        // out), then at the ends, as many as fit. In the line, patch i is i
        // and scarecrow k is -1 - k.
        std::vector<int> prefs;  // before patch i (0 and G: the ends)
        for (int i = 1; i < G; ++i) prefs.push_back(i);
        std::stable_sort(prefs.begin(), prefs.end(), [G](int a, int b) { return std::abs(2 * a - G) < std::abs(2 * b - G); });
        prefs.push_back(leftFirst ? 0 : G);
        prefs.push_back(leftFirst ? G : 0);
        std::vector<std::vector<int>> into(static_cast<size_t>(G) + 1);
        for (size_t k = 0; k < widths.size(); ++k)
            into[static_cast<size_t>(prefs[k % prefs.size()])].push_back(-1 - static_cast<int>(k));
        std::vector<int> line;
        for (int i = 0; i <= G; ++i) {
            line.insert(line.end(), into[static_cast<size_t>(i)].begin(), into[static_cast<size_t>(i)].end());
            if (i < G) line.push_back(i);
        }
        auto width = [&](int e) {
            return e >= 0 ? patches_[static_cast<size_t>(e)].w : widths[static_cast<size_t>(-1 - e)];
        };
        auto need = [&] {
            float w = -kMinGap;
            for (int e : line) w += width(e) + kMinGap;
            return w;
        };
        while (need() > usable) {
            auto it = std::find_if(line.rbegin(), line.rend(), [](int e) { return e < 0; });
            if (it == line.rend()) break;
            line.erase(std::next(it).base());
        }
        std::vector<float> lw;
        for (int e : line) lw.push_back(width(e));
        const std::vector<float> lefts = row(lw);
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] >= 0) patchLeft[static_cast<size_t>(line[i])] = lefts[i];
            else at.push_back({lefts[i], static_cast<size_t>(-1 - line[i])});
        }
    }

    // The pumpkins to their places (any that move go straight onto the pile there).
    for (int i = 0; i < G; ++i)
        spans_.push_back({patchLeft[static_cast<size_t>(i)], patchLeft[static_cast<size_t>(i)] + patches_[static_cast<size_t>(i)].w});
    for (Pumpkin& p : pumpkins_) {
        const float x = std::round((patchLeft[static_cast<size_t>(p.patch)] +
                                    patches_[static_cast<size_t>(p.patch)].pumpkins[static_cast<size_t>(p.member)].dx) * s);
        if (x == p.x) continue;
        p.x = x;
        p.placed = p.probed = false;
    }
    // The scarecrows, each facing the nearest patch; each draws its own numbers.
    for (const auto& [left, k] : at) {
        const float cx = left + 0.5f * widths[k];
        float target = 0.5f * wDip, best = 1.0e9f;
        for (const Span& sp : spans_) {
            const float c = 0.5f * (sp.left + sp.right);
            if (std::fabs(c - cx) < best) {
                best = std::fabs(c - cx);
                target = c;
            }
        }
        Rng own(seed ^ Hash32(static_cast<uint32_t>(k) * 2u + 0x5CA35CA3u), 0x5CA3ull);
        BuildScarecrow(crows_.emplace_back(), std::round(cx * s), sizes[k] * s, target >= cx ? 1 : -1, own);
    }
    NumberCandles(false);
}

void Scenery::BuildPumpkin(Pumpkin& p, float H, float lead, Rng& rng) const {
    const float s = layout_.dpiScale;
    // Round, squat or tall, a few lopsided; six to ten ribs.
    Gourd& g = p.shape;
    const float form = rng.Float();
    const float aspect = form < 0.42f ? rng.Range(0.74f, 0.84f) : (form < 0.74f ? rng.Range(0.58f, 0.68f) : rng.Range(0.88f, 1.0f));
    g.ribs = rng.Int(6, 10);
    g.turn = rng.Range(0.0f, kTau / static_cast<float>(g.ribs));
    g.rib = rng.Range(0.08f, 0.13f);
    g.hollow = rng.Range(0.1f, 0.16f);
    g.full = rng.Range(0.78f, 0.92f);
    g.lop = rng.Chance(0.3f) ? rng.Range(0.5f, 1.0f) * (rng.Chance(0.5f) ? 1.0f : -1.0f) : rng.Range(-0.15f, 0.15f);
    g.B = 1.0f;
    g.R = 1.0f / aspect;
    g.Settle(H);
    // Mostly rich oranges.
    float pick = rng.Float();
    p.rind = 0;
    for (int i = 0; i < static_cast<int>(std::size(kRindOdds)); ++i) {
        if (pick < kRindOdds[i]) {
            p.rind = i;
            break;
        }
        pick -= kRindOdds[i];
    }
    // A twisted stalk, leaning; green on a few, dried on most.
    p.stalk = p.rind == kGhost ? 2 : (rng.Chance(0.4f) ? 0 : 1);
    p.stalkLen = H * rng.Range(0.2f, 0.3f);
    p.stalkLean = rng.Range(0.12f, 0.45f) * (rng.Chance(0.5f) ? 1.0f : -1.0f);
    p.stalkW = std::max(2.2f * s, g.R * rng.Range(0.15f, 0.19f));
    p.stalkY = g.lift + g.At(0.0f, 0.0f).y;
    p.H = p.stalkY + 0.97f * p.stalkLen * std::cos(p.stalkLean);
    // Most of the big ones get a face at Halloween; a few small ones too.
    const float odds = (H > 0.62f * lead ? 0.78f : 0.3f) * (p.rind >= kGhost ? 0.6f : 1.0f);
    const float face = rng.Float();
    p.face = rng.Chance(odds) ? (face < 0.34f ? 0 : (face < 0.58f ? 1 : (face < 0.78f ? 2 : 3))) : -1;
    p.faceTurn = rng.Range(-0.2f, 0.2f);
    const Vec3 mid = g.Face(p.faceTurn, 0.0f, 0.05f);
    p.faceX = mid.x;
    p.faceY = g.lift + mid.y;
    p.phase = rng.Range(0.0f, 100.0f);
    // A curl of vine from the stalk now and then.
    p.tendril = rng.Chance(0.55f) ? (rng.Chance(0.5f) ? 1 : -1) : 0;
    p.tendrilLen = g.R * rng.Range(0.6f, 0.85f);
    p.rockW = kTau * kRockHz * std::pow(40.0f / std::max(H / s, 10.0f), 0.3f) * rng.Range(0.92f, 1.08f);

    // Big leaves on their stems behind it: one or two by the biggest of a
    // patch, now and then one by another.
    const int fronds = H >= 0.95f * lead ? (rng.Chance(0.55f) ? 2 : 1) : (rng.Chance(0.25f) ? 1 : 0);
    float side = rng.Chance(0.5f) ? 1.0f : -1.0f;
    float reach = g.R;
    for (int i = 0; i < fronds; ++i, side = -side) {
        Frond f;
        f.dx = side * g.R * rng.Range(0.45f, 0.8f);
        f.stem = H * rng.Range(0.3f, 0.55f);
        f.size = lead * rng.Range(0.62f, 0.8f);
        f.lean = side * rng.Range(0.3f, 0.65f);
        f.turned = rng.Float();
        f.w = kTau * kFrondHz * rng.Range(0.85f, 1.15f);
        f.gust = rng.Range(0.0f, 100.0f);
        reach = std::max(reach, std::fabs(f.dx) + std::sin(std::fabs(f.lean)) * (f.stem + f.size) + 0.3f * f.size);
        p.fronds.push_back(f);
    }
    p.halfW = reach + kReach * s;

    // Leaves settle on its top: along the top of its outline, and down into
    // the hollow round the stalk; none on the steep shoulders.
    constexpr int kCols = 48;
    float top[kCols];
    const float span = 1.12f * g.R;
    TopLine(g, span, top, kCols);
    auto topAt = [&](float x) {
        const float f = Clamp((x + span) / (2.0f * span) * static_cast<float>(kCols) - 0.5f, 0.0f, kCols - 1.001f);
        const int i = static_cast<int>(f);
        return Lerp(top[i], top[i + 1], f - static_cast<float>(i));
    };
    const float sink = 0.8f * std::max(0.0f, topAt(0.0f) - (p.stalkY - g.lift));
    auto shelfY = [&](float x) {
        const float u = Saturate(1.0f - (x * x) / (0.5f * g.R * g.R));
        return -(g.lift + topAt(x) - sink * u * u);  // body px, y down
    };
    const float reachX = 0.82f * g.R;
    const int n = std::max(3, static_cast<int>(std::ceil(2.0f * reachX / (kCellDip * s))));
    for (int i = 0; i < n; ++i) {
        Cell c;
        c.lx = -reachX + (static_cast<float>(i) + 0.5f) * 2.0f * reachX / static_cast<float>(n);
        c.ey = shelfY(c.lx);
        const float e = 0.5f * kCellDip * s;
        const float slope = (shelfY(c.lx + e) - shelfY(c.lx - e)) / (2.0f * e);
        const float inv = 1.0f / std::sqrt(1.0f + slope * slope);
        c.nx = slope * inv;
        c.ny = -inv;
        const float deg = std::atan(std::fabs(slope)) * (180.0f / kPi);
        c.capF = std::pow(Saturate((58.0f - deg) / 30.0f), 1.2f) * rng.Range(0.85f, 1.15f);
        if (std::fabs(c.lx) < 0.6f * p.stalkW) c.capF *= 0.75f;  // round the stalk
        p.litter.push_back(c);
    }
}

void Scenery::BuildScarecrow(Scarecrow& m, float x, float S, int dir, Rng& rng) const {
    const float s = layout_.dpiScale;
    m.x = x;
    m.S = S;
    m.dir = dir;
    m.shirt = rng.Int(0, kPlaidCount - 1);
    m.cloth = rng.Int(0, kPatchCount - 1);
    m.kerchief = rng.Int(0, kKerchiefCount - 1);
    const float pose = rng.Float();
    m.pose = pose < 0.42f ? 0 : (pose < 0.72f ? 1 : 2);
    m.topHat = rng.Chance(0.35f);
    m.witch = rng.Int(0, kWitchCount - 1);
    m.ribbon = rng.Int(0, static_cast<int>(std::size(kHatRibbons)) - 1);
    m.face = rng.Int(0, kFaceCount - 1);
    m.buttons = rng.Chance(0.65f);
    m.flutter = rng.Range(0.0f, 100.0f);
    m.phase = rng.Range(0.0f, 100.0f);
    // A pumpkin for a head at Halloween (its shape carries the sack's face too).
    const float neck = 0.71f * S;
    Gourd& g = m.head;
    g.ribs = rng.Chance(0.5f) ? 8 : 10;
    g.turn = rng.Range(0.0f, kTau / static_cast<float>(g.ribs));
    g.rib = 0.1f;
    g.hollow = 0.1f;
    g.full = 0.86f;
    g.lop = rng.Range(-0.2f, 0.2f);
    g.tilt = 0.2f;
    g.B = 1.0f;
    g.R = 1.0f / rng.Range(0.78f, 0.86f);
    g.Settle(S - neck);
    m.headR = g.R;
    m.headY = neck + g.lift;
    m.armY = 0.655f * S;
    m.hatY = S - 0.2f * m.headR;
    m.hatTilt0 = static_cast<float>(-dir) * rng.Range(0.04f, 0.14f);
    m.knotX = static_cast<float>(dir) * 0.085f * S;
    m.knotY = neck - 0.025f * S;
    // Straw hands at the cuffs, out along the crossbar; the arm away from the
    // pumpkins may have slipped off it and hang from the elbow, or the hand
    // toward them be raised in a wave.
    for (int k = 0; k < 2; ++k) {
        m.handX[k] = (k ? 1.0f : -1.0f) * 0.41f * S;
        m.handY[k] = m.armY;
        m.handA[k] = k ? 0.1f : kPi - 0.1f;
    }
    const int away = dir > 0 ? 0 : 1, toward = 1 - away;
    if (m.pose == 1) {
        const float sx = away ? 1.0f : -1.0f, a = rng.Range(1.0f, 1.25f), len = 0.16f * S;
        m.handX[away] = sx * (0.24f * S + len * std::cos(a));
        m.handY[away] = m.armY - len * std::sin(a);
        m.handA[away] = away ? a : kPi - a;
    } else if (m.pose == 2) {
        m.handA[toward] = toward ? -0.85f : kPi + 0.85f;
    }
    m.halfW = kCrowShare * S + kReach * s;
    m.rockW = kTau * kSwayHz * std::pow(60.0f / std::max(S / s, 20.0f), 0.3f) * rng.Range(0.92f, 1.08f);

    // Leaves settle along its shoulders and arms (not on a hanging forearm).
    const float sleeveTop = m.armY + 0.042f * S;
    auto shelf = [&](float x0, float x1, int seg) {
        const int n = std::max(2, static_cast<int>(std::ceil((x1 - x0) / (kCellDip * s))));
        for (int i = 0; i < n; ++i) {
            Cell c;
            c.lx = Lerp(x0, x1, (static_cast<float>(i) + 0.5f) / static_cast<float>(n));
            c.ey = -sleeveTop;
            const float u = std::fabs(c.lx) / (0.41f * S);  // 0 at the neck .. 1 at the cuff
            c.capF = 0.55f * (1.0f - 0.45f * u * u) * rng.Range(0.85f, 1.15f);
            c.seg = seg;
            m.shoulderLitter.push_back(c);
        }
    };
    const bool hangs = m.pose == 1;
    shelf(-(hangs && away == 0 ? 0.24f : 0.39f) * S, -0.1f * S, 0);
    shelf(0.1f * S, (hangs && away == 1 ? 0.24f : 0.39f) * S, 1);
    BuildHatShelf(m);
}

int Scenery::HatOf(const Scarecrow& m) const {
    return m.topHat ? kBatteredHat : (halloween_ ? kWitchHat : kStrawHat);
}

float Scenery::HatTop(const Scarecrow& m) const { return m.hatY + kHatHeight[HatOf(m)] * m.headR; }

float Scenery::PumpkinTop(const Pumpkin& p) const {
    float top = p.H;
    for (const Frond& f : p.fronds) top = std::max(top, f.stem * std::cos(f.lean) + 0.95f * f.size);
    return top;
}

// Where leaves lie on a scarecrow's hat: its brim (and crown, if it has a top
// to it). What lies on the hat stays when the hat changes, spread over the new one.
void Scenery::BuildHatShelf(Scarecrow& m) const {
    const float s = layout_.dpiScale;
    const float r = m.headR;
    float was = 0.0f;
    for (const Cell& c : m.hatLitter) was += c.load;
    m.hatLitter.clear();
    const int hat = HatOf(m);
    Rng rng(Hash32(static_cast<uint32_t>(std::lround(m.x)) * 31u + static_cast<uint32_t>(hat)), 0x4A7ull);
    int seg = 0;
    auto add = [&](float x0, float x1, float capF, auto&& shelfY) {
        const int n = std::max(2, static_cast<int>(std::ceil((x1 - x0) / (kCellDip * s))));
        for (int i = 0; i < n; ++i) {
            Cell c;
            c.lx = Lerp(x0, x1, (static_cast<float>(i) + 0.5f) / static_cast<float>(n));
            c.ey = shelfY(c.lx);
            const float e = 0.5f * kCellDip * s;
            const float slope = (shelfY(c.lx + e) - shelfY(c.lx - e)) / (2.0f * e);
            const float inv = 1.0f / std::sqrt(1.0f + slope * slope);
            c.nx = slope * inv;
            c.ny = -inv;
            const float edge = std::fabs(2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(n) - 1.0f);
            c.capF = capF * rng.Range(0.85f, 1.15f) * (1.0f - 0.4f * edge * edge * edge);
            c.seg = seg;
            m.hatLitter.push_back(c);
        }
        ++seg;
    };
    auto flat = [](float y) { return [y](float) { return y; }; };
    if (hat == kWitchHat) {
        // The brim, either side of the cone.
        add(-1.42f * r, -0.66f * r, 0.8f, flat(-0.06f * r));
        add(0.66f * r, 1.42f * r, 0.8f, flat(-0.06f * r));
    } else if (hat == kBatteredHat) {
        // The crown's dented top, and the brim either side.
        add(-0.58f * r, 0.58f * r, 1.0f, [r](float x) { return -1.34f * r + 0.08f * x; });
        add(-1.1f * r, -0.74f * r, 0.5f, flat(-0.08f * r));
        add(0.74f * r, 1.1f * r, 0.5f, flat(-0.08f * r));
    } else {
        // A straw hat's round crown and its wide brim.
        add(-0.52f * r, 0.52f * r, 0.75f, [r](float x) {
            const float u = x / (0.7f * r);
            return -0.04f * r - 0.84f * r * std::sqrt(std::max(0.0f, 1.0f - u * u));
        });
        add(-1.58f * r, -0.76f * r, 0.75f, flat(-0.05f * r));
        add(0.76f * r, 1.58f * r, 0.75f, flat(-0.05f * r));
    }
    if (was > 0.0f) {
        float cap = 0.0f;
        for (const Cell& c : m.hatLitter) cap += c.capF;
        for (Cell& c : m.hatLitter) c.load = was * c.capF / std::max(cap, 1e-3f);
    }
}

// Candles are lit in a shuffled order, the same each time for a monitor.
void Scenery::NumberCandles(bool pumpkins) {
    std::vector<int*> slots;
    if (pumpkins) {
        for (Pumpkin& p : pumpkins_) {
            p.candle = -1;
            if (p.face >= 0) slots.push_back(&p.candle);
        }
        candles_ = static_cast<int>(slots.size());
    } else {
        for (Scarecrow& m : crows_) slots.push_back(&m.candle);
    }
    for (size_t i = 0; i < slots.size(); ++i) *slots[i] = static_cast<int>(i);
    Rng rng(Seed() ^ (pumpkins ? 0xCA4D1Eu : 0xC40Bu), 0xF1A3ull);
    for (size_t i = slots.size(); i > 1; --i)
        std::swap(*slots[i - 1], *slots[static_cast<size_t>(rng.Int(0, static_cast<int>(i) - 1))]);
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

D2D1_POINT_2F Scenery::BodyPoint(const Pumpkin& p, float bx, float by) const {
    const float c = std::cos(p.rock), sn = std::sin(p.rock);
    return {p.x + bx * c - by * sn, layout_.groundY - p.lift + bx * sn + by * c};
}

// Body-local point (from the base, y down) to scene px, swaying about the base.
D2D1_POINT_2F Scenery::BodyPoint(const Scarecrow& m, float bx, float by) const {
    const float c = std::cos(m.rock), sn = std::sin(m.rock);
    return {m.x + bx * c - by * sn, layout_.groundY - m.lift + bx * sn + by * c};
}

// Hat-local point (from the hat's foot, y down) to scene px.
D2D1_POINT_2F Scenery::HatPoint(const Scarecrow& m, float hx, float hy) const {
    const D2D1_POINT_2F foot = BodyPoint(m, 0.0f, -m.hatY);
    const float a = m.rock + m.hatTilt0 + m.hatTilt;
    const float c = std::cos(a), sn = std::sin(a);
    return {foot.x + hx * c - hy * sn, foot.y + hx * sn + hy * c};
}

// A candle's light now: 0 (out) .. about 1. A kind's candles are lit one by
// one (`lit`: s since that began), each flaring as it catches; then they
// flicker, now and then guttering for a moment, and dance in a draught.
// Steady when Windows animations are off.
float Scenery::Flame(int candle, int count, float phase, float lit) const {
    if (candle < 0 || !halloween_) return 0.0f;
    const float span = std::min(kLightUp, 0.45f * static_cast<float>(count));
    const float on = 0.3f + span * static_cast<float>(candle) / static_cast<float>(std::max(count, 1));
    const float caught = SmoothStep(on, on + 0.25f, lit);
    if (caught <= 0.0f) return 0.0f;
    if (calm_) return 0.9f * caught;
    const double t = time_;
    float f = 0.86f + 0.08f * noise::Noise1(Wrap256(t * 2.3 + phase)) +
              0.05f * noise::Noise1(Wrap256(t * 6.7 + phase * 1.7)) +
              0.035f * noise::Noise1(Wrap256(t * 14.3 + phase * 2.9));
    const float gutter = Saturate((noise::Noise1(Wrap256(t * 0.37 + phase * 3.1)) - 0.42f) * 2.5f);
    f -= 0.25f * gutter * (0.6f + 0.4f * noise::Noise1(Wrap256(t * 11.0 + phase)));
    f += 0.08f * std::fabs(gust_) * noise::Noise1(Wrap256(t * 19.0 + phase * 0.7));
    const float u = lit - on;
    if (u > 0.0f && u < 0.6f) f += 0.35f * std::sin(kPi * u / 0.6f);
    return caught * Clamp(f, 0.4f, 1.3f);
}

// Litter over dt: it crumbles away slowly, and blows away bit by bit in the
// wind (much faster in a gust, and fast while being cleared); neighbours on a
// shelf even out, and what is over a spot's cap slides off. What blows away
// goes to `loose`, what slides off to `spill` (px^2), to leave as leaves.
void Scenery::Weather(std::vector<Cell>& cells, float capPx, float dt, float& loose, float& spill) const {
    const float s = layout_.dpiScale;
    const float cellW = kCellDip * s;
    const float g = std::fabs(gust_);
    const float crumble = 1.0f / kBlowTau;
    const float blow = kGustBlow * g * g + (clearLeft_ > 0.0f ? 1.0f / kClearTau : 0.0f);
    const float gone = 1.0f - std::exp(-dt * (crumble + blow));
    const float share = blow / (crumble + blow);
    for (Cell& c : cells) {
        if (c.load <= 0.0f) continue;
        float lost = c.load * gone;
        if (c.load - lost < 0.02f * s) lost = c.load;  // the last scrap goes
        c.load -= lost;
        loose += lost * share * cellW;
    }
    const float k = std::min(0.25f, kDiffuse * dt);
    for (size_t i = 1; i < cells.size(); ++i) {
        if (cells[i].seg != cells[i - 1].seg) continue;
        const float f = k * (cells[i - 1].load - cells[i].load);
        cells[i - 1].load -= f;
        cells[i].load += f;
    }
    const float slide = ApproachFactor(dt, kSlideTau);
    for (Cell& c : cells) {
        const float excess = c.load - c.capF * capPx;
        if (excess <= 0.0f) continue;
        c.load -= excess * slide;
        spill += excess * slide * cellW;
    }
}

// Litter knocked loose (px^2) leaves a leaf at a time from `at`, at (vx, vy).
void Scenery::Toss(float& shed, float area, D2D1_POINT_2F at, float vx, float vy, std::vector<LeafClump>& clumps) {
    const float s = layout_.dpiScale;
    shed += area;
    const float per = kClumpArea * s * s;
    for (int n = 0; shed >= per && n < 16; ++n) {
        shed -= per;
        LeafClump k;
        k.x = at.x + rng_.Range(-1.5f, 1.5f) * s;
        k.y = at.y;
        k.vx = vx + rng_.Range(-25.0f, 25.0f) * s;
        k.vy = std::max(vy + rng_.Range(-30.0f, 10.0f) * s, -70.0f * s);
        k.d = rng_.Range(2.6f, 4.4f) * s;
        clumps.push_back(k);
    }
    shed = std::min(shed, 4.0f * per);  // a big knock doesn't queue up a flurry
}

void Scenery::Update(float dt, double time, float windPx, const LeafPile& pile, std::vector<LeafClump>& clumps) {
    time_ = time;
    if (relayoutAt_ >= 0.0 && time >= relayoutAt_) Relayout(false);
    if (!(dt > 0.0f)) dt = 0.0f;
    // Halloween switched: the props fade out, change their looks, and fade back in.
    if (halloweenWanted_ != halloween_) {
        swap_ = std::max(0.0f, swap_ - dt / kSwapOut);
        if (swap_ <= 0.0f || (pumpkins_.empty() && crows_.empty())) ApplyHalloween(halloweenWanted_);
    } else {
        swap_ = std::min(1.0f, swap_ + dt / kSwapIn);
    }
    if (pumpkins_.empty() && crows_.empty()) return;
    // Fade each kind in and out; a kind fully hidden drops its leaves (and the leaves perched on it).
    auto fade = [&](float& f, bool show, bool pumpkins) {
        if (f == (show ? 1.0f : 0.0f)) return;
        if (f == 0.0f) {
            // Appearing: its candles are lit afresh, and windows over it are looked for at once.
            lit_[pumpkins ? 0 : 1] = 0.0f;
            if (pumpkins) {
                for (Pumpkin& p : pumpkins_) p.probed = false;
            } else {
                for (Scarecrow& m : crows_) m.probed = false;
            }
            coverStale_ = true;
        }
        f = show ? std::min(1.0f, f + dt / 0.7f) : std::max(0.0f, f - dt / 0.5f);
        if (f > 0.0f) return;
        if (pumpkins) {
            for (Pumpkin& p : pumpkins_) {
                for (Cell& c : p.litter) c.load = 0.0f;
                p.loose = p.spill = p.shed = 0.0f;
                p.rock = p.rockV = 0.0f;
                for (Frond& fr : p.fronds) fr.sway = fr.swayV = 0.0f;
            }
            ++genPumpkins_;
        } else {
            for (Scarecrow& m : crows_) {
                for (Cell& c : m.hatLitter) c.load = 0.0f;
                for (Cell& c : m.shoulderLitter) c.load = 0.0f;
                m.loose = m.spill = m.shed = 0.0f;
                m.rock = m.rockV = m.hatTilt = m.hatTiltV = 0.0f;
            }
            ++genCrows_;
        }
    };
    fade(fadePumpkins_, showPumpkins_, true);
    fade(fadeCrows_, showCrows_, false);
    if (!Active()) {
        // Nothing follows the pile while everything is hidden: whatever comes
        // back goes straight onto it.
        for (Pumpkin& p : pumpkins_) p.placed = false;
        for (Scarecrow& m : crows_) m.placed = false;
        return;
    }
    if (clearLeft_ > 0.0f && (clearLeft_ -= dt) <= 0.0f) {
        // The end of a clear: the last scraps go.
        for (Pumpkin& p : pumpkins_)
            for (Cell& c : p.litter) c.load = 0.0f;
        for (Scarecrow& m : crows_) {
            for (Cell& c : m.hatLitter) c.load = 0.0f;
            for (Cell& c : m.shoulderLitter) c.load = 0.0f;
        }
    }
    for (float& l : lit_) l = std::min(l + dt, 60.0f);
    // People who turn off Windows' animation effects get steady candles.
    if ((calmCheck_ -= dt) <= 0.0f) {
        calmCheck_ = 2.0f;
        BOOL animate = TRUE;
        calm_ = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && !animate;
    }

    const float s = layout_.dpiScale;
    const float gust = Clamp(windPx / (kGustWind * s), -1.0f, 1.0f);
    gust_ = gust;
    const int sub = std::max(1, static_cast<int>(std::ceil(dt * 240.0f)));
    const float h = dt / static_cast<float>(sub);
    const double t = time;
    float top = layout_.groundY;
    // Stand in the pile: a pumpkin settles a little into the leaves under it, a
    // scarecrow's post goes down through them. Rising leaves lift a prop gently
    // to whole pixels, only once they have really built up, so it never
    // twitches as the pile settles; leaves going from under it (blown away,
    // cleared, raked) drop it straight down onto what is left.
    auto stand = [&](float& lift, float& goal, float& fall, bool& placed, float& surface, float& surfaceL,
                     float& surfaceR, float x, float footW, float spanW, bool post) {
        surface = surfaceL = surfaceR = 0.0f;
        if (pile.Width() > 0) {
            const float a = pile.HeightAt(x - 0.5f * footW), b = pile.HeightAt(x), c = pile.HeightAt(x + 0.5f * footW);
            const float lowest = std::min({a, b, c});
            surface = post ? lowest : 0.5f * lowest + (a + b + c) / 6.0f;
            surfaceL = pile.HeightAt(std::max(0.0f, x - spanW));
            surfaceR = pile.HeightAt(std::min(static_cast<float>(pile.Width() - 1), x + spanW));
        }
        const float sunk = post ? std::min(0.3f * surface, 5.0f * s) : std::min(0.12f * surface, 2.0f * s);
        const float want = std::max(0.0f, surface - sunk);
        if (!placed) {
            goal = lift = std::round(want);
            fall = 0.0f;
            placed = true;
            return;
        }
        if (std::fabs(want - goal) > 1.5f * s) goal = std::round(want);
        if (goal < lift) {
            fall += kDrop * s * dt;
            lift = std::max(goal, lift - fall * dt);
        } else {
            lift += (goal - lift) * ApproachFactor(dt, 1.2f);
            if (goal - lift < 0.05f) lift = goal;
        }
        if (lift == goal) fall = 0.0f;
    };
    auto step = [&](float& vis, bool covered) {
        vis = covered ? std::max(0.0f, vis - dt / 0.25f) : std::min(1.0f, vis + dt / 0.7f);
    };
    // Litter come loose leaves a leaf at a time: lifted off downwind from a
    // full spot, or sliding off the end of its shelf.
    const float per = kClumpArea * s * s;
    auto release = [&](std::vector<Cell>& cells, float& loose, float& spill, auto&& place) {
        for (int n = 0; n < 3 && (loose >= per || spill >= per) && !cells.empty(); ++n) {
            const bool slid = spill >= per;
            (slid ? spill : loose) -= per;
            size_t at = rng_.Chance(0.5f) ? 0 : cells.size() - 1;
            if (!slid) {
                float most = -1.0f;
                for (int k = 0; k < 4; ++k) {
                    const size_t i = static_cast<size_t>(rng_.Int(0, static_cast<int>(cells.size()) - 1));
                    if (cells[i].load > most) {
                        most = cells[i].load;
                        at = i;
                    }
                }
            }
            const Cell& c = cells[at];
            LeafClump k;
            const D2D1_POINT_2F p = place(c);
            k.x = p.x;
            k.y = p.y;
            if (slid) {
                k.vx = (c.lx < 0.0f ? -1.0f : 1.0f) * rng_.Range(12.0f, 35.0f) * s + 0.3f * windPx;
                k.vy = rng_.Range(0.0f, 15.0f) * s;
            } else {
                k.vx = windPx * rng_.Range(0.5f, 0.9f) + rng_.Range(-15.0f, 15.0f) * s;
                k.vy = -rng_.Range(20.0f, 55.0f) * s;
            }
            k.d = rng_.Range(2.6f, 4.4f) * s;
            clumps.push_back(k);
        }
        loose = std::min(loose, 4.0f * per);
        spill = std::min(spill, 4.0f * per);
    };
    // Rocked hard, a prop shakes off its litter.
    auto shake = [&](std::vector<Cell>& cells, float amount, float& shed, auto&& place, float vx) {
        if (amount <= 0.0f) return;
        for (Cell& c : cells) {
            if (c.load <= 0.0f) continue;
            const float take = c.load * std::min(amount, 1.0f);
            c.load -= take;
            Toss(shed, take * kCellDip * s, place(c), vx, -rng_.Range(10.0f, 40.0f) * s, clumps);
        }
    };

    for (Pumpkin& p : pumpkins_) {
        stand(p.lift, p.liftGoal, p.fall, p.placed, p.pile, p.pileL, p.pileR, p.x, 0.9f * p.shape.R, p.shape.R, false);
        step(p.vis, p.covered);
        if (fadePumpkins_ <= 0.0f) continue;
        // Heavy: the wind doesn't move it; bumped, it rocks a little and settles.
        for (int i = 0; i < sub; ++i) Spring(p.rock, p.rockV, 0.0f, p.rockW, kRockZeta, h);
        p.rock = Clamp(p.rock, -kRockMax, kRockMax);
        // Its leaves nod on their stems, leaning with the wind and fluttering in gusts.
        for (Frond& f : p.fronds) {
            const float flutter = calm_ ? 0.0f : noise::Noise1(Wrap256(t * 1.7 + f.gust));
            const float rest = 0.18f * gust + (0.03f + 0.12f * std::fabs(gust)) * flutter;
            for (int i = 0; i < sub; ++i) Spring(f.sway, f.swayV, rest, f.w, kFrondZeta, h);
            f.sway = Clamp(f.sway, -kFrondMax, kFrondMax);
        }
        auto place = [&](const Cell& c) { return BodyPoint(p, c.lx, c.ey - 0.6f * c.load); };
        Weather(p.litter, CapPx(p.H, kLitterPerH), dt, p.loose, p.spill);
        release(p.litter, p.loose, p.spill, place);
        shake(p.litter, 3.0f * Saturate(std::fabs(p.rockV) - 0.35f) * dt, p.shed, place, p.rockV * p.H * 0.6f);
        p.flame = Flame(p.candle, candles_, p.phase, lit_[0]);
        top = std::min(top, layout_.groundY - p.lift - PumpkinTop(p));
    }
    for (Scarecrow& m : crows_) {
        stand(m.lift, m.liftGoal, m.fall, m.placed, m.pile, m.pileL, m.pileR, m.x, 0.06f * m.S, 0.4f * m.S, true);
        step(m.vis, m.covered);
        if (fadeCrows_ <= 0.0f) continue;
        // The post gives a little in the wind; the hat settles back on its own.
        const float breeze = calm_ ? 0.0f : noise::Noise1(Wrap256(t * 0.9 + m.flutter));
        const float rest = 0.012f * gust + 0.006f * std::fabs(gust) * breeze;
        for (int i = 0; i < sub; ++i) {
            Spring(m.rock, m.rockV, rest, m.rockW, kSwayZeta, h);
            Spring(m.hatTilt, m.hatTiltV, 0.0f, kTau * kHatHz, kHatZeta, h);
        }
        m.rock = Clamp(m.rock, -kSwayMax, kSwayMax);
        m.hatTilt = Clamp(m.hatTilt, -kHatMax, kHatMax);
        const float cap = CapPx(m.S, 0.03f);
        auto onHat = [&](const Cell& c) { return HatPoint(m, c.lx, c.ey - 0.6f * c.load); };
        auto onArms = [&](const Cell& c) { return BodyPoint(m, c.lx, c.ey - 0.6f * c.load); };
        Weather(m.hatLitter, cap, dt, m.loose, m.spill);
        release(m.hatLitter, m.loose, m.spill, onHat);
        Weather(m.shoulderLitter, cap, dt, m.loose, m.spill);
        release(m.shoulderLitter, m.loose, m.spill, onArms);
        const float rocked = 3.0f * Saturate(std::fabs(m.rockV) + 0.5f * std::fabs(m.hatTiltV) - 0.5f) * dt;
        shake(m.hatLitter, rocked, m.shed, onHat, m.rockV * m.hatY * 0.6f);
        shake(m.shoulderLitter, rocked, m.shed, onArms, m.rockV * m.armY * 0.6f);
        m.flame = Flame(m.candle, static_cast<int>(crows_.size()), m.phase, lit_[1]);
        top = std::min(top, layout_.groundY - m.lift - HatTop(m) - 2.0f * s);
    }
    top_ = top - 4.0f * s;
}

void Scenery::Touch(float dt, float x, float y, float vx, float vy, std::vector<LeafClump>& clumps) {
    if (!Active() || y < top_ || !(dt > 0.0f)) return;
    const float s = layout_.dpiScale;
    const float speed = std::sqrt(vx * vx + vy * vy);
    if (speed < 25.0f * s) return;  // a resting pointer leaves the scenery alone
    const float reach = kReach * s;
    const float cvx = Clamp(vx, -1200.0f * s, 1200.0f * s), cvy = Clamp(vy, -1200.0f * s, 1200.0f * s);
    const float push = ApproachFactor(dt, 0.08f);
    const float knock = Saturate(speed / (280.0f * s)) * ApproachFactor(dt, 0.04f);
    // The leaves round the pointer come off a shelf; (px, py): the pointer in the shelf's frame.
    auto brush = [&](std::vector<Cell>& cells, float px, float py, float& shed, auto&& place) {
        const float zone = reach + 0.5f * kCellDip * s;
        for (Cell& c : cells) {
            if (c.load <= 0.0f) continue;
            const float dx = std::fabs(c.lx - px), dy = std::fabs(c.ey - 0.5f * c.load - py);
            if (dx > zone || dy > zone + c.load) continue;
            const float take = c.load * knock * (1.0f - dx / zone) * (1.0f - dy / (zone + c.load + 1.0f));
            c.load -= take;
            Toss(shed, take * kCellDip * s, place(c), cvx * rng_.Range(0.2f, 0.55f),
                 std::min(0.25f * cvy, 0.0f) - rng_.Range(30.0f, 100.0f) * s, clumps);
        }
    };
    for (Pumpkin& p : pumpkins_) {
        if (Alpha(false, p.vis) < 0.999f) continue;
        const float base = layout_.groundY - p.lift;
        if (std::fabs(x - p.x) > p.halfW || y > base + reach || y < base - PumpkinTop(p) - reach) continue;
        // Its leaves are brushed aside on their stems.
        for (Frond& f : p.fronds) {
            const float a = f.lean + f.sway, l = f.stem + 0.45f * f.size;
            const float fx = p.x + f.dx + std::sin(a) * l, fy = base - std::cos(a) * l;
            const float r = 0.5f * f.size + reach;
            if ((x - fx) * (x - fx) + (y - fy) * (y - fy) > r * r) continue;
            f.swayV += (0.2f * cvx / std::max(l, 1.0f) - f.swayV) * push;
        }
        // The pointer in body coordinates (undoing the rock).
        const float c = std::cos(p.rock), sn = std::sin(p.rock);
        const float px = x - p.x, py = y - base;
        const float bx = px * c + py * sn, by = -px * sn + py * c;
        const float ex = bx / (p.shape.R + reach), ey = (-by - p.shape.lift) / (1.1f * p.shape.lift + reach);
        const bool body = ex * ex + ey * ey < 1.0f;
        const bool stalk = std::fabs(bx) < p.stalkW + reach && -by > p.stalkY - reach && -by < p.H + reach;
        if (!body && !stalk) continue;
        // Heavy: it rocks a little, more for a knock up high.
        const float lever = Saturate(-by / std::max(p.H, 1.0f));
        p.rockV += (0.09f * cvx / std::max(p.H, 1.0f) * (0.4f + 0.6f * lever) - p.rockV) * push;
        brush(p.litter, bx, by, p.shed, [&](const Cell& cell) { return BodyPoint(p, cell.lx, cell.ey - 0.5f * cell.load); });
    }
    for (Scarecrow& m : crows_) {
        if (Alpha(true, m.vis) < 0.999f) continue;
        const float base = layout_.groundY - m.lift;
        if (std::fabs(x - m.x) > m.halfW || y > base + reach || y < base - HatTop(m) - reach) continue;
        // The pointer in body coordinates (undoing the sway).
        const float c = std::cos(m.rock), sn = std::sin(m.rock);
        const float px = x - m.x, py = y - base;
        const float bx = px * c + py * sn, by = -px * sn + py * c, up = -by;
        const float S = m.S;
        const bool post = std::fabs(bx) < 0.03f * S + reach && up < m.armY;
        const bool shirt = std::fabs(bx) < 0.17f * S + reach && up > 0.36f * S - reach && up < 0.72f * S + reach;
        const bool arms = std::fabs(bx) < 0.5f * S + reach && std::fabs(up - m.armY) < 0.06f * S + reach;
        const float hx = bx, hy = up - m.headY;
        const bool head = hx * hx + hy * hy < (m.headR + reach) * (m.headR + reach);
        const bool hat = up > m.hatY - reach && up < HatTop(m) + reach && std::fabs(bx) < 1.5f * m.headR + reach;
        if (!post && !shirt && !arms && !head && !hat) continue;
        // Bumped: it sways (more for a bump up high) and its hat wobbles on its head.
        const float lever = Saturate(up / std::max(S, 1.0f));
        m.rockV += (0.1f * cvx / std::max(S, 1.0f) * (0.3f + 0.7f * lever) - m.rockV) * push;
        m.hatTiltV += (hat ? 0.3f : 0.12f) * cvx / std::max(m.headR, 1.0f) * push;
        if (hat) {
            // The pointer in hat coordinates.
            const D2D1_POINT_2F foot = BodyPoint(m, 0.0f, -m.hatY);
            const float a = m.rock + m.hatTilt0 + m.hatTilt, ca = std::cos(a), sa = std::sin(a);
            const float qx = x - foot.x, qy = y - foot.y;
            brush(m.hatLitter, qx * ca + qy * sa, -qx * sa + qy * ca, m.shed,
                  [&](const Cell& cell) { return HatPoint(m, cell.lx, cell.ey - 0.5f * cell.load); });
        }
        if (arms)
            brush(m.shoulderLitter, bx, by, m.shed,
                  [&](const Cell& cell) { return BodyPoint(m, cell.lx, cell.ey - 0.5f * cell.load); });
    }
}

bool Scenery::Catch(float x, float y0, float y1, float r, float amount, uint32_t id, Perch* perch) {
    if (leafDepth_ <= 0.0f || !Active() || y1 + r < top_) return false;
    const float s = layout_.dpiScale;
    if (2.0f * r > kMaxCatchDip * s) return false;
    const float dDip = Clamp(2.0f * r / s, 0.5f, kMaxCatchDip);
    const float area = kLeafVolume * std::pow(dDip, kVolumeExp) * s * s * std::min(amount, 64.0f) * rng_.Range(0.7f, 1.3f);
    const float cellW = kCellDip * s;
    // Came down past `surface` this frame (not already below it: then it is in front).
    auto crossed = [&](float surface) { return y1 + 0.5f * r >= surface && y0 + 0.5f * r < surface + 1.5f * s; };
    // Whether it settles on a surface or passes in front: the same answer
    // every frame it spends crossing, so the odds don't grow with the frame rate.
    auto settles = [&](float chance, int kind, int prop, int part) {
        const uint32_t where = static_cast<uint32_t>(((kind * 64 + prop) * 8 + part) * 4);
        return HashFloat(id * 0x9E3779B1u ^ Hash32(where + 0x2545F491u)) < chance;
    };
    // The cell of a shelf it came down on: -1 none, -2 it passes in front.
    auto land = [&](const std::vector<Cell>& cells, float chance, int kind, int prop, int part, auto&& place) {
        for (size_t i = 0; i < cells.size(); ++i) {
            const Cell& c = cells[i];
            if (c.capF < 0.1f) continue;
            const D2D1_POINT_2F p = place(c);
            if (std::fabs(p.x - x) > 0.5f * cellW + 0.5f * r || !crossed(p.y)) continue;
            return settles(chance, kind, prop, part) ? static_cast<int>(i) : -2;
        }
        return -1;
    };
    auto settle = [&](std::vector<Cell>& cells, int i, float capPx, float& spill, int kind, int prop, int part) {
        Deposit(cells, static_cast<size_t>(i), capPx, area, cellW, spill);
        const Cell& c = cells[static_cast<size_t>(i)];
        if (perch) *perch = {kind, prop, part, i, c.lx, c.load, kind == 0 ? genPumpkins_ : genCrows_};
        return true;
    };
    for (size_t pi = 0; pi < pumpkins_.size(); ++pi) {
        Pumpkin& p = pumpkins_[pi];
        if (std::fabs(x - p.x) > p.shape.R + r || Alpha(false, p.vis) < 0.999f) continue;
        const int i = land(p.litter, kTopCatch, 0, static_cast<int>(pi), 0,
                           [&](const Cell& c) { return BodyPoint(p, c.lx, c.ey - 0.9f * c.load); });
        if (i >= 0) return settle(p.litter, i, CapPx(p.H, kLitterPerH), p.spill, 0, static_cast<int>(pi), 0);
    }
    for (size_t mi = 0; mi < crows_.size(); ++mi) {
        Scarecrow& m = crows_[mi];
        if (std::fabs(x - m.x) > m.halfW || Alpha(true, m.vis) < 0.999f) continue;
        const float capPx = CapPx(m.S, 0.03f);
        int i = land(m.hatLitter, kHatCatch, 1, static_cast<int>(mi), 0,
                     [&](const Cell& c) { return HatPoint(m, c.lx, c.ey - 0.9f * c.load); });
        if (i >= 0) return settle(m.hatLitter, i, capPx, m.spill, 1, static_cast<int>(mi), 0);
        if (i == -2) continue;
        i = land(m.shoulderLitter, kShoulderCatch, 1, static_cast<int>(mi), 1,
                 [&](const Cell& c) { return BodyPoint(m, c.lx, c.ey - 0.9f * c.load); });
        if (i >= 0) return settle(m.shoulderLitter, i, capPx, m.spill, 1, static_cast<int>(mi), 1);
    }
    return false;
}

bool Scenery::PerchPoint(const Perch& perch, float& x, float& y) const {
    if (perch.gen != (perch.kind == 0 ? genPumpkins_ : genCrows_) || perch.prop < 0 || perch.shelf < 0) return false;
    const float s = layout_.dpiScale;
    const size_t i = static_cast<size_t>(perch.shelf);
    auto at = [&](const std::vector<Cell>& cells, auto&& place) {
        if (i >= cells.size() || cells[i].lx != perch.lx) return false;
        const Cell& c = cells[i];
        if (c.load < 0.35f * perch.load - 0.3f * s) return false;  // knocked off or blown away
        const D2D1_POINT_2F p = place(c);
        x = p.x;
        y = p.y;
        return true;
    };
    if (perch.kind == 0 && perch.prop < static_cast<int>(pumpkins_.size())) {
        const Pumpkin& p = pumpkins_[static_cast<size_t>(perch.prop)];
        return at(p.litter, [&](const Cell& c) { return BodyPoint(p, c.lx, c.ey - 0.8f * c.load); });
    }
    if (perch.kind == 1 && perch.prop < static_cast<int>(crows_.size())) {
        const Scarecrow& m = crows_[static_cast<size_t>(perch.prop)];
        if (perch.part == 0) return at(m.hatLitter, [&](const Cell& c) { return HatPoint(m, c.lx, c.ey - 0.8f * c.load); });
        return at(m.shoulderLitter, [&](const Cell& c) { return BodyPoint(m, c.lx, c.ey - 0.8f * c.load); });
    }
    return false;
}

void Scenery::ProbeCover(const std::function<bool(float, float)>& covered) {
    coverStale_ = false;
    // A hidden kind isn't looked at (it is again when it reappears). A prop
    // looked at for the first time takes the answer at once, so it never shows
    // over a window, even for a moment.
    // Points spread over each prop, its outermost parts included (leaves,
    // stalk, hands and hat), so a window over any part of it counts.
    if (fadePumpkins_ > 0.0f) {
        for (Pumpkin& p : pumpkins_) {
            const float base = layout_.groundY - p.lift;
            const float R = p.shape.R, mid = base - p.shape.lift;
            bool hit = covered(p.x, mid) || covered(p.x - 0.85f * R, mid) || covered(p.x + 0.85f * R, mid) ||
                       covered(p.x, base - 0.9f * p.H) || covered(p.x - 0.5f * R, base - 0.15f * p.H) ||
                       covered(p.x + 0.5f * R, base - 0.15f * p.H);
            for (const Frond& f : p.fronds) {
                if (hit) break;
                const float l = f.stem + 0.5f * f.size;
                hit = covered(p.x + f.dx + std::sin(f.lean) * l, base - std::cos(f.lean) * l);
            }
            p.covered = hit;
            if (!p.probed) p.vis = p.covered ? 0.0f : 1.0f;
            p.probed = true;
        }
    }
    if (fadeCrows_ > 0.0f) {
        for (Scarecrow& m : crows_) {
            const float base = layout_.groundY - m.lift;
            const float arms = base - m.armY, brim = base - m.hatY - 0.1f * m.headR;
            m.covered = covered(m.x, base - m.headY) || covered(m.x, base - HatTop(m) + 0.15f * m.headR) ||
                        covered(m.x, base - 0.5f * m.S) || covered(m.x, base - 0.15f * m.S) ||
                        covered(m.x - 0.45f * m.S, arms) || covered(m.x + 0.45f * m.S, arms) ||
                        covered(m.x - 1.2f * m.headR, brim) || covered(m.x + 1.2f * m.headR, brim);
            if (!m.probed) m.vis = m.covered ? 0.0f : 1.0f;
            m.probed = true;
        }
    }
}

void Scenery::ClearLeaves(bool animated) {
    float most = 0.0f;
    auto clear = [&](std::vector<Cell>& cells) {
        for (Cell& c : cells) {
            most = std::max(most, c.load);
            if (!animated) c.load = 0.0f;
        }
    };
    for (Pumpkin& p : pumpkins_) {
        clear(p.litter);
        p.loose = p.spill = p.shed = 0.0f;
    }
    for (Scarecrow& m : crows_) {
        clear(m.hatLitter);
        clear(m.shoulderLitter);
        m.loose = m.spill = m.shed = 0.0f;
    }
    clearLeft_ = animated && most > 0.0f ? 2.0f : 0.0f;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void Scenery::ReleaseDeviceResources() {
    page_.Reset();
    pageGen_ = 0;
}

bool Scenery::Prepare(ID2D1DeviceContext* dc) {
    if (!Active() || !dc) return false;
    if (page_ && pageGen_ == artGen_) return true;
    const double now = QpcSeconds();
    if (now < pageRetry_) return false;
    page_.Reset();
    if (FAILED(BuildPage(dc))) {
        page_.Reset();
        pageRetry_ = now + 1.0;
        return false;
    }
    pageGen_ = artGen_;
    return true;
}

// A prop's opacity: its kind's fade, its own stepping aside and a Halloween
// switch's fade, all eased.
float Scenery::Alpha(bool scarecrow, float vis) const {
    const float f = scarecrow ? fadeCrows_ : fadePumpkins_;
    return Ease(Saturate(f)) * Ease(Saturate(vis)) * Ease(Saturate(swap_));
}

void Scenery::EmitProps(std::vector<ScenerySprite>& out) const {
    if (!Active() || !page_ || pageGen_ != artGen_) return;
    const float s = layout_.dpiScale;
    const double t = time_;
    const float stir = calm_ ? 0.0f : 1.0f;  // animation effects off: nothing flutters on its own
    auto put = [&](const Sprite& sp, float x, float y, float angle, D2D1_COLOR_F col) {
        out.push_back(Quad(sp, 1.0f, 1.0f, angle, x, y, col));
    };
    // Leaves lying along a shelf: one at a spot to begin with, more on top as
    // it fills, each its own shape, colour, size and turn. place(cell, rise,
    // point, angle): where a cell's leaves lie `rise` px up off the shelf.
    auto litter = [&](const std::vector<Cell>& cells, uint32_t salt, float alpha, auto&& place) {
        if (litterCount_ <= 0) return;
        for (size_t i = 0; i < cells.size(); ++i) {
            const Cell& c = cells[i];
            for (int layer = 0; layer < 3; ++layer) {
                const float from = (0.08f + kLayerDip * static_cast<float>(layer)) * s;
                if (c.load <= from) break;
                const float a = alpha * Saturate((c.load - from) / (0.5f * s));
                const uint32_t h = Hash32(salt + static_cast<uint32_t>(i) * 31u + static_cast<uint32_t>(layer) * 977u);
                D2D1_POINT_2F p;
                float angle;
                place(c, std::min(c.load, from + 0.5f * kLayerDip * s) * 0.8f, p, angle);
                const float along = (Byte(h, 0) - 0.5f) * 0.9f * kCellDip * s;
                p.x += std::cos(angle) * along;
                p.y += std::sin(angle) * along;
                const float len = kLeafletDip * s * (0.75f + 0.5f * Byte(h, 1));
                const int shape = static_cast<int>(h >> 29) % kLeaflets;
                const int l = PickLevel(leafletPx_, kLevels, len);
                const D2D1_COLOR_F& col = litter_[(h >> 11) % static_cast<uint32_t>(litterCount_)];
                const float shade = (layer == 0 ? 0.8f : (layer == 1 ? 0.9f : 1.0f)) * (0.88f + 0.14f * Byte(h, 2));
                const float turn = kPi * (2.0f * Byte(h, 3) - 1.0f);
                const float squash = 0.6f + 0.35f * Byte(h >> 7, 1);  // lying flat, or propped up on the others
                out.push_back(Leaflet(leaflet_[shape][l], len / leafletPx_[l], turn, squash, angle + 0.25f * (Byte(h, 2) - 0.5f),
                                      p.x, p.y, {col.r * shade, col.g * shade, col.b * shade, a}));
            }
        }
    };
    // What shows through the cuts: the candle's light, or dim embers before it is lit.
    auto lantern = [](float flame, float alpha) -> D2D1_COLOR_F {
        const float k = Lerp(0.14f, 1.0f, Saturate(flame / 0.9f));
        return {k, k, k, alpha};
    };

    // Scarecrows first: where one meets a patch, the pumpkins stand in front.
    for (size_t mi = 0; mi < crows_.size(); ++mi) {
        const Scarecrow& m = crows_[mi];
        const float alpha = Alpha(true, m.vis);
        if (alpha <= 0.004f) continue;
        const D2D1_COLOR_F plain = {1.0f, 1.0f, 1.0f, alpha};
        put(m.body, m.x, layout_.groundY - m.lift, m.rock, plain);
        const uint32_t salt = Hash32(static_cast<uint32_t>(mi) + 0xC40Bu);
        litter(m.shoulderLitter, salt, alpha, [&](const Cell& c, float rise, D2D1_POINT_2F& p, float& angle) {
            p = BodyPoint(m, c.lx + c.nx * rise, c.ey + c.ny * rise);
            angle = m.rock + std::atan2(c.nx, -c.ny);
        });
        // Straw hands stir in the wind; a waving one waves.
        const int waving = m.pose == 2 ? (m.dir > 0 ? 1 : 0) : -1;
        for (int k = 0; k < 2; ++k) {
            const D2D1_POINT_2F p = BodyPoint(m, m.handX[k], -m.handY[k]);
            float a = m.rock + m.handA[k] +
                      stir * (0.04f + 0.14f * std::fabs(gust_)) * noise::Noise1(Wrap256(t * 2.1 + m.flutter + 13.0 * k));
            if (k == waving) a += stir * 0.22f * std::sin(kTau * Frac(t / 1.7 + 0.01 * m.flutter));
            put(m.hand[k], p.x, p.y, a, plain);
        }
        // The head, and through its cuts the candle.
        const D2D1_POINT_2F head = BodyPoint(m, 0.0f, -m.headY);
        put(m.headArt, head.x, head.y, m.rock, plain);
        if (halloween_) put(m.lantern, head.x, head.y, m.rock, lantern(m.flame, alpha));
        // The kerchief's tail hangs from the knot and streams a little
        // downwind (a positive turn swings its end to the left), lagging the sway.
        const float stream = -0.35f * gust_;
        const float sway = stir * (0.06f * std::sin(kTau * Frac(t / 2.7 + 0.01 * m.flutter)) +
                                   0.08f * noise::Noise1(Wrap256(t * 1.9 + m.flutter)));
        const D2D1_POINT_2F knot = BodyPoint(m, m.knotX, -m.knotY);
        put(m.tail, knot.x, knot.y, m.rock + stream + sway + 0.3f * m.rockV / std::max(m.rockW, 1.0f), plain);
        // The hat, and the leaves on it.
        const D2D1_POINT_2F foot = BodyPoint(m, 0.0f, -m.hatY);
        const float hatAngle = m.rock + m.hatTilt0 + m.hatTilt;
        put(m.hat, foot.x, foot.y, hatAngle, plain);
        litter(m.hatLitter, salt ^ 0x4A7u, alpha, [&](const Cell& c, float rise, D2D1_POINT_2F& p, float& angle) {
            p = HatPoint(m, c.lx + c.nx * rise, c.ey + c.ny * rise);
            angle = hatAngle + std::atan2(c.nx, -c.ny);
        });
    }

    // The pumpkins' big leaves, behind every pumpkin.
    for (const Pumpkin& p : pumpkins_) {
        const float alpha = Alpha(false, p.vis);
        if (alpha <= 0.004f || p.fronds.empty()) continue;
        for (const Frond& f : p.fronds)
            put(f.art, p.x + f.dx, layout_.groundY - p.lift, f.lean + f.sway, {1.0f, 1.0f, 1.0f, alpha});
    }
    // The pumpkins, big ones first.
    for (size_t pi = 0; pi < pumpkins_.size(); ++pi) {
        const Pumpkin& p = pumpkins_[pi];
        const float alpha = Alpha(false, p.vis);
        if (alpha <= 0.004f) continue;
        const D2D1_COLOR_F plain = {1.0f, 1.0f, 1.0f, alpha};
        const float bx = p.x, by = layout_.groundY - p.lift;
        put(p.body, bx, by, p.rock, plain);
        if (halloween_ && p.face >= 0) put(p.lantern, bx, by, p.rock, lantern(p.flame, alpha));
        if (p.tendril != 0) {
            const D2D1_POINT_2F foot = BodyPoint(p, 0.0f, -p.stalkY);
            const float curl = stir * (0.05f + 0.1f * std::fabs(gust_)) * noise::Noise1(Wrap256(t * 1.3 + p.phase)) -
                               0.12f * gust_ * static_cast<float>(p.tendril);
            put(p.vine, foot.x, foot.y, p.rock + curl, plain);
        }
        litter(p.litter, Hash32(static_cast<uint32_t>(pi) + 0x9A7C4u), alpha,
               [&](const Cell& c, float rise, D2D1_POINT_2F& q, float& angle) {
                   q = BodyPoint(p, c.lx + c.nx * rise, c.ey + c.ny * rise);
                   angle = p.rock + std::atan2(c.nx, -c.ny);
               });
    }
}

void Scenery::EmitGround(std::vector<ScenerySprite>& out) const {
    if (!Active() || !page_ || pageGen_ != artGen_) return;
    const float s = layout_.dpiScale;
    auto blob = [&](float x, float y, float w, float h, float angle, D2D1_COLOR_F col) {
        if (col.a <= 0.004f) return;
        const int l = PickLevel(blobPx_, kLevels, std::max(w, h));
        out.push_back(Quad(blob_[l], w / blobPx_[l], h / blobPx_[l], angle, x, y, col));
    };
    // Leaves heaped round a foot, on the pile's surface and tilted with it,
    // over the prop's shade (a little to the right: the light comes from the
    // upper left). Smaller where there is no pile: a few leaves on the
    // taskbar's edge.
    auto heap = [&](float x, float pile, float pileL, float pileR, float W, float alpha, uint32_t salt, int count) {
        const float baseY = layout_.groundY - pile;
        const float slope = (pileR - pileL) / std::max(W, 1.0f);  // height gained per px to the right
        auto surf = [&](float dx) { return baseY - slope * dx; };
        const float angle = std::atan2(-slope, 1.0f);
        const float sh = std::max(3.0f * s, 0.14f * W);
        blob(x + 0.1f * W, surf(0.1f * W) + 0.2f * sh, 1.1f * W, sh, angle, Color(kPileShade, 0.3f * alpha));
        blob(x + 0.04f * W, surf(0.04f * W) + 0.05f * sh, 0.5f * W, 0.55f * sh, angle, Color(kPileShade, 0.25f * alpha));
        if (litterCount_ <= 0) return;
        const bool deep = pile > 1.5f * s;  // in a pile, half in front of it; else lying on the edge
        for (int k = 0; k < count; ++k) {
            const uint32_t h = Hash32(salt + static_cast<uint32_t>(k) * 0x9E37u);
            const float dx = (Byte(h, 0) - 0.5f) * 0.95f * W;
            const float len = (deep ? 1.0f : 0.8f) * kLeafletDip * s * (0.8f + 0.5f * Byte(h, 1));
            const int shape = static_cast<int>(h >> 29) % kLeaflets;
            const int l = PickLevel(leafletPx_, kLevels, len);
            const D2D1_COLOR_F& col = litter_[(h >> 11) % static_cast<uint32_t>(litterCount_)];
            const float shade = 0.72f + 0.3f * Byte(h, 2);
            const float y = surf(dx) + ((deep ? 0.2f : -0.32f) + 0.3f * (Byte(h, 1) - 0.5f)) * 0.4f * len;
            out.push_back(Leaflet(leaflet_[shape][l], len / leafletPx_[l], kPi * (2.0f * Byte(h, 3) - 1.0f), 0.5f,
                                  angle + 0.3f * (Byte(h, 2) - 0.5f), x + dx, y,
                                  {col.r * shade, col.g * shade, col.b * shade, alpha}));
        }
    };
    for (size_t pi = 0; pi < pumpkins_.size(); ++pi) {
        const Pumpkin& p = pumpkins_[pi];
        const float alpha = Alpha(false, p.vis);
        if (alpha <= 0.004f) continue;
        const float W = 2.0f * p.shape.R;
        heap(p.x, p.pile, p.pileL, p.pileR, W, alpha, Hash32(static_cast<uint32_t>(pi) * 7u + 0x4EA9u),
             4 + static_cast<int>(p.shape.R / (5.0f * s)));
        // Candlelight spilling from the cuts onto the leaves in front.
        if (halloween_ && p.face >= 0 && p.flame > 0.0f) {
            const float slope = (p.pileR - p.pileL) / std::max(W, 1.0f);
            const float y = layout_.groundY - p.pile + 0.12f * p.shape.R;
            const int l = PickLevel(glowPx_, kLevels, 2.8f * p.shape.R);
            out.push_back(Quad(glow_[l], 2.8f * p.shape.R / glowPx_[l], 0.7f * p.shape.R / glowPx_[l],
                               std::atan2(-slope, 1.0f), p.x + 0.4f * p.faceX, y,
                               Glow(kGlowPool, (glowAlpha_ < 1.0f ? 0.32f : 0.2f) * p.flame * alpha, glowAlpha_)));
        }
    }
    for (size_t mi = 0; mi < crows_.size(); ++mi) {
        const Scarecrow& m = crows_[mi];
        const float alpha = Alpha(true, m.vis);
        if (alpha <= 0.004f) continue;
        heap(m.x, m.pile, m.pileL, m.pileR, 0.36f * m.S, alpha, Hash32(static_cast<uint32_t>(mi) * 7u + 0xC40Bu), 5);
    }
}

void Scenery::EmitGlow(std::vector<ScenerySprite>& out) const {
    if (!Active() || !page_ || pageGen_ != artGen_ || !halloween_) return;
    const bool mist = glowAlpha_ >= 1.0f;
    auto glow = [&](float x, float y, float d, Rgb c, float a) {
        if (a <= 0.004f) return;
        const int l = PickLevel(glowPx_, kLevels, d);
        const float k = d / glowPx_[l];
        out.push_back(Quad(glow_[l], k, k, 0.0f, x, y, Glow(c, a, glowAlpha_)));
    };
    // A lantern's glow: a warm halo round its face and a wide soft bloom.
    // Ordinary blending can't brighten: there, a faint warm mist.
    auto lantern = [&](D2D1_POINT_2F at, float r, float v) {
        if (v <= 0.004f) return;
        if (mist) {
            glow(at.x, at.y, 3.2f * r, Mix(kGlowHalo, kWhite, 0.2f), kMistAlpha * 0.34f * v);
            return;
        }
        glow(at.x, at.y, 5.0f * r, kGlowHalo, 0.12f * v);
        glow(at.x, at.y, 2.4f * r, kGlowCore, 0.11f * v);
    };
    for (const Pumpkin& p : pumpkins_) {
        if (p.face < 0) continue;
        lantern(BodyPoint(p, p.faceX, -p.faceY), p.shape.R, p.flame * Alpha(false, p.vis));
    }
    for (const Scarecrow& m : crows_) {
        const Vec3 f = m.head.Face(static_cast<float>(m.dir) * 0.42f, 0.0f, 0.05f);
        lantern(BodyPoint(m, f.x, -(m.headY + f.y)), m.headR, m.flame * Alpha(true, m.vis));
    }
}
