// Scenery internals shared by Scenery.cpp (simulation, drawing) and
// SceneryArt.cpp (the sprite page): tunables, palette, the pumpkin's shape and
// the props.
#pragma once

#include "Scenery.h"

#include <initializer_list>
#include <iterator>

namespace scenery {

// ---------------------------------------------------------------------------
// Tunables. Lengths are DIPs (scaled by dpiScale where used) or fractions of a
// prop's size, so the scenery looks the same at every display scale.
// ---------------------------------------------------------------------------

// Layout.
constexpr float kPatchSpacing = 820.0f;       // one pumpkin patch per this much screen width (usual amount)
constexpr float kScarecrowSpacing = 1600.0f;  // one scarecrow per this much (usual amount)
constexpr float kAmountDouble = 30.0f;        // slider steps that double the count
constexpr int kMaxPatches = 12, kMaxPumpkins = 30, kMaxScarecrows = 10;
constexpr float kMaxPumpkin = 40.0f;    // the biggest pumpkin's body, base to shoulders (its stalk stands above)
constexpr float kMaxScarecrow = 72.0f;  // the tallest scarecrow, base to the top of its head (its hat sits above)
constexpr float kTallest = 102.0f;      // ... to the tip of a witch's hat: the most room a prop needs
constexpr float kSkyShare = 0.5f;       // props stay under this share of the height above the ground
constexpr float kMinScale = 0.5f;       // rather no props than ones smaller than this

// Leaf litter on the pumpkins' tops, hats and shoulders.
constexpr float kCellDip = 3.0f;        // spacing of the litter cells
constexpr float kLitterBase = 1.5f;     // deepest litter, DIP: kLitterBase + kLitterPerH * H(DIP)
constexpr float kLitterPerH = 0.05f;
constexpr float kLeafVolume = 2.4f;     // DIP^2 of litter per landed leaf of 1 DIP (as the pile's)
constexpr float kVolumeExp = 1.3f;      // same law as the pile
constexpr float kMaxCatchDip = 22.0f;   // bigger leaves pass in front of the props
constexpr float kTopCatch = 0.6f;       // share of leaves coming down on a pumpkin's top that stay there
constexpr float kHatCatch = 0.75f;      // ... on a hat
constexpr float kShoulderCatch = 0.35f; // ... on a scarecrow's shoulders and arms
constexpr float kBlowTau = 1500.0f;     // s: litter blows away bit by bit even in still air
constexpr float kGustBlow = 0.02f;      // 1/s: ... and this much faster in a strong gust
constexpr float kClearTau = 0.6f;       // s: blown off when cleared
constexpr float kDiffuse = 0.6f;        // 1/s: neighbouring cells even out
constexpr float kSlideTau = 0.5f;       // s: litter above a cell's cap slides off
constexpr float kCapKnee = 0.7f;        // fill level where leaves start to slide off
constexpr float kClumpArea = 12.2f;     // DIP^2 of litter per leaf knocked or blown off: what one of
                                        // 2.6-4.4 DIP (LeafClump::d) adds to the pile where it lands
constexpr float kLayerDip = 1.3f;       // litter thickness per layer of leaves drawn
constexpr float kLeafletDip = 7.5f;     // a litter leaf's length

// Motion. A pumpkin rocks slowly on its round base, a big leaf nods on its
// stem, a scarecrow sways on its post and its hat wobbles on its head.
constexpr float kRockHz = 1.5f, kRockZeta = 0.13f, kRockMax = 0.12f;       // pumpkin (rad)
constexpr float kFrondHz = 1.4f, kFrondZeta = 0.11f, kFrondMax = 0.5f;     // big leaf (rad)
constexpr float kSwayHz = 1.1f, kSwayZeta = 0.09f, kSwayMax = 0.14f;       // scarecrow (rad)
constexpr float kHatHz = 2.4f, kHatZeta = 0.13f, kHatMax = 0.35f;          // its hat
constexpr float kGustWind = 220.0f;  // DIP/s counted as a strong gust
constexpr float kReach = 9.0f;       // DIP: how far from the pointer props feel it

// Light. Glows add light (premultiplied colour above alpha); kGlowAlpha is how
// much of the desktop they veil.
constexpr float kGlowAlpha = 0.35f;
constexpr float kMistAlpha = 0.6f;   // the same where they can't add light: soft coloured mist
constexpr float kLightUp = 2.6f;     // s at most for the candles to be lit, one by one

// Carved faces: face units (u right, v down, about the face's middle; a face
// spans about -1..1) go round the rind: azimuth turn + u * kFaceU and polar
// angle pi/2 + kFaceDrop + v * kFaceV * R / B.
constexpr float kFaceU = 0.78f, kFaceV = 0.6f, kFaceDrop = 0.05f;
constexpr int kFaceCount = 4;  // classic, jagged, surprised, cute

// Hats, in head radii above the hat's foot: how tall each stands.
enum HatKind { kWitchHat, kBatteredHat, kStrawHat };
constexpr float kHatHeight[] = {2.5f, 1.42f, 0.9f};

// Art.
constexpr int kPad = 3;          // per side of every page cell: 1 px antialiasing + 2 px clear border
constexpr int kClearBorder = 2;
constexpr int kLeaflets = 3;     // litter leaf shapes: maple, oval, oak

struct Rgb {
    float r, g, b;
};
constexpr Rgb Hex(uint32_t c) {
    return {static_cast<float>((c >> 16) & 0xFF) / 255.0f, static_cast<float>((c >> 8) & 0xFF) / 255.0f,
            static_cast<float>(c & 0xFF) / 255.0f};
}

// ---------------------------------------------------------------------------
// Palette: warm autumn colours, lit from the upper left like the pile.
// ---------------------------------------------------------------------------

struct Shade3 {
    Rgb lit, mid, dark;
};
// Rinds: mostly rich oranges, now and then a pale ghost or a striped heirloom.
constexpr Shade3 kRinds[] = {
    {Hex(0xFFA640), Hex(0xF07A1C), Hex(0xAE4610)},  // classic orange
    {Hex(0xFB9238), Hex(0xE0631C), Hex(0x9A3610)},  // deep orange
    {Hex(0xFFC55A), Hex(0xF39A2A), Hex(0xBA6418)},  // golden
    {Hex(0xF5834A), Hex(0xD8502A), Hex(0x922C14)},  // Cinderella red
    {Hex(0xFCF8EF), Hex(0xE9E0D0), Hex(0xAFA18B)},  // ghost
    {Hex(0xF4CB88), Hex(0xDDA45E), Hex(0x9E6C3A)},  // heirloom (green stripes)
};
constexpr int kGhost = 4, kHeirloom = 5;
constexpr float kRindOdds[] = {0.3f, 0.22f, 0.17f, 0.13f, 0.09f, 0.09f};
constexpr Rgb kHeirloomStripe = Hex(0x5C7646);
constexpr Rgb kUnderside = Hex(0x3A1408);  // the shade under a pumpkin
constexpr Shade3 kStalks[] = {
    {Hex(0x9EA45E), Hex(0x6E7838), Hex(0x3C451E)},  // green
    {Hex(0xBDA670), Hex(0x8A7442), Hex(0x4C3D20)},  // dried
    {Hex(0xC6CB9E), Hex(0x8E9966), Hex(0x56603E)},  // pale (on a ghost)
};
constexpr Rgb kVine = Hex(0x86A040), kVineDry = Hex(0xB59E5C);
// Pumpkin leaves going over: olive green yellowing at the edges, browning in spots.
constexpr Rgb kFrondLit = Hex(0x93A256), kFrondMid = Hex(0x687838), kFrondDark = Hex(0x3C4A20);
constexpr Rgb kFrondTurn = Hex(0xC9A443), kFrondBrown = Hex(0x8A5A2A), kFrondVein = Hex(0xC8CC8E);

// Candlelight.
constexpr Rgb kFlameCore = Hex(0xFFF8DA);  // the hottest light through a cut
constexpr Rgb kFlameInner = Hex(0xFFB640); // the lit inner wall
constexpr Rgb kFlameDeep = Hex(0xDA621C);  // its far, dimmer corners
constexpr Rgb kFlesh = Hex(0xFFE59A);      // the cut rind, lit from inside
constexpr Rgb kGlowHalo = Hex(0xFF8A2C);   // the glow round a lantern
constexpr Rgb kGlowCore = Hex(0xFFB44A);
constexpr Rgb kGlowPool = Hex(0xFF9A3A);   // candlelight on the leaves in front
constexpr Rgb kUnlit = Hex(0x2A1206);      // a cut with no candle behind it

// Scarecrows.
constexpr Shade3 kWood = {Hex(0x9C7E58), Hex(0x735838), Hex(0x45331E)};
constexpr Shade3 kStraw = {Hex(0xF5D886), Hex(0xDBB052), Hex(0x9A7428)};
constexpr Shade3 kBurlap = {Hex(0xDCC496), Hex(0xBFA06C), Hex(0x86683E)};
constexpr Rgb kTwine = Hex(0xA7834A), kThread = Hex(0x2C2017);
struct Plaid {
    Rgb base, band, line;
};
constexpr Plaid kPlaids[] = {
    {Hex(0xB23A2C), Hex(0x641A14), Hex(0xEFD9AE)},  // red
    {Hex(0x8E5E36), Hex(0x4A301B), Hex(0xE4C48E)},  // brown
    {Hex(0x3F6496), Hex(0x1E3356), Hex(0xD8E1EB)},  // blue
    {Hex(0x4D7442), Hex(0x263D20), Hex(0xE3D9A2)},  // green
};
constexpr int kPlaidCount = static_cast<int>(std::size(kPlaids));
constexpr Rgb kPatches[] = {Hex(0x4C70A0), Hex(0xC6A474), Hex(0xB26A4E), Hex(0xC79A3C)};  // denim, sacking, rust, mustard
constexpr int kPatchCount = static_cast<int>(std::size(kPatches));
struct Kerchief {
    Rgb main, accent;
    bool dotted;  // a bandana with dots; otherwise a scarf with stripes
};
constexpr Kerchief kKerchiefs[] = {
    {Hex(0xB8302A), Hex(0xF4E8D2), true},   // red bandana
    {Hex(0x2F4B82), Hex(0xEEF0F5), true},   // navy bandana
    {Hex(0xD49A2C), Hex(0x7A4C1E), false},  // mustard scarf
    {Hex(0x416C3F), Hex(0xDCC48A), false},  // forest green scarf
};
constexpr int kKerchiefCount = static_cast<int>(std::size(kKerchiefs));
struct Felt {
    Shade3 felt;
    Rgb band;
};
constexpr Felt kWitchHats[] = {
    {{Hex(0x56476C), Hex(0x2E2541), Hex(0x15101E)}, Hex(0xE0782A)},  // black, an orange band
    {{Hex(0x725090), Hex(0x47305F), Hex(0x221633)}, Hex(0xD7A640)},  // purple, an ochre band
    {{Hex(0x50555A), Hex(0x2B2E31), Hex(0x121315)}, Hex(0x8E3CB0)},  // charcoal, a violet band
};
constexpr int kWitchCount = static_cast<int>(std::size(kWitchHats));
constexpr Felt kTopHat = {{Hex(0x625852), Hex(0x3C3531), Hex(0x1D1917)}, Hex(0x94402F)};
constexpr Rgb kBuckleLit = Hex(0xF0CD62), kBuckleDark = Hex(0x96741F);
constexpr Rgb kHatRibbons[] = {Hex(0xA8322A), Hex(0x3A5C8E), Hex(0x6B4426)};  // straw hats' bands

constexpr Rgb kShadow = Hex(0x0A0603);   // soft shadows behind the props
constexpr Rgb kPileShade = Hex(0x1E0F06); // a prop's shade on the leaves
constexpr Rgb kWhite = {1.0f, 1.0f, 1.0f};

inline D2D1_COLOR_F Color(Rgb c, float a) { return {c.r, c.g, c.b, a}; }
inline Rgb Mix(Rgb a, Rgb b, float t) { return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t)}; }

