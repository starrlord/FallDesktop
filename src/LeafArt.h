// Leaf artwork: procedural vector designs for every LeafKind, drawn with
// Direct2D in greys so that they take their colour from the sprite tint (see
// Palette.h), plus a multi-resolution sprite atlas used for fast batched drawing.
#pragma once

#include "Common.h"
#include "Settings.h"

#include <d2d1_3.h>
#include <wincodec.h>

// Random variations generated per procedural kind (pixel-art kinds have one).
constexpr int kMaxVariants = 4;

struct LeafArtOptions {
    bool shadow = true;  // faint dark outline so leaves stay visible on light windows
};

// Static per-kind traits the simulation uses.
struct LeafTraits {
    float minDip, maxDip;  // diameter range in DIPs at size setting 50
    bool rotates;          // turns in its plane while falling
    bool twinkles;         // flickers as it falls (aspen leaves quaking)
    bool pixelArt;         // pixel art: sample crisp
    bool tinted;           // drawn in greys to take the leaf colour; false: keeps its own colours (acorns)
    float weight;          // how often it falls relative to the other enabled kinds (1 = a typical leaf)
};
const LeafTraits& GetLeafTraits(LeafKind kind);

int LeafVariantCount(LeafKind kind);

// Loads a pixel-art leaf PNG (IDR_PIXEL1 + index) from the exe resources
// as a 32bpp premultiplied BGRA WIC bitmap. index is 0..7.
HRESULT LoadPixelLeaf(IWICImagingFactory* wic, int index, IWICBitmapSource** out);

// Draws one leaf design centred at (cx, cy), fitting a circle of radius r: in
// greys for tinted kinds, in its own colours otherwise. Works on any render
// target (device context, WIC or DC target). `pixel` must be the pixel-art
// bitmap (created on `rt`) for pixel-art kinds and may be null otherwise.
// `blush`: draw the design's blush layer instead, the soft-edged heart that
// two-tone leaves tint in their second colour (nothing for kinds without one).
void DrawLeafDesign(ID2D1RenderTarget* rt, LeafKind kind, int variant, float cx, float cy, float r,
                    const LeafArtOptions& opt, ID2D1Bitmap* pixel, bool blush = false);

// All leaf sprites, pre-rendered at several resolutions so that drawing any
// on-screen size samples a cell at most 2x larger than the target (no shimmer).
class LeafAtlas {
public:
    struct Sprite {
        ID2D1Bitmap1* bitmap = nullptr;  // atlas page
        D2D1_RECT_U src{};               // cell rectangle in the page
        float extent = 1.0f;             // cell size / leaf diameter (cells include outline padding)
    };

    // (Re)creates the atlas on the device that owns `dc`. On failure the
    // previous atlas built on the same device is kept.
    HRESULT Build(ID2D1DeviceContext* dc, IWICImagingFactory* wic, const LeafArtOptions& opt);
    void Reset();
    bool Ready() const { return !levels_.empty(); }

    static int SpriteIndex(LeafKind kind, int variant);
    // The blush layer of SpriteIndex(kind, variant): drawn over it with the
    // same centre, diameter, angle and squash, tinted the leaf's blush colour.
    // -1 when the design has none.
    static int BlushIndex(LeafKind kind, int variant);
    static int SpriteCount();

    // Best source cell for drawing sprite `index` at `diameterPx` on screen.
    const Sprite& Get(int index, float diameterPx) const;

private:
    struct Level {
        std::vector<Sprite> sprites;  // by sprite index
    };
    std::vector<ComPtr<ID2D1Bitmap1>> pages_;  // every level shares these pages
    std::vector<Level> levels_;                // largest first; diameters shrink by sqrt(2) per level
    std::vector<float> diameter_;              // drawn leaf diameter (px) per level: procedural sprites
    std::vector<float> pixelDiameter_;         // ... and pixel-art sprites (whole-pixel scales)
    ComPtr<ID2D1Device> device_;               // device the pages belong to
};
