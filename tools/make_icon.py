"""Generate the Fall Desktop application/tray icons (res/falldesktop.ico,
res/falldesktop_paused.ico) and the MSIX logos (installer/msix/Assets/*.png).

Draws a sugar maple leaf turning in autumn: gold at its heart, flushing orange
and then scarlet towards the tips, with pale veins, a crimson margin and a
curved stalk. Shapes are signed distance fields evaluated with numpy on a
supersampled grid (a polygon for the blade, tapered capsules for the veins and
the stalk) and box-filtered down, at every standard icon size. Small sizes use
a simpler outline and drop the veins so they stay crisp. The paused icon is the
same leaf faded to a translucent grey.

Usage:  python tools/make_icon.py [--preview PATH]
        (PATH: a PNG sheet of every size and the paused variant, on dark and light)
Needs Python 3 with numpy and Pillow.
"""
import io
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]

# The leaf leans as if caught falling: degrees, positive turns it clockwise.
TILT = 16.0

# Autumn ramp from the heart of the leaf (0) to its tips (1), sRGB 0..1.
RAMP = [
    (0.00, (1.00, 0.84, 0.36)),  # warm gold
    (0.30, (1.00, 0.69, 0.18)),  # amber
    (0.58, (0.96, 0.45, 0.11)),  # orange
    (0.82, (0.86, 0.24, 0.10)),  # scarlet
    (1.00, (0.70, 0.13, 0.09)),  # crimson
]
VEIN = np.array([1.00, 0.86, 0.52])        # pale gold
RIM = np.array([0.60, 0.09, 0.05])         # deep crimson: reads on dark and light taskbars
STALK = np.array([0.66, 0.34, 0.13])       # russet
STALK_TIP = np.array([0.52, 0.25, 0.10])


# ---------------------------------------------------------------------------
# Shape: the blade's outline, with the base of the leaf (where the stalk joins
# and the main veins fan out) at the origin, y up and the central lobe's tip
# at (0, 1) before tilting. Each entry is (x, y, tooth?) for the right half,
# clockwise from the central tip round to the stalk; the left half mirrors it.
# ---------------------------------------------------------------------------

# Large sizes: lobes with their teeth.
OUTLINE_LARGE = [
    (0.000, 1.000, True),    # central lobe tip
    (0.080, 0.850, False),
    (0.235, 0.915, True),    # central lobe shoulder
    (0.185, 0.745, False),
    (0.315, 0.725, True),    # central lobe, lower tooth
    (0.225, 0.610, False),
    (0.205, 0.520, False),
    (0.215, 0.455, False),   # rounded sinus to the side lobe
    (0.255, 0.420, False),
    (0.315, 0.425, False),
    (0.400, 0.500, False),
    (0.505, 0.705, True),    # side lobe, upper tooth
    (0.545, 0.595, False),
    (0.700, 0.665, True),    # side lobe, second upper tooth
    (0.700, 0.560, False),
    (0.960, 0.505, True),    # side lobe tip
    (0.790, 0.400, False),
    (0.835, 0.290, True),    # side lobe, lower tooth
    (0.655, 0.260, False),
    (0.540, 0.150, False),
    (0.470, 0.070, False),   # rounded sinus to the basal lobe
    (0.480, 0.000, False),
    (0.540, -0.090, False),
    (0.650, -0.200, True),   # basal lobe tip
    (0.490, -0.215, False),
    (0.470, -0.300, True),   # basal lobe, small tooth
    (0.330, -0.240, False),
    (0.200, -0.150, False),
    (0.090, -0.080, False),
    (0.030, -0.050, False),
]

# Small sizes: the five lobes and the central lobe's shoulders only.
OUTLINE_SMALL = [
    (0.000, 1.000, True),
    (0.090, 0.840, False),
    (0.270, 0.860, True),
    (0.215, 0.640, False),
    (0.210, 0.480, False),
    (0.270, 0.420, False),
    (0.420, 0.530, False),
    (0.640, 0.680, False),
    (0.960, 0.505, True),
    (0.780, 0.330, False),
    (0.560, 0.180, False),
    (0.470, 0.050, False),
    (0.540, -0.090, False),
    (0.650, -0.200, True),
    (0.420, -0.260, False),
    (0.200, -0.150, False),
    (0.030, -0.050, False),
]

# The main veins run from the base to the lobe tips.
LOBE_TIPS = [(0.000, 1.000), (0.960, 0.505), (0.650, -0.200), (-0.960, 0.505), (-0.650, -0.200)]
MAIN_VEINS = sorted(math.degrees(math.atan2(y, x)) % 360.0 for x, y in LOBE_TIPS)