// ---------------------------------------------------------------------------
// A pumpkin's body: a squashed ball with deep ribs, a hollow round the stalk
// and a dimple underneath, seen from a little above. Px about its centre in
// view space: x right, y up, z toward the viewer. Azimuth th is 0 facing the
// viewer and grows to the right; polar angle ps is 0 at the stalk.
// ---------------------------------------------------------------------------

struct Vec3 {
    float x, y, z;
};

struct Gourd {
    float R = 10.0f, B = 8.0f;  // px: equatorial radius, half height
    float hollow = 0.12f;       // the top sinks this much (of B) round the stalk
    float rib = 0.1f;           // rib depth (of R)
    int ribs = 8;
    float turn = 0.0f;          // rad: azimuth of a groove
    float full = 0.85f;         // profile exponent: below 1, fuller shoulders and base
    float lop = 0.0f;           // -1..1: fuller and taller on the right (left)
    float tilt = 0.26f;         // rad: seen from this much above
    float lift = 0.0f;          // px: centre above the base it rests on (Settle)

    // 0 in a groove, 1 mid-rib.
    float Lobe(float th) const {
        float u = (th - turn) * static_cast<float>(ribs) / kTau;
        u -= std::floor(u);
        const float c = 2.0f * u - 1.0f;
        return std::sqrt(std::max(0.0f, 1.0f - c * c));
    }
    Vec3 At(float th, float ps) const {
        const float sp = std::max(0.0f, std::sin(ps)), cp = std::cos(ps), sth = std::sin(th);  // (sin(kPi) < 0 in float)
        const float grow = 1.0f + 0.08f * lop * sth;
        const float r = R * grow * std::pow(sp, full) * (1.0f + rib * (Lobe(th) - 0.6f) * std::sqrt(sp));
        const float top = ps / 0.5f, bottom = (kPi - ps) / 0.5f;
        const float y = B * grow * (cp - hollow * std::exp(-top * top) + 0.5f * hollow * std::exp(-bottom * bottom));
        const float X = r * sth, Z = r * std::cos(th);
        const float ct = std::cos(tilt), st = std::sin(tilt);
        return {X, y * ct - Z * st, y * st + Z * ct};
    }
    // Outward unit normal (view space).
    Vec3 Normal(float th, float ps) const {
        constexpr float e = 2e-3f;
        ps = Clamp(ps, 0.02f, kPi - 0.02f);
        const Vec3 a = At(th + e, ps), b = At(th - e, ps), c = At(th, ps + e), d = At(th, ps - e);
        const Vec3 u = {a.x - b.x, a.y - b.y, a.z - b.z}, v = {c.x - d.x, c.y - d.y, c.z - d.z};
        Vec3 n = {v.y * u.z - v.z * u.y, v.z * u.x - v.x * u.z, v.x * u.y - v.y * u.x};
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0f) n = {n.x / len, n.y / len, n.z / len};
        return n;
    }
    // Scales it to `height` px from its lowest point to the top of its outline
    // and sets `lift`.
    void Settle(float height) {
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i <= 48; ++i)
            for (int j = 0; j <= 32; ++j) {
                const Vec3 p = At(kTau * static_cast<float>(i) / 48.0f, kPi * static_cast<float>(j) / 32.0f);
                lo = std::min(lo, p.y);
                hi = std::max(hi, p.y);
            }
        const float k = hi > lo ? height / (hi - lo) : 1.0f;
        R *= k;
        B *= k;
        lift = -lo * k;
    }
    // A point on the rind from carved-face units, about the centre (y up).
    Vec3 Face(float faceTurn, float u, float v) const {
        return At(faceTurn + u * kFaceU, 0.5f * kPi + kFaceDrop + v * kFaceV * R / std::max(B, 1e-3f));
    }
};

}  // namespace scenery

