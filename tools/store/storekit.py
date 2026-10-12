#!/usr/bin/env python3
"""Fall Desktop store-media kit: a staged Windows 11 desktop for screenshots and the trailer.

This is the autumn sister of the kit behind Snowy Desktop's Microsoft Store media.  It renders,
at any resolution, the pieces the Store screenshots and trailer frames are composited from:

    wallpaper(w, h, scale)                     -> RGB    autumn dusk landscape
    taskbar(w, h, scale, wallpaper_img, ...)   -> RGBA   Windows 11 taskbar, full frame
    window_chrome(client_rgba, scale, ...)     -> (RGBA, (x, y))  dark window frame + shadow
    cursor(scale)                              -> RGBA   standard arrow pointer
    caption(text, w, h, scale, ...)            -> RGBA   title card / caption overlay, full frame
    desktop(w, h, scale)                       -> RGB    wallpaper + taskbar, ready to draw on

Helpers for compositing: taskbar_layout() (where every taskbar icon, the tray and the clock
sit), chrome_margin() (the shadow margin around window_chrome's image), CURSOR_HOTSPOT,
app_icon(px), font(px, weight, optical), paste(base, overlay, xy), and the caption presets
CAPTION, TITLE_CARD and END_CARD measured on Snowy's trailer.

Units
-----
Everything is laid out in DIPs (device-independent pixels) and converted with px = DIP * scale,
exactly like Windows display scaling.  The Store screenshots are 3840x2160 at scale 1.5 (a
2560x1440 DIP desktop); 1920x1080 at scale 0.75 is the same picture at half the resolution, so
trailer frames and screenshots match.

The Windows 11 measurements (taskbar 48 DIP, 44 DIP buttons, 24 DIP tiles, 32 DIP title bar,
8 DIP corners, the window shadow, the caption sizes) were taken from Snowy Desktop's 4K Store
screenshots and 1080p trailer, so both apps' media line up.

Compositing order for a frame
-----------------------------
    wallpaper -> app windows (window_chrome) -> taskbar -> Fall Desktop's overlay (leaves, piles,
    pumpkins, webs; it is topmost, so piles may overlap the taskbar's top edge) -> cursor ->
    caption

CLI
---
    python tools/store/storekit.py demo <outdir>
    python tools/store/storekit.py wallpaper <w> <h> <scale> <out.png>
    python tools/store/storekit.py desktop <w> <h> <scale> <out.png>

Needs Python 3.9+, numpy and Pillow.  Fonts come from C:\\Windows\\Fonts (Segoe UI Variable,
falling back to Segoe UI).
"""

from __future__ import annotations

import argparse
import functools
import math
import os
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# =============================================================================================
# Layout constants (DIP)
# =============================================================================================

TASKBAR_DIP = 48            # Windows 11 taskbar height
TASKBAR_BUTTON_DIP = 44     # pitch of the centred taskbar buttons
TASKBAR_TILE_DIP = 24       # app icon size on the taskbar
TRAY_ICON_DIP = 16          # notification-area icon size
TITLEBAR_DIP = 32           # caption bar of a standard window
WINDOW_RADIUS_DIP = 8       # Windows 11 window corner radius
CAPTION_BUTTON_DIP = 46     # width of the minimise / maximise / close buttons
CURSOR_DIP = 32             # pointer box

# Window drop shadow, fitted to Snowy's settings window (black, Gaussian, pushed down).
SHADOW_ALPHA = 0.54
SHADOW_SIGMA_DIP = 17.7
SHADOW_DY_DIP = 8.5

# Caption presets measured on Snowy's 1080p trailer (64 px captions, 104 px title, ...),
# expressed in DIP so they work at any resolution: caption(text, w, h, scale, **CAPTION).
# y_center is the vertical centre of the title line's capital letters.
TITLE_CARD = dict(size_dip=138.67, weight="Semibold", y_center=584.8, subtitle_size_dip=50.67)
CAPTION = dict(size_dip=85.33, weight="Semibold", y_center=632.8)
END_CARD = dict(size_dip=122.67, weight="Semibold", y_center=815.7, subtitle_size_dip=48.0)
# Snowy's end card put its 266 DIP logo centred at (W/2, 533 DIP) above END_CARD's title.

_FONT_DIR = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
_STATIC_FONTS = {"Light": "segoeuil.ttf", "Semilight": "segoeuisl.ttf", "Regular": "segoeui.ttf",
                 "Semibold": "seguisb.ttf", "Bold": "segoeuib.ttf"}
APP_ICON = Path(__file__).resolve().parents[2] / "res" / "falldesktop.ico"

# =============================================================================================
# Small helpers: colour, blending, blur, fonts
# =============================================================================================


def _rgb(c) -> np.ndarray:
    """'#rrggbb' or (r, g, b) in 0..255 -> float32 array in 0..1."""
    if isinstance(c, str):
        c = c.lstrip("#")
        c = tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))
    return np.asarray(c[:3], np.float32) / 255.0


def _smooth(e0, e1, x):
    """Hermite smoothstep from e0 to e1 (works for e0 > e1 too)."""
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _over(dst, color, alpha):
    """In place: dst = lerp(dst, color, alpha).  dst (..., 3), alpha broadcasts to dst[..., 0]."""
    dst += (color - dst) * np.asarray(alpha, np.float32)[..., None]


def _screen(dst, color, amount):
    """In place screen blend of color * amount over dst (a soft additive glow)."""
    dst[...] = 1.0 - (1.0 - dst) * (1.0 - color * np.asarray(amount, np.float32)[..., None])


def _to_u8(img, seed=7):
    """Float image (0..1) -> uint8 with a tiny triangular dither so gradients never band."""
    h, w = img.shape[:2]
    rng = np.random.default_rng(seed)
    tile = (rng.random((256, 256, 1), np.float32) - rng.random((256, 256, 1), np.float32))
    dither = np.tile(tile, (h // 256 + 1, w // 256 + 1, 1))[:h, :w]
    out = img * 255.0 + dither + 0.5
    return np.clip(out, 0, 255).astype(np.uint8)


def _sl(ndim, axis, s):
    idx = [slice(None)] * ndim
    idx[axis] = s
    return tuple(idx)


def _box_blur(a, r, axis):
    """Box blur of radius r (window 2r+1) along one axis via a running sum; edges clamp."""
    if r <= 0:
        return a
    n = a.shape[axis]
    pad = [(0, 0)] * a.ndim
    pad[axis] = (r + 1, r)
    c = np.cumsum(np.pad(a, pad, mode="edge"), axis=axis, dtype=np.float64)
    out = c[_sl(c.ndim, axis, slice(2 * r + 1, 2 * r + 1 + n))] - c[_sl(c.ndim, axis, slice(0, n))]
    return (out / (2 * r + 1)).astype(np.float32)


def _blur(a, sigma):
    """Gaussian blur (sigma in px) of a float array shaped (H, W) or (H, W, C); edges clamp.

    Small sigmas use an exact separable kernel, larger ones three box passes (which are
    indistinguishable from a true Gaussian at these sizes and run in linear time)."""
    a = np.asarray(a, np.float32)
    if sigma < 0.3:
        return a
    if sigma < 3.0:
        r = int(math.ceil(3 * sigma))
        k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2).astype(np.float32)
        k /= k.sum()
        for axis in (0, 1):
            n = a.shape[axis]
            pad = [(0, 0)] * a.ndim
            pad[axis] = (r, r)
            p = np.pad(a, pad, mode="edge")
            out = np.zeros_like(a)
            for i, kv in enumerate(k):
                out += kv * p[_sl(a.ndim, axis, slice(i, i + n))]
            a = out
        return a
    # Three box passes approximating a Gaussian (W. Jarosz / I. Kutskir's box sizes).
    n = 3
    wi = math.sqrt(12.0 * sigma * sigma / n + 1.0)
    wl = int(math.floor(wi))
    wl -= (wl % 2 == 0)
    m = round((12.0 * sigma * sigma - n * wl * wl - 4 * n * wl - 3 * n) / (-4.0 * wl - 4.0))
    for i in range(n):
        r = ((wl if i < m else wl + 2) - 1) // 2
        a = _box_blur(a, r, 0)
        a = _box_blur(a, r, 1)
    return a


def _resize_f(a, w, h):
    """Bicubic resize of a float32 2-D array (pixel centres stay aligned)."""
    return np.asarray(Image.fromarray(np.ascontiguousarray(a, np.float32)).resize((w, h), Image.BICUBIC))


@functools.lru_cache(maxsize=None)
def font(px: float, weight: str = "Regular", optical: str = "Text") -> ImageFont.FreeTypeFont:
    """Segoe UI Variable at `px` pixels.

    weight: Light, Semilight, Regular, Semibold or Bold.  optical: Small, Text or Display (the
    variable font's optical sizes; Windows uses Text for UI and Display for big headings).
    Falls back to the static Segoe UI files, then to Pillow's built-in font."""
    px = round(float(px), 2)
    var = _FONT_DIR / "SegUIVar.ttf"
    if var.exists():
        f = ImageFont.truetype(str(var), px)
        want = "Regular" if (weight, optical) == ("Regular", "Text") else f"{weight} {optical}"
        for name in f.get_variation_names():
            if (name.decode() if isinstance(name, bytes) else name) == want:
                f.set_variation_by_name(name)
                break
        return f
    static = _FONT_DIR / _STATIC_FONTS.get(weight, "segoeui.ttf")
    if static.exists():
        return ImageFont.truetype(str(static), px)
    return ImageFont.load_default(px)


# =============================================================================================
# Procedural noise (deterministic, evaluated in DIP space so every resolution matches)
# =============================================================================================


def _hash(ix, iy, seed):
    """Integer lattice hash -> float32 in [-1, 1]."""
    h = (np.asarray(ix, np.int64) * 0x27D4EB2D + np.asarray(iy, np.int64) * 0x165667B1
         + int(seed) * 0x61C88647) & 0xFFFFFFFF
    h = ((h ^ (h >> 15)) * 0x2C1B3C6D) & 0xFFFFFFFF
    h = ((h ^ (h >> 12)) * 0x297A2D39) & 0xFFFFFFFF
    h ^= h >> 15
    return (h & 0xFFFFFF).astype(np.float32) * np.float32(2.0 / 0xFFFFFF) - 1.0


def _noise2(x, y, seed):
    """2-D value noise in [-1, 1] with smooth interpolation; x, y broadcast."""
    fx, fy = np.floor(x), np.floor(y)
    tx, ty = (x - fx).astype(np.float32), (y - fy).astype(np.float32)
    ix, iy = fx.astype(np.int64), fy.astype(np.int64)
    u, v = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a, b = _hash(ix, iy, seed), _hash(ix + 1, iy, seed)
    c, d = _hash(ix, iy + 1, seed), _hash(ix + 1, iy + 1, seed)
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * (u * v)


def _fbm2(x, y, seed, octaves=4, gain=0.5):
    """Fractal sum of value noise, rotated each octave to hide the lattice; about [-1, 1]."""
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total = total + amp * _noise2(x, y, seed + 101 * o)
        norm += amp
        amp *= gain
        x, y = (x * 0.8 - y * 0.6) * 2.03 + 5.3, (x * 0.6 + y * 0.8) * 2.03 + 1.7
    return total / norm


def _noise1(x, seed):
    """Smooth 1-D noise (Catmull-Rom through random lattice values), about [-1, 1]."""
    fx = np.floor(x)
    t = (x - fx).astype(np.float32)
    i = fx.astype(np.int64)
    z = np.zeros_like(i)
    p0, p1, p2, p3 = (_hash(i + k, z, seed) for k in (-1, 0, 1, 2))
    return 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                  + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t)


