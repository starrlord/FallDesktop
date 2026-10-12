#!/usr/bin/env python3
"""Fall Desktop's Microsoft Store screenshots and store art.

Every image in dist-store/listing (the nine screenshots, the super hero art, the box and
poster art and the tiles) is rendered by the app itself: a FALLDESKTOP_DEV_TOOLS build's
offscreen --render and --settings modes, composited with storekit's staged Windows 11
desktop (wallpaper, taskbar, window frame, pointer).

    cmake -S . -B build/media -DFALLDESKTOP_DEV_TOOLS=ON && cmake --build build/media --config Release
    python tools/store/make_screenshots.py --exe build/media/Release/falldesktop.exe
    python tools/store/make_screenshots.py --exe ... --only 3,hero     # just some of them

Needs Python 3.9+, numpy and Pillow.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent))
import storekit as kit  # noqa: E402

W4, H4 = 3840, 2160
TASKBAR_DIP = kit.TASKBAR_DIP

# Leaf kinds (Settings.h LeafKind order) for --mask.
FLECK, BLUR, SAMARA, ACORN, ASPEN, MAPLE, OAK, BIRCH, GINKGO, SWEETGUM, WILLOW, JMAPLE = range(12)
BIG_LEAVES = sum(1 << k for k in (BLUR, MAPLE, OAK, BIRCH, GINKGO, SWEETGUM, JMAPLE))
HERO_LEAVES = BIG_LEAVES | (1 << SAMARA)


class Kit:
    def __init__(self, exe: Path, work: Path):
        self.exe = exe
        self.work = work
        work.mkdir(parents=True, exist_ok=True)

    def run(self, mode: str, out: Path, *args) -> None:
        cmd = [str(self.exe), mode, str(out), *[str(a) for a in args]]
        r = subprocess.run(cmd, capture_output=True, text=True)
        log = Path(str(out) + ".log")
        if r.returncode != 0:
            detail = log.read_text(encoding="utf-8-sig", errors="replace") if log.exists() else ""
            raise RuntimeError(f"{' '.join(cmd)} failed ({r.returncode})\n{detail}")

    def cached(self, name: str, make) -> Image.Image:
        """An intermediate image, made once per work directory."""
        path = self.work / name
        if not path.exists():
            make().save(path)
        return Image.open(path)

    def desktop(self, w: int, h: int, scale: float) -> Path:
        name = f"desktop-{w}x{h}@{scale:g}.png"
        self.cached(name, lambda: kit.desktop(w, h, scale))
        return self.work / name

    def wallpaper(self, w: int, h: int, scale: float) -> Path:
        name = f"wallpaper-{w}x{h}@{scale:g}.png"
        self.cached(name, lambda: kit.wallpaper(w, h, scale))
        return self.work / name

    def settings(self, name: str, *args) -> Image.Image:
        """The settings window's client area at 150%, dark, as the app draws it."""
        out = self.work / f"settings-{name}.png"
        self.run("--settings", out, "--ui-scale", "1.5", "--theme", "dark", *args)
        return Image.open(out).convert("RGBA")

    def render(self, name: str, w: int, h: int, scale: float, seconds: float, *args,
               background: Path | None = None, foreground: Path | None = None) -> Image.Image:
        out = self.work / f"{name}.png"
        extra = []
        if background is not None:
            extra += ["--background", background]
        if foreground is not None:
            extra += ["--foreground", foreground]
        self.run("--render", out, "--width", w, "--height", h, "--scale", scale, "--seconds", seconds,
                 *extra, *args)
        return Image.open(out).convert("RGB")


def place_window(client: Image.Image, scale: float, x: int, y: int, w: int, h: int):
    """A window frame round `client` with its outer top-left at (x, y) px, as a full-frame RGBA
    layer, and its outer rectangle in DIP (for --cover)."""
    img, _ = kit.window_chrome(client, scale)
    m = kit.chrome_margin(scale)
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    layer = kit.paste(layer, img, (x - m, y - m))
    ow, oh = img.width - 2 * m, img.height - 2 * m
    return layer, (x / scale, y / scale, ow / scale, oh / scale)


def cover_arg(rect) -> str:
    return ",".join(f"{v:.1f}" for v in rect)


def with_cursor(img: Image.Image, scale: float, x_dip: float, y_dip: float) -> Image.Image:
    cur = kit.cursor(scale)
    hx, hy = cur.info.get("hotspot", kit.CURSOR_HOTSPOT)
    return kit.paste(img, cur, (x_dip * scale - hx, y_dip * scale - hy)).convert("RGB")


