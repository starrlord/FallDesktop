// The scenery standing in the leaf piles along the taskbar: pumpkins, in
// patches, and scarecrows.
//
// A scene places its props along the ground line (the same spots for the same
// monitor and amounts): patches of pumpkins, and scarecrows in the open
// stretches between them, facing the pumpkins. Pumpkins are heavy: the pointer
// rocks them a little and knocks the leaves off them, and their vines and
// leaves stir in the wind. Scarecrows sway on their posts when bumped, their
// hats wobble, their scarves and straw flutter in the wind and leaves settle
// on their hats and shoulders. Everything stands in the pile and rises and
// settles with it, steps aside where a window covers it, and is never moved or
// removed by the pointer.
//
// At Halloween (SetHalloween) many pumpkins are carved jack-o'-lanterns lit by
// flickering candles, and the scarecrows wear jack-o'-lantern heads and witch
// or battered top hats; otherwise the pumpkins are plain and the scarecrows
// have burlap heads with stitched faces and straw hats (or battered top hats).
//
// Drawing uses one sprite page per scene, built on the device when the props
// are laid out (and again when Halloween is switched): pumpkins, scarecrows,
// leaf litter and candle glows. The scene feeds the sprites through its sprite
// batch. Glow sprites are tinted so that the premultiplied result has more
// colour than alpha: they add light to the desktop rather than cover it.
#pragma once

#include "Common.h"
#include "LeafPile.h"
#include "Settings.h"

#include <d2d1_3.h>
#include <functional>

struct SceneryLayout {
    int width = 0, height = 0;  // scene px
    float dpiScale = 1.0f;      // px per DIP
    float groundY = 0.0f;       // px from the top: the line the props stand on
    float originX = 0.0f;       // scene's left edge in virtual-screen px: seeds the layout

    bool operator==(const SceneryLayout&) const = default;
};

// One sprite in the scene's batch: a cell of Page() drawn through `xf`.
struct ScenerySprite {
    D2D1_RECT_F dst;
    D2D1_RECT_U src;
    D2D1_COLOR_F color;  // straight alpha tint
    D2D1_MATRIX_3X2_F xf;
};

// Leaves knocked or sliding off a prop, for the scene to send tumbling down.
struct LeafClump {
    float x, y, vx, vy, d;  // px, px/s, diameter px
};

// Where a leaf came to rest on a prop.
struct Perch {
    int kind = -1;        // 0 pumpkin, 1 scarecrow; -1 none
    int prop = 0, part = 0, shelf = 0;
    float lx = 0.0f;      // along the shelf, px from the part's axis
    float load = 0.0f;    // litter thickness there when it landed (px)
    unsigned gen = 0;     // that kind's generation
};

class Scenery {
public:
    // The amount (0..100) that gives the usual few pumpkins or scarecrows.
    static constexpr int kUsualAmount = 35;

    Scenery();
    ~Scenery();
    Scenery(const Scenery&) = delete;
    Scenery& operator=(const Scenery&) = delete;

    // Lays the props out for a scene (keeps them when nothing relevant changed).
    void Configure(const SceneryLayout& layout);
    // Shown or hidden (they fade), and how many (0..100, see kUsualAmount). A
    // new amount only rebuilds the props when it changes how many there are,
    // and waits until the amounts have rested for `settle` seconds (a dragged
    // slider passes through many); pumpkins keep their looks, leaves and
    // candles when only the scarecrows change.
    void SetPumpkins(bool show, int amount);
    void SetScarecrows(bool show, int amount);
    void SetSettle(float seconds) { settle_ = seconds; }
    // Halloween: jack-o'-lanterns and pumpkin-headed scarecrows (see above).
    // Switching keeps the layout; the props fade out, change their looks and
    // fade back in.
    void SetHalloween(bool on);
    // The colours of the leaves that settle on the props (Palette.h).
    void SetLeafPalette(LeafColor color, COLORREF custom);
    // Whether glows may add light (premultiplied colour above alpha). Where
    // Windows composes in floating point it clamps such pixels; there they are
    // drawn as ordinary soft colour instead.
    void SetAdditiveGlow(bool on);
    // How deep leaves may lie on the pumpkins and hats, relative to the usual
    // (0 = none: what is there blows away).
    void SetLeafDepth(float scale);
    void ClearLeaves(bool animated);