# =============================================================================================
# Signed distance functions (negative inside) for crisp, analytically anti-aliased shapes
# =============================================================================================


def _cov(d, aa=1.0):
    """Coverage of a pixel whose centre is at signed distance d (in px) from an edge."""
    return np.clip(0.5 - d / aa, 0.0, 1.0)


def _sd_circle(X, Y, cx, cy, r):
    return np.hypot(X - cx, Y - cy) - r


def _sd_segment(X, Y, ax, ay, bx, by):
    """Distance to segment a-b, and the projection parameter h (0 at a, 1 at b)."""
    px, py, dx, dy = X - ax, Y - ay, bx - ax, by - ay
    l2 = dx * dx + dy * dy
    h = np.clip((px * dx + py * dy) / l2, 0.0, 1.0) if l2 > 1e-12 else np.zeros_like(px + py)
    return np.hypot(px - dx * h, py - dy * h), h


def _sd_capsule(X, Y, ax, ay, bx, by, ra, rb=None):
    """Stroke from a to b with round ends; radius tapers from ra to rb."""
    d, h = _sd_segment(X, Y, ax, ay, bx, by)
    return d - (ra if rb is None else ra + (rb - ra) * h)


def _sd_polyline(X, Y, pts, r):
    """Round-capped, round-joined stroke along a polyline."""
    d = None
    for (ax, ay), (bx, by) in zip(pts[:-1], pts[1:]):
        di, _ = _sd_segment(X, Y, ax, ay, bx, by)
        d = di if d is None else np.minimum(d, di)
    return d - r


def _sd_box(X, Y, cx, cy, hw, hh, r=0.0):
    """Rounded rectangle centred at (cx, cy) with half sizes hw, hh and corner radius r."""
    qx = np.abs(X - cx) - (hw - r)
    qy = np.abs(Y - cy) - (hh - r)
    return (np.hypot(np.maximum(qx, 0.0), np.maximum(qy, 0.0))
            + np.minimum(np.maximum(qx, qy), 0.0) - r)


