// Smooth gradient noise for wind, gusts and turbulence.
// 1D gradient (Perlin-style) noise and 3D simplex noise, written from the
// published algorithms (Perlin 2002, Gustavson 2005). Output range ~[-1, 1].
#pragma once

#include "Common.h"

namespace noise {

namespace detail {
inline const uint8_t* Perm() {
    static const auto table = [] {
        static uint8_t p[512];
        Rng rng(0x5EED5EEDu);
        uint8_t base[256];
        for (int i = 0; i < 256; ++i) base[i] = static_cast<uint8_t>(i);
        for (int i = 255; i > 0; --i) std::swap(base[i], base[rng.Int(0, i)]);
        for (int i = 0; i < 512; ++i) p[i] = base[i & 255];
        return p;
    }();
    return table;
}

inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

inline int FastFloor(float x) {
    int i = static_cast<int>(x);
    return x < static_cast<float>(i) ? i - 1 : i;
}

constexpr float kGrad3[12][3] = {
    {1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0},
    {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1},
    {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1},
};
}  // namespace detail

// 1D gradient noise, smooth (C2), ~[-1, 1].
inline float Noise1(float x) {
    using namespace detail;
    const uint8_t* p = Perm();
    int i0 = FastFloor(x);
    float f = x - static_cast<float>(i0);
    // Gradients in [-1, 1] derived from the permutation table.
    float g0 = static_cast<float>(p[i0 & 255]) * (2.0f / 255.0f) - 1.0f;
    float g1 = static_cast<float>(p[(i0 + 1) & 255]) * (2.0f / 255.0f) - 1.0f;
    float v0 = g0 * f;
    float v1 = g1 * (f - 1.0f);
    return 2.0f * Lerp(v0, v1, Fade(f));
}

// Fractal sum of 1D noise.
inline float Fbm1(float x, int octaves, float lacunarity = 2.0f, float gain = 0.5f) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * Noise1(x + static_cast<float>(i) * 17.31f);
        norm += amp;
        amp *= gain;
        x *= lacunarity;
    }
    return sum / norm;
}

// 3D simplex noise, ~[-1, 1].
inline float Simplex3(float xin, float yin, float zin) {
    using namespace detail;
    const uint8_t* perm = Perm();
    constexpr float F3 = 1.0f / 3.0f;
    constexpr float G3 = 1.0f / 6.0f;

    float s = (xin + yin + zin) * F3;
    int i = FastFloor(xin + s), j = FastFloor(yin + s), k = FastFloor(zin + s);
    float t = static_cast<float>(i + j + k) * G3;
    float x0 = xin - (static_cast<float>(i) - t);
    float y0 = yin - (static_cast<float>(j) - t);
    float z0 = zin - (static_cast<float>(k) - t);

    int i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0)      { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
        else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
        else               { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
    } else {
        if (y0 < z0)       { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
        else if (x0 < z0)  { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
        else               { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
    }

    float x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
    float x2 = x0 - i2 + 2 * G3, y2 = y0 - j2 + 2 * G3, z2 = z0 - k2 + 2 * G3;
    float x3 = x0 - 1 + 3 * G3, y3 = y0 - 1 + 3 * G3, z3 = z0 - 1 + 3 * G3;

    int ii = i & 255, jj = j & 255, kk = k & 255;
    auto corner = [&](float x, float y, float z, int gi) {
        float tt = 0.6f - x * x - y * y - z * z;
        if (tt < 0) return 0.0f;
        const float* g = kGrad3[gi % 12];
        tt *= tt;
        return tt * tt * (g[0] * x + g[1] * y + g[2] * z);
    };
    float n0 = corner(x0, y0, z0, perm[ii + perm[jj + perm[kk]]]);
    float n1 = corner(x1, y1, z1, perm[ii + i1 + perm[jj + j1 + perm[kk + k1]]]);
    float n2 = corner(x2, y2, z2, perm[ii + i2 + perm[jj + j2 + perm[kk + k2]]]);
    float n3 = corner(x3, y3, z3, perm[ii + 1 + perm[jj + 1 + perm[kk + 1]]]);
    return 32.0f * (n0 + n1 + n2 + n3);
}

}  // namespace noise