    // Anything to simulate or draw.
    bool Active() const;
    // Highest point of any prop (px from the top), for quick rejects.
    float Top() const { return top_; }

    // Springs, wind, leaves settling and blowing away; props ride on the pile.
    void Update(float dt, double time, float windPx, const LeafPile& pile, std::vector<LeafClump>& clumps);
    // The pointer at (x, y) moving at (vx, vy) px/s rocks the props it bumps
    // and knocks leaves off them.
    void Touch(float dt, float x, float y, float vx, float vy, std::vector<LeafClump>& clumps);
    // Something falling at x moved from y0 to y1 (its centre; radius r): if it
    // came down on a leaf-catching surface it lands there, adding litter worth
    // `amount` leaves of its size. Fills `perch` (optional) on landing. `id`
    // tells falling things apart: whether one settles on a given surface or
    // passes in front of it is decided once, however often it is asked.
    bool Catch(float x, float y0, float y1, float r, float amount, uint32_t id, Perch* perch);
    // Where a perched leaf sits now. False once the litter under it is gone
    // (knocked off or blown away) or the props changed.
    bool PerchPoint(const Perch& perch, float& x, float& y) const;
    // Props step aside where a window covers them: `covered(x, y)` says whether
    // one covers that scene point. Call now and then (it is not free), and
    // before drawing whenever CoverStale(): props that have not been probed yet
    // take its answer at once rather than fading.
    void ProbeCover(const std::function<bool(float, float)>& covered);
    bool CoverStale() const { return coverStale_; }
    // What covers them changed all at once (the overlay moved behind or in
    // front of windows): probe everything afresh.
    void RecheckCover();

    // ---- Drawing ----------------------------------------------------------
    // Builds the sprite page on `dc`'s device if needed. False: nothing to draw.
    bool Prepare(ID2D1DeviceContext* dc);
    ID2D1Bitmap1* Page() const { return page_.Get(); }
    // Pumpkins and scarecrows, back to front (drawn before the pile, which buries their feet).
    void EmitProps(std::vector<ScenerySprite>& out) const;
    // Over the pile: leaves heaped round their feet, their shade, and
    // candlelight pooling on the leaves.
    void EmitGround(std::vector<ScenerySprite>& out) const;
    // The candles' glow, drawn over everything else.
    void EmitGlow(std::vector<ScenerySprite>& out) const;
    void ReleaseDeviceResources();

    // The props' insides (SceneryParts.h), named here for the drawing helpers.
    struct Cell;
    struct Frond;
    struct Patch;
    struct Pumpkin;
    struct Scarecrow;
    struct Sprite {
        D2D1_RECT_U src{};
        float cx = 0.0f, cy = 0.0f;  // page px where the art's anchor sits
    };

private:
    struct Counts {
        int patches = 0, crows = 0;  // pumpkin patches and scarecrows (none when the screen is too small for props)
        bool operator==(const Counts&) const = default;
    };
    struct Span {
        float left, right;  // DIP: a patch's extent along the ground
    };
    Counts CountsFor(int pumpkinAmount, int scarecrowAmount) const;
    float SizeScale() const;
    uint32_t Seed() const;
    void AmountsChanged();
    void Relayout(bool all);
    void LayoutPumpkins(int patches);
    void LayoutScarecrows(int crows);
    float Margin() const;
    void BuildPumpkin(Pumpkin& p, float heightPx, float leadPx, Rng& rng) const;
    void BuildScarecrow(Scarecrow& m, float x, float heightPx, int dir, Rng& rng) const;
    void BuildHatShelf(Scarecrow& m) const;
    int HatOf(const Scarecrow& m) const;  // scenery::HatKind
    float HatTop(const Scarecrow& m) const;
    float PumpkinTop(const Pumpkin& p) const;
    void NumberCandles(bool pumpkins);
    float CapPx(float sizePx, float share) const;
    float Flame(int candle, int count, float phase, float lit) const;
    void Weather(std::vector<Cell>& cells, float capPx, float dt, float& loose, float& spill) const;
    void Toss(float& shed, float area, D2D1_POINT_2F at, float vx, float vy, std::vector<LeafClump>& clumps);
    D2D1_POINT_2F BodyPoint(const Pumpkin& p, float bx, float by) const;
    D2D1_POINT_2F BodyPoint(const Scarecrow& m, float bx, float by) const;
    D2D1_POINT_2F HatPoint(const Scarecrow& m, float hx, float hy) const;
    float Alpha(bool scarecrow, float vis) const;
    void ApplyHalloween(bool on);
    HRESULT BuildPage(ID2D1DeviceContext* dc);