def _sd_polygon(X, Y, pts):
    """Signed distance to a simple polygon given as [(x, y), ...] (Inigo Quilez's method)."""
    X, Y = np.broadcast_arrays(X, Y)
    d = (X - pts[0][0]) ** 2 + (Y - pts[0][1]) ** 2
    s = np.ones(X.shape, np.float32)
    j = len(pts) - 1
    for i in range(len(pts)):
        (xi, yi), (xj, yj) = pts[i], pts[j]
        ex, ey = xj - xi, yj - yi
        wx, wy = X - xi, Y - yi
        h = np.clip((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0, 1.0)
        bx, by = wx - ex * h, wy - ey * h
        d = np.minimum(d, bx * bx + by * by)
        c1, c2, c3 = Y >= yi, Y < yj, ex * wy > ey * wx
        s = np.where((c1 & c2 & c3) | (~c1 & ~c2 & ~c3), -s, s)
        j = i
    return s * np.sqrt(d)


def _sd_arc(X, Y, cx, cy, R, a0, a1):
    """Distance to the arc of radius R from angle a0 to a1 (degrees, screen space: 0 = right,
    90 = down; the arc runs through increasing angles)."""
    dx, dy = X - cx, Y - cy
    rel = (np.degrees(np.arctan2(dy, dx)) - a0) % 360.0
    on = rel <= (a1 - a0) % 360.0
    r0, r1 = math.radians(a0), math.radians(a1)
    e0 = np.hypot(dx - R * math.cos(r0), dy - R * math.sin(r0))
    e1 = np.hypot(dx - R * math.cos(r1), dy - R * math.sin(r1))
    return np.where(on, np.abs(np.hypot(dx, dy) - R), np.minimum(e0, e1))


def _stamp(mask, row0, scale, bbox, sdf, aa=1.0, add=None):
    """Rasterise sdf(X, Y) (DIP coordinates -> DIP distance) into a float mask band starting at
    image row `row0`, max-combining coverage inside the DIP bbox (x0, y0, x1, y1).  `add`
    offsets the distance (e.g. leafy edge noise): a band-sized array, or a function of (X, Y).
    Returns (rows slice, cols slice, coverage) or None when the bbox misses the band."""
    hb, wb = mask.shape
    x0, y0, x1, y1 = bbox
    c0, c1 = max(0, int(math.floor(x0 * scale)) - 1), min(wb, int(math.ceil(x1 * scale)) + 2)
    r0, r1 = max(row0, int(math.floor(y0 * scale)) - 1), min(row0 + hb, int(math.ceil(y1 * scale)) + 2)
    if c1 <= c0 or r1 <= r0:
        return None
    X = ((np.arange(c0, c1, dtype=np.float32) + 0.5) / scale)[None, :]
    Y = ((np.arange(r0, r1, dtype=np.float32) + 0.5) / scale)[:, None]
    rs, cs = slice(r0 - row0, r1 - row0), slice(c0, c1)
    d = sdf(X, Y)
    if callable(add):
        d = d + add(X, Y)
    elif add is not None:
        d = d + add[rs, cs]
    cov = _cov(d * scale, aa)
    np.maximum(mask[rs, cs], cov, out=mask[rs, cs])
    return rs, cs, cov


# =============================================================================================
# 1. Wallpaper: an autumn dusk landscape
# =============================================================================================

# Sky gradients (fraction of height, sRGB).  "glow" is the column under the afterglow
# (x = GLOW_X), "side" the cooler columns towards the edges; the sky blends between them.
_SKY_GLOW = [
    (0.00, (11, 11, 34)), (0.12, (18, 17, 50)), (0.24, (30, 24, 70)), (0.34, (47, 32, 88)),
    (0.42, (70, 42, 100)), (0.48, (98, 54, 108)), (0.53, (134, 71, 112)), (0.57, (172, 93, 110)),
    (0.605, (210, 121, 102)), (0.635, (237, 153, 94)), (0.665, (248, 182, 110)),
    (0.72, (252, 200, 132)),
]
_SKY_SIDE = [
    (0.00, (11, 11, 34)), (0.12, (17, 16, 48)), (0.24, (28, 23, 67)), (0.34, (42, 30, 82)),
    (0.42, (59, 38, 92)), (0.48, (77, 46, 98)), (0.53, (97, 56, 104)), (0.57, (117, 66, 108)),
    (0.605, (137, 77, 110)), (0.635, (155, 88, 112)), (0.665, (168, 98, 114)),
    (0.72, (176, 105, 116)),
]
GLOW_X = 0.40        # afterglow centre (fraction of width)
GLOW_Y = 0.655       # ... and height: just behind the farthest hills
MOON_X, MOON_Y, MOON_R = 0.78, 0.18, 46.0     # centre (fractions) and radius (DIP)

# Landscape layers, far -> near.  base: ridge height (fraction of H); octaves: (amplitude DIP,
# wavelength DIP) of the rolling ridge line; top/bottom: body colour at the ridge / `depth` DIP
# below; rim: back-lit edge colour (warm under the glow, cool at the sides); haze: the mist that
# fills the valley behind the layer (strongest at its ridge, fading upwards over haze_h DIP).
_LAYERS = [
    dict(base=0.614, octaves=[(30, 900), (12, 360), (3, 130)], seed=11,
         top="#9a5c78", bottom="#8a5270", depth=70,
         rim="#ffc39a", rim_cool="#c98fae", rim_s=0.30, rim_w=9.0,
         haze="#ffc490", haze_cool="#c48ea8", haze_s=0.55, haze_h=42),
    dict(base=0.668, octaves=[(36, 980), (14, 380), (4, 140)], seed=23,
         top="#714265", bottom="#643b5d", depth=80, woods=0.35,
         rim="#e8a08c", rim_cool="#9a6c98", rim_s=0.28, rim_w=7.0,
         haze="#efab8a", haze_cool="#a87a9c", haze_s=0.55, haze_h=36),
    dict(base=0.728, octaves=[(40, 1050), (15, 400), (5, 150)], seed=37,
         top="#4f3154", bottom="#452c4c", depth=90, woods=0.55, barn=True,
         rim="#b87482", rim_cool="#6e5288", rim_s=0.28, rim_w=5.0,
         haze="#d8957f", haze_cool="#8a6a96", haze_s=0.55, haze_h=30),
    dict(base=0.842, octaves=[(22, 1000), (10, 400), (3, 160)], seed=53,
         top="#271c31", bottom="#1d1525", depth=120, trees=True,
         back="#3b2b47",
         rim="#7c4a4c", rim_cool="#4c3a62", rim_s=0.28, rim_w=2.5,
         haze="#b47e86", haze_cool="#6e5a8a", haze_s=0.60, haze_h=52, haze_lift=34),
    dict(base=0.915, octaves=[(9, 1100), (5, 420), (1.5, 170)], seed=71,
         rises=[(0.0, 40, 560), (1.0, 30, 640)],          # the field rises at both edges
         top="#1c1522", bottom="#120d17", depth=110,
         rim="#4c3040", rim_cool="#342842", rim_s=0.10, rim_w=2.0,
         haze="#7d5a74", haze_cool="#54466e", haze_s=0.16, haze_h=40),
]
_BARN_X = 0.655      # barn on the third hill (fraction of width)
_BARN_SIZE = 1.35


def _layer_ridge(spec, W, H, xs, barn_x=None):
    """Ridge line (DIP) of a landscape layer at DIP positions xs."""
    y = np.full(xs.shape, spec["base"] * H, np.float32)
    for k, (amp, wl) in enumerate(spec["octaves"]):
        y += amp * _noise1(xs / wl + 3.7 * k, spec["seed"] + 17 * k)
    for fx, amp, width in spec.get("rises", ()):
        y -= amp * np.exp(-((xs - fx * W) / width) ** 2)
    if barn_x is not None:   # a gentle crest for the barn to stand on
        y -= 24.0 * np.exp(-((xs - barn_x) / 190.0) ** 2)
    return y


def _gradient_rows(stops, ys, H, scale):
    """Per-row colours for a vertical gradient, smoothed so the stops leave no kinks."""
    ts = np.array([s[0] for s in stops], np.float32)
    cs = np.array([s[1] for s in stops], np.float32) / 255.0
    t = ys / H
    rows = np.stack([np.interp(t, ts, cs[:, k]) for k in range(3)], axis=-1).astype(np.float32)
    return _blur(rows[:, None, :], 0.012 * H * scale)[:, 0, :]


def _sky(xs, ys, W, H, scale):
    """Dusk sky: indigo overhead through violet and plum to an amber afterglow on the horizon,
    warmest around GLOW_X and cooler towards the sides."""
    side = _gradient_rows(_SKY_SIDE, ys, H, scale)
    glow = _gradient_rows(_SKY_GLOW, ys, H, scale)
    wx = np.exp(-((xs / W - GLOW_X) / 0.42) ** 2).astype(np.float32)
    img = side[:, None, :] + (glow - side)[:, None, :] * wx[None, :, None]
    # The bright core of the afterglow, sitting on the horizon.
    r0, r1 = int(0.50 * H * scale), int(0.78 * H * scale)
    gx, gy = GLOW_X * W, GLOW_Y * H
    core = np.exp(-((xs[None, :] - gx) / (0.20 * W)) ** 2 - ((ys[r0:r1, None] - gy) / (0.055 * H)) ** 2)
    _screen(img[r0:r1], _rgb("#ffd9a0"), 0.42 * core)
    return img


def _low_res_field(W, H, y0, y1, step, fn):
    """Evaluate fn(X, Y) on a coarse DIP grid (step DIP) covering rows y0..y1; returns the
    grid so callers can upsample it.  Sampling in DIP keeps every output size identical."""
    nx = max(2, int(math.ceil(W / step)))
    ny = max(2, int(math.ceil((y1 - y0) / step)))
    X = ((np.arange(nx, dtype=np.float32) + 0.5) * (W / nx))[None, :]
    Y = (y0 + (np.arange(ny, dtype=np.float32) + 0.5) * ((y1 - y0) / ny))[:, None]
    return fn(X, Y)


def _clouds(img, xs, ys, W, H, scale, seed):
    """Thin streaks of cloud low in the sky, lit peach under the afterglow."""
    y0, y1 = 0.30 * H, 0.64 * H
    r0, r1 = int(y0 * scale), min(img.shape[0], int(math.ceil(y1 * scale)))

    def field(X, Y):
        n = _fbm2(X / 700.0, Y / 34.0, seed + 5, octaves=5)
        n += 0.30 * _fbm2(X / 160.0, Y / 12.0, seed + 9, octaves=3)
        patches = _fbm2(X / 1100.0, Y / 150.0, seed + 13, octaves=2)    # break the streaks up
        env = _smooth(0.36 * H, 0.46 * H, Y) * _smooth(0.625 * H, 0.55 * H, Y)
        return (_smooth(0.16, 0.72, n + 0.6 * patches + 0.06) * env).astype(np.float32)

    dens = _resize_f(_low_res_field(W, H, y0, y1, 4.0, field), img.shape[1], r1 - r0)
    t = _smooth(0.36 * H, 0.60 * H, ys[r0:r1])[:, None]
    wx = np.exp(-((xs / W - GLOW_X) / 0.36) ** 2)[None, :]
    lit = (t * (0.35 + 0.65 * wx)).astype(np.float32)
    cool, warm = _rgb("#6c4a86"), _rgb("#ffb894")
    color = cool + (warm - cool) * lit[..., None]
    _over(img[r0:r1], color, np.clip(dens * (0.20 + 0.32 * lit), 0, 1))


def _stars(img, W, H, scale, seed):
    """A sprinkle of faint stars high up, kept away from the moon and the top corners."""
    rng = np.random.default_rng([seed, 1])
    mx, my = MOON_X * W, MOON_Y * H
    n = int(150 * W * H / (2560 * 1440))
    hgt, wid = img.shape[:2]
    for _ in range(n):
        x = rng.uniform(0, W)
        y = 0.48 * H * rng.random() ** 1.5
        b = rng.uniform(0.35, 1.0) ** 1.6
        b *= _smooth(0.47 * H, 0.22 * H, y)                       # fade into the dusk
        b *= _smooth(1.8 * MOON_R, 6.0 * MOON_R, math.hypot(x - mx, y - my))
        corner = min(math.hypot(x, y), math.hypot(W - x, y))
        b *= 0.25 + 0.75 * _smooth(160.0, 460.0, corner)          # calm corners for the webs
        if b < 0.04:
            continue
        r = 0.45 + 0.75 * rng.random() ** 3                     # DIP
        sig = max(r * scale * 0.7, 0.55)                          # px
        peak = min(1.0, (r * scale) ** 2 / (2 * sig * sig) * 1.6) * b
        rad = int(math.ceil(3.5 * sig)) + 1
        cx, cy = x * scale, y * scale
        x0, x1 = max(0, int(cx) - rad), min(wid, int(cx) + rad + 1)
        y0, y1 = max(0, int(cy) - rad), min(hgt, int(cy) + rad + 1)
        if x0 >= x1 or y0 >= y1:
            continue
        gx = (np.arange(x0, x1) + 0.5 - cx)[None, :]
        gy = (np.arange(y0, y1) + 0.5 - cy)[:, None]
        a = peak * np.exp(-(gx * gx + gy * gy) / (2 * sig * sig))
        tint = _rgb("#fff1dc") if rng.random() < 0.7 else _rgb("#dfe4ff")
        _screen(img[y0:y1, x0:x1], tint, a.astype(np.float32))


def _moon(img, xs, ys, W, H, scale, seed):
    """Big golden harvest moon with a soft warm halo."""
    mx, my, R = MOON_X * W, MOON_Y * H, MOON_R
    # Broad halo: smooth, so evaluate coarsely and upsample.
    def halo(X, Y):
        r = np.hypot(X - mx, Y - my)
        return np.stack([np.exp(-(r / (3.0 * R)) ** 2), np.exp(-(r / (7.5 * R)) ** 2)]).astype(np.float32)

    y1 = min(H, my + 22 * R)
    g = _low_res_field(W, H, 0.0, y1, 6.0, halo)
    r1 = min(img.shape[0], int(math.ceil(y1 * scale)))
    _screen(img[:r1], _rgb("#ffbf86"), 0.20 * _resize_f(g[0], img.shape[1], r1))
    _screen(img[:r1], _rgb("#e9a2b4"), 0.10 * _resize_f(g[1], img.shape[1], r1))

    # Disc and tight halo, at full resolution around the moon.
    ext = 5.0 * R
    c0, c1 = max(0, int((mx - ext) * scale)), min(img.shape[1], int((mx + ext) * scale))
    r0, r1 = max(0, int((my - ext) * scale)), min(img.shape[0], int((my + ext) * scale))
    X, Y = xs[None, c0:c1], ys[r0:r1, None]
    r = np.hypot(X - mx, Y - my)
    sub = img[r0:r1, c0:c1]
    tight = np.exp(-np.maximum(r - R, 0.0) / (0.36 * R))
    _screen(sub, _rgb("#ffd89e"), 0.42 * tight * (r > R - 2))
    # The disc: limb-darkened gold with faint maria.
    rr = np.clip(r / R, 0.0, 1.0)
    mu = np.sqrt(1.0 - rr * rr)
    centre, limb = _rgb("#ffdd9a"), _rgb("#f29c4b")
    col = limb + (centre - limb) * (mu ** 0.55)[..., None]
    maria = _fbm2((X - mx) / (0.62 * R) + 0.3, (Y - my) / (0.62 * R) + 0.6, seed + 77, octaves=4)
    spots = _smooth(-0.05, 0.6, maria) * 0.075 + _smooth(0.3, 0.9, maria) * 0.035
    col *= (1.0 - spots * mu)[..., None]
    _over(sub, col, _cov((r - R) * scale))


def _birds(img, W, H, scale, seed):
    """A few distant birds crossing the dusk sky."""
    rng = np.random.default_rng([seed, 3])
    flock = [(0.585, 0.355, 9.0), (0.604, 0.337, 7.4), (0.618, 0.366, 8.0), (0.636, 0.347, 6.2)]
    r0 = max(0, int((min(f[1] for f in flock) * H - 12) * scale))
    r1 = min(img.shape[0], int((max(f[1] for f in flock) * H + 12) * scale))
    mask = np.zeros((r1 - r0, img.shape[1]), np.float32)
    for fx, fy, s in flock:
        x, y = fx * W, fy * H
        up = rng.uniform(0.15, 0.45)        # wing beat: how high the tips are

        def bird(X, Y, x=x, y=y, s=s, up=up):
            left = _sd_polyline(X, Y, [(x - s, y - up * s), (x - 0.45 * s, y - 0.18 * s), (x, y + 0.12 * s)], 0.42)
            right = _sd_polyline(X, Y, [(x, y + 0.12 * s), (x + 0.45 * s, y - 0.2 * s), (x + 1.05 * s, y - (up + 0.08) * s)], 0.42)
            return np.minimum(left, right)

        _stamp(mask, r0, scale, (x - s - 2, y - s - 2, x + s + 3, y + s + 2), bird)
    _over(img[r0:r1], _rgb("#24182e"), 0.75 * mask)


def _tree_round(rng, x, gy, hgt):
    """A broadleaf tree: a lumpy, rounded crown that comes down low on a short trunk.
    Returns (sdf, bbox, crown) with crown = (cx, cy, rx, ry)."""
    ry = hgt * rng.uniform(0.40, 0.46)          # crown half-height
    rx = hgt * rng.uniform(0.34, 0.50)          # crown half-width
    cx = x + rng.uniform(-0.12, 0.12) * rx
    cy = gy - hgt + ry
    m = min(rx, ry)
    blobs = [(cx, cy + 0.1 * ry, m * 0.74)]
    for _ in range(int(rng.integers(8, 14))):
        a = rng.uniform(0, 2 * math.pi)
        br = m * rng.uniform(0.30, 0.52)
        d = math.sqrt(rng.uniform(0.25, 1.0))
        blobs.append((cx + math.cos(a) * d * (rx - br), cy + math.sin(a) * d * (ry - br), br))
    trunk = (x, gy + 6, cx, cy, max(1.0, hgt * 0.034), max(0.7, hgt * 0.02))
    limbs = [(cx, cy + ry * 0.4, cx + s * rx * rng.uniform(0.35, 0.6), cy - ry * rng.uniform(0.0, 0.3),
              max(0.6, hgt * 0.015)) for s in (-1, 1)]

    def sdf(X, Y):
        d = None
        for bx, by, br in blobs:
            di = np.hypot(X - bx, Y - by) - br
            d = di if d is None else np.minimum(d, di)
        d = np.minimum(d, _sd_capsule(X, Y, *trunk))
        for ax, ay, bx, by, r in limbs:
            d = np.minimum(d, _sd_capsule(X, Y, ax, ay, bx, by, r))
        return d

    bbox = (cx - rx - 6, gy - hgt - 6, cx + rx + 6, gy + 8)
    return sdf, bbox, (cx, cy, rx, ry)


def _tree_tall(rng, x, gy, hgt):
    """A slender poplar: a tapering spire of foliage with a few fuller lobes low down."""
    cr = hgt * rng.uniform(0.09, 0.115)              # half-width at the widest point
    bot, top = gy - hgt * 0.13, gy - hgt
    core = (x, bot - 0.6 * cr, x, top + 0.25 * cr, 0.8 * cr, 0.22 * cr)
    lobes = []
    for i in range(4):
        t = 0.12 + 0.16 * i                          # along the crown, from the bottom
        side = (-1) ** i * rng.uniform(0.1, 0.3) * cr
        lobes.append((x + side, bot - t * (bot - top), cr * (0.95 - 0.12 * i)))
    trunk = (x, gy + 6, x, bot - hgt * 0.1, max(0.8, hgt * 0.022), max(0.6, hgt * 0.016))

    def sdf(X, Y):
        d = np.minimum(_sd_capsule(X, Y, *trunk), _sd_capsule(X, Y, *core))
        for bx, by, br in lobes:
            d = np.minimum(d, np.hypot(X - bx, Y - by) - br)
        return d

    return sdf, (x - cr * 1.6, top - 5, x + cr * 1.6, gy + 8), (x, (top + bot) / 2, cr, (bot - top) / 2)


def _tree_spruce(rng, x, gy, hgt):
    """A dark conifer among the broadleaves: a jagged spire."""
    half = hgt * rng.uniform(0.17, 0.22)
    top = gy - hgt
    tiers = int(rng.integers(5, 8))
    pts = [(x, top)]
    for i in range(1, tiers + 1):
        t = i / tiers
        wv = half * t
        yv = top + hgt * 0.86 * t
        pts.append((x + wv, yv))
        if i < tiers:
            pts.append((x + wv * 0.55, yv - hgt * 0.02))
    pts += [(x + half * 0.12, top + hgt * 0.86), (x + half * 0.12, gy + 6),
            (x - half * 0.12, gy + 6), (x - half * 0.12, top + hgt * 0.86)]
    for i in range(tiers, 0, -1):
        t = i / tiers
        wv = half * t
        yv = top + hgt * 0.86 * t
        if i < tiers:
            pts.append((x - wv * 0.55, yv - hgt * 0.02))
        pts.append((x - wv, yv))
    return (lambda X, Y: _sd_polygon(X, Y, pts)), (x - half - 2, top - 2, x + half + 2, gy + 8), None


def _tree_bare(rng, x, gy, hgt):
    """A leafless, twiggy tree (recursive branching)."""
    segs = []

    def grow(x0, y0, ang, length, r, depth):
        x1, y1 = x0 + length * math.cos(ang), y0 - length * math.sin(ang)
        segs.append((x0, y0, x1, y1, r, r * 0.72))
        if depth == 0:
            return
        kids = 2 if rng.random() < 0.65 else 3
        for k in range(kids):
            spread = rng.uniform(0.28, 0.62) * (1 if k % 2 == 0 else -1)
            if kids == 3 and k == 2:
                spread = rng.uniform(-0.15, 0.15)
            grow(x1, y1, ang + spread + rng.normal(0, 0.08) + 0.06 * (math.pi / 2 - ang),
                 length * rng.uniform(0.66, 0.80), r * 0.68, depth - 1)

    trunk_len = hgt * 0.34
    grow(x, gy + 6, math.pi / 2 + rng.normal(0, 0.05), trunk_len + 6, max(1.2, hgt * 0.03), 6)
    xs_ = [s[0] for s in segs] + [s[2] for s in segs]
    ys_ = [s[1] for s in segs] + [s[3] for s in segs]
    return segs, (min(xs_) - 3, min(ys_) - 3, max(xs_) + 3, max(ys_) + 3)


def _barn(mask, row0, scale, x, gy, s=1.0):
    """Gambrel-roofed barn with a cupola, lean-to and silo, stamped into mask.  Returns the lit
    window as (centre x, centre y, half width, half height) in DIP."""
    def P(pts):
        return [(x + px * s, gy + py * s) for px, py in pts]

    barn = P([(-21, 6), (-21, -15), (-17, -27), (0, -36), (17, -27), (21, -15), (21, 6)])
    eaves = P([(-22.5, -14), (-18.2, -27.6), (-17, -27), (-21, -15)])      # roof overhang
    eaves_r = [(2 * x - px, py) for px, py in eaves]
    shed = P([(-20, 6), (-20, -10), (-33, -5.5), (-33, 6)])
    cupola = P([(-2.6, -34), (-2.6, -39.5), (0, -42.5), (2.6, -39.5), (2.6, -34)])
    silo_box = (x + 30 * s, gy - 18 * s, 5.2 * s, 26 * s)
    dome = (x + 30 * s, gy - 44 * s, 5.2 * s)

    def sdf(X, Y):
        d = np.minimum(_sd_polygon(X, Y, barn), _sd_polygon(X, Y, shed))
        d = np.minimum(d, np.minimum(_sd_polygon(X, Y, eaves), _sd_polygon(X, Y, eaves_r)))
        d = np.minimum(d, _sd_box(X, Y, silo_box[0], silo_box[1], silo_box[2], silo_box[3]))
        d = np.minimum(d, _sd_circle(X, Y, *dome))
        return np.minimum(d, _sd_polygon(X, Y, cupola))

    _stamp(mask, row0, scale, (x - 36 * s, gy - 52 * s, x + 38 * s, gy + 8 * s), sdf)
    return (x + 9.5 * s, gy - 7.0 * s, 2.2 * s, 2.9 * s)      # window centre and half size


def _rim(mask, scale, off_dip, soft_dip):
    """Back-light rim: 1 at the top edges of a silhouette mask, fading inwards."""
    off = max(1, int(round(off_dip * scale)))
    shifted = np.zeros_like(mask)
    shifted[off:] = mask[:-off]
    return np.clip(mask - _blur(shifted, soft_dip * scale), 0.0, 1.0)


def _landscape(img, xs, ys, W, H, scale, seed):
    """Paint the _LAYERS far to near: each one's valley mist, then its silhouette (hill plus
    woods, barn or treeline) with a back-lit rim along its top edges."""
    gwx = np.exp(-((xs / W - GLOW_X) / 0.42) ** 2).astype(np.float32)   # warmth per column
    barn_x = _BARN_X * W

    for li, spec in enumerate(_LAYERS):
        rng = np.random.default_rng([seed, 10 + li])
        ridge = _layer_ridge(spec, W, H, xs, barn_x if spec.get("barn") else None)
        slope = np.gradient(ridge, xs).astype(np.float32)
        inv = (1.0 / np.sqrt(1.0 + slope * slope)).astype(np.float32)
        reach = max(4.0 * spec["haze_h"], 210.0 if spec.get("trees") else 70.0)
        r0 = max(0, int((ridge.min() - reach) * scale))
        band = img[r0:]
        dy = ys[r0:, None] - ridge[None, :]
        dn = dy * inv[None, :]
        rim_rows = slice(0, min(band.shape[0], int((ridge.max() + 40) * scale) - r0 + 1))

        def warm(key):
            c, k = _rgb(spec[key + "_cool"]), _rgb(spec[key])
            return c[None, :] + (k - c)[None, :] * gwx[:, None]          # (w, 3)

        def paint(mask, body):
            """Back-lit rim along the silhouette's top edges, then the silhouette itself."""
            rim = _rim(mask[rim_rows], scale, spec["rim_w"], spec["rim_w"] * 1.1)
            _over(body[rim_rows], warm("rim")[None], spec["rim_s"] * rim)
            _over(band, body, mask)

        # Mist in the valley behind this layer, brightest at its ridge (or `haze_lift` DIP above
        # it, so a treeline's feet stay dark instead of standing in a bright band).
        lift = spec.get("haze_lift", 0.0)
        above = np.maximum(-dn - lift, 0.0)
        haze = spec["haze_s"] * np.exp(-above / spec["haze_h"])
        if lift:
            haze *= 0.35 + 0.65 * _smooth(0.0, lift, -dn)
        _over(band, warm("haze")[None], haze)

        # Body colour: a gentle gradient down from the ridge.
        t = _smooth(0.0, spec["depth"], dy)
        top, bottom = _rgb(spec["top"]), _rgb(spec["bottom"])
        body = top + (bottom - top) * t[..., None]

        # Silhouette: the hill itself plus whatever stands on it.
        mask = _cov(-dn * scale)
        window = None
        if spec.get("woods"):
            _woods(mask, r0, scale, W, ridge, xs, spec, rng,
                   clear=(barn_x - 4 * _BARN_SIZE, 48 * _BARN_SIZE) if spec.get("barn") else None)
        if spec.get("barn"):
            gy = float(np.interp(barn_x, xs, ridge)) + 3.0
            window = _barn(mask, r0, scale, barn_x, gy, _BARN_SIZE)
        if spec.get("trees"):
            # A hazier back row first, then the front row with the undergrowth.
            leaf = _leaf_noise(r0, mask.shape[0], mask.shape[1], scale, seed)
            back = np.zeros_like(mask)
            _treeline(back, r0, scale, W, ridge - 9.0, xs, np.random.default_rng([seed, 30]),
                      seed + 1, barn_x, leaf, back=True)
            paint(back, np.broadcast_to(_rgb(spec["back"]), body.shape).copy())
            tint = _treeline(mask, r0, scale, W, ridge, xs, rng, seed, barn_x, leaf)
            _over(body, _rgb("#57292c"), tint)          # the faintest rust in some crowns
        paint(mask, body)
        if window is not None:
            _lit_window(img, xs, ys, scale, window)


def _woods(mask, row0, scale, W, ridge, xs, spec, rng, clear=None):
    """Small distant woods and hedgerows along a far ridge: clumps of overlapping, leafy crowns
    of mixed sizes, a few standing taller.  `clear` = (x, half width) keeps a stretch open."""
    seed = spec["seed"]
    x = -20.0
    while x < W + 20:
        x += rng.uniform(2.5, 6.0)
        dens = 0.5 + 0.5 * float(_noise1(np.array([x / 240.0]), seed + 5)[0])
        if dens < 1.0 - spec["woods"] or (clear and abs(x - clear[0]) < clear[1]):
            continue
        tall = rng.random() < 0.12
        r = rng.uniform(2.5, 6.5) * (0.65 + 0.6 * dens) * (1.6 if tall else 1.0)
        gy = float(np.interp(x, xs, ridge))
        cy = gy - r * rng.uniform(0.1, 0.9) - (r * 0.8 if tall else 0.0)
        _stamp(mask, row0, scale, (x - r - 2, cy - r - 2, x + r + 2, gy + 2),
               lambda X, Y, x=x, cy=cy, r=r, gy=gy: np.minimum(
                   _sd_circle(X, Y, x, cy, r), _sd_capsule(X, Y, x, gy + 1, x, cy, max(0.5, 0.12 * r))),
               add=lambda X, Y, r=r: (0.16 * r) * _fbm2(X / 2.6, Y / 2.6, seed + 9, octaves=2))


def _leaf_noise(row0, hb, wb, scale, seed):
    """Leafy-edge displacement (DIP) for a band of rows: noise sampled on a 1 DIP grid and
    resized, so crowns get the same ragged outline at every output size."""
    y0, y1 = row0 / scale, (row0 + hb) / scale
    field = _low_res_field(wb / scale, 0.0, y0, y1, 1.0,
                           lambda X, Y: 2.4 * _fbm2(X / 7.0, Y / 7.0, seed + 31, octaves=2))
    return _resize_f(field, wb, hb)


def _treeline(mask, row0, scale, W, ridge, xs, rng, seed, barn_x, leaf, back=False):
    """A treeline of autumn broadleaves with a few conifers, bare trees and undergrowth,
    stamped into mask.  `leaf` displaces the crowns' edges (see _leaf_noise); back=True makes
    the sparser, taller row behind.  Returns a per-pixel rust tint (0..1) for the crowns."""
    tint = np.zeros_like(mask)

    def envelope(x):
        e = 112.0 + 42.0 * float(_noise1(np.array([x / 330.0]), seed + 41)[0])
        e *= 1.0 - 0.50 * math.exp(-((x - barn_x) / 160.0) ** 2)     # keep the barn in view
        return e * (1.12 if back else 1.0)

    def ground(x):
        return float(np.interp(x, xs, ridge)) + 6.0

    bare_at = [] if back else sorted(rng.uniform(0.04, 0.96, 5) * W)
    x = -60.0
    while x < W + 60:
        x += rng.uniform(26, 52) if back else rng.uniform(14, 32)
        if bare_at and x > bare_at[0]:
            bx = bare_at.pop(0)
            segs, _ = _tree_bare(rng, bx, ground(bx), envelope(bx) * rng.uniform(1.1, 1.3))
            for (ax, ay, ex, ey, ra, rb) in segs:
                _stamp(mask, row0, scale, (min(ax, ex) - ra - 1, min(ay, ey) - ra - 1,
                                           max(ax, ex) + ra + 1, max(ay, ey) + ra + 1),
                       lambda X, Y, s=(ax, ay, ex, ey, ra, rb): _sd_capsule(X, Y, *s))
            continue
        hgt = envelope(x) * rng.uniform(0.72, 1.18)
        roll = rng.random()
        if roll < 0.025:
            sdf, bbox, crown = _tree_spruce(rng, x, ground(x), hgt * 1.05)
        elif roll < 0.10:
            sdf, bbox, crown = _tree_tall(rng, x, ground(x), hgt * 1.1)
        else:
            sdf, bbox, crown = _tree_round(rng, x, ground(x), hgt)
        res = _stamp(mask, row0, scale, bbox, sdf, add=leaf if crown is not None else None)
        if res is not None and not back:
            # Paint this tree's tint over the ones behind it: crowns only, never the trunk.
            rs, cs, cov = res
            rust = rng.uniform(0.0, 1.0) ** 2 * 0.2
            if crown is not None:
                Y = ((np.arange(rs.start, rs.stop, dtype=np.float32) + row0 + 0.5) / scale)[:, None]
                rust = rust * _smooth(crown[1] + crown[3] * 0.95, crown[1] + crown[3] * 0.45, Y)
            else:
                rust = 0.0
            sub = tint[rs, cs]
            sub += (rust - sub) * cov

    if not back:   # undergrowth along the foot of the trees, with a few gaps
        x = -20.0
        while x < W + 20:
            x += rng.uniform(4, 10)
            if float(_noise1(np.array([x / 70.0]), seed + 47)[0]) < -0.45:
                continue
            r = rng.uniform(7.0, 16.0)
            cy = ground(x) - 2.0 - r * rng.uniform(0.15, 0.55)
            res = _stamp(mask, row0, scale, (x - r - 3, cy - r - 3, x + r + 3, cy + r + 3),
                         lambda X, Y, x=x, cy=cy, r=r: _sd_circle(X, Y, x, cy, r), add=leaf)
            if res is not None:
                rs, cs, cov = res
                sub = tint[rs, cs]
                sub -= sub * cov
    return tint


def _lit_window(img, xs, ys, scale, window):
    """One warm lit window on the barn, with a soft glow."""
    wx, wy, hw, hh = window
    ext = 9.0 * hw
    c0, c1 = max(0, int((wx - ext) * scale)), min(img.shape[1], int((wx + ext) * scale) + 1)
    r0, r1 = max(0, int((wy - ext) * scale)), min(img.shape[0], int((wy + ext) * scale) + 1)
    X, Y = xs[None, c0:c1], ys[r0:r1, None]
    sub = img[r0:r1, c0:c1]
    r = np.hypot(X - wx, (Y - wy) * 1.15)
    _screen(sub, _rgb("#ffac58"), 0.34 * np.exp(-(r / (2.6 * hw)) ** 2) + 0.12 * np.exp(-(r / (5.5 * hw)) ** 2))
    pane = _cov(_sd_box(X, Y, wx, wy, hw, hh, 0.3 * hw) * scale)
    _over(sub, _rgb("#ffd47c"), pane)
    bar = _cov(np.minimum(_sd_box(X, Y, wx, wy, hw, 0.1 * hw), _sd_box(X, Y, wx, wy, 0.1 * hw, hh)) * scale)
    _over(sub, _rgb("#c8782e"), 0.55 * bar * pane)


def _vignette(img, W, H):
    """Darken the top corners (where the app's cobwebs hang) and, slightly, the edges."""
    def field(X, Y):
        s = 470.0
        v = np.exp(-(X * X + Y * Y) / (2 * s * s)) + np.exp(-((W - X) ** 2 + Y * Y) / (2 * s * s))
        e = ((X / W - 0.5) * 2) ** 2
        return (1.0 - 0.34 * np.clip(v, 0, 1) - 0.10 * e * _smooth(0.65 * H, 0.0, Y)).astype(np.float32)

    f = _resize_f(_low_res_field(W, H, 0.0, H, 8.0, field), img.shape[1], img.shape[0])
    img *= f[..., None]


def wallpaper(w: int, h: int, scale: float | None = None, seed: int = 1031) -> Image.Image:
    """Render the autumn dusk wallpaper at w x h px.

    The picture is laid out in DIPs (px / scale), so 1920x1080 at scale 0.75 is the same
    picture as 3840x2160 at 1.5.  scale defaults to h / 1440 (a 1440 DIP tall desktop).
    Returns an RGB image."""
    if scale is None:
        scale = h / 1440.0
    W, H = w / scale, h / scale
    xs = ((np.arange(w, dtype=np.float32) + 0.5) / scale)
    ys = ((np.arange(h, dtype=np.float32) + 0.5) / scale)
    img = _sky(xs, ys, W, H, scale)
    _clouds(img, xs, ys, W, H, scale, seed)
    _stars(img, W, H, scale, seed)
    _moon(img, xs, ys, W, H, scale, seed)
    _birds(img, W, H, scale, seed)
    _landscape(img, xs, ys, W, H, scale, seed)
    _vignette(img, W, H)
    return Image.fromarray(_to_u8(np.clip(img, 0.0, 1.0), seed))


# =============================================================================================
# Drawing onto opaque surfaces (taskbar strip, title bar)
# =============================================================================================

_WHITE = "#ffffff"


def _stroke(d, width):
    """Turn a filled shape's distance into a centred outline of the given width."""
    return np.abs(d) - width / 2.0


def _sd_ellipse_line(X, Y, cx, cy, rx, ry):
    """Approximate distance to an ellipse's outline (accurate enough for thin strokes)."""
    px, py = X - cx, Y - cy
    k0 = np.hypot(px / rx, py / ry)
    k1 = np.hypot(px / (rx * rx), py / (ry * ry))
    return np.abs(k0 * (k0 - 1.0) / np.maximum(k1, 1e-6))


def _paint(canvas, scale, cx, cy, ext, layers):
    """Paint shape layers onto an RGB float canvas around (cx, cy) px.  layers is a list of
    (sdf, colour, alpha); each sdf takes DIP coordinates relative to the centre."""
    h, w = canvas.shape[:2]
    e = ext * scale + 2
    x0, x1 = max(0, int(math.floor(cx - e))), min(w, int(math.ceil(cx + e)))
    y0, y1 = max(0, int(math.floor(cy - e))), min(h, int(math.ceil(cy + e)))
    if x1 <= x0 or y1 <= y0:
        return
    X = ((np.arange(x0, x1, dtype=np.float32) + 0.5 - cx) / scale)[None, :]
    Y = ((np.arange(y0, y1, dtype=np.float32) + 0.5 - cy) / scale)[:, None]
    sub = canvas[y0:y1, x0:x1]
    for sdf, color, alpha in layers:
        _over(sub, _rgb(color), alpha * _cov(sdf(X, Y) * scale))


def _paste_rgba(canvas, im, x, y):
    """Composite a PIL RGBA image onto an RGB float canvas at integer px (x, y)."""
    a = np.asarray(im.convert("RGBA"), np.float32) / 255.0
    h, w = canvas.shape[:2]
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(w, x + a.shape[1]), min(h, y + a.shape[0])
    if x1 <= x0 or y1 <= y0:
        return
    src = a[y0 - y:y1 - y, x0 - x:x1 - x]
    _over(canvas[y0:y1, x0:x1], src[..., :3], src[..., 3])


@functools.lru_cache(maxsize=None)
def app_icon(px: int) -> Image.Image:
    """Fall Desktop's icon at px x px (RGBA), from res/falldesktop.ico: the exact size if the
    .ico has it (as Windows would pick), else the next larger one scaled down.  Falls back to a
    drawn maple leaf if the .ico is missing."""
    px = max(1, int(px))
    try:
        with Image.open(APP_ICON) as ico:        # closed promptly: the .ico may be rebuilt
            sizes = sorted(ico.info.get("sizes") or [ico.size])
            bigger = [s for s in sizes if s[0] >= px]
            ico.size = bigger[0] if bigger else sizes[-1]
            ico.load()
            im = ico.convert("RGBA")
        return im if im.size == (px, px) else im.resize((px, px), Image.LANCZOS)
    except (OSError, ValueError, KeyError):
        return _maple_leaf(px)


# Right half of a maple leaf in a 16-unit box (tip at the top, stem at the bottom centre).
_MAPLE_RIGHT = [(8.0, 0.6), (9.3, 2.9), (10.7, 2.2), (10.3, 5.3), (12.5, 3.7), (12.1, 5.2), (14.9, 4.8),
                (13.8, 6.6), (15.0, 7.2), (11.7, 9.6), (12.2, 10.8), (8.9, 10.2), (8.0, 11.2)]


def _maple_leaf(px):
    """A small orange maple leaf (fallback app icon)."""
    pts = _MAPLE_RIGHT + [(16.0 - x, y) for x, y in reversed(_MAPLE_RIGHT[1:-1])]
    s = px / 16.0
    X = ((np.arange(px, dtype=np.float32) + 0.5) / s)[None, :]
    Y = ((np.arange(px, dtype=np.float32) + 0.5) / s)[:, None]
    d = np.minimum(_sd_polygon(X, Y, pts), _sd_capsule(X, Y, 8.0, 10.6, 8.4, 15.0, 0.5))
    rgb = np.empty((px, px, 3), np.float32)
    rgb[...] = _rgb("#e8661c")
    _over(rgb, _rgb("#ffb43c"), np.clip(1.0 - np.hypot(X - 8.0, Y - 7.0) / 6.5, 0, 1) * 0.8)
    _over(rgb, _rgb("#b0401a"), np.clip(_cov(d * s) - _cov((d + 0.7) * s), 0, 1))      # darker edge
    rgba = np.dstack([rgb, _cov(d * s)])
    return Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8))


