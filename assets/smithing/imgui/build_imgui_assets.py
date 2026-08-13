#!/usr/bin/env python3
"""Build deterministic ImGui-ready UI sprites for the smithing minigames.

The script writes standalone RGBA PNG files, a padded atlas, a JSON manifest,
generated C++ atlas constants, and two previews. No text is baked into runtime
assets; ImGui remains responsible for typography and localization.
"""

from __future__ import annotations

import json
import math
import random
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont


ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / "assets"
INTEGRATION = ROOT / "integration"
PREVIEW = ROOT / "preview"

PALETTE = {
    "ink": "#160f0a",
    "charred_wood": "#3a2315",
    "blackened_iron": "#25221f",
    "parchment": "#d7c198",
    "parchment_light": "#ead7ad",
    "parchment_dark": "#b79866",
    "ember": "#f07a31",
    "hot_metal": "#ffc05b",
    "steel": "#9aabb3",
    "positive": "#8fa679",
    "warning": "#c49a4c",
    "damage": "#a25745",
    "text_light": "#eadfc8",
    "muted": "#b9aa91",
}


def rgba(hex_value: str, alpha: int = 255) -> tuple[int, int, int, int]:
    value = hex_value.lstrip("#")
    return tuple(int(value[i : i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def mix(a, b, t: float):
    return tuple(round(a[i] * (1.0 - t) + b[i] * t) for i in range(4))


def rounded_mask(size: tuple[int, int], radius: int) -> Image.Image:
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius, fill=255)
    return mask


def textured_gradient(size, top, bottom, seed: int, noise: int = 7) -> Image.Image:
    w, h = size
    img = Image.new("RGBA", size)
    px = img.load()
    rng = random.Random(seed)
    for y in range(h):
        t = y / max(1, h - 1)
        base = mix(top, bottom, t)
        for x in range(w):
            n = rng.randint(-noise, noise)
            px[x, y] = (
                max(0, min(255, base[0] + n)),
                max(0, min(255, base[1] + n)),
                max(0, min(255, base[2] + n)),
                base[3],
            )
    return img


def add_rivet(draw: ImageDraw.ImageDraw, x: int, y: int, r: int = 7):
    draw.ellipse((x - r, y - r, x + r, y + r), fill=(12, 11, 10, 255), outline=(103, 93, 80, 255), width=2)
    draw.ellipse((x - r + 2, y - r + 2, x + 1, y + 1), fill=(130, 119, 103, 210))
    draw.line((x - 3, y + 1, x + 3, y - 1), fill=(25, 22, 19, 210), width=1)


def panel(size=(192, 192), kind="parchment", state="normal", rivets=True) -> Image.Image:
    w, h = size
    radius = max(6, min(w, h) // 18)
    mask = rounded_mask(size, radius)
    if kind == "parchment":
        top, bottom, seed, noise = rgba("#e3d0a8"), rgba("#b99c6a"), 121, 5
    elif kind == "wood":
        top, bottom, seed, noise = rgba("#6a4329"), rgba("#21140d"), 302, 9
    elif kind == "iron":
        top, bottom, seed, noise = rgba("#504941"), rgba("#171512"), 617, 7
    else:
        top, bottom, seed, noise = (44, 31, 23, 235), (12, 9, 7, 235), 909, 4
    if state == "hover":
        top, bottom = mix(top, rgba(PALETTE["hot_metal"]), .12), mix(bottom, rgba(PALETTE["ember"]), .08)
    elif state == "pressed":
        top, bottom = mix(top, (0, 0, 0, 255), .22), mix(bottom, (0, 0, 0, 255), .30)
    elif state == "disabled":
        top, bottom = mix(top, (77, 73, 67, 255), .48), mix(bottom, (44, 42, 39, 255), .48)

    fill = textured_gradient(size, top, bottom, seed + len(state), noise)
    if kind == "wood":
        d = ImageDraw.Draw(fill, "RGBA")
        rng = random.Random(403)
        for y in range(10, h, 14):
            wave = [(x, y + math.sin(x / 18.0 + y) * 2 + rng.uniform(-1, 1)) for x in range(0, w + 1, 8)]
            d.line(wave, fill=(17, 10, 7, 65), width=2)
        for x in range(30, w, 70):
            d.arc((x - 16, h // 2 - 18, x + 16, h // 2 + 18), 15, 345, fill=(143, 87, 47, 45), width=2)
    elif kind == "iron":
        d = ImageDraw.Draw(fill, "RGBA")
        rng = random.Random(777)
        for _ in range(max(10, w * h // 1200)):
            x, y = rng.randrange(w), rng.randrange(h)
            r = rng.randrange(1, 4)
            d.ellipse((x-r, y-r, x+r, y+r), outline=(8, 8, 7, 55), width=1)
    elif kind == "parchment":
        d = ImageDraw.Draw(fill, "RGBA")
        rng = random.Random(221)
        for _ in range(max(16, w * h // 900)):
            x, y = rng.randrange(w), rng.randrange(h)
            d.point((x, y), fill=(89, 62, 31, rng.randrange(16, 42)))

    img = Image.new("RGBA", size)
    img.paste(fill, (0, 0), mask)
    d = ImageDraw.Draw(img, "RGBA")
    outer = (22, 17, 13, 255)
    inner = (121, 91, 54, 255) if kind == "parchment" else (114, 94, 76, 230)
    d.rounded_rectangle((1, 1, w - 2, h - 2), radius, outline=outer, width=max(3, min(w, h) // 32))
    d.rounded_rectangle((7, 7, w - 8, h - 8), max(3, radius - 4), outline=inner, width=2)
    d.line((10, 10, w - 11, 10), fill=(241, 220, 173, 45), width=2)
    if rivets and w >= 60 and h >= 60:
        for x, y in ((15, 15), (w - 16, 15), (15, h - 16), (w - 16, h - 16)):
            add_rivet(d, x, y, max(4, min(w, h) // 28))
    return img


def save(img: Image.Image, rel: str, records: dict[str, Image.Image], atlas=True):
    path = ASSETS / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path, optimize=True)
    if atlas:
        records[rel.removesuffix(".png")] = img


def split_nine(source: Image.Image, rel_dir: str, border: int):
    w, h = source.size
    xs = (0, border, w - border, w)
    ys = (0, border, h - border, h)
    names_y = ("top", "middle", "bottom")
    names_x = ("left", "center", "right")
    for yi in range(3):
        for xi in range(3):
            piece = source.crop((xs[xi], ys[yi], xs[xi + 1], ys[yi + 1]))
            out = ASSETS / rel_dir / f"{names_y[yi]}_{names_x[xi]}.png"
            out.parent.mkdir(parents=True, exist_ok=True)
            piece.save(out, optimize=True)


def draw_control(size, kind, state) -> Image.Image:
    if kind == "button":
        img = panel(size, "iron", state, False)
        d = ImageDraw.Draw(img, "RGBA")
        color = rgba(PALETTE["ember"], 255 if state in ("hover", "pressed") else 115)
        if state != "disabled":
            d.line((13, size[1] - 9, size[0] - 14, size[1] - 9), fill=color, width=3)
        return img
    if kind == "tab":
        img = panel(size, "iron", state if state in ("hover", "pressed", "disabled") else "normal", False)
        d = ImageDraw.Draw(img, "RGBA")
        accent = {
            "active": rgba(PALETTE["ember"]),
            "completed": rgba(PALETTE["positive"]),
            "locked": (91, 84, 75, 160),
            "hover": rgba(PALETTE["hot_metal"]),
            "normal": (100, 90, 78, 220),
        }[state]
        d.rounded_rectangle((7, 7, size[0] - 8, size[1] - 8), 6, outline=accent, width=3)
        d.rectangle((12, size[1] - 13, size[0] - 13, size[1] - 9), fill=accent)
        return img
    raise ValueError(kind)


def chip(size, state) -> Image.Image:
    bg = {
        "normal": "#17110d", "active": "#2d1b10", "warning": "#33240f", "damage": "#321712"
    }[state]
    stroke = {
        "normal": "#705038", "active": PALETTE["ember"], "warning": PALETTE["warning"], "damage": PALETTE["damage"]
    }[state]
    img = Image.new("RGBA", size)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((1, 1, size[0] - 2, size[1] - 2), 5, fill=rgba(bg, 244), outline=rgba(stroke), width=2)
    d.line((10, 7, size[0] - 11, 7), fill=(234, 207, 163, 35), width=1)
    return img


def observation_card(size, state) -> Image.Image:
    img = Image.new("RGBA", size)
    d = ImageDraw.Draw(img)
    fill = (216, 193, 151, 210)
    color = {
        "neutral": "#846944", "positive": PALETTE["positive"], "warning": PALETTE["warning"], "damage": PALETTE["damage"]
    }[state]
    d.rounded_rectangle((1, 1, size[0]-2, size[1]-2), 4, fill=fill, outline=rgba("#846944"), width=2)
    d.rectangle((3, 3, 9, size[1]-4), fill=rgba(color))
    d.polygon(((24, size[1]//2), (34, size[1]//2-10), (44, size[1]//2), (34, size[1]//2+10)), fill=rgba(color))
    return img


def keycap(size, state) -> Image.Image:
    img = Image.new("RGBA", size)
    d = ImageDraw.Draw(img)
    top = 7 if state == "normal" else 10
    d.rounded_rectangle((2, top, size[0]-3, size[1]-3), 6, fill=(25,22,19,255), outline=(117,105,92,255), width=2)
    d.rounded_rectangle((6, top+3, size[0]-7, size[1]-8), 4, fill=(50,45,40,255), outline=(91,82,72,255), width=1)
    if state == "normal":
        d.line((8, size[1]-5, size[0]-9, size[1]-5), fill=(0,0,0,180), width=3)
    return img


def glow_sprite(size=(128, 128)):
    img = Image.new("RGBA", size)
    px = img.load()
    cx, cy = size[0] / 2, size[1] / 2
    radius = min(size) * .48
    for y in range(size[1]):
        for x in range(size[0]):
            dist = math.hypot(x - cx, y - cy) / radius
            if dist <= 1:
                a = int(150 * (1 - dist) ** 2)
                px[x, y] = rgba(PALETTE["ember"], a)
    return img.filter(ImageFilter.GaussianBlur(2))


def paste_nine_slice(dst: Image.Image, source: Image.Image, box, border=32):
    """Paste a resizable panel without scaling its corners or rivets."""
    x0, y0, x1, y1 = box
    tw, th = x1 - x0, y1 - y0
    sw, sh = source.size
    if tw < border * 2 or th < border * 2:
        raise ValueError(f"Nine-slice target {tw}x{th} is smaller than {border * 2}")
    sx = (0, border, sw - border, sw)
    sy = (0, border, sh - border, sh)
    dx = (x0, x0 + border, x1 - border, x1)
    dy = (y0, y0 + border, y1 - border, y1)
    for yi in range(3):
        for xi in range(3):
            part = source.crop((sx[xi], sy[yi], sx[xi + 1], sy[yi + 1]))
            size = (dx[xi + 1] - dx[xi], dy[yi + 1] - dy[yi])
            if part.size != size:
                part = part.resize(size, Image.Resampling.LANCZOS)
            dst.paste(part, (dx[xi], dy[yi]), part)


def phase_node(state: str, size=(64, 64)):
    img = Image.new("RGBA", size)
    d = ImageDraw.Draw(img, "RGBA")
    colors = {
        "pending": (23, 20, 17, 255), "active": rgba(PALETTE["ember"]),
        "completed": rgba("#667e56"), "warning": rgba(PALETTE["warning"]), "locked": (39, 36, 32, 185)
    }
    strokes = {
        "pending": (113, 100, 86, 255), "active": rgba("#ffd185"),
        "completed": rgba("#9db18c"), "warning": rgba("#e4c779"), "locked": (87, 81, 73, 180)
    }
    d.ellipse((9, 9, 54, 54), fill=colors[state], outline=strokes[state], width=4)
    d.ellipse((15, 15, 48, 48), outline=(245, 225, 188, 44), width=2)
    if state == "locked":
        d.rounded_rectangle((25, 29, 39, 42), 2, outline=(167, 156, 139, 200), width=2)
        d.arc((27, 20, 37, 34), 180, 360, fill=(167, 156, 139, 200), width=2)
    return img


def simple_icon(name: str, size=(64, 64)) -> Image.Image:
    img = Image.new("RGBA", size)
    d = ImageDraw.Draw(img, "RGBA")
    c = rgba("#e6c793")
    m = rgba("#9b8060")
    dark = rgba("#34251a")
    W = max(2, size[0] // 24)
    # Station icons
    if name == "forge":
        d.polygon(((15,48),(19,34),(27,26),(31,12),(39,25),(48,35),(47,49)), fill=c)
        d.polygon(((24,47),(27,37),(33,31),(39,39),(40,48)), fill=rgba(PALETTE["ember"]))
    elif name == "anvil":
        d.polygon(((8,22),(43,20),(55,27),(43,32),(39,42),(22,42),(18,33),(8,31)), fill=c)
        d.rectangle((21,42,40,48), fill=m)
    elif name == "quench":
        d.arc((9,11,55,38), 15, 165, fill=c, width=W)
        d.arc((9,24,55,51), 15, 165, fill=c, width=W)
        d.arc((9,37,55,60), 15, 165, fill=c, width=W)
    elif name == "grindstone":
        d.ellipse((11,11,53,53), outline=c, width=W+1)
        d.ellipse((26,26,38,38), outline=m, width=W)
        d.line((32,11,32,53), fill=(230,199,147,65), width=1)
    elif name == "workbench":
        d.rectangle((10,23,54,34), fill=c)
        d.polygon(((15,34),(22,34),(18,54),(12,54)), fill=m)
        d.polygon(((42,34),(49,34),(54,54),(48,54)), fill=m)
        d.line((17,18,48,41), fill=c, width=W)
    elif name == "testing":
        d.polygon(((32,8),(37,25),(54,30),(39,35),(35,54),(29,38),(11,33),(27,28)), fill=c)
    # Observation icons
    elif name in ("heat", "color"):
        d.polygon(((30,7),(38,20),(35,28),(46,37),(42,52),(31,58),(19,50),(16,39),(24,28)), fill=c)
        d.polygon(((31,28),(37,37),(35,49),(27,50),(23,43)), fill=rgba(PALETTE["ember"]))
    elif name == "sparks":
        for a, r in ((0,20),(45,18),(90,22),(135,17)):
            x=32+math.cos(math.radians(a))*r; y=32+math.sin(math.radians(a))*r
            d.line((32,32,x,y), fill=c, width=W)
        d.ellipse((27,27,37,37), fill=rgba(PALETTE["hot_metal"]))
    elif name == "sound":
        d.polygon(((12,27),(22,27),(34,17),(34,47),(22,37),(12,37)), fill=c)
        d.arc((31,20,50,44), -65, 65, fill=c, width=W)
        d.arc((31,13,59,51), -60, 60, fill=m, width=W)
    elif name == "shape":
        d.polygon(((11,42),(19,22),(40,14),(53,27),(45,48),(24,52)), outline=c)
        d.line((18,34,47,30), fill=m, width=W)
    elif name == "axis":
        d.line((9,32,55,32), fill=c, width=W)
        d.polygon(((9,32),(18,26),(18,38)), fill=c); d.polygon(((55,32),(46,26),(46,38)), fill=c)
        d.line((32,9,32,55), fill=m, width=2)
    elif name == "thickness":
        d.rectangle((12,21,52,43), outline=c, width=W)
        d.line((19,14,19,50), fill=m, width=2); d.line((45,14,45,50), fill=m, width=2)
    elif name == "surface":
        d.line([(9,38),(17,30),(25,37),(33,25),(41,34),(55,20)], fill=c, width=W)
    elif name == "steam":
        for x in (19,32,45):
            d.arc((x-8,7,x+8,54), -75, 75, fill=c, width=W)
    elif name == "vibration":
        d.line([(7,34),(15,34),(20,21),(27,46),(34,18),(41,43),(48,29),(57,29)], fill=c, width=W)
    elif name == "reflection":
        d.polygon(((9,39),(43,15),(55,27),(21,51)), fill=c)
        d.line((19,43,46,24), fill=(255,255,255,170), width=2)
    elif name == "wood":
        d.rounded_rectangle((15,7,49,57), 12, fill=c)
        d.arc((21,14,43,50), 80, 280, fill=dark, width=2)
    elif name == "fit":
        d.ellipse((10,16,39,48), outline=c, width=W)
        d.ellipse((27,16,56,48), outline=m, width=W)
    elif name == "crack":
        d.line([(35,7),(25,24),(35,30),(24,44),(30,57)], fill=c, width=W+1)
    elif name == "impact":
        d.ellipse((24,24,40,40), fill=rgba(PALETTE["hot_metal"]))
        for a in range(0,360,45):
            d.line((32+math.cos(math.radians(a))*12,32+math.sin(math.radians(a))*12,32+math.cos(math.radians(a))*25,32+math.sin(math.radians(a))*25),fill=c,width=W)
    elif name == "edge":
        d.polygon(((9,45),(49,13),(56,20),(19,53)), fill=c)
        d.line((12,48,52,16), fill=(255,255,255,180), width=2)
    elif name == "accept":
        d.line((11,34,26,49,54,15), fill=c, width=W+2, joint="curve")
    elif name == "repair":
        d.arc((11,11,53,53), 35, 305, fill=c, width=W)
        d.polygon(((9,19),(11,35),(23,25)), fill=c)
    elif name == "exit":
        d.rectangle((12,10,39,54), outline=c, width=W)
        d.line((27,32,55,32), fill=m, width=W)
        d.polygon(((55,32),(44,23),(44,41)), fill=m)
    elif name == "info":
        d.ellipse((10,10,54,54), outline=c, width=W)
        d.ellipse((29,18,35,24), fill=c); d.rectangle((29,29,35,47), fill=c)
    else:
        d.ellipse((12,12,52,52), outline=c, width=W)
    return img


@dataclass
class Packed:
    key: str
    x: int
    y: int
    w: int
    h: int


def pack_atlas(records: dict[str, Image.Image], atlas_size=(2048, 1024), pad=4):
    items = sorted(records.items(), key=lambda kv: (-kv[1].height, -kv[1].width, kv[0]))
    atlas = Image.new("RGBA", atlas_size)
    x = y = pad
    row_h = 0
    packed: list[Packed] = []
    for key, img in items:
        if x + img.width + pad > atlas_size[0]:
            x = pad
            y += row_h + pad * 2
            row_h = 0
        if y + img.height + pad > atlas_size[1]:
            raise RuntimeError(f"Atlas is too small at {key}")
        atlas.alpha_composite(img, (x, y))
        packed.append(Packed(key, x, y, img.width, img.height))
        x += img.width + pad * 2
        row_h = max(row_h, img.height)
    return atlas, packed


def preview_sheet(records: dict[str, Image.Image]):
    bg = Image.new("RGB", (1600, 1200), (18, 12, 8))
    d = ImageDraw.Draw(bg)
    font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
    bold_path = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
    font = ImageFont.truetype(font_path, 19)
    title = ImageFont.truetype(bold_path, 32)
    d.text((38, 28), "Řemeslo středověku — prvky rozhraní pro ImGui", font=title, fill=(235, 218, 188))
    d.text((40, 76), "Všechny nápisy v samotné hře kreslí ImGui; PNG obsahují jen vzhled a ikony.", font=font, fill=(189, 169, 137))

    def place(key, xy, size=None):
        im = records[key]
        if size:
            im = im.resize(size, Image.Resampling.LANCZOS)
        bg.paste(im, xy, im)

    # Panels and controls
    place("chrome/panel_parchment", (40, 125), (360, 220))
    place("chrome/panel_wood", (420, 125), (360, 220))
    place("chrome/panel_iron", (800, 125), (360, 220))
    d.text((60,145), "SVĚTLÝ PANEL", font=font, fill=(47,31,20))
    d.text((440,145), "DŘEVĚNÝ PANEL", font=font, fill=(238,215,180))
    d.text((820,145), "ŽELEZNÝ PANEL", font=font, fill=(238,215,180))
    for i,state in enumerate(("normal","hover","pressed","disabled")):
        place(f"controls/button_{state}", (40, 380+i*72))
        d.text((65,399+i*72), state.upper(), font=font, fill=(232,215,186))
    for i,state in enumerate(("normal","hover","active","completed","locked")):
        place(f"controls/tab_{state}", (420+i*126, 382))
        place("icons/stations/anvil", (444+i*126, 410))
    for i,state in enumerate(("neutral","positive","warning","damage")):
        place(f"controls/observation_{state}", (420, 545+i*93))
    # Icons
    names = [k for k in records if k.startswith("icons/")]
    for i,key in enumerate(names):
        col=i%9; row=i//9
        place(key,(40+col*86,750+row*86))
    d.text((1220,125), "STAVY FÁZE", font=font, fill=(230,210,176))
    for i,state in enumerate(("pending","active","completed","warning","locked")):
        place(f"indicators/phase_{state}",(1220,165+i*75))
        d.text((1295,183+i*75),state,font=font,fill=(210,192,164))
    for i,state in enumerate(("normal","pressed")):
        place(f"controls/keycap_{state}",(1215,575+i*70))
    place("chrome/portrait_medallion",(1215,735))
    place("indicators/target_reticle",(1370,735))
    return bg


def compose_runtime_preview(records: dict[str, Image.Image]) -> Image.Image:
    canvas = Image.new("RGB", (1920,1080), (17,12,8))
    d = ImageDraw.Draw(canvas, "RGBA")
    # dark workspace placeholder
    for y in range(1080):
        t=y/1080
        d.line((0,y,1920,y),fill=(24+int(18*(1-t)),15+int(10*(1-t)),10+int(7*(1-t)),255))
    d.rectangle((28,156,1492,831),fill=(7,5,4,185),outline=(62,42,28,255),width=5)

    paste_nine_slice(canvas,records["chrome/panel_parchment"],(28,24,418,140))
    paste_nine_slice(canvas,records["chrome/panel_wood"],(440,26,1482,130))
    paste_nine_slice(canvas,records["chrome/panel_parchment"],(1520,24,1892,829))
    paste_nine_slice(canvas,records["chrome/panel_parchment"],(28,847,908,980))
    paste_nine_slice(canvas,records["chrome/panel_wood"],(1520,847,1892,980))
    paste_nine_slice(canvas,records["chrome/panel_wood"],(28,990,1892,1062))
    d = ImageDraw.Draw(canvas, "RGBA")
    # title and sample text are preview-only
    font_path="/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"; bold_path="/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
    f16=ImageFont.truetype(font_path,16); f22=ImageFont.truetype(font_path,22); f34=ImageFont.truetype(bold_path,34)
    d.text((62,48),"ŘEMESLO STŘEDOVĚKU",font=f16,fill=(112,87,58));d.text((62,72),"KOVADLINA",font=f34,fill=(40,25,14));d.text((62,112),"Tvarování údery",font=f16,fill=(89,62,39))
    # phase nodes
    phase_states=["completed","completed","active","pending","pending","pending","pending"]
    for i,state in enumerate(phase_states):
        x=472+i*140
        place=records[f"indicators/phase_{state}"]
        canvas.paste(place,(x,38),place)
        if i<6:d.line((x+50,70,x+135,70),fill=(106,91,77,255),width=3)
    # observation cards
    d.text((1580,49),"CO PRÁVĚ POZORUJI",font=f16,fill=(112,84,51))
    for i,state in enumerate(("positive","warning","warning","positive")):
        card=records[f"controls/observation_{state}"]
        card=card.resize((306,74),Image.Resampling.LANCZOS)
        canvas.paste(card,(1550,279+i*88),card)
    # tabs
    for i,state in enumerate(("normal","active","normal","normal","normal","normal")):
        tab=records[f"controls/tab_{state}"].resize((52,82),Image.Resampling.LANCZOS)
        x=1532+i*58; canvas.paste(tab,(x,865),tab)
        icon=records[f"icons/stations/{['forge','anvil','quench','grindstone','workbench','testing'][i]}"].resize((27,27),Image.Resampling.LANCZOS)
        canvas.paste(icon,(x+12,890),icon)
    # controls
    for i,label in enumerate(("MYŠ","LMB","RMB","KOLEČKO")):
        key=records["controls/keycap_normal"].resize((120,38),Image.Resampling.LANCZOS); x=65+i*320
        canvas.paste(key,(x,1007),key);d.text((x+60,1015),label,font=f16,anchor="ma",fill=(239,218,184))
    # center visual cues
    d = ImageDraw.Draw(canvas, "RGBA")
    d.polygon(((320,565),(1040,525),(1210,575),(1050,620),(425,628)),fill=(73,68,62,255),outline=(15,14,13,255))
    d.polygon(((450,510),(1038,478),(1160,530),(1025,563),(450,576)),fill=(219,98,42,255))
    ret=records["indicators/target_reticle"]; canvas.paste(ret,(728,450),ret)
    d.text((158,885),"BENEŠ, KOVÁŘ",font=f16,fill=(111,82,51));d.text((158,918),"„Dobrá rána má směr.“",font=f22,fill=(47,30,18))
    return canvas


def make_assets():
    records: dict[str, Image.Image] = {}
    for folder in (ASSETS, INTEGRATION, PREVIEW):
        folder.mkdir(parents=True, exist_ok=True)

    # Reusable nine-slice sources and their nine standalone fragments.
    for name, kind in (("panel_parchment","parchment"),("panel_wood","wood"),("panel_iron","iron"),("panel_overlay","overlay")):
        img=panel((192,192),kind)
        save(img,f"chrome/{name}.png",records)
        split_nine(img,f"chrome/{name}_9slice",32)
    title=panel((384,112),"parchment")
    save(title,"chrome/title_plaque.png",records)
    portrait=panel((128,128),"parchment",rivets=False)
    pd=ImageDraw.Draw(portrait,"RGBA");pd.ellipse((17,17,111,111),fill=(54,37,26,255),outline=(115,84,54,255),width=5)
    pd.ellipse((28,28,100,100),outline=(227,195,144,55),width=2)
    save(portrait,"chrome/portrait_medallion.png",records)
    sep=Image.new("RGBA",(512,16));sd=ImageDraw.Draw(sep,"RGBA");sd.line((0,7,511,7),fill=(31,22,16,255),width=5);sd.line((0,5,511,5),fill=(130,99,64,210),width=1);sd.polygon(((248,1),(264,1),(272,8),(264,15),(248,15),(240,8)),fill=(53,39,29,255),outline=(141,106,66,220))
    save(sep,"chrome/separator_horizontal.png",records)

    for state in ("normal","hover","pressed","disabled"):
        save(draw_control((320,64),"button",state),f"controls/button_{state}.png",records)
    for state in ("normal","hover","active","completed","locked"):
        save(draw_control((112,128),"tab",state),f"controls/tab_{state}.png",records)
    for state in ("normal","active","warning","damage"):
        save(chip((192,48),state),f"controls/chip_{state}.png",records)
    for state in ("neutral","positive","warning","damage"):
        save(observation_card((320,84),state),f"controls/observation_{state}.png",records)
    for state in ("normal","pressed"):
        save(keycap((144,48),state),f"controls/keycap_{state}.png",records)

    station_icons=("forge","anvil","quench","grindstone","workbench","testing")
    observation_icons=("heat","color","sparks","sound","shape","axis","thickness","surface","steam","vibration","reflection","wood","fit","crack","impact","edge","accept","repair","exit","info")
    for name in station_icons:
        save(simple_icon(name),f"icons/stations/{name}.png",records)
    for name in observation_icons:
        save(simple_icon(name),f"icons/observations/{name}.png",records)

    for state in ("pending","active","completed","warning","locked"):
        save(phase_node(state),f"indicators/phase_{state}.png",records)
    for state,color in (("neutral","#6e6255"),("positive",PALETTE["positive"]),("warning",PALETTE["warning"]),("damage",PALETTE["damage"])):
        im=Image.new("RGBA",(40,40));di=ImageDraw.Draw(im,"RGBA");di.polygon(((20,3),(37,20),(20,37),(3,20)),fill=rgba(color),outline=(49,35,25,255));save(im,f"indicators/status_{state}.png",records)
    save(glow_sprite(),"indicators/focus_glow.png",records)
    ret=Image.new("RGBA",(128,128));rd=ImageDraw.Draw(ret,"RGBA");rc=rgba("#ffd17c");rd.ellipse((28,28,100,100),outline=rc,width=5);rd.ellipse((45,45,83,83),outline=(255,209,124,100),width=2);rd.line((64,12,64,116),fill=rc,width=4);rd.line((12,64,116,64),fill=rc,width=4);save(ret,"indicators/target_reticle.png",records)

    atlas, packed=pack_atlas(records)
    atlas_path=ASSETS/"atlases"/"smithing_ui_atlas_2048.png";atlas_path.parent.mkdir(parents=True,exist_ok=True);atlas.save(atlas_path,optimize=True)

    manifest={
        "schema":"remeslo-stredoveku.smithing-imgui-assets.v1",
        "atlas":{"file":"assets/atlases/smithing_ui_atlas_2048.png","size":[2048,1024],"padding":4,"filter":"linear","wrap":"clamp"},
        "reference_resolution":[1920,1080],
        "font_policy":"No text is baked into runtime sprites. Render UTF-8 Czech text with ImGui.",
        "palette":PALETTE,
        "nine_slice":{
            "panel_parchment":{"sprite":"chrome/panel_parchment","border":[32,32,32,32],"minimum_size":[96,96]},
            "panel_wood":{"sprite":"chrome/panel_wood","border":[32,32,32,32],"minimum_size":[96,96]},
            "panel_iron":{"sprite":"chrome/panel_iron","border":[32,32,32,32],"minimum_size":[96,96]},
            "panel_overlay":{"sprite":"chrome/panel_overlay","border":[32,32,32,32],"minimum_size":[96,96]},
        },
        "nine_slice_fragments":{
            "panel_parchment":"assets/chrome/panel_parchment_9slice/*.png",
            "panel_wood":"assets/chrome/panel_wood_9slice/*.png",
            "panel_iron":"assets/chrome/panel_iron_9slice/*.png",
            "panel_overlay":"assets/chrome/panel_overlay_9slice/*.png"
        },
        "recommended_sizes":{"button":[160,48],"station_tab":[52,82],"sense_chip_height":34,"observation_card_height":74,"keycap_height":38,"station_icon":[28,28],"observation_icon":[32,32]},
        "sprites":{}
    }
    for p in packed:
        manifest["sprites"][p.key]={
            "file":f"assets/{p.key}.png",
            "src":[p.x,p.y,p.w,p.h],
            "uv0":[round(p.x/2048,8),round(p.y/1024,8)],
            "uv1":[round((p.x+p.w)/2048,8),round((p.y+p.h)/1024,8)],
            "native_size":[p.w,p.h]
        }
    (INTEGRATION/"smithing_imgui_assets.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")

    # Generated C++ constants; independent of the chosen SDL/OpenGL ImGui backend.
    lines=["#pragma once","","// Generated by build_imgui_assets.py. Atlas coordinates use pixels.","namespace SmithingUiAtlas {","inline constexpr int Width = 2048;","inline constexpr int Height = 1024;","struct Rect { int x, y, w, h; };",""]
    for p in packed:
        ident=p.key.replace("/","_").replace("-","_")
        lines.append(f"inline constexpr Rect {ident}{{{p.x}, {p.y}, {p.w}, {p.h}}};")
    lines.extend(["","} // namespace SmithingUiAtlas",""])
    (INTEGRATION/"SmithingUiAtlas.generated.h").write_text("\n".join(lines),encoding="utf-8")

    preview_sheet(records).save(PREVIEW/"smithing_imgui_assets_overview.png",optimize=True)
    compose_runtime_preview(records).save(PREVIEW/"smithing_imgui_runtime_preview.jpg",quality=91,optimize=True)
    return records, packed


if __name__ == "__main__":
    records, packed = make_assets()
    print(f"Built {len(records)} runtime sprites, 36 nine-slice fragments and {len(packed)} atlas entries.")
