// Leaf colours, shared by the falling leaves (Scene), the leaf piles (LeafPile)
// and the leaves that settle on the pumpkins and scarecrows (Scenery), so that a
// pile is made of the leaves that fell on it.
//
// Leaf art is drawn in greys and takes its colour from a tint (the sprite
// batch multiplies), so one sprite serves every colour setting. Two-tone
// leaves add a "blush" over the middle of the leaf in a second colour: the
// colour the leaf turned last, like the gold heart of a scarlet maple leaf.
#pragma once

#include "Settings.h"

struct LeafRgb {
    float r, g, b;
};

namespace palette {

struct Entry {
    uint32_t base, blush;  // 0xRRGGBB
    float weight;          // how common
};

// The natural mix: maple scarlets and crimsons, oranges, ambers and golds,
// russet and brown oaks, and a few leaves still turning from green.
inline constexpr Entry kAutumn[] = {
    {0xD63A22, 0xF08A2A, 1.3f},  // scarlet, orange heart
    {0xB8232B, 0xE0572E, 0.9f},  // crimson
    {0xE36A1E, 0xF5B03A, 1.3f},  // burnt orange
    {0xF08A24, 0xF7C948, 1.1f},  // pumpkin
    {0xF0A92E, 0xF8DA62, 1.1f},  // amber
    {0xF2C53A, 0xFBE68A, 1.0f},  // golden yellow
    {0xDCC744, 0xB8C452, 0.5f},  // lemon, still green at the heart
    {0x9C4E24, 0xC27838, 0.8f},  // russet
    {0x7A4628, 0xA06A3C, 0.6f},  // oak brown
    {0x8E9C34, 0xD0B842, 0.35f}, // late green, turning gold
    {0x8A1F33, 0xC2413A, 0.5f},  // burgundy
};
// Yellows and ambers: ginkgo, birch and aspen gold.
inline constexpr Entry kGolden[] = {
    {0xF2C53A, 0xFBE68A, 1.2f}, {0xF0A92E, 0xF8D35A, 1.2f}, {0xE8B020, 0xF5D870, 1.0f},
    {0xD98E1C, 0xF2BE48, 0.8f}, {0xF7D65C, 0xFFF0A8, 0.9f}, {0xC9881E, 0xE8B648, 0.6f},
};
// Reds and burgundies: red maple, sumac and dogwood.
inline constexpr Entry kCrimson[] = {
    {0xB8232B, 0xE0572E, 1.2f}, {0x8A1F33, 0xC2413A, 0.9f}, {0xD63A22, 0xEE7A30, 1.2f},
    {0x6E1A2A, 0xA8323A, 0.6f}, {0xC42E3C, 0xE86A4A, 1.0f}, {0x9E2A20, 0xD25A2C, 0.8f},
};

inline LeafRgb FromHex(uint32_t c) {
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f};
}

inline LeafRgb Scale(LeafRgb c, float k) { return {Saturate(c.r * k), Saturate(c.g * k), Saturate(c.b * k)}; }

inline void ToHsv(LeafRgb c, float& h, float& s, float& v) {
    const float mx = std::max(c.r, std::max(c.g, c.b)), mn = std::min(c.r, std::min(c.g, c.b));
    const float d = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 0.0f) h = 0.0f;
    else if (mx == c.r) h = (c.g - c.b) / d / 6.0f;
    else if (mx == c.g) h = ((c.b - c.r) / d + 2.0f) / 6.0f;
    else h = ((c.r - c.g) / d + 4.0f) / 6.0f;
    h -= std::floor(h);
}

inline LeafRgb FromHsv(float h, float s, float v) {
    h = (h - std::floor(h)) * 6.0f;
    const int i = static_cast<int>(h);
    const float f = h - static_cast<float>(i);
    const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i % 6) {
    case 0: return {v, t, p};
    case 1: return {q, v, p};
    case 2: return {p, v, t};
    case 3: return {p, q, v};
    case 4: return {t, p, v};
    default: return {v, p, q};
    }
}