# =============================================================================================
# 2. Taskbar
# =============================================================================================

def _tile(color, glyph, border=None):
    """A 24 DIP app tile: flat rounded square with a white outline glyph (Snowy's style)."""
    half = TASKBAR_TILE_DIP / 2.0
    layers = [(lambda X, Y: _sd_box(X, Y, 0.0, 0.0, half, half, 4.6), color, 1.0)]
    if border:
        layers.append((lambda X, Y: _stroke(_sd_box(X, Y, 0.0, 0.0, half - 0.4, half - 0.4, 4.2), 0.7), border, 1.0))
    layers.append((glyph, _WHITE, 1.0))
    return layers


def _g_folder(X, Y):
    w = 1.3
    body = _sd_box(X, Y, 0.0, 1.0, 7.4, 5.2, 1.7)
    tab = _sd_box(X, Y, -3.9, -3.5, 3.5, 2.1, 1.2)
    flap = _sd_segment(X, Y, -6.8, -1.6, 6.8, -1.6)[0] - w / 2
    return np.minimum(_stroke(np.minimum(body, tab), w), flap)


def _g_globe(X, Y):
    w = 1.25
    R = 6.7
    d = _stroke(_sd_circle(X, Y, 0, 0, R), w)
    d = np.minimum(d, _sd_ellipse_line(X, Y, 0, 0, 2.9, R) - w / 2)
    d = np.minimum(d, _sd_segment(X, Y, -R, 0, R, 0)[0] - w / 2)
    for yy in (-3.6, 3.6):
        hx = math.sqrt(R * R - yy * yy) - 0.3
        d = np.minimum(d, _sd_segment(X, Y, -hx, yy, hx, yy)[0] - w / 2)
    return d


