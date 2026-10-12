#!/usr/bin/env python3
"""Fall Desktop's Microsoft Store trailer (46 s, 1920x1080, 60 fps) and its thumbnail.

Each shot is rendered by the app itself: a FALLDESKTOP_DEV_TOOLS build's --frames mode draws the
scene at 3840x2160 over storekit's staged Windows 11 desktop and saves every frame scaled to
1080p, with the pointer's path in frames.csv. This script adds the pointer and the captions,
dissolves from shot to shot and encodes with ffmpeg (H.264 High, yuv420p, a silent stereo
AAC track, as the Store asks for).

    python tools/store/make_trailer.py --exe build/media/Release/falldesktop.exe

Needs Python 3.9+, numpy, Pillow and ffmpeg on PATH.
"""

from __future__ import annotations

import argparse
import csv
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import storekit as kit  # noqa: E402

FPS = 60
W, H = 1920, 1080           # output
RW, RH = 3840, 2160         # rendered (supersampled)
DISSOLVE = 0.5              # s between shots
WARM = 30.0                 # s simulated before a shot starts: the piles are built by then
FADE = 0.4                  # s for captions to fade in and out
SUBTITLE = "Autumn for your Windows desktop"


def ground(scale):
    return RH / scale - kit.TASKBAR_DIP


# A shot: render scale, length (s, before the dissolve into the next), settings, pointer sweeps
# (DIP, seconds into the shot) and captions (text, preset, from, to; seconds into the shot).
def shots():
    g = ground(2.25)
    return [
        dict(name="intro", scale=1.5, length=11.0,
             args=["--amount", 55, "--piles", 110, "--pumpkins", 60, "--scarecrows", 55],
             captions=[("Fall Desktop", "title", 0.7, 4.6),
                       ("Autumn leaves drift across your desktop", "caption", 5.4, 11.5)]),
        dict(name="lanterns", scale=3.0, length=7.0,
             args=["--amount", 40, "--piles", 100, "--pumpkins", 75, "--scarecrows", 60],
             captions=[("Pumpkins, jack-o'-lanterns and scarecrows", "caption", 0.6, 7.5)]),
        dict(name="rake", scale=2.25, length=7.0,
             args=["--amount", 70, "--piles", 120, "--pumpkins", 55, "--scarecrows", 50],
             sweeps=[(260, 560, 520, g - 10, 0.8, 1.7),
                     (520, g - 10, 1280, g - 6, 1.7, 3.6),
                     (1280, g - 6, 1470, 430, 3.6, 4.6),
                     (1470, 430, 1500, 410, 4.6, 7.5)],
             captions=[("Rake the leaves with your mouse", "caption", 0.6, 7.5)]),
        dict(name="webs", scale=3.0, length=7.0,
             args=["--amount", 30, "--piles", 80, "--pumpkins", 40, "--scarecrows", 40],
             sweeps=[(950, 330, 1100, 170, 0.5, 1.9),
                     (1100, 170, 1235, 105, 1.9, 2.05),
                     (1235, 105, 1010, 420, 2.05, 5.0)],
             captions=[("Push through the cobwebs", "caption", 0.6, 7.5)]),
        dict(name="switch", scale=2.25, length=7.0,
             args=["--amount", 45, "--piles", 100, "--pumpkins", 75, "--scarecrows", 65],
             sets=[(2.6, "halloween", 0)],
             captions=[("Spooky or cozy: switch Halloween on or off", "caption", 0.6, 7.5)]),
        dict(name="storm", scale=1.5, length=7.0,
             args=["--amount", 100, "--wind", 55, "--gusts", 90, "--piles", 200, "--pumpkins", 100,
                   "--scarecrows", 100, "--boost", 10],
             captions=[("Fall Desktop", "end", 1.4, 99.0)]),
    ]


