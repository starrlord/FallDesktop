// One monitor's worth of autumn: falling leaves, landing, cursor interaction,
// the leaf piles along the taskbar, the pumpkins and scarecrows standing in
// them and the cobwebs in the corners. Pure simulation plus a Draw() that issues
// Direct2D calls.
#pragma once

#include "Common.h"
#include "Cobweb.h"
#include "LeafArt.h"
#include "LeafPile.h"
#include "Scenery.h"
#include "Settings.h"

#include <d2d1_3.h>
#include <functional>

struct LeafRgb;  // Palette.h

// Simulation parameters derived from Settings (resolution independent).
struct SimParams {
    float density = 70.0f;      // leaves per 1920x1080 DIP of screen area
    float speed = 1.0f;         // fall speed multiplier
    float wind = 0.0f;          // steady wind, DIP/s (positive blows right)
    float gusts = 0.5f;         // 0..1 wind variability
    float size = 1.0f;          // leaf size multiplier
    float pileMaxDip = 30.0f;   // leaf pile height cap in DIPs, 0 = no piles
    LeafColor color = LeafColor::Autumn;
    COLORREF customColor = RGB(232, 116, 36);
    uint32_t leafMask = kDefaultLeafMask;
    bool mouse = true;
    bool pumpkins = true;       // pumpkins in the leaf piles
    int pumpkinAmount = Scenery::kUsualAmount;  // 0..100: how many
    bool scarecrows = true;     // scarecrows too
    int scarecrowAmount = Scenery::kUsualAmount;
    bool cobwebs = true;        // cobwebs in the corners
    bool halloween = true;      // jack-o'-lanterns, pumpkin-headed scarecrows, spiders and bats
};
SimParams MakeSimParams(const Settings& s);

struct SceneGeometry {
    int width = 0, height = 0;  // px
    float dpiScale = 1.0f;      // px per DIP
    float groundY = 0.0f;       // px from the top: line where leaves land (taskbar top)
    float originX = 0.0f;       // left edge in virtual-screen px (gusts sweep across monitors)
    float insetTop = 0.0f, insetLeft = 0.0f, insetRight = 0.0f;  // px: the work area's edges, where the cobwebs
                                                                // hang (a taskbar docked at the top or side)
    float originScale = 1.0f;   // one global px per DIP (the primary monitor's) that maps virtual-screen px onto
                                // the shared gust timeline, so gusts are continuous across monitors of mixed DPI
    bool leftOpen = true;       // the pile may spill off the left end
    bool rightOpen = true;
    bool rakeable = true;       // false when the ground is the bare screen edge under an auto-hide taskbar: the
                                // mouse rests there, so it must not rake the pile
    bool additiveGlow = true;   // lights may add light to the desktop (false where Windows composes in floating
                                // point and would clamp it away: they glow like coloured mist instead)

    bool operator==(const SceneGeometry&) const = default;
};

struct CursorState {
    bool valid = false;
    float x = 0, y = 0;    // scene-local px
    float vx = 0, vy = 0;  // px/s
};

// Wind shared by every monitor: a deterministic function of time so that
// adjacent screens agree. Returns DIP/s.
float WindAt(const SimParams& p, double time);

class Scene {
public:
    Scene();
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void Configure(const SceneGeometry& geo, const SimParams& params);
    void SetParams(const SimParams& params);
    const SceneGeometry& Geometry() const { return geo_; }

    // Advances the simulation. time is the shared clock (seconds).
    void Update(float dt, double time, const CursorState& cursor);

    // Draws the leaves, the piles, the props and the cobwebs into the current
    // target of `dc` (cleared by the caller). The target's DPI must be 96 so
    // that DIPs equal pixels.
    void Draw(ID2D1DeviceContext* dc, const LeafAtlas& atlas);

    // GPU resources (sprite batch, pile bitmap, sprite pages) belong to one device.
    void ReleaseDeviceResources();

    void ClearPiles(bool animated);

    // Speeds up accumulation (used by the small settings preview).
    void SetPileBoost(float boost) { pileBoost_ = boost; }

    // Lets the pumpkins, scarecrows and cobwebs step aside where a window
    // covers them: `covered(x, y)` says whether one covers that scene point.
    // Call every so often, and before drawing whenever CoverStale() (things new
    // or shown again take the answer at once). RecheckCover: probe them all afresh.
    void ProbeCover(const std::function<bool(float, float)>& covered) {
        scenery_.ProbeCover(covered);
        cobwebs_.ProbeCover(covered);
    }
    bool CoverStale() const { return scenery_.CoverStale() || cobwebs_.CoverStale(); }
    void RecheckCover() {
        scenery_.RecheckCover();
        cobwebs_.RecheckCover();
    }

    // How long new pumpkin and scarecrow amounts rest before the props are laid out
    // again (a dragged slider passes through many); 0: at once.
    void SetScenerySettle(float seconds) { scenery_.SetSettle(seconds); }

    // Leaves in the air, caught in webs and lying on the piles and props.
    size_t LeafCount() const;

private:
    struct Leaf;
    struct Particle;
    struct Bucket {
        ID2D1Bitmap1* page = nullptr;
        std::vector<D2D1_RECT_F> dst;
        std::vector<D2D1_RECT_U> src;
        std::vector<D2D1_COLOR_F> col;
        std::vector<D2D1_MATRIX_3X2_F> xf;
    };