def _g_mail(X, Y):
    w = 1.3
    d = _stroke(_sd_box(X, Y, 0.0, 0.3, 7.6, 5.6, 1.5), w)
    return np.minimum(d, _sd_polyline(X, Y, [(-6.4, -3.6), (0.0, 1.0), (6.4, -3.6)], w / 2))


def _g_music(X, Y):
    w = 1.25
    d = np.minimum(_stroke(_sd_circle(X, Y, -3.7, 4.6, 2.1), w), _stroke(_sd_circle(X, Y, 4.1, 3.2, 2.1), w))
    d = np.minimum(d, _sd_segment(X, Y, -1.6, 4.4, -1.6, -5.0)[0] - w / 2)
    d = np.minimum(d, _sd_segment(X, Y, 6.2, 3.0, 6.2, -6.6)[0] - w / 2)
    d = np.minimum(d, _sd_polyline(X, Y, [(-1.6, -5.0), (6.2, -6.6)], w / 2))
    return np.minimum(d, _sd_polyline(X, Y, [(-1.6, -2.4), (6.2, -4.0)], w / 2))


def _g_photos(X, Y):
    w = 1.3
    frame = _sd_box(X, Y, 0.0, 0.0, 7.2, 6.4, 2.0)
    d = _stroke(frame, w)
    hills = _sd_polyline(X, Y, [(-6.6, 5.2), (-2.2, 0.6), (1.2, 4.0), (3.0, 2.3), (6.6, 5.6)], w / 2)
    d = np.minimum(d, np.maximum(hills, frame))
    return np.minimum(d, _sd_circle(X, Y, 3.2, -2.6, 1.45))