def desktop(work: Path, scale: float) -> Path:
    path = work / f"desktop-{RW}x{RH}@{scale:g}.png"
    if not path.exists():
        kit.desktop(RW, RH, scale).save(path)
    return path


def render(exe: Path, work: Path, shot: dict) -> subprocess.Popen:
    d = work / f"trailer-{shot['name']}"
    if d.exists():
        shutil.rmtree(d)
    d.mkdir(parents=True)
    t0 = WARM
    t1 = WARM + shot["length"] + DISSOLVE
    args = [str(exe), "--frames", str(d), "--from", f"{t0}", "--to", f"{t1}", "--fps", str(FPS),
            "--width", str(RW), "--height", str(RH), "--scale", f"{shot['scale']}",
            "--out-width", str(W), "--out-height", str(H), "--background", str(desktop(work, shot["scale"])),
            "--set", f"{t0 - 0.5},boost,1"]  # the piles build at their own pace on camera
    args += [str(a) for a in shot["args"]]
    for x0, y0, x1, y1, a, b in shot.get("sweeps", []):
        args += ["--cursor", f"{x0:.0f},{y0:.0f},{x1:.0f},{y1:.0f},{t0 + a:.3f},{t0 + b:.3f}"]
    for t, key, value in shot.get("sets", []):
        args += ["--set", f"{t0 + t:.3f},{key},{value}"]
    return subprocess.Popen(args)


def overlay(text: str, kind: str) -> np.ndarray:
    """A caption as premultiplied float RGBA."""
    s = W / 2560.0  # the 2560x1440 DIP desktop at 1080p
    if kind == "title":
        img = kit.caption(text, W, H, s, subtitle=SUBTITLE, **kit.TITLE_CARD)
    elif kind == "end":
        img = kit.caption(text, W, H, s, subtitle=SUBTITLE, **kit.END_CARD)
    else:
        img = kit.caption(text, W, H, s, **kit.CAPTION)
    a = np.asarray(img.convert("RGBA"), np.float32) / 255.0
    a[..., :3] *= a[..., 3:4]
    return a


def ramp(t, a, b):
    """0 before a, 1 between a + FADE and b - FADE, 0 after b."""
    up = np.clip((t - a) / FADE, 0.0, 1.0)
    down = np.clip((b - t) / FADE, 0.0, 1.0)
    return float(min(up, down))


def shot_frame(work: Path, shot: dict, rows: list, captions: list, cur: np.ndarray, n: int) -> np.ndarray:
    d = work / f"trailer-{shot['name']}"
    img = np.asarray(Image.open(d / f"f{n:05d}.png").convert("RGB"), np.float32) / 255.0
    t = n / FPS
    if n < len(rows):
        _, _, shown, x, y = rows[n]
        if int(shown):
            img = paste(img, cur, float(x) - 1.0, float(y) - 1.0)
    for (text, kind, a, b), ov in zip(shot["captions"], captions):
        k = ramp(t, a, b)
        if k > 0.0:
            img = img * (1.0 - k * ov[..., 3:4]) + k * ov[..., :3]
    return img