    SceneryLayout layout_;
    bool configured_ = false;
    int pumpkinAmount_ = kUsualAmount, scarecrowAmount_ = kUsualAmount;
    Counts built_;              // what the props were laid out for
    float settle_ = 0.25f;      // SetSettle
    double relayoutAt_ = -1.0;  // when to lay out for new amounts (-1: nothing waiting)
    std::vector<Patch> patches_;
    std::vector<Pumpkin> pumpkins_;  // big ones first: the smaller stand in front
    std::vector<Scarecrow> crows_;
    std::vector<Span> spans_;   // where the patches stand, left to right
    unsigned genPumpkins_ = 1, genCrows_ = 1;  // bumped whenever a kind is rebuilt, cleared or re-hatted (perches lapse)
    unsigned artGen_ = 1;       // bumped whenever the props are rebuilt or change their looks (the page must follow)
    bool coverStale_ = true;    // some props have not been probed for cover yet
    bool showPumpkins_ = true, showCrows_ = true;
    float fadePumpkins_ = 0.0f, fadeCrows_ = 0.0f;  // 0 hidden .. 1 shown
    bool halloween_ = true;     // the props' looks
    bool halloweenWanted_ = true;  // SetHalloween: the looks change once the props have faded out
    float swap_ = 1.0f;         // that fade: 0 hidden .. 1 shown
    float leafDepth_ = 0.0f;    // SetLeafDepth
    D2D1_COLOR_F litter_[24] = {};  // SetLeafPalette: the colours litter comes in
    int litterCount_ = 0;
    float glowAlpha_ = 1.0f;    // share of the desktop a glow veils (below 1: it adds light)
    float clearLeft_ = 0.0f;    // s left of an animated clear
    float top_ = 0.0f;
    float gust_ = 0.0f;         // the wind now, -1..1 of a strong gust
    double time_ = 0.0;
    float lit_[2] = {};         // s since each kind's candles began to be lit (pumpkins, scarecrows)
    int candles_ = 0;           // carved pumpkins: their candles are lit in turn
    bool calm_ = false;         // Windows animation effects are off: steady candles, nothing fluttering by itself
    float calmCheck_ = 0.0f;    // s until calm_ is looked up again
    Rng rng_;

    // Device resources.
    ComPtr<ID2D1Bitmap1> page_;
    unsigned pageGen_ = 0;      // artGen_ the page was built for
    double pageRetry_ = 0.0;    // earliest time to retry a failed build
    // Shared cells at a few resolutions (largest first, halving): soft blobs,
    // glows, and leaves for the litter (three shapes).
    static constexpr int kLevels = 4;
    Sprite blob_[kLevels], glow_[kLevels], leaflet_[3][kLevels];
    float blobPx_[kLevels] = {}, glowPx_[kLevels] = {}, leafletPx_[kLevels] = {};
};