// ---------------------------------------------------------------------------
// The props
// ---------------------------------------------------------------------------

// A spot on a shelf where leaves come to rest (a pumpkin's top, a hat's crown
// or brim, a scarecrow's shoulder).
struct Scenery::Cell {
    float lx = 0.0f;           // part-local x of the centre (px from the part's anchor)
    float ey = 0.0f;           // the shelf the leaves lie on, there (px below the anchor)
    float nx = 0.0f, ny = -1;  // outward (upward) unit normal of the shelf
    float capF = 1.0f;         // share of the cap this spot holds
    float load = 0.0f;         // litter thickness (px)
    int seg = 0;               // which shelf (litter only evens out within one)
};

// A big pumpkin leaf on its stem, standing behind a pumpkin.
struct Scenery::Frond {
    float dx = 0.0f;                  // stem's foot from the pumpkin's base centre (px)
    float stem = 0.0f, size = 0.0f;   // px: stem length, blade length
    float lean = 0.0f;                // rad: how it leans at rest (positive: to the right)
    float turned = 0.0f;              // 0 green .. 1 gone yellow
    float sway = 0.0f, swayV = 0.0f;  // rad
    float w = 0.0f;                   // rad/s
    float gust = 0.0f;                // noise offset
    Sprite art;                       // anchored at the stem's foot, standing up
};

