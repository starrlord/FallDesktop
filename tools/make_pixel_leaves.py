"""Generate the pixel-art leaves (res/pixel/leaf1.png .. leaf8.png).

Eight little 15 x 15 leaves: a sugar maple, a white oak, a birch, a ginkgo
fan, an elm, a quaking aspen, a sweetgum star and a twig of beech leaves. Each is drawn below as a map of
characters (blade, veins, stalk); the tool gives it a darker one-pixel outline,
a lit edge towards the upper left and a shaded one towards the lower right.

They are drawn in greys on transparency: the app tints its leaves by
multiplying, so the greys become the leaf's autumn colour with darker veins and
outline. Pixels are fully opaque or fully clear, so they stay crisp when the
app enlarges them by whole pixels.

Usage:  python tools/make_pixel_leaves.py [--preview PATH]
        (PATH: a PNG sheet of the leaves enlarged, bare and tinted, on dark and light)
Needs Python 3 with numpy and Pillow.
"""
import os
import sys

import numpy as np
from PIL import Image

SIZE = 15

# Greys (0..255) the tint multiplies.
OUTLINE = 104
STALK = 112
VEIN = 156
SHADE = 192
FILL = 220
LIGHT = 242

# Map characters: '.' clear, 'X' blade, 'v' vein, 's' stalk (outside the blade),
# 'o' an outline pixel inside the blade (to part two lobes).
LEAVES = [
    ("maple", [
        ".......X.......",
        "......XXX......",
        "..X...XXX...X..",
        "..XX.XXvXX.XX..",
        "...XXXXvXXXX...",
        "X..XXXXvXXXX..X",
        "XXX.XvXvXvX.XXX",
        ".XXXXXvvvXXXXX.",
        "..XXXXXvXXXXX..",
        ".XXXXXXvXXXXXX.",
        "...XXX.v.XXX...",
        "....X..v..X....",
        ".......s.......",
        ".......s.......",
        "......s........",
    ]),
    ("oak", [
        "......XXX......",
        ".....XXXXX.....",
        "...XXXXvXXXX...",
        "..XXXvXvXvXXX..",
        "...XXXXvXXXX...",
        "....XXXvXXX....",
        "..XXXXXvXXXXX..",
        ".XXXXvXvXvXXXX.",
        "..XXXXXvXXXXX..",
        "....XXXvXXX....",
        "...XXXXvXXXX...",
        "...XXXvvvXXX...",
        ".....XXvXX.....",
        ".......s.......",
        ".......s.......",
    ]),
    ("birch", [
        "...........XX..",
        ".........XXXX..",
        ".......XXXXXX..",
        "......XXXXXvX..",
        ".....XXXXXvXX..",
        "....XXXXXvXXX..",
        "...XXXXXvXXX...",
        "...XXXXvXXXX...",
        "..XXXXvXXXX....",
        "..XXXvXXXX.....",
        "..XXvXXXX......",
        "..XvXXXX.......",
        "..vXXX.........",
        ".s.............",
        "s..............",
    ]),
    ("ginkgo", [
        "...............",
        ".XX.XXX.XXX.XX.",
        "XXXXXXX.XXXXXXX",
        "XvXXvXXXXXvXXvX",
        ".XvXXvXXXvXXvX.",
        "..XvXXvXvXXvX..",
        "...XvXXvXXvX...",
        "....XvXvXvX....",
        ".....XvvvX.....",
        "......XvX......",
        ".......s.......",
        ".......s.......",
        ".......s.......",
        "........s......",
        "........s......",
    ]),
    ("elm", [
        "...X...........",
        "..XXXX.........",
        "..XvXXXX.......",
        ".XXXvXXXXX.....",
        "..XXXvXXvXXX...",
        ".XXvXXvXvXXX...",
        "..XXvXXvXXXXX..",
        "..XXXvXXvXvXX..",
        "...XXXvXXvXXXX.",
        "...XXXXvXXXXX..",
        "....XXXXvXXXX..",
        ".....XXXXvXX...",
        "........XXv....",
        "...........s...",
        "............s..",
    ]),
    ("aspen", [
        ".......X.......",
        "......XXX......",
        "....XXXXXXX....",
        "...XXXXvXXXX...",
        "..XXXXXvXXXXX..",
        "..XXvXXvXXvXX..",
        ".XXXXvXvXvXXXX.",
        ".XXXXXvvvXXXXX.",
        ".XXXXXXvXXXXXX.",
        "..XXXXXvXXXXX..",
        "...XXXXvXXXX...",
        ".....XXvXX.....",
        ".......s.......",
        "......s........",
        "......s........",
    ]),
    ("sweetgum", [
        ".......X.......",
        ".......X.......",
        "......XXX......",
        "......XvX......",
        "X.....XvX.....X",
        "XXX..XXvXX..XXX",
        ".XXXXXXvXXXXXX.",
        "..XXXvXvXvXXX..",
        "...XXXvvvXXX...",
        "...XXXXvXXXX...",
        "..XXXX.v.XXXX..",
        "..XXX..s..XXX..",
        ".XX....s....XX.",
        ".X.....s.....X.",
        "...............",
    ]),
    ("beech twig", [
        ".XX............",
        ".XXX...........",
        ".XXXX..........",
        "..XvXX.........",
        "..XXvXX....XXX.",
        "...XXvX..XXXXXX",
        "....XXvsXXXXvX.",
        "......sXXXvXXX.",
        ".....s.XvXXXX..",
        ".....s.vXXX....",
        "....s..........",
        "....s..........",
        "...s...........",
        "...s...........",
        "..s............",
    ]),
]