def outline(level):
    half = OUTLINE_SMALL if level == 0 else OUTLINE_LARGE
    right = list(half)
    left = [(-x, y, t) for x, y, t in reversed(half) if x != 0.0]
    pts = np.array([(x, y) for x, y, _ in right + left])
    teeth = [(x, y) for x, y, t in right + left if t]
    return pts, teeth


def polygon_sd(px, py, pts):
    """Signed distance from points (px, py) to a closed polygon (negative inside)."""
    d2 = np.full(px.shape, np.inf)
    inside = np.zeros(px.shape, bool)
    n = len(pts)
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        wx, wy = px - ax, py - ay
        h = np.clip((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0, 1.0)
        dx, dy = wx - ex * h, wy - ey * h
        d2 = np.minimum(d2, dx * dx + dy * dy)
        if ay != by:
            crosses = ((ay > py) != (by > py)) & (px < ax + ex * (py - ay) / ey)
            inside ^= crosses
    return np.where(inside, -1.0, 1.0) * np.sqrt(d2)


def tapered_sd(px, py, ax, ay, bx, by, ra, rb):
    """Distance to a segment a-b whose radius runs from ra to rb, and the position along it (0..1)."""
    pax, pay = px - ax, py - ay
    bax, bay = bx - ax, by - ay
    h = np.clip((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.0, 1.0)
    dx, dy = pax - bax * h, pay - bay * h
    return np.sqrt(dx * dx + dy * dy) - (ra + (rb - ra) * h), h


def bezier(p0, p1, p2, n):
    t = np.linspace(0.0, 1.0, n)[:, None]
    return (1 - t) ** 2 * np.array(p0) + 2 * (1 - t) * t * np.array(p1) + t * t * np.array(p2)


def ramp(t):
    t = np.clip(t, 0.0, 1.0)
    out = np.zeros(t.shape + (3,))
    for (t0, c0), (t1, c1) in zip(RAMP, RAMP[1:]):
        k = np.clip((t - t0) / (t1 - t0), 0.0, 1.0)
        k = k * k * (3 - 2 * k)
        seg = (t >= t0) & (t <= t1)
        mix = np.array(c0)[None, None, :] * (1 - k[..., None]) + np.array(c1)[None, None, :] * k[..., None]
        out = np.where(seg[..., None], mix, out)
    return out


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


# Stalk, in leaf space: from the base, curving down and away.
STALK_PATH = ((0.0, 0.02), (0.0, -0.24), (0.11, -0.43))


def layout(size):
    """Leaf space -> icon space ([-1, 1], y up): tilt, then fit with a margin."""
    pts, _ = outline(2)
    allp = np.vstack([pts, bezier(*STALK_PATH, 9)])
    a = math.radians(-TILT)
    ca, sa = math.cos(a), math.sin(a)
    rx = allp[:, 0] * ca - allp[:, 1] * sa
    ry = allp[:, 0] * sa + allp[:, 1] * ca
    margin = 0.6 / size + (0.03 if size >= 64 else 0.0)  # room for the antialiased edge
    scale = (2.0 - 2 * margin) / max(rx.max() - rx.min(), ry.max() - ry.min())
    cx, cy = (rx.max() + rx.min()) / 2, (ry.max() + ry.min()) / 2
    return ca, sa, scale, cx, cy


def angle_between(a, b):
    """Signed difference a - b in degrees, in (-180, 180]."""
    return (a - b + 180.0) % 360.0 - 180.0


def render(size, paused=False):
    ss = max(4, min(16, 1024 // size))  # supersamples per axis
    n = size * ss
    coords = (np.arange(n) + 0.5) / n * 2.0 - 1.0
    ix, iy = np.meshgrid(coords, -coords)  # icon space, y up

    ca, sa, scale, cx, cy = layout(size)
    # Icon space -> leaf space: undo the fit and the tilt.
    ux, uy = ix / scale + cx, iy / scale + cy
    px = ux * ca + uy * sa
    py = -ux * sa + uy * ca
    pixel = 2.0 / size / scale  # one output pixel in leaf space

    level = 0 if size <= 20 else (1 if size <= 48 else 2)
    pts, teeth = outline(level)
    blade = polygon_sd(px, py, pts)

    # Stalk: a tapered curve, never thinner than a pixel.
    path = bezier(*STALK_PATH, 9)
    w0 = max(0.045, 0.55 * pixel)
    w1 = max(0.028, 0.50 * pixel)
    stalk = np.full(px.shape, np.inf)
    along = np.zeros(px.shape)
    for i in range(len(path) - 1):
        f0, f1 = i / (len(path) - 1), (i + 1) / (len(path) - 1)
        d, h = tapered_sd(px, py, *path[i], *path[i + 1], w0 + (w1 - w0) * f0, w0 + (w1 - w0) * f1)
        closer = d < stalk
        stalk = np.where(closer, d, stalk)
        along = np.where(closer, f0 + (f1 - f0) * h, along)

    # The blade's colour: gold at the heart, warming towards the tips and the margin.
    r = np.hypot(px, py)
    depth = np.clip(-blade / 0.28, 0.0, 1.0)  # 0 at the margin
    t = 0.70 * np.clip(r / 0.98, 0.0, 1.0) + 0.42 * (1.0 - depth) - 0.04
    rgb = ramp(t)

    # Each lobe is creased along its vein: the side anticlockwise of a vein
    # catches the light, the other is in shade, evening out between lobes.
    ang = np.degrees(np.arctan2(py, px)) % 360.0
    veins_at = MAIN_VEINS + [MAIN_VEINS[0] + 360.0]
    u = np.zeros(px.shape)
    for v0, v1 in zip(veins_at, veins_at[1:]):
        a = np.where(ang < v0, ang + 360.0, ang)
        here = (a >= v0) & (a < v1)
        u = np.where(here, (a - v0) / (v1 - v0), u)
    crease = (1.0 - 2.0 * u) * smoothstep(0.04, 0.22, r)
    rgb = rgb * (1.0 + 0.085 * crease)[..., None]

    # Veins: the five main ones, and at large sizes the side veins to the teeth.
    veins = np.full(px.shape, np.inf)
    if level >= 1:
        wv = max(0.019 if level == 2 else 0.026, 0.42 * pixel)
        for tx, ty in LOBE_TIPS:
            reach = 0.88 if ty > 0 else 0.80
            d, _ = tapered_sd(px, py, 0.0, 0.0, tx * reach, ty * reach, wv, wv * 0.35)
            veins = np.minimum(veins, d)
        if level == 2:
            for tx, ty in teeth:
                if any(abs(tx - x) < 1e-6 and abs(ty - y) < 1e-6 for x, y in LOBE_TIPS):
                    continue  # a lobe tip: its main vein
                # The vein of the lobe this tooth belongs to.
                ta = math.degrees(math.atan2(ty, tx))
                vx, vy = min(LOBE_TIPS, key=lambda p: abs(angle_between(ta, math.degrees(math.atan2(p[1], p[0])))))
                vl = math.hypot(vx, vy)
                # Leaves the main vein a little below the tooth and runs out into it.
                along_vein = max(0.12, (tx * vx + ty * vy) / vl - 0.30)
                bx, by = vx / vl * along_vein, vy / vl * along_vein
                ex, ey = bx + (tx - bx) * 0.84, by + (ty - by) * 0.84
                d, _ = tapered_sd(px, py, bx, by, ex, ey, wv * 0.60, wv * 0.22)
                veins = np.minimum(veins, d)
    vein_cover = np.clip(0.5 - veins / pixel, 0.0, 1.0) * 0.70
    rgb = rgb * (1 - vein_cover[..., None]) + VEIN[None, None, :] * vein_cover[..., None]

    # A crimson margin keeps the leaf crisp on light and dark taskbars.
    rim = max(0.95 * pixel, 0.020)
    edge = smoothstep(-rim - 0.5 * pixel, -rim + 0.5 * pixel, blade) * 0.88
    rgb = rgb * (1 - edge[..., None]) + RIM[None, None, :] * edge[..., None]

    # Stalk colour, darkening towards its end, with a darker edge where it is
    # wide enough to have one.
    stalk_rgb = STALK[None, None, :] * (1 - along[..., None]) + STALK_TIP[None, None, :] * along[..., None]
    if w0 > 1.2 * pixel:
        stalk_edge = smoothstep(-0.9 * pixel, 0.0, stalk)
        stalk_rgb = stalk_rgb * (1 - 0.35 * stalk_edge[..., None])
    on_stalk = (stalk < blade).astype(np.float64)
    rgb = rgb * (1 - on_stalk[..., None]) + stalk_rgb * on_stalk[..., None]

    inside = (np.minimum(blade, stalk) < 0).astype(np.float64)

    if paused:
        lum = rgb @ np.array([0.30, 0.59, 0.11])
        rgb = np.repeat(lum[..., None], 3, axis=2) * 0.85 + 0.08

    # Box-filter premultiplied colour and coverage down to the icon size.
    def down(a):
        return a.reshape(size, ss, size, ss, *a.shape[2:]).mean(axis=(1, 3))

    alpha = down(inside)
    prgb = down(rgb * inside[..., None])
    if paused:
        alpha, prgb = alpha * 0.75, prgb * 0.75
    with np.errstate(invalid="ignore", divide="ignore"):
        straight = np.where(alpha[..., None] > 0, prgb / alpha[..., None], 0)
    rgba = np.concatenate([straight, alpha[..., None]], axis=2)
    return (np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8)


# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------

def save_png(path, rgba):
    Image.fromarray(rgba).save(path, optimize=True)


def png_bytes(rgba):
    buf = io.BytesIO()
    Image.fromarray(rgba).save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def dib_bytes(rgba):
    """32-bit BMP icon image (BITMAPINFOHEADER + BGRA bottom-up + AND mask)."""
    h, w, _ = rgba.shape
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    bgra = rgba[::-1, :, [2, 1, 0, 3]].tobytes()
    row_bytes = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray(row_bytes)
        for x in range(w):
            if rgba[y, x, 3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    return header + bgra + bytes(mask)


def write_ico(path, images):
    entries, blobs = [], []
    offset = 6 + 16 * len(images)
    for size, rgba in images:
        blob = png_bytes(rgba) if size >= 256 else dib_bytes(rgba)
        dim = 0 if size >= 256 else size
        entries.append(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset))
        blobs.append(blob)
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(images)))
        f.write(b"".join(entries))
        f.write(b"".join(blobs))


def msix_logos():
    """(file name, canvas px, leaf px) for every logo the MSIX manifest uses.

    Square44x44Logo is the app list / taskbar / Start icon: target sizes, in the
    plated, unplated and light-theme unplated forms (without all three Windows
    puts the icon on a plate), plus scales. Square150x150Logo is the Start tile,
    with room around the leaf. StoreLogo is shown by the Store and App Installer.
    """
    px = lambda v: int(v + 0.5)  # halves up, as Windows sizes them (StoreLogo at 125% is 63)
    logos = []
    for size in (16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 256):
        for form in ("", "_altform-unplated", "_altform-lightunplated"):
            logos.append((f"Square44x44Logo.targetsize-{size}{form}.png", size, size))
    for scale in (100, 125, 150, 200, 400):
        s = scale / 100
        logos.append((f"Square44x44Logo.scale-{scale}.png", px(44 * s), px(44 * s)))
        logos.append((f"Square150x150Logo.scale-{scale}.png", px(150 * s), px(75 * s)))
        logos.append((f"StoreLogo.scale-{scale}.png", px(50 * s), px(50 * s)))
    return logos


_cache = {}


def cached(size, paused=False):
    if (size, paused) not in _cache:
        _cache[size, paused] = render(size, paused)
    return _cache[size, paused]


def centred(canvas, leaf):
    img = np.zeros((canvas, canvas, 4), np.uint8)
    o = (canvas - leaf) // 2
    img[o:o + leaf, o:o + leaf] = cached(leaf)
    return img


def preview(path):
    """Every icon size at 1:1 and enlarged, normal and paused, on a dark and a light taskbar."""
    pad, zoom = 12, 4
    small = [s for s in SIZES if s <= 64]
    width = pad + sum(s + pad for s in SIZES) + sum(s * zoom + pad for s in small[:4])
    band = 256 + 2 * pad
    sheet = Image.new("RGBA", (width, band * 4), (0, 0, 0, 255))
    for row, (paused, bg) in enumerate([(False, (32, 32, 32)), (False, (243, 243, 243)),
                                        (True, (32, 32, 32)), (True, (243, 243, 243))]):
        y0 = row * band
        sheet.paste(Image.new("RGBA", (width, band), bg + (255,)), (0, y0))
        x = pad
        for s in SIZES:
            sheet.alpha_composite(Image.fromarray(cached(s, paused)), (x, y0 + pad))
            x += s + pad
        for s in small[:4]:
            big = Image.fromarray(cached(s, paused)).resize((s * zoom, s * zoom), Image.NEAREST)
            sheet.alpha_composite(big, (x, y0 + pad))
            x += s * zoom + pad
    sheet.save(path)
    print("wrote", path)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.join(here, "..", "res")
    os.makedirs(root, exist_ok=True)
    for name, paused in (("falldesktop.ico", False), ("falldesktop_paused.ico", True)):
        write_ico(os.path.join(root, name), [(s, cached(s, paused)) for s in SIZES])
        print("wrote", os.path.normpath(os.path.join(root, name)))
    assets = os.path.join(here, "..", "installer", "msix", "Assets")
    os.makedirs(assets, exist_ok=True)
    for name, canvas, leaf in msix_logos():
        save_png(os.path.join(assets, name), centred(canvas, leaf))
    print("wrote", len(msix_logos()), "logos to", os.path.normpath(assets))
    if "--preview" in sys.argv[1:-1]:
        preview(sys.argv[sys.argv.index("--preview") + 1])


if __name__ == "__main__":
    main()
