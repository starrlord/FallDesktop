// The leaf pile that builds up on a ledge (the top edge of the taskbar).
//
// The pile is a 1D heightfield with one column per physical pixel. Landed
// leaves add volume, steep slopes tumble (angle of repose), wind lifts leaves
// off exposed crests and drops them in the lee, and leaves slowly crumble and
// blow away so the pile never fills up evenly. The surface is shaded on the
// CPU into a premultiplied BGRA strip of leaf litter that the renderer draws
// over the taskbar edge: a field of leaves fixed in place, each of which shows
// while the pile is deep enough where it lies, so leaves stay put as the pile
// grows and slumps around them.
#pragma once

#include "Common.h"
#include "Settings.h"

#include <utility>

struct PileParams {
    float maxHeight = 0.0f;  // px; 0 disables the pile
    float dpiScale = 1.0f;   // px per DIP
    bool leftOpen = true;    // leaves can slide off the left end
    bool rightOpen = true;   // leaves can slide off the right end
    LeafColor color = LeafColor::Autumn;      // the litter's colours (Palette.h)
    COLORREF customColor = RGB(232, 116, 36);

    bool operator==(const PileParams&) const = default;
};

class LeafPile {
public:
    // Sizes the pile. Keeps the leaves already there when only params change.
    void Configure(int widthPx, const PileParams& params);
    void SetParams(const PileParams& params);
    const PileParams& Params() const { return params_; }

    int Width() const { return width_; }
    bool Enabled() const { return params_.maxHeight > 0.0f; }

    // Smoothed surface height above the ground line at x (px), x clamped.
    float HeightAt(float x) const;

    // A leaf of the given on-screen diameter landed at x. `amount` scales
    // the deposited volume (1 = one leaf's worth).
    void Deposit(float x, float diameterPx, float amount = 1.0f);

    // Rakes away the leaves above `ceiling` (px above ground) in columns [x0, x1).
    // Returns the removed area in px^2 so the caller can throw up leaves.
    float Rake(float x0, float x1, float ceiling);

    // Advances settling, tumbling, wind transport and crumbling.
    // wind is the current wind speed in px/s (positive = blowing right).
    void Update(float dt, float wind);

    void Clear();                 // remove everything now
    void BlowAway(float seconds); // animated: the leaves blow away, all gone in `seconds`
    bool Empty() const;

    // ---- Rendering --------------------------------------------------------
    // The strip covers every column; row GroundRow() is the first row at or
    // below the ground line. Rows above it hold the pile, rows below it hold
    // the leaves hanging over the edge and their soft shadow on the taskbar.
    int TexWidth() const { return width_; }
    int TexHeight() const { return texHeight_; }
    int GroundRow() const { return groundRow_; }

    // Re-shades changed columns and writes the column spans that must be
    // re-uploaded: up to maxSpans disjoint, ascending spans [x0[i], x1[i]).
    // Returns how many it wrote; 0 means nothing changed. If more separate
    // stretches changed than that, neighbours are merged across the smallest
    // gaps, so every changed column is still covered.
    int Shade(int* x0, int* x1, int maxSpans);
    const uint32_t* Pixels() const { return pixels_.data(); }

    // A random point on the visible surface (for effects that play on it):
    // returns false if there are no leaves. y is height above ground (px).
    bool RandomSurfacePoint(Rng& rng, float& x, float& y) const;

private:
    // Implementation detail; see LeafPile.cpp.
    friend struct PileTestAccess;  // offline test harness only

    static constexpr int kShadeBlock = 32;    // dirty-tracking granularity for Shade (px)
    static constexpr int kCrumbleBlock = 64;  // crumbling is applied one block at a time (px)
    static constexpr int kTileShift = 6;      // litter leaf ids count from the first leaf of a 64-px tile
    static constexpr uint16_t kNoLeaf = 0xFFFF;

    // One leaf of the litter, in order of its centre column.
    struct Leaf {
        float key;         // px above ground: it shows while the surface where it lies is higher
        uint16_t cx;       // centre column (clamped to the strip)
        uint8_t gene;      // picks its colours from the palette
        uint8_t look : 2;  // kFresh, kUnderside or kDead
        uint8_t span : 6;  // px: it lies over columns cx - span .. cx + span
    };
    // The litter at one pixel: the front-most leaf there and the one behind it.
    struct LitterPx {
        uint16_t top, behind;  // leaf ids (from the tile's first leaf), kNoLeaf if none
        uint8_t cov;           // how much of the pixel the top leaf covers (255 = all of it)
        uint8_t lum;           // its brightness here (128 = 1): veins, curl, margin, shade of leaves in front
        uint8_t blush;         // how far it has turned to its blush colour here (255 = fully)
        uint8_t behindLum;     // the brightness of the leaf behind
    };
    // A base colour and the blush colour its heart turns.
    struct Tone {
        float r, g, b, br, bg, bb;
    };
    static constexpr int kLooks = 3;
    static constexpr int kMaxTones = 12;