// A patch of pumpkins as laid out (DIP).
struct Scenery::Patch {
    struct Member {
        float dx;  // base centre from the patch's left edge
    };
    std::vector<Member> pumpkins;
    float w = 0.0f;
};

// A pumpkin. Body-local px have its base as origin and y pointing down, as on
// screen (it rocks about its base); the body's centre is shape.lift above it.
struct Scenery::Pumpkin {
    int patch = 0, member = 0;  // where it stands: patches_[patch].pumpkins[member]
    float x = 0.0f;             // base centre (px, whole)
    scenery::Gourd shape;       // px
    float H = 0.0f;             // base to the stalk's tip (px)
    float halfW = 0.0f;         // half width incl. its leaves, plus the pointer's reach (px)
    int rind = 0;               // colours (kRinds)
    int face = -1;              // carving at Halloween (-1: left whole)
    float faceTurn = 0.0f;      // rad: the face looks a little to one side
    float faceX = 0.0f, faceY = 0.0f;  // the face's middle from the base (px, y up)
    int stalk = 0;              // kStalks
    float stalkLen = 0.0f, stalkLean = 0.0f, stalkW = 0.0f;  // px, rad, px at its foot
    float stalkY = 0.0f;        // the stalk's foot above the base (px)
    int tendril = 0;            // a curl of vine from the stalk: -1 left, 1 right, 0 none
    float tendrilLen = 0.0f;    // px
    std::vector<Frond> fronds;
    float lift = 0.0f, liftGoal = 0.0f;   // pile height it stands on (px); where it is gliding to
    float fall = 0.0f;          // px/s: dropping onto leaves that went from under it
    bool placed = false;        // stood on the pile yet (until then it goes straight there)
    float pile = 0.0f;          // the pile's surface at its foot (px above the ground)
    float pileL = 0.0f, pileR = 0.0f;  // ... half a width either side
    float rock = 0.0f, rockV = 0.0f;   // rad: about its base
    float rockW = 0.0f;         // rad/s
    float vis = 1.0f;           // 0..1: stepping aside for a window
    bool covered = false;
    bool probed = false;        // covered has been looked up (until then vis follows it at once)
    std::vector<Cell> litter;   // on its top
    float loose = 0.0f;         // px^2 of litter blown loose, leaving a leaf at a time
    float spill = 0.0f;         // px^2 of it sliding off the edge
    float shed = 0.0f;          // px^2 of it knocked off
    int candle = -1;            // the order its candle is lit in (-1: none)
    float phase = 0.0f;         // flicker noise offset
    float flame = 0.0f;         // its candle now: 0 out .. about 1
    Sprite body, lantern, vine; // the lantern: what shows through the cuts, lit
};