def _g_store(X, Y):
    w = 1.3
    d = _stroke(_sd_box(X, Y, 0.0, 2.2, 6.8, 4.9, 1.6), w)
    d = np.minimum(d, _sd_arc(X, Y, 0.0, -2.5, 3.2, 180.0, 360.0) - w / 2)
    for sx in (-2.6, 2.6):
        d = np.minimum(d, _sd_circle(X, Y, sx, 0.4, 0.75))
    return d


def _g_terminal(X, Y):
    w = 1.4
    d = _sd_polyline(X, Y, [(-5.6, -3.4), (-2.2, 0.0), (-5.6, 3.4)], w / 2)
    return np.minimum(d, _sd_segment(X, Y, 0.2, 3.6, 5.8, 3.6)[0] - w / 2)


# name -> (tile colour, glyph, tile border).  Flat, generic icons (not real apps' marks).
TASKBAR_APPS = {
    "folder": ("#e0a030", _g_folder, None),
    "browser": ("#2f7de1", _g_globe, None),
    "mail": ("#1c8fd0", _g_mail, None),
    "music": ("#e0566f", _g_music, None),
    "photos": ("#7a5af0", _g_photos, None),
    "store": ("#23a26c", _g_store, None),
    "terminal": ("#2a2a33", _g_terminal, "#5a5a68"),
}


def _start_layers():
    p, g = 9.35, 1.3
    o = (p + g) / 2
    panes = {(-1, -1): (28, 155, 240), (1, -1): (43, 170, 247), (-1, 1): (22, 137, 224), (1, 1): (35, 152, 236)}
    return [(lambda X, Y, sx=sx, sy=sy: _sd_box(X, Y, sx * o, sy * o, p / 2, p / 2, 0.9), c, 1.0)
            for (sx, sy), c in panes.items()]


def _search_layers():
    return [(lambda X, Y: np.minimum(_stroke(_sd_circle(X, Y, -1.4, -1.4, 5.55), 1.3),
                                     _sd_capsule(X, Y, 2.75, 2.75, 7.0, 7.0, 0.68)), _WHITE, 1.0)]


def _taskview_layers():
    w = 1.3

    def sdf(X, Y):
        front = _sd_box(X, Y, -2.3, 2.1, 5.4, 4.8, 1.7)
        back = _sd_box(X, Y, 2.3, -2.1, 5.4, 4.8, 1.7)
        hidden = -(front - w / 2 - 1.0)            # the back pane hides behind the front one
        return np.minimum(_stroke(front, w), np.maximum(_stroke(back, w), hidden))
    return [(sdf, _WHITE, 1.0)]


def _chevron_layers():
    return [(lambda X, Y: _sd_polyline(X, Y, [(-4.3, 2.1), (0.0, -2.2), (4.3, 2.1)], 0.5), "#dcdcdc", 1.0)]


def _wifi_layers():
    w = 1.3

    def sdf(X, Y):
        d = _sd_circle(X, Y, 0.0, 3.6, 1.2)
        for R in (3.3, 6.4, 9.5):
            d = np.minimum(d, _sd_arc(X, Y, 0.0, 3.6, R, 223.0, 317.0) - w / 2)
        return d
    return [(sdf, _WHITE, 1.0)]


def _volume_layers():
    w = 1.2

    def sdf(X, Y):
        body = [(-7.6, -2.6), (-4.7, -2.6), (-1.0, -6.1), (-1.0, 6.1), (-4.7, 2.6), (-7.6, 2.6)]
        d = _stroke(_sd_polygon(X, Y, body), w)
        for R in (3.8, 6.8):
            d = np.minimum(d, _sd_arc(X, Y, -0.6, 0.0, R, -52.0, 52.0) - w / 2)
        return d
    return [(sdf, _WHITE, 1.0)]


def _battery_layers():
    w = 1.2

    def sdf(X, Y):
        d = _stroke(_sd_box(X, Y, -0.9, 0.0, 6.5, 3.7, 1.6), w)
        d = np.minimum(d, _sd_box(X, Y, 6.5, 0.0, 0.75, 1.6, 0.5))
        return np.minimum(d, _sd_box(X, Y, -2.9, 0.0, 3.4, 1.85, 0.6))      # about 60% charged
    return [(sdf, _WHITE, 1.0)]