    void Resize(int widthPx);
    void MarkDirty(int x0, int x1);
    void MarkAllDirty();
    // Heights changed in [x0, x1). `steep`: the change may leave slopes past repose.
    void Touch(int x0, int x1, bool steep = true);
    void ComputeStaticFields();
    void RefreshFields(int x0, int x1);
    void MapFields(int x0, int x1);
    void FieldStatistics();
    void NormalizeCatch();
    float EndTaper(float xDip, float widthDip) const;
    void UpdateLayout();
    float LocalCap(int i) const { return capHeight_ * capF_[static_cast<size_t>(i)]; }
    void AddBump(float cx, float halfWidth, float volume);
    float LeeFactor(int i) const;
    void Relax(int sweeps);
    void Creep(float dt);
    void WindStep(float dt, float windPx);
    void CrumbleBlocks(float dt);
    void Derive();
    float WideAverage(int i, int radius) const;
    bool LitterReady() const;
    void BuildLitter();
    void RefreshTones();
    bool Shows(const Leaf& f) const;
    void MarkLeaf(const Leaf& f, int skip0 = 0, int skip1 = 0);
    void MarkFlips(int x0, int x1);
    void ShadeRange(int x0, int x1);

    PileParams params_;
    int width_ = 0;
    int texHeight_ = 0;
    int groundRow_ = 0;
    std::vector<float> height_;   // physical heights (px)
    std::vector<float> smooth_;   // smoothed heights used for collision and shading
    std::vector<float> scratch_;  // temporaries: bump weights, creep fluxes
    std::vector<float> work_;     // temporaries: creep curvature
    std::vector<uint32_t> pixels_;

    // Per-column fields from smooth noise (DIP space, so the look is resolution independent).
    std::vector<float> talus_;     // max stable slope (tan of the angle of repose)
    std::vector<float> crestInv_;  // 1 / sharpest crest curvature that creep leaves alone (px)
    std::vector<float> capF_;      // local cap as a fraction of the cap height (incl. end taper)
    std::vector<float> catch_;     // how many leaves a stretch gathers
    std::vector<float> rawCatch_, rawSpot_, rawCap_;  // raw noise behind catch_ and capF_
    float fieldStats_[6] = {0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};  // mean, spread of each raw field
    float catchScale_ = 1.0f;      // makes catch_ average 1 over the strip
    std::vector<double> prefix_;   // prefix sums of smooth_ for O(1) neighbourhood averages
    std::vector<float> shadedS_;   // smooth_ as of the last shade of each column
    std::vector<uint8_t> blockDirty_;
    std::vector<std::pair<int, int>> spans_;  // Shade: block ranges shaded in this call
    std::vector<float> crumbleAge_;  // seconds since each crumble block was last visited

    // The litter (see BuildLitter): rebuilt when the width or scale changes or
    // the strip outgrows it; the colours come from tones_, so a new colour
    // setting only re-shades.
    std::vector<Leaf> leaves_;
    std::vector<uint32_t> tileBase_;  // per 64-px tile: index of the first leaf that can reach into it
    std::vector<LitterPx> litter_;    // mapRows x width_, row 0 mapAbove_ px above the ground line
    std::vector<uint8_t> present_;    // per leaf: showing (as of the last ShadeRange near it)
    Tone tones_[kLooks][kMaxTones] = {};
    uint8_t toneOf_[256] = {};        // gene -> tone, the commoner colours more often
    int mapAbove_ = 0, mapBelow_ = 0; // litter rows above and below the ground line
    int litterWidth_ = 0;
    float litterScale_ = 0.0f;
    int reachPx_ = 0;                 // furthest any leaf reaches from its centre column
    int pokePx_ = 0;                  // highest any leaf stands above its key
    bool tonesValid_ = false;

    int dirtyBlocks_ = 0;
    bool fullDirty_ = true;
    int shadeCursor_ = 0;          // next block Shade looks at (round robin)
    int unshadedUpdates_ = 0;      // Update calls since the last Shade
    int chg0_ = 0, chg1_ = 0;      // heights changed in [chg0_, chg1_) since the last Derive
    int av0_ = 0, av1_ = 0;        // columns that may be steeper than repose
    int crumbleCursor_ = 0;
    float crumbleDebt_ = 0.0f;
    float sweepDebt_ = 0.0f;
    float windDebt_ = 0.0f;
    unsigned sweepParity_ = 0;
    int fieldCursor_ = 0;
    float capHeight_ = 0.0f;       // effective cap (px); eases down when maxHeight shrinks
    float maxH_ = 0.0f;            // tallest column (px)
    float windDip_ = 0.0f;         // last wind, DIP/s
    float blowRate_ = 0.0f;        // px/s while blowing away
    float blowLeft_ = 0.0f;        // seconds of blowing away remaining
    float blowTotal_ = 0.0f;       // length of the whole blow-away (s)
    int blowDir_ = 1;              // the way it sweeps (with the wind)
    float seed_ = 0.0f;            // noise-field offset, differs per pile
    bool seeded_ = false;
    bool fieldsValid_ = false;
    double time_ = 0.0;
    Rng rng_{0xD21F7u};
};
