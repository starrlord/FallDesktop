// Cobwebs strung across the corners of the screen, and at Halloween the
// spiders that keep them and the bats that flit past now and then.
//
// A web is a mesh of silk: frame and radial threads from anchors on the screen
// edges, and a spiral of capture thread across them. Its knots are point
// masses on damped springs, so the breeze makes a web breathe, the pointer
// pushes through it (threads stretch and spring back, and the web trembles for
// a while), and a fast swipe snaps threads, whose loose ends curl and dangle
// until they are spun again. Leaves that blow into a web may stick there and
// sway with it until the web is disturbed.
//
// A Halloween spider sits at the hub or hangs on a dragline below it, bobbing
// gently; it scurries back up when the pointer comes near, and it mends torn
// threads. Without spiders the threads mend themselves (fading back in).
//
// Webs step aside (fade) where a window covers their corner, like the props.
// Drawing uses one small sprite page per scene: threads are a soft line cell
// stretched along each segment; dew drops, spiders and bat frames are cells too.
#pragma once

#include "Common.h"
#include "Scenery.h"  // ScenerySprite

#include <d2d1_3.h>
#include <functional>
#include <memory>

struct CobwebLayout {
    int width = 0, height = 0;  // scene px
    float dpiScale = 1.0f;      // px per DIP
    float groundY = 0.0f;       // px from the top: the leaf piles' ground line (taskbar top)
    float originX = 0.0f;       // scene's left edge in virtual-screen px: seeds the look
    float insetTop = 0.0f, insetLeft = 0.0f, insetRight = 0.0f;  // px: the work area's edges (a taskbar or
                                                                // appbar docked at the top or side)

    bool operator==(const CobwebLayout&) const = default;
};

// Where a caught leaf hangs in a web.
struct WebHold {
    int web = -1;                // -1: not held
    int knot = 0;                // the knot it hangs from
    float dx = 0.0f, dy = 0.0f;  // offset from the knot (px)
    unsigned gen = 0;            // the webs' generation (holds lapse when they are strung again)
};

class Cobwebs {
public:
    Cobwebs();
    ~Cobwebs();
    Cobwebs(const Cobwebs&) = delete;
    Cobwebs& operator=(const Cobwebs&) = delete;

    // Strings webs for a scene (keeps them when nothing relevant changed).
    void Configure(const CobwebLayout& layout);
    // Shown or hidden (they fade).
    void SetShown(bool show);
    // Spiders on the webs and bats now and then.
    void SetHalloween(bool on);

    // Anything to simulate or draw.
    bool Active() const;

    // The breeze (windPx: the wind now, px/s, positive blowing right),
    // trembling and mending; spiders and bats.
    void Update(float dt, double time, float windPx);
    // The pointer at (x, y) moving at (vx, vy) px/s pushes through the threads
    // it meets; fast enough, it snaps them and shakes loose the leaves caught there.
    void Touch(float dt, float x, float y, float vx, float vy);
    // A leaf whose centre moved from (x0, y0) to (x1, y1) (radius r) blew
    // through a web: true if it stuck there, and `hold` says where. `id` tells
    // falling things apart: whether one sticks in a given web is decided once.
    bool Catch(float x0, float y0, float x1, float y1, float r, uint32_t id, WebHold& hold);
    // Where a caught leaf hangs now. False once it has shaken loose (the web
    // was disturbed or torn there, faded, or strung again).
    bool HoldPoint(const WebHold& hold, float& x, float& y) const;

    // Webs step aside where a window covers their corner (as Scenery::ProbeCover):
    // `covered(x, y)` says whether a window covers that scene point. Call now
    // and then, and before drawing whenever CoverStale().
    void ProbeCover(const std::function<bool(float, float)>& covered);
    bool CoverStale() const;
    // What covers them changed all at once: probe everything afresh.
    void RecheckCover();

    // ---- Drawing ----------------------------------------------------------
    // Builds the sprite page on `dc`'s device if needed. False: nothing to draw.
    bool Prepare(ID2D1DeviceContext* dc);
    ID2D1Bitmap1* Page() const;
    // The webs, their dew and their spiders (drawn behind the falling leaves).
    void EmitWebs(std::vector<ScenerySprite>& out) const;
    // Bats (drawn over everything).
    void EmitBats(std::vector<ScenerySprite>& out) const;
    void ReleaseDeviceResources();

private:
    struct Impl;  // Cobweb.cpp
    std::unique_ptr<Impl> impl_;
};