// The custom colour's family: hue, saturation and brightness wander a little
// leaf to leaf (t in [0, 1)); the blush is lighter and a touch warmer.
inline void Custom(COLORREF custom, float t, float t2, LeafRgb& base, LeafRgb& blush) {
    const LeafRgb c = {GetRValue(custom) / 255.0f, GetGValue(custom) / 255.0f, GetBValue(custom) / 255.0f};
    float h, s, v;
    ToHsv(c, h, s, v);
    const float hb = h + (t - 0.5f) * 0.06f;
    const float sb = Saturate(s * Lerp(0.82f, 1.08f, t2));
    const float vb = Saturate(v * Lerp(0.72f, 1.06f, t));
    base = FromHsv(hb, sb, vb);
    // Warmer: hues drift towards yellow (1/6) by a little.
    float toward = 1.0f / 6.0f - hb;
    toward -= std::round(toward);
    blush = FromHsv(hb + Clamp(toward, -0.05f, 0.05f), sb * 0.85f, Saturate(vb * 1.18f + 0.05f));
}

template <size_t N>
inline const Entry& Pick(const Entry (&entries)[N], float gene) {
    float total = 0.0f;
    for (const Entry& e : entries) total += e.weight;
    float x = Saturate(gene) * total;
    for (const Entry& e : entries) {
        if (x < e.weight) return e;
        x -= e.weight;
    }
    return entries[N - 1];
}

template <size_t N>
inline int List(const Entry (&entries)[N], LeafRgb* out, int max) {
    int n = 0;
    for (const Entry& e : entries) {
        if (n < max) out[n++] = FromHex(e.base);
        if (n < max) out[n++] = FromHex(e.blush);
    }
    return n;
}

}  // namespace palette

// The colours of one leaf. `gene` in [0, 1) picks it (a leaf keeps its gene
// for life, so its colour is stable); `blush` is the colour its heart turns on
// two-tone leaves. A small per-leaf brightness variation keeps neighbours apart.
inline void LeafColors(LeafColor setting, COLORREF custom, float gene, LeafRgb& base, LeafRgb& blush) {
    const float t = HashFloat(static_cast<uint32_t>(gene * 16777216.0f) ^ 0x5bd1e995u);
    const float t2 = HashFloat(static_cast<uint32_t>(gene * 16777216.0f) ^ 0x68e31da4u);
    const palette::Entry* e = nullptr;
    switch (setting) {
    case LeafColor::Golden: e = &palette::Pick(palette::kGolden, gene); break;
    case LeafColor::Crimson: e = &palette::Pick(palette::kCrimson, gene); break;
    case LeafColor::Custom: palette::Custom(custom, t, t2, base, blush); return;
    default: e = &palette::Pick(palette::kAutumn, gene); break;
    }
    const float k = Lerp(0.88f, 1.08f, t);
    base = palette::Scale(palette::FromHex(e->base), k);
    blush = palette::Scale(palette::FromHex(e->blush), k);
}

// The colours leaf litter is made of in this setting (bases and blushes, the
// most common first), for the piles and the leaves lying on the props.
// Returns how many it wrote (at most `max`; 24 is always enough).
inline int PileColors(LeafColor setting, COLORREF custom, LeafRgb* out, int max) {
    switch (setting) {
    case LeafColor::Golden: return palette::List(palette::kGolden, out, max);
    case LeafColor::Crimson: return palette::List(palette::kCrimson, out, max);
    case LeafColor::Custom: {
        int n = 0;
        for (int i = 0; i < 6 && n + 1 < max; ++i) {
            LeafRgb base, blush;
            palette::Custom(custom, (static_cast<float>(i) + 0.5f) / 6.0f, HashFloat(static_cast<uint32_t>(i) * 977u),
                            base, blush);
            out[n++] = base;
            out[n++] = blush;
        }
        return n;
    }
    default: return palette::List(palette::kAutumn, out, max);
    }
}