def parse(rows):
    assert len(rows) == SIZE and all(len(r) == SIZE for r in rows), "maps are 15 x 15"
    grid = np.array([list(r) for r in rows])
    blade = np.isin(grid, ["X", "v", "o"])
    return grid, blade


def render(rows):
    """One leaf as a 15 x 15 RGBA array."""
    grid, blade = parse(rows)
    stalk = grid == "s"
    padded = np.pad(blade, 1)
    # Blade pixels next to a clear one (or the edge) form the outline.
    up, down = padded[:-2, 1:-1], padded[2:, 1:-1]
    left, right = padded[1:-1, :-2], padded[1:-1, 2:]
    outline = blade & ~(up & down & left & right) | (grid == "o")
    inner = blade & ~outline
    # Light from the upper left: the inner pixels just inside the outline catch it there
    # and fall into shade on the far side.
    pad_out = np.pad(outline, 1)
    lit = inner & (pad_out[:-2, 1:-1] | pad_out[1:-1, :-2])
    shaded = inner & ~lit & (pad_out[2:, 1:-1] | pad_out[1:-1, 2:])

    grey = np.zeros((SIZE, SIZE), np.uint8)
    grey[inner] = FILL
    grey[lit] = LIGHT
    grey[shaded] = SHADE
    grey[inner & (grid == "v")] = VEIN
    grey[outline] = OUTLINE
    grey[stalk] = STALK
    alpha = np.where(blade | stalk, 255, 0).astype(np.uint8)
    return np.dstack([grey, grey, grey, alpha])


def preview(leaves, path):
    """A sheet: each leaf enlarged 12x, bare then tinted, on dark (top) and light (bottom)."""
    scale, pad = 12, 6
    tints = [(1.0, 1.0, 1.0), (0.84, 0.23, 0.13), (0.89, 0.42, 0.12), (0.95, 0.77, 0.23), (0.48, 0.27, 0.16)]
    cell = SIZE * scale + 2 * pad
    sheet = Image.new("RGBA", (cell * len(leaves), cell * len(tints) * 2), (30, 26, 38, 255))
    light = Image.new("RGBA", (cell * len(leaves), cell * len(tints)), (233, 236, 241, 255))
    sheet.paste(light, (0, cell * len(tints)))
    for i, rgba in enumerate(leaves):
        for j, tint in enumerate(tints):
            px = rgba.astype(np.float32)
            px[..., :3] *= np.array(tint, np.float32)
            img = Image.fromarray(px.clip(0, 255).astype(np.uint8)).resize(
                (SIZE * scale, SIZE * scale), Image.NEAREST)
            for half in (0, 1):
                y = (j + half * len(tints)) * cell + pad
                sheet.alpha_composite(img, (i * cell + pad, y))
    sheet.save(path)


def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "pixel")
    leaves = [render(rows) for _, rows in LEAVES]
    for i, rgba in enumerate(leaves, 1):
        Image.fromarray(rgba).save(os.path.join(out_dir, f"leaf{i}.png"), optimize=True)
    if "--preview" in sys.argv:
        preview(leaves, sys.argv[sys.argv.index("--preview") + 1])
    print(f"wrote {len(leaves)} leaves to {os.path.normpath(out_dir)}")


if __name__ == "__main__":
    main()