def taskbar_layout(w: int, h: int, scale: float, date_text: str = "10/31/2026",
                   time_text: str = "6:42 PM", apps=tuple(TASKBAR_APPS), battery: bool = True) -> dict:
    """Pixel geometry of the taskbar for a w x h screen (all values in px):

        top, height       taskbar rows [top, top + height)
        buttons           [(name, cx, cy)] for start, search, taskview and each app, centred
        tray              {name: (cx, cy)} for chevron, app (Fall Desktop's icon), wifi, volume
                          and battery (when shown)
        clock             (left, right, time_baseline, date_baseline); left/right are the ink
                          extents of the two right-aligned lines
        font_px           clock font size

    Measured on Snowy's 4K screenshot: 44 DIP buttons centred on the screen, the clock's ink
    ending 14.7 DIP from the right edge, tray icons 25 DIP apart left of the clock."""
    th = int(round(TASKBAR_DIP * scale))
    top = h - th
    cy = top + th / 2.0
    names = ["start", "search", "taskview"] + list(apps)
    pitch = TASKBAR_BUTTON_DIP * scale
    x0 = w / 2.0 - pitch * (len(names) - 1) / 2.0
    # Snap the row once (so tiles sit on whole pixels at 1.5) and keep the pitch exact.
    buttons = [(n, float(round(x0)) + i * pitch, float(round(cy))) for i, n in enumerate(names)]

    fpx = 12.0 * scale
    f = font(fpx, "Regular", "Text")
    right = w - 14.7 * scale
    widths = [f.getbbox(t, anchor="ls") for t in (time_text, date_text)]
    left = right - max(b[2] - b[0] for b in widths)
    tray_y = top + 23.6 * scale
    step = 25.0 * scale
    tray = {}
    x = left - step
    for name in reversed(["wifi", "volume"] + (["battery"] if battery else [])):
        tray[name] = (x, tray_y)
        x -= step
    tray["app"] = (x - 9.0 * scale, tray_y)
    tray["chevron"] = (x - 39.0 * scale, tray_y)
    return dict(top=top, height=th, buttons=buttons, tray=tray, font_px=fpx,
                clock=(left, right, top + 20.0 * scale, top + 38.0 * scale))


def taskbar(w: int, h: int, scale: float, wallpaper_img: Image.Image | None = None,
            date_text: str = "10/31/2026", time_text: str = "6:42 PM", *,
            apps=tuple(TASKBAR_APPS), running=("folder",), battery: bool = True) -> Image.Image:
    """The Windows 11 taskbar as a full-frame RGBA overlay (transparent above the taskbar).

    wallpaper_img is what shows through the acrylic (normally the wallpaper; a whole composed
    desktop works too); without it the bar is plain #202020.  `apps` picks the pinned icons
    from TASKBAR_APPS, `running` which of them get the small running pill.  The bar is
    TASKBAR_DIP * scale px tall (72 px at 1.5, 36 px at 0.75); see taskbar_layout()."""
    lay = taskbar_layout(w, h, scale, date_text, time_text, apps, battery)
    top, th = lay["top"], lay["height"]

    # Acrylic: the backdrop blurred, under a dark #202020 tint at 85%.
    tint = _rgb("#202020")
    if wallpaper_img is not None:
        bg = wallpaper_img.convert("RGB")
        if bg.size != (w, h):
            bg = bg.resize((w, h), Image.LANCZOS)
        sig = 20.0 * scale
        pad = int(3 * sig) + 1
        src = np.asarray(bg, np.float32)[max(0, top - pad):] / 255.0
        strip = tint * 0.85 + _blur(src, sig)[-th:] * 0.15
    else:
        strip = np.broadcast_to(tint, (th, w, 3))
    strip = np.array(strip, np.float32)
    strip[0] = strip[0] * 0.9 + 0.1          # hairline lighter top edge (one physical pixel)

    # Centred buttons.
    for name, cx, cy in lay["buttons"]:
        if name == "start":
            layers, ext = _start_layers(), 11
        elif name == "search":
            layers, ext = _search_layers(), 9
        elif name == "taskview":
            layers, ext = _taskview_layers(), 10
        else:
            color, glyph, border = TASKBAR_APPS[name]
            layers, ext = _tile(color, glyph, border), 13
        _paint(strip, scale, cx, cy - top, ext, layers)
        if name in running:          # small grey "running" pill under the icon
            pill = (lambda X, Y: _sd_box(X, Y, 0.0, 0.0, 3.0, 1.5, 1.5), "#9b9da1", 1.0)
            _paint(strip, scale, cx, 44.5 * scale, 5, [pill])

    # Notification area.
    tray = lay["tray"]
    _paint(strip, scale, tray["chevron"][0], tray["chevron"][1] - top, 6, _chevron_layers())
    _paint(strip, scale, tray["wifi"][0], tray["wifi"][1] - top, 9, _wifi_layers())
    _paint(strip, scale, tray["volume"][0], tray["volume"][1] - top, 9, _volume_layers())
    if battery:
        _paint(strip, scale, tray["battery"][0], tray["battery"][1] - top, 9, _battery_layers())
    ipx = int(round(TRAY_ICON_DIP * scale))
    ax, ay = tray["app"]
    _paste_rgba(strip, app_icon(ipx), int(round(ax - ipx / 2)), int(round(ay - top - ipx / 2)))

    # Clock: time over date, right-aligned, white.
    left, right, b1, b2 = lay["clock"]
    f = font(lay["font_px"], "Regular", "Text")
    m = Image.new("L", (w, th), 0)
    d = ImageDraw.Draw(m)
    for text, base in ((time_text, b1), (date_text, b2)):
        bb = f.getbbox(text, anchor="ls")
        d.text((right - bb[2], base - top), text, font=f, fill=255, anchor="ls")
    _over(strip, _rgb(_WHITE), np.asarray(m, np.float32) / 255.0)

    out = np.zeros((h, w, 4), np.uint8)
    out[top:, :, :3] = _to_u8(np.clip(strip, 0, 1), 11)
    out[top:, :, 3] = 255
    return Image.fromarray(out)


# =============================================================================================
# 3. Window chrome
# =============================================================================================

def chrome_margin(scale: float) -> int:
    """Transparent margin (px) that window_chrome() leaves around the window for its shadow."""
    return int(math.ceil((3.0 * SHADOW_SIGMA_DIP + SHADOW_DY_DIP) * scale))


def window_chrome(client_rgba: Image.Image, scale: float, title: str = "Fall Desktop settings",
                  icon: Image.Image | None = None, *, active: bool = True,
                  maximize_enabled: bool = False):
    """Wrap a window's client-area image in a dark Windows 11 frame, like Snowy's settings
    window: a TITLEBAR_DIP title bar (#202020, the app icon, the title in Segoe UI Variable,
    minimise / maximise / close glyphs), WINDOW_RADIUS_DIP rounded corners, a 1 px #3b3b3b
    border and a soft drop shadow.

    client_rgba is the client area at this scale (e.g. a 1080x1191 capture at 1.5); icon
    defaults to Fall Desktop's icon.  The settings window is not resizable, so maximise is
    greyed out unless maximize_enabled.

    Returns (image, (cx, cy)): an RGBA image holding the shadowed window inside a transparent
    margin of m = chrome_margin(scale) px, and the client area's offset inside it.  The
    window's outer top-left corner is at (m, m) = (cx, cy - TITLEBAR_DIP * scale); to put the
    window at screen position (x, y), paste the image at (x - m, y - m)."""
    client = client_rgba.convert("RGBA")
    cw, ch = client.size
    tb = int(round(TITLEBAR_DIP * scale))
    ww, wh = cw, ch + tb
    m = chrome_margin(scale)
    W, H = ww + 2 * m, wh + 2 * m

    # Window shape and its shadow (black Gaussian, pushed down), fitted to Snowy's.
    X = (np.arange(W, dtype=np.float32) + 0.5)[None, :]
    Y = (np.arange(H, dtype=np.float32) + 0.5)[:, None]
    r = WINDOW_RADIUS_DIP * scale
    d = _sd_box(X, Y, m + ww / 2.0, m + wh / 2.0, ww / 2.0, wh / 2.0, r)
    cov = _cov(d)
    dy = SHADOW_DY_DIP * scale
    shadow = _blur(_cov(_sd_box(X, Y, m + ww / 2.0, m + wh / 2.0 + dy, ww / 2.0, wh / 2.0, r)),
                   SHADOW_SIGMA_DIP * scale) * (SHADOW_ALPHA if active else SHADOW_ALPHA * 0.6)

    # Window contents: title bar + client (any client transparency shows #202020).
    rgb = np.empty((H, W, 3), np.float32)
    rgb[...] = _rgb("#202020")
    ca = np.asarray(client, np.float32) / 255.0
    _over(rgb[m + tb:m + wh, m:m + ww], ca[..., :3], ca[..., 3])

    # Title bar: icon, title, caption buttons.
    fg = "#ffffff" if active else "#9a9a9a"
    ipx = int(round(16 * scale))
    ic = (icon if icon is not None else app_icon(ipx)).convert("RGBA")
    if ic.size != (ipx, ipx):
        ic = ic.resize((ipx, ipx), Image.LANCZOS)
    _paste_rgba(rgb, ic, m + int(round(12 * scale)), m + int(round(8 * scale)))
    f = font(12 * scale, "Regular", "Text")
    bb = f.getbbox(title, anchor="ls")
    tm = Image.new("L", (W, H), 0)
    ImageDraw.Draw(tm).text((m + 38 * scale - bb[0], m + 20.67 * scale), title, font=f, fill=255, anchor="ls")
    _over(rgb, _rgb(fg), np.asarray(tm, np.float32) / 255.0)

    cy = m + TITLEBAR_DIP * scale / 2.0
    sw = 1.0                                       # glyph stroke, DIP
    glyphs = [   # minimise, maximise, close: 10 DIP glyphs centred in 46 DIP buttons
        (lambda X, Y: _sd_segment(X, Y, -5.0, 0.0, 5.0, 0.0)[0] - sw / 2, fg),
        (lambda X, Y: _stroke(_sd_box(X, Y, 0.0, 0.0, 4.5, 4.5, 1.0), sw),
         fg if maximize_enabled else "#6e6e6e"),
        (lambda X, Y: np.minimum(_sd_segment(X, Y, -4.6, -4.6, 4.6, 4.6)[0],
                                 _sd_segment(X, Y, -4.6, 4.6, 4.6, -4.6)[0]) - sw / 2, fg),
    ]
    for k, (sdf, color) in enumerate(glyphs):
        from_right = CAPTION_BUTTON_DIP * (len(glyphs) - k - 0.5)
        _paint(rgb, scale, m + ww - from_right * scale, cy, 7, [(sdf, color, 1.0)])

    # 1 px border along the (rounded) window edge.
    _over(rgb, _rgb("#3b3b3b"), np.clip(cov - _cov(d + 1.0), 0.0, 1.0))

    # Composite the window over its shadow.
    alpha = cov + shadow * (1.0 - cov)
    color = rgb * (cov / np.maximum(alpha, 1e-6))[..., None]
    out = np.dstack([np.clip(color, 0, 1), np.clip(alpha, 0, 1)])
    img = Image.fromarray((out * 255.0 + 0.5).astype(np.uint8))
    return img, (m, m + tb)