    // Leaves: spawning and looks.
    void SpawnLeaf(Leaf& f, bool initial);
    void Respawn(Leaf& f);
    LeafKind PickKind();
    void ChooseLook(Leaf& f);
    void Derive(Leaf& f) const;
    void ColorOf(LeafKind kind, float gene, LeafRgb& base, LeafRgb& heart) const;
    void Recolor();
    // Coming to rest, and back into the air.
    void Hang(Leaf& f, const WebHold& hold);
    void Land(Leaf& f);
    void LandOnProp(Leaf& f);
    void LieDown(Leaf& r, float slope);
    void Unrest(Leaf& r, float vx, float vy);
    // Litter: tossed off the pile, knocked off the props, stirred by the breeze.
    Particle& NewDebris(float dPx);
    void TossDebris(Leaf& r, float vx, float vy);
    void RestDebris(const Particle& p);
    void SpawnClumps();
    float LeafDepth() const;
    float SlopeAt(float x, float halfWidth) const;

    void LayoutField();
    void UpdateField();
    void SampleField(float x, float y, float& u, float& v, float& wind) const;
    void UpdateLeaves(float dt, const CursorState& cursor);
    void UpdateHeld(Leaf& f, float dt);
    void UpdateResting(float dt);
    void UpdateParticles(float dt);
    void UpdateRake(float dt, const CursorState& cursor);
    void UpdateStirs(float dt);
    void UpdateClearing(float dt);
    void Prune();
    void ResizePopulation();
    int TargetCount() const;

    // Drawing: sprites queue in one bucket per page and draw in batches.
    Bucket& BucketFor(ID2D1Bitmap1* page, bool late);
    void AddLook(const LeafAtlas& atlas, int sprite, int blush, bool tinted, const LeafRgb& base,
                 const LeafRgb& heart, float x, float y, float diameter, float angle, float axis, float sx, float sy,
                 float alpha);
    void AddLeaf(const LeafAtlas& atlas, const Leaf& f, float alpha);
    void AddPageSprites(ID2D1Bitmap1* page);
    bool Queued() const;
    void FlushSprites(ID2D1DeviceContext* dc);
    void FlushClipped(ID2D1DeviceContext* dc, const D2D1_RECT_F& clip);
    void DrawPile(ID2D1DeviceContext* dc);

    SceneGeometry geo_;
    SimParams params_;
    std::vector<Leaf> leaves_;        // in the air, or caught in a web
    std::vector<Leaf> resting_;       // lying on the piles and the props, fading into the litter
    std::vector<Particle> particles_; // tossed, knocked or stirred litter
    std::vector<LeafKind> kinds_;     // enabled kinds
    std::vector<float> kindWeights_;  // and how often each falls (all zero: evenly)
    float kindWeightSum_ = 0.0f;
    std::vector<LeafKind> debris_;    // small foliage the litter is made of (crumbs besides)
    LeafColor colorNow_ = LeafColor::Autumn;  // what the leaves are coloured for
    COLORREF customNow_ = RGB(232, 116, 36);
    LeafPile pile_;
    Scenery scenery_;
    Cobwebs cobwebs_;
    std::vector<LeafClump> clumps_;            // leaves off the pumpkins and scarecrows this frame
    std::vector<ScenerySprite> pageSprites_;   // scenery or cobweb sprites on their way to a bucket
    Rng rng_;
    double time_ = 0.0;
    float windNow_ = 0.0f;  // px/s
    float stirTimer_ = 0.0f;
    float sprayBudget_ = 0.0f;
    float clearing_ = 0.0f;   // s left of the piles blowing away (ClearPiles)
    float clearDebt_ = 0.0f;  // leaves owed to that
    float pileBoost_ = 1.0f;

    // Raking: how long the current sweep has qualified (s), and where its
    // trench ends so far (px; ceiling is px above the ground line).
    float rakeDwell_ = 0.0f;
    float rakeX_ = 0.0f, rakeCeiling_ = 0.0f;
    bool rakeValid_ = false;

    // Air flow on a coarse grid: divergence-free turbulence (the curl of a
    // noise potential, so leaves never bunch into lanes) plus the wind, whose
    // gusts travel across the desktop downwind.
    int fieldCols_ = 0, fieldRows_ = 0;
    float fieldCell_ = 1.0f, fieldX0_ = 0.0f, fieldY0_ = 0.0f;
    std::vector<float> psi_, fieldU_, fieldV_, fieldWind_;
    double fieldTime_ = -1.0;
    float fieldRms_ = 0.0f;    // running RMS of the raw curl (normalizes amplitude)
    float fieldScale_ = 0.0f;  // raw curl -> px/s
    float fieldSeed_ = 0.0f;

    // Device resources.
    ComPtr<ID2D1Bitmap1> pileBitmap_;
    ComPtr<ID2D1SpriteBatch> batch_;
    ComPtr<ID2D1DeviceContext3> dc3_;
    ID2D1DeviceContext* dcOwner_ = nullptr;
    bool pileUploadAll_ = true;

    std::vector<Bucket> buckets_;  // one per page, drawn in this order...
    std::vector<Bucket> late_;     // ...then these: blushes whose page differs from their leaf's
};
