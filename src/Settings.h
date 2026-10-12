// User settings, persisted under HKCU\Software\falldesktop.
#pragma once

#include "Common.h"

// Every selectable leaf design. The order is the order of the tiles in the
// settings window; the stored bitmask uses these values, so only append.
enum class LeafKind : int {
    Fleck,          // a tiny crumb of leaf
    Blur,           // a leaf drifting past close up, out of focus
    Samara,         // maple key: whirls down like a little helicopter
    Acorn,          // drops nearly straight down; keeps its own colours
    Aspen,          // small round leaf that quakes and flickers as it falls
    Maple,          // five-lobed sugar maple
    Oak,            // round-lobed oak
    Birch,          // small toothed oval
    Ginkgo,         // fan
    Sweetgum,       // five-pointed star
    Willow,         // long and slender
    JapaneseMaple,  // seven slender lobes
    Pixel1, Pixel2, Pixel3, Pixel4,   // pixel-art leaves
    Pixel5, Pixel6, Pixel7, Pixel8,
    Count
};

constexpr int kLeafKindCount = static_cast<int>(LeafKind::Count);
constexpr uint32_t LeafBit(LeafKind k) { return 1u << static_cast<int>(k); }
constexpr uint32_t kAllLeavesMask = (1u << kLeafKindCount) - 1u;
constexpr uint32_t kPixelMask = LeafBit(LeafKind::Pixel1) | LeafBit(LeafKind::Pixel2) | LeafBit(LeafKind::Pixel3) |
                                LeafBit(LeafKind::Pixel4) | LeafBit(LeafKind::Pixel5) | LeafBit(LeafKind::Pixel6) |
                                LeafBit(LeafKind::Pixel7) | LeafBit(LeafKind::Pixel8);
// The tree leaves.
constexpr uint32_t kFoliageMask = LeafBit(LeafKind::Aspen) | LeafBit(LeafKind::Maple) | LeafBit(LeafKind::Oak) |
                                  LeafBit(LeafKind::Birch) | LeafBit(LeafKind::Ginkgo) | LeafBit(LeafKind::Sweetgum) |
                                  LeafBit(LeafKind::Willow) | LeafBit(LeafKind::JapaneseMaple);
// Crumbs and out-of-focus leaves.
constexpr uint32_t kSmallMask = LeafBit(LeafKind::Fleck) | LeafBit(LeafKind::Blur);
// Seeds and nuts.
constexpr uint32_t kSeedMask = LeafBit(LeafKind::Samara) | LeafBit(LeafKind::Acorn);
constexpr uint32_t kDefaultLeafMask = kFoliageMask | LeafBit(LeafKind::Fleck) | LeafBit(LeafKind::Samara);

const wchar_t* LeafKindName(LeafKind k);

// The colours the leaves (and the piles they make) come in; see Palette.h.
enum class LeafColor : int {
    Autumn,   // the natural mix: scarlet, orange, gold, russet, a few still green
    Golden,   // yellows and ambers
    Crimson,  // reds and burgundies
    Custom,   // shades of the custom colour
    Count
};

struct Settings {
    uint32_t leafMask = kDefaultLeafMask;
    int amount = 40;          // 0..100, maps to leaf density
    int speed = 50;           // 0..100 (50 = natural speed)
    int wind = 15;            // -100..100, negative blows left
    int gusts = 50;           // 0..100, how much the wind varies
    int size = 50;            // 0..100
    int piles = 55;           // 0..200, how deep the leaf piles grow; 0 disables them (above 100: great heaps)
    LeafColor color = LeafColor::Autumn;
    COLORREF customColor = RGB(232, 116, 36);
    bool shadow = true;       // faint dark outline so leaves read on light windows
    bool mouse = true;        // the pointer blows leaves about, rakes the piles and tugs the cobwebs
    bool pauseFullscreen = true;
    bool allMonitors = true;
    bool behindWindows = false;  // leaves only on the desktop: windows cover them
    bool pumpkins = true;     // pumpkins stand in the leaf piles
    int pumpkinAmount = 35;   // 0..100: how many (35 = the usual few; more is a pumpkin patch)
    bool scarecrows = true;   // scarecrows stand in the piles too
    int scarecrowAmount = 35; // 0..100
    bool cobwebs = true;      // cobwebs strung across the corners of the screen
    bool halloween = true;    // Halloween touches: carved, candle-lit jack-o'-lanterns, pumpkin-headed
                              // scarecrows in witch hats, spiders on the cobwebs and bats; off: plain
                              // pumpkins and burlap scarecrows, cobwebs without spiders
    bool paused = false;

    bool operator==(const Settings&) const = default;

    // Ensure values are in range and at least one leaf kind is enabled.
    void Sanitize();
};

Settings LoadSettings();
void SaveSettings(const Settings& s);

// Running from the Microsoft Store (MSIX) package rather than a plain install.
bool IsPackaged();

// Start-with-Windows: the HKCU Run key, or the startup task when running from
// the Microsoft Store (MSIX) package. Reads the live state.
bool IsStartWithWindows();
bool SetStartWithWindows(bool enable);

// Simple presets offered in the tray menu.
struct Preset {
    const wchar_t* name;
    int value;
};
extern const Preset kAmountPresets[5];
extern const Preset kSpeedPresets[3];
extern const Preset kWindPresets[5];
extern const Preset kPilePresets[5];
extern const Preset kPumpkinPresets[4];
extern const Preset kScarecrowPresets[4];