# =============================================================================================
# 4. Cursor
# =============================================================================================

# The standard arrow, as at 100% scaling (DIP), tip at the origin.
# Traced from the classic 12x19 arrow bitmap (outer edge of its 1 px black border).
_ARROW = [(0.0, 0.0), (0.0, 17.0), (3.6, 13.4), (6.9, 19.0), (9.0, 19.0), (10.6, 18.2), (7.6, 12.0), (12.2, 12.0)]
CURSOR_HOTSPOT = (1, 1)        # px offset of the arrow's tip inside cursor() images


def cursor(scale: float = 1.5) -> Image.Image:
    """The standard Windows arrow pointer: white fill, black outline, faint drop shadow, in a
    CURSOR_DIP box (48 px at 1.5).  Rendered 4x4 supersampled.  The tip (hotspot) is at
    CURSOR_HOTSPOT px, so paste at (mouse_x - 1, mouse_y - 1); also in img.info['hotspot']."""
    size = int(round(CURSOR_DIP * scale))
    ss = 4
    n = size * ss
    tip = CURSOR_HOTSPOT[0]
    # Supersampled grid in DIP, relative to the tip.
    g = ((np.arange(n, dtype=np.float32) + 0.5) / ss - tip) / scale
    X, Y = g[None, :], g[:, None]
    d = _sd_polygon(X, Y, _ARROW)                         # DIP
    k = scale * ss                                         # DIP -> subsample px
    outer = _cov(d * k)
    inner = _cov((d + 1.0) * k)                            # 1 DIP black outline
    sd = _sd_polygon(X - 0.9, Y - 1.4, _ARROW)             # shadow, offset down-right
    shadow = _blur(_cov(sd * k), 1.1 * k) * 0.32

    def down(a):
        return a.reshape(size, ss, size, ss).mean(axis=(1, 3))

    outer, inner, shadow = down(outer), down(inner), down(shadow)
    alpha = outer + shadow * (1.0 - outer)
    lum = inner / np.maximum(alpha, 1e-6)                  # white inside, black outline
    out = np.dstack([lum, lum, lum, alpha])
    img = Image.fromarray((np.clip(out, 0, 1) * 255.0 + 0.5).astype(np.uint8))
    img.info["hotspot"] = CURSOR_HOTSPOT
    return img


# =============================================================================================
# 5. Captions and title cards
# =============================================================================================

def caption(text: str, w: int, h: int, scale: float, size_dip: float = CAPTION["size_dip"],
            weight: str = "Semibold", y_center: float = CAPTION["y_center"],
            subtitle: str | None = None, *, subtitle_size_dip: float | None = None,
            x_center: float | None = None, color="#ffffff", opacity: float = 1.0) -> Image.Image:
    """A trailer caption or title card as a full-frame RGBA overlay: centred white text in
    Segoe UI Variable Display (weight: Semibold by default) over a soft dark shadow, with an
    optional smaller Regular subtitle underneath, exactly like Snowy's trailer.

    Sizes and positions are DIP (px = DIP * scale).  y_center is where the middle of the title's
    capital letters goes; x_center defaults to the middle of the frame.  The presets CAPTION,
    TITLE_CARD and END_CARD reproduce Snowy's: caption(text, w, h, scale, **CAPTION).
    opacity scales the whole overlay (for fades)."""
    px = size_dip * scale
    f = font(px, weight, "Display")
    cap = -f.getbbox("H", anchor="ls")[1]
    cx = (w / 2.0) if x_center is None else x_center * scale
    base = y_center * scale + cap / 2.0
    lines = [(text, f, base)]
    if subtitle:
        spx = (subtitle_size_dip if subtitle_size_dip else 0.365 * size_dip) * scale
        lines.append((subtitle, font(spx, "Regular", "Display"), base + 0.38 * px + 0.74 * spx))
    m = Image.new("L", (w, h), 0)
    dr = ImageDraw.Draw(m)
    for s, fnt, b in lines:
        bb = fnt.getbbox(s, anchor="ls")
        dr.text((cx - (bb[0] + bb[2]) / 2.0, b), s, font=fnt, fill=255, anchor="ls")
    text_a = np.asarray(m, np.float32) / 255.0

    # Soft shadow under the text (fitted to Snowy's): a wide faint glow + a tighter drop.
    sh = np.zeros_like(text_a)
    for sigma, dy, a in _CAPTION_SHADOW:
        s_px, dy_px = sigma * px, int(round(dy * px))
        layer = _blur(text_a, s_px)
        if dy_px:
            layer = np.vstack([np.zeros((dy_px, w), np.float32), layer[:-dy_px]])
        sh = sh + a * layer * (1.0 - sh)
    alpha = text_a + sh * (1.0 - text_a)
    rgb = _rgb(color)[None, None, :] * (text_a / np.maximum(alpha, 1e-6))[..., None]
    out = np.dstack([np.clip(rgb, 0, 1), np.clip(alpha * opacity, 0, 1)])
    return Image.fromarray((out * 255.0 + 0.5).astype(np.uint8))


# (sigma, offset down, opacity) per shadow layer, as fractions of the font size; fitted to the
# darkening around the captions and title in Snowy's trailer frames.
_CAPTION_SHADOW = [(0.22, 0.07, 0.70), (0.05, 0.035, 0.30)]


# =============================================================================================
# Compositing helpers and CLI
# =============================================================================================

def paste(base: Image.Image, overlay: Image.Image, xy=(0, 0)) -> Image.Image:
    """Alpha-composite overlay onto a copy of base at integer px xy (may be partly off-frame)."""
    out = base.convert("RGBA")
    x, y = int(round(xy[0])), int(round(xy[1]))
    ox0, oy0 = max(0, -x), max(0, -y)
    ox1, oy1 = min(overlay.width, out.width - x), min(overlay.height, out.height - y)
    if ox1 > ox0 and oy1 > oy0:
        out.alpha_composite(overlay.convert("RGBA").crop((ox0, oy0, ox1, oy1)), dest=(x + ox0, y + oy0))
    return out if base.mode == "RGBA" else out.convert(base.mode)


def desktop(w: int, h: int, scale: float | None = None, *, wallpaper_img: Image.Image | None = None,
            **taskbar_args) -> Image.Image:
    """Wallpaper with the taskbar on top (RGB): the empty staged desktop."""
    if scale is None:
        scale = h / 1440.0
    wall = wallpaper_img if wallpaper_img is not None else wallpaper(w, h, scale)
    return paste(wall, taskbar(w, h, scale, wall, **taskbar_args)).convert("RGB")


def _demo(outdir: Path, settings: Path | None = None) -> None:
    """Write every kit piece to outdir for review.  settings is a client-area capture of the
    settings window at scale 1.5 (else $STOREKIT_SETTINGS, else a blank stand-in)."""
    outdir.mkdir(parents=True, exist_ok=True)
    t0 = time.time()

    def log(msg):
        print(f"[{time.time() - t0:6.1f}s] {msg}")

    wall4k = wallpaper(3840, 2160, 1.5)
    wall4k.save(outdir / "wallpaper-3840x2160.png")
    log("wallpaper-3840x2160.png")
    desk4k = desktop(3840, 2160, 1.5, wallpaper_img=wall4k)
    desk4k.save(outdir / "desktop-3840x2160.png")
    log("desktop-3840x2160.png")
    desk1k = desktop(1920, 1080, 0.75)
    desk1k.save(outdir / "desktop-1920x1080.png")
    log("desktop-1920x1080.png")

    # Taskbar close-ups at 100%: the centred icons and the notification area.
    th = int(round(TASKBAR_DIP * 1.5))
    lay = taskbar_layout(3840, 2160, 1.5)
    x0 = int(lay["buttons"][0][1]) - 60
    x1 = int(lay["buttons"][-1][1]) + 60
    strip = Image.new("RGB", (x1 - x0 + 1100, th + 60))
    strip.paste(desk4k.crop((x0, 2160 - th - 60, x1, 2160)), (0, 0))
    strip.paste(desk4k.crop((3840 - 1100, 2160 - th - 60, 3840, 2160)), (x1 - x0, 0))
    strip.save(outdir / "taskbar-closeup.png")
    log("taskbar-closeup.png")

    if settings is None and os.environ.get("STOREKIT_SETTINGS"):
        settings = Path(os.environ["STOREKIT_SETTINGS"])
    if settings is not None and settings.exists():
        client = Image.open(settings)
    else:
        client = Image.new("RGBA", (1080, 1191), (32, 32, 32, 255))
        ImageDraw.Draw(client).text((540, 595), "settings capture", font=font(24, "Regular"),
                                    fill=(140, 140, 140, 255), anchor="mm")
    chrome, (cx, cy) = window_chrome(client, 1.5)
    chrome.save(outdir / "chrome-demo.png")
    m = chrome_margin(1.5)
    paste(desk4k, chrome, (270 - m, 216 - m)).save(outdir / "window-on-desktop-3840x2160.png")
    log(f"chrome-demo.png (client at {cx},{cy}; margin {m})")

    cur = cursor(1.5)
    cur.save(outdir / "cursor.png")
    log("cursor.png")

    cap = caption("Leaves pile up along your taskbar", 1920, 1080, 0.75, **CAPTION)
    paste(desk1k, cap).save(outdir / "caption-demo.png")
    card = caption("Fall Desktop", 1920, 1080, 0.75, subtitle="Autumn for your Windows desktop", **TITLE_CARD)
    paste(desk1k, card).save(outdir / "title-demo.png")
    log("caption-demo.png, title-demo.png")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Fall Desktop store-media kit (staged Windows 11 desktop).")
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("demo", help="write every kit piece to a folder for review")
    d.add_argument("outdir", type=Path)
    d.add_argument("--settings", type=Path, help="settings window client capture (PNG) for chrome-demo")
    for name in ("wallpaper", "desktop"):
        p = sub.add_parser(name, help=f"render the {name} at a size and scale")
        p.add_argument("w", type=int)
        p.add_argument("h", type=int)
        p.add_argument("scale", type=float)
        p.add_argument("out", type=Path)
    a = ap.parse_args(argv)
    if a.cmd == "demo":
        _demo(a.outdir, a.settings)
    elif a.cmd == "wallpaper":
        wallpaper(a.w, a.h, a.scale).save(a.out)
    else:
        desktop(a.w, a.h, a.scale).save(a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