def paste(img: np.ndarray, sprite: np.ndarray, x: float, y: float) -> np.ndarray:
    x, y = int(round(x)), int(round(y))
    h, w = sprite.shape[:2]
    x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
    if x1 <= x0 or y1 <= y0:
        return img
    s = sprite[y0 - y:y1 - y, x0 - x:x1 - x]
    out = img.copy()
    out[y0:y1, x0:x1] = out[y0:y1, x0:x1] * (1.0 - s[..., 3:4]) + s[..., :3]
    return out


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=ROOT / "build/media/Release/falldesktop.exe")
    ap.add_argument("--out", type=Path, default=ROOT / "dist-store/listing/trailer")
    ap.add_argument("--work", type=Path, default=ROOT / "build/store-work")
    ap.add_argument("--reuse", action="store_true", help="keep the frames rendered last time")
    ap.add_argument("--crf", type=int, default=14)
    a = ap.parse_args(argv)
    exe, work, out = a.exe.resolve(), a.work.resolve(), a.out.resolve()
    work.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    plan = shots()

    if not a.reuse:
        print("rendering shots...")
        procs = [(s, render(exe, work, s)) for s in plan]
        for s, p in procs:
            if p.wait() != 0:
                log = work / f"trailer-{s['name']}" / "frames.log"
                print(log.read_text(encoding="utf-8-sig", errors="replace") if log.exists() else "")
                raise SystemExit(f"shot {s['name']} failed")

    # The pointer at 1080p for each shot's scale (render scale / 2).
    pointers = {}
    for s in plan:
        sc = s["scale"] * W / RW
        if sc not in pointers:
            c = np.asarray(kit.cursor(sc).convert("RGBA"), np.float32) / 255.0
            c[..., :3] *= c[..., 3:4]
            pointers[sc] = c
    rows = {}
    for s in plan:
        with open(work / f"trailer-{s['name']}" / "frames.csv", newline="") as f:
            r = list(csv.reader(f))
        rows[s["name"]] = [x for x in r if x and x[0].isdigit()]
    caps = {s["name"]: [overlay(t, k) for t, k, _, _ in s["captions"]] for s in plan}

    starts, t = [], 0.0
    for s in plan:
        starts.append(t)
        t += s["length"]
    total = int(round(t * FPS))
    mp4 = out / "trailer-1920x1080.mp4"
    ff = subprocess.Popen(
        ["ffmpeg", "-y", "-loglevel", "error",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
         "-f", "lavfi", "-i", "anullsrc=channel_layout=stereo:sample_rate=48000",
         "-map", "0:v", "-map", "1:a", "-shortest",
         "-c:v", "libx264", "-profile:v", "high", "-pix_fmt", "yuv420p", "-preset", "slow", "-crf", str(a.crf),
         "-g", str(FPS), "-c:a", "aac", "-b:a", "128k", "-movflags", "+faststart", str(mp4)],
        stdin=subprocess.PIPE)
    print(f"compositing {total} frames...")
    for i in range(total):
        tt = i / FPS
        k = max(j for j, st in enumerate(starts) if st <= tt + 1e-9)
        s = plan[k]
        n = int(round((tt - starts[k]) * FPS))
        sc = s["scale"] * W / RW
        frame = shot_frame(work, s, rows[s["name"]], caps[s["name"]], pointers[sc], n)
        # Dissolve: the previous shot's tail fades out over this one's start.
        if k > 0 and tt - starts[k] < DISSOLVE:
            p = plan[k - 1]
            pn = int(round((tt - starts[k - 1]) * FPS))
            prev = shot_frame(work, p, rows[p["name"]], caps[p["name"]], pointers[p["scale"] * W / RW], pn)
            mix = (tt - starts[k]) / DISSOLVE
            mix = mix * mix * (3.0 - 2.0 * mix)
            frame = prev * (1.0 - mix) + frame * mix
        # In from black, out to black.
        fade = min(1.0, tt / 0.6, (t - tt) / 0.8)
        frame = frame * max(0.0, fade)
        ff.stdin.write((np.clip(frame, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8).tobytes())
        if i % 300 == 0:
            print(f"  {i}/{total}")
    ff.stdin.close()
    if ff.wait() != 0:
        raise SystemExit("ffmpeg failed")
    print(f"  {mp4}")

    # Thumbnail: the opening shot with its title.
    s = plan[0]
    thumb = shot_frame(work, s, rows[s["name"]], caps[s["name"]], pointers[s["scale"] * W / RW], int(2.6 * FPS))
    Image.fromarray((np.clip(thumb, 0, 1) * 255 + 0.5).astype(np.uint8)).save(
        out / "trailer-thumbnail-1920x1080.png", optimize=True)
    print(f"  {out / 'trailer-thumbnail-1920x1080.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