// A scarecrow: a post and crossbar, a stuffed plaid shirt, straw hands, a
// kerchief and a hat; at Halloween a jack-o'-lantern head, otherwise a
// burlap sack. Body-local px have the base as origin and y pointing down (it
// sways about the base); hat-local px likewise from the hat's foot.
struct Scenery::Scarecrow {
    float x = 0.0f;             // base centre (px, whole)
    float S = 0.0f;             // base to the top of the head (px)
    int dir = 1;                // which way it faces (+1: right)
    int shirt = 0, cloth = 0;   // plaid; the patch sewn on it
    int kerchief = 0;
    int pose = 0;               // 0 arms out, 1 one arm drooping, 2 a hand waving
    bool topHat = false;        // a battered top hat, whatever the season; else a witch's or straw hat
    int witch = 0, ribbon = 0;  // the witch's hat; the straw hat's band
    int face = 0;               // carving of its pumpkin head
    bool buttons = false;       // burlap head: button eyes rather than cross-stitches
    scenery::Gourd head;        // its pumpkin head (px)
    float headR = 0.0f, headY = 0.0f;  // px: head radius; its centre above the base
    float armY = 0.0f;          // the crossbar above the base (px)
    float handX[2] = {}, handY[2] = {}, handA[2] = {};  // straw hands: root (from the base, y up) and rest angle
    float hatY = 0.0f;          // hat foot above the base (px)
    float hatTilt0 = 0.0f;      // the jaunty angle it sits at (rad)
    float knotX = 0.0f, knotY = 0.0f;  // where the kerchief's tail hangs from (px from the base, y up)
    float halfW = 0.0f;         // half width incl. hands, plus the pointer's reach (px)
    float lift = 0.0f, liftGoal = 0.0f, fall = 0.0f, pile = 0.0f, pileL = 0.0f, pileR = 0.0f;  // as for a pumpkin
    bool placed = false;
    float rock = 0.0f, rockV = 0.0f;   // rad: sway about the base
    float hatTilt = 0.0f, hatTiltV = 0.0f;
    float rockW = 0.0f;         // rad/s
    float flutter = 0.0f;       // noise offset for the kerchief, straw and hands
    float vis = 1.0f;
    bool covered = false;
    bool probed = false;
    std::vector<Cell> hatLitter;       // on the crown and brim of its hat
    std::vector<Cell> shoulderLitter;  // on its shoulders and arms
    float loose = 0.0f, spill = 0.0f, shed = 0.0f;
    int candle = -1;
    float phase = 0.0f, flame = 0.0f;
    Sprite body, headArt, lantern, hat, tail, hand[2];
};