def save(img: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    img.convert("RGB").save(path, optimize=True)
    print(f"  {path.relative_to(ROOT) if path.is_relative_to(ROOT) else path}")


# ---------------------------------------------------------------------------------------------
# Screenshots (3840x2160, a 2560x1440 DIP desktop at 150% unless noted)
# ---------------------------------------------------------------------------------------------

def shot_1(k: Kit, out: Path):
    img = k.render("1", W4, H4, 1.5, 42, "--amount", 55, "--piles", 110, "--pumpkins", 60,
                   "--scarecrows", 55, background=k.desktop(W4, H4, 1.5))
    save(img, out / "screenshots/1-pumpkins-and-leaf-piles.png")


def shot_2(k: Kit, out: Path):
    # Up close: the same desktop at 300%.
    img = k.render("2", W4, H4, 3.0, 40, "--amount", 40, "--piles", 100, "--pumpkins", 75,
                   "--scarecrows", 60, background=k.desktop(W4, H4, 3.0))
    save(img, out / "screenshots/2-jack-o-lanterns-up-close.png")


def shot_3(k: Kit, out: Path):
    # A web up close: the pointer has torn through the right-hand web and drifts on.
    s = 3.0
    w = W4 / s
    sweeps = ["--cursor", f"{w - 330:.0f},40,{w - 120:.0f},250,24.0,24.25",
              "--cursor", f"{w - 120:.0f},250,{w - 95:.0f},262,24.25,30"]
    t = 25.4
    img = k.render("3", W4, H4, s, t, "--amount", 30, "--piles", 80, "--pumpkins", 40, "--scarecrows", 40,
                   *sweeps, background=k.desktop(W4, H4, s))
    f = (t - 24.25) / (30 - 24.25)
    save(with_cursor(img, s, (w - 120) + f * 25, 250 + f * 12), out / "screenshots/3-cobwebs-and-spiders.png")


def shot_4(k: Kit, out: Path):
    # The pointer rakes through the pile and blows the falling leaves about.
    s = 1.5
    ground = H4 / s - TASKBAR_DIP
    y = ground - 14
    sweeps = ["--cursor", f"760,{y - 140:.0f},1000,{y:.0f},39.0,39.35",
              "--cursor", f"1000,{y:.0f},1620,{y + 4:.0f},39.35,40.3",
              "--cursor", f"1620,{y + 4:.0f},1650,{y - 20:.0f},40.3,44"]
    t = 40.55
    img = k.render("4", W4, H4, s, t, "--amount", 60, "--piles", 120, "--pumpkins", 55, "--scarecrows", 50,
                   *sweeps, background=k.desktop(W4, H4, s))
    f = (t - 40.3) / (44 - 40.3)
    save(with_cursor(img, s, 1620 + 30 * f, y + 4 - 24 * f), out / "screenshots/4-mouse-play.png")


def shot_5(k: Kit, out: Path):
    img = k.render("5", W4, H4, 2.0, 40, "--halloween", 0, "--amount", 45, "--piles", 100, "--pumpkins", 75,
                   "--scarecrows", 65, background=k.desktop(W4, H4, 2.0))
    save(img, out / "screenshots/5-halloween-off.png")


def shot_6(k: Kit, out: Path):
    # Leaves behind windows: the settings window (with that switch on) in front of the leaves.
    s = 1.5
    client = k.settings("behind", "--behind", 1)
    layer, rect = place_window(client, s, 1290, 150, W4, H4)
    taskbar = kit.taskbar(W4, H4, s, Image.open(k.wallpaper(W4, H4, s)))
    front = Image.alpha_composite(layer, taskbar)
    fg = k.work / "front-6.png"
    front.save(fg)
    img = k.render("6", W4, H4, s, 40, "--amount", 70, "--piles", 110, "--pumpkins", 55, "--scarecrows", 50,
                   "--behind", 1, "--cover", cover_arg(rect), background=k.wallpaper(W4, H4, s), foreground=fg)
    save(img, out / "screenshots/6-leaves-behind-windows.png")


def shot_7(k: Kit, out: Path):
    # Make it yours: the settings window, golden leaves falling in front of it.
    s = 1.5
    client = k.settings("golden", "--color", "golden", "--amount", 65, "--size", 60)
    layer, rect = place_window(client, s, 269, 216, W4, H4)
    bg = Image.alpha_composite(Image.open(k.desktop(W4, H4, s)).convert("RGBA"), layer)
    path = k.work / "back-7.png"
    bg.convert("RGB").save(path)
    img = k.render("7", W4, H4, s, 40, "--color", "golden", "--amount", 65, "--size", 60, "--piles", 100,
                   "--pumpkins", 55, "--scarecrows", 50, "--cover", cover_arg(rect), background=path)
    save(img, out / "screenshots/7-make-it-yours.png")


def shot_8(k: Kit, out: Path):
    # Big leaves up close.
    img = k.render("8", W4, H4, 2.5, 14, "--mask", f"{BIG_LEAVES:x}", "--size", 100, "--amount", 42,
                   "--piles", 70, "--pumpkins", 45, "--scarecrows", 40, background=k.desktop(W4, H4, 2.5))
    save(img, out / "screenshots/8-autumn-leaves.png")


def shot_9(k: Kit, out: Path):
    img = k.render("9", W4, H4, 1.5, 60, "--amount", 100, "--wind", 55, "--gusts", 90, "--piles", 200,
                   "--pumpkins", 100, "--scarecrows", 100, "--boost", 10, background=k.desktop(W4, H4, 1.5))
    save(img, out / "screenshots/9-leaf-storm.png")


# ---------------------------------------------------------------------------------------------
# Store art
# ---------------------------------------------------------------------------------------------

def scene_without_taskbar(k: Kit, name: str, w: int, h: int, scale: float, seconds: float, *args) -> Image.Image:
    """The scene with the leaf piles on the bottom edge: rendered taller, the taskbar's rows cut off."""
    tb = int(round(TASKBAR_DIP * scale))
    img = k.render(name, w, h + tb, scale, seconds, *args, background=k.wallpaper(w, h + tb, scale))
    return img.crop((0, 0, w, h))


def hero(k: Kit, out: Path):
    img = scene_without_taskbar(k, "hero", W4, H4, 3.0, 30, "--mask", f"{HERO_LEAVES:x}", "--size", 75,
                                "--amount", 30, "--piles", 110, "--pumpkins", 85, "--scarecrows", 55)
    save(img, out / "super-hero-art-3840x2160.png")


def titled(base: Image.Image, scale: float, icon_px: int, icon_cy: float, title_cy: float,
           title_dip: float, sub_dip: float) -> Image.Image:
    import make_icon
    w, h = base.size
    icon = Image.fromarray(make_icon.render(icon_px))
    img = kit.paste(base, icon, (w / 2 - icon_px / 2, icon_cy - icon_px / 2))
    cap = kit.caption("Fall Desktop", w, h, scale, size_dip=title_dip, y_center=title_cy / scale,
                      subtitle="Autumn for your Windows desktop", subtitle_size_dip=sub_dip)
    return kit.paste(img, cap).convert("RGB")


def box_and_poster(k: Kit, out: Path):
    calm = ("--amount", 16, "--size", 70, "--piles", 90, "--pumpkins", 70, "--scarecrows", -1,
            "--mask", f"{HERO_LEAVES:x}")
    box = scene_without_taskbar(k, "box", 2160, 2160, 2.25, 28, *calm)
    save(titled(box, 1.5, 760, 640, 1300, 120, 46), out / "store-logos/box-art-2160x2160.png")
    poster = scene_without_taskbar(k, "poster", 1440, 2160, 2.25, 28, *calm)
    save(titled(poster, 1.5, 620, 700, 1270, 100, 40), out / "store-logos/poster-art-1440x2160.png")


def tiles(k: Kit, out: Path):
    import make_icon
    for size, name in ((300, "app-tile-icon-300x300"), (150, "tile-150x150"), (71, "tile-71x71")):
        img = Image.fromarray(make_icon.render(size))
        path = out / f"store-logos/{name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        img.save(path, optimize=True)
        print(f"  {path.relative_to(ROOT)}")


JOBS = {"1": shot_1, "2": shot_2, "3": shot_3, "4": shot_4, "5": shot_5, "6": shot_6, "7": shot_7,
        "8": shot_8, "9": shot_9, "hero": hero, "art": box_and_poster, "tiles": tiles}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=ROOT / "build/media/Release/falldesktop.exe",
                    help="a FALLDESKTOP_DEV_TOOLS build of falldesktop.exe")
    ap.add_argument("--out", type=Path, default=ROOT / "dist-store/listing")
    ap.add_argument("--work", type=Path, default=ROOT / "build/store-work")
    ap.add_argument("--only", default="", help="comma-separated: 1..9, hero, art, tiles")
    a = ap.parse_args(argv)
    if not a.exe.exists():
        ap.error(f"{a.exe} not found (build with -DFALLDESKTOP_DEV_TOOLS=ON)")
    k = Kit(a.exe.resolve(), a.work.resolve())
    names = [n.strip() for n in a.only.split(",") if n.strip()] or list(JOBS)
    for n in names:
        print(f"[{n}]")
        JOBS[n](k, a.out.resolve())
    return 0


if __name__ == "__main__":
    sys.exit(main())
