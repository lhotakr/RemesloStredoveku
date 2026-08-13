#!/usr/bin/env python3
"""Build deterministic ImGui Node Editor assets for the smithing skill tree.

Runtime sprites contain no text. Czech labels are used only in previews and in
the example JSON data, so the atlas remains localization-friendly.
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

ATLAS_SIZE = (2048, 2048)
PAD = 4

PALETTE = {
    "ink": "#160f0a",
    "charred_wood": "#3a2315",
    "iron": "#25221f",
    "iron_light": "#655d54",
    "parchment": "#d7c198",
    "parchment_light": "#ead7ad",
    "ember": "#f07a31",
    "hot_metal": "#ffc05b",
    "positive": "#8fa679",
    "warning": "#c49a4c",
    "damage": "#a25745",
    "text_light": "#eadfc8",
    "muted": "#9f9484",
    "locked": "#625f5b",
}

NODE_STATES = (
    "completed",
    "active",
    "available",
    "locked_near",
    "locked_far",
    "secret",
    "selected",
)

ICON_NAMES = (
    "observer", "apprentice", "journeyman", "toolmaking", "farriery",
    "building_ironwork", "master", "innovator", "workshop_master",
    "forge_safety", "fuel", "tools", "heat_color", "tongs", "bellows",
    "straighten", "grind", "nail", "staple", "hook", "repair", "quench",
    "axe", "chisel", "knife", "hoe", "horseshoe", "forge_weld",
    "heat_treatment", "material_estimate", "defect_detection",
    "temperature_control", "planning", "resources", "diagnostics", "teamwork",
)

FONT_REGULAR = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"


def rgba(value: str, alpha: int = 255) -> tuple[int, int, int, int]:
    value = value.lstrip("#")
    return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4)) + (alpha,)


def mix(a, b, t: float):
    return tuple(round(a[i] * (1.0 - t) + b[i] * t) for i in range(4))


def line(draw, points, fill, width=4):
    draw.line(points, fill=fill, width=width, joint="curve")


def texture(size, top, bottom, seed=42, noise=7):
    w, h = size
    image = Image.new("RGBA", size)
    px = image.load()
    rng = random.Random(seed)
    for y in range(h):
        t = y / max(1, h - 1)
        base = mix(top, bottom, t)
        for x in range(w):
            n = rng.randint(-noise, noise)
            px[x, y] = tuple(max(0, min(255, base[i] + n)) for i in range(3)) + (base[3],)
    return image


def soft_glow(size, color, radius_scale=.44, strength=155):
    image = Image.new("RGBA", size)
    px = image.load()
    cx, cy = size[0] / 2, size[1] / 2
    radius = min(size) * radius_scale
    for y in range(size[1]):
        for x in range(size[0]):
            dist = math.hypot(x - cx, y - cy) / radius
            if dist < 1.0:
                px[x, y] = color[:3] + (int(strength * (1.0 - dist) ** 2),)
    return image.filter(ImageFilter.GaussianBlur(5))


def node_frame(state: str, size=(360, 176)):
    w, h = size
    if state == "secret":
        image = Image.new("RGBA", size)
        fog = Image.new("RGBA", size)
        d = ImageDraw.Draw(fog, "RGBA")
        d.rounded_rectangle((22, 28, w - 23, h - 25), 24, fill=(43, 42, 40, 145))
        for i in range(11):
            y = 24 + i * 14
            d.ellipse((-30 + (i % 3) * 35, y - 22, w + 35, y + 28), fill=(122, 121, 116, 14 + i % 2 * 9))
        return fog.filter(ImageFilter.GaussianBlur(11))

    styles = {
        "completed": ("#34342a", "#171612", PALETTE["positive"], 235),
        "active": ("#4a2817", "#17100b", PALETTE["ember"], 255),
        "available": ("#3a3020", "#17130e", PALETTE["warning"], 225),
        "locked_near": ("#363431", "#181715", "#77736d", 190),
        "locked_far": ("#292826", "#131312", "#4f4d4a", 135),
        "selected": ("#56301b", "#19110c", PALETTE["hot_metal"], 255),
    }
    top_hex, bottom_hex, accent_hex, accent_alpha = styles[state]
    image = Image.new("RGBA", size)

    if state in ("active", "selected"):
        image.alpha_composite(soft_glow(size, rgba(PALETTE["ember"]), .62, 175))

    mask = Image.new("L", size)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle((8, 8, w - 9, h - 9), 18, fill=255)
    body = texture(size, rgba(top_hex), rgba(bottom_hex), 500 + len(state), 6)
    body_draw = ImageDraw.Draw(body, "RGBA")
    rng = random.Random(200 + len(state))
    for _ in range(60):
        x, y = rng.randrange(14, w - 14), rng.randrange(14, h - 14)
        body_draw.point((x, y), fill=(238, 215, 180, rng.randrange(8, 26)))
    image.paste(body, (0, 0), mask)
    d = ImageDraw.Draw(image, "RGBA")
    accent = rgba(accent_hex, accent_alpha)
    d.rounded_rectangle((8, 8, w - 9, h - 9), 18, outline=(10, 8, 7, 255), width=7)
    d.rounded_rectangle((13, 13, w - 14, h - 14), 14, outline=accent, width=3)
    d.rectangle((18, 18, 25, h - 19), fill=accent)
    d.line((33, 52, w - 24, 52), fill=accent, width=2)
    d.line((34, 56, w - 24, 56), fill=(238, 215, 180, 24), width=1)
    for x, y in ((20, 20), (w - 21, 20), (20, h - 21), (w - 21, h - 21)):
        d.ellipse((x - 5, y - 5, x + 5, y + 5), fill=(21, 18, 15, 255), outline=(118, 104, 87, 220), width=2)
        d.ellipse((x - 2, y - 2, x + 1, y + 1), fill=(196, 180, 153, 110))

    if state in ("locked_near", "locked_far"):
        fog_alpha = 42 if state == "locked_near" else 92
        fog = Image.new("RGBA", size)
        fd = ImageDraw.Draw(fog, "RGBA")
        for i in range(7):
            y = 30 + i * 21
            fd.ellipse((-30 + (i % 2) * 40, y - 26, w + 30, y + 22), fill=(160, 158, 151, fog_alpha // 3))
        image.alpha_composite(fog.filter(ImageFilter.GaussianBlur(9)))
        if state == "locked_far":
            gray = Image.new("RGBA", size, (24, 24, 23, 65))
            image.alpha_composite(gray)
    return image


def pin_sprite(state: str, size=(32, 32)):
    colors = {
        "completed": PALETTE["positive"],
        "active": PALETTE["ember"],
        "available": PALETTE["warning"],
        "locked": PALETTE["locked"],
        "secret": "#3e3d3a",
    }
    image = Image.new("RGBA", size)
    if state == "active":
        image.alpha_composite(soft_glow(size, rgba(PALETTE["ember"]), .55, 215))
    d = ImageDraw.Draw(image, "RGBA")
    c = rgba(colors[state])
    d.ellipse((4, 4, 27, 27), fill=(18, 15, 12, 255), outline=(8, 7, 6, 255), width=3)
    d.ellipse((8, 8, 23, 23), fill=c, outline=(238, 218, 183, 115), width=2)
    d.ellipse((11, 10, 15, 14), fill=(255, 246, 220, 100))
    return image


def badge(kind: str, size=(48, 48)):
    image = Image.new("RGBA", size)
    d = ImageDraw.Draw(image, "RGBA")
    d.ellipse((3, 3, 44, 44), fill=(29, 23, 18, 250), outline=rgba(PALETTE["warning"]), width=3)
    c = rgba(PALETTE["parchment_light"])
    if kind == "complete":
        line(d, ((13, 24), (21, 32), (36, 15)), rgba(PALETTE["positive"]), 6)
    elif kind == "lock":
        d.rounded_rectangle((14, 22, 34, 37), 3, fill=c)
        d.arc((16, 10, 32, 28), 180, 360, fill=c, width=5)
    elif kind == "question":
        d.arc((14, 9, 34, 28), 215, 540, fill=c, width=5)
        d.line((25, 25, 24, 31), fill=c, width=4)
        d.ellipse((22, 35, 27, 40), fill=c)
    elif kind == "choice":
        line(d, ((24, 37), (24, 22), (13, 12)), c, 4)
        line(d, ((24, 22), (36, 11)), c, 4)
        d.ellipse((9, 7, 16, 14), fill=rgba(PALETTE["ember"]))
        d.ellipse((32, 7, 39, 14), fill=rgba(PALETTE["warning"]))
    return image


def progress_sprite(kind: str, size=(256, 18)):
    image = Image.new("RGBA", size)
    d = ImageDraw.Draw(image, "RGBA")
    d.rounded_rectangle((1, 1, size[0] - 2, size[1] - 2), 8, fill=(13, 11, 9, 235), outline=(103, 91, 77, 255), width=2)
    if kind == "fill":
        d.rounded_rectangle((3, 3, size[0] - 4, size[1] - 4), 6, fill=rgba(PALETTE["ember"]))
        d.line((10, 5, size[0] - 11, 5), fill=(255, 224, 165, 95), width=2)
    elif kind == "complete_fill":
        d.rounded_rectangle((3, 3, size[0] - 4, size[1] - 4), 6, fill=rgba(PALETTE["positive"]))
    return image


def icon(name: str, size=(72, 72)):
    image = Image.new("RGBA", size)
    d = ImageDraw.Draw(image, "RGBA")
    ink = rgba(PALETTE["ink"])
    metal = rgba("#ddd1b8")
    muted = rgba("#9b8d78")
    ember = rgba(PALETTE["ember"])
    gold = rgba(PALETTE["hot_metal"])
    green = rgba(PALETTE["positive"])

    d.ellipse((4, 4, 67, 67), fill=(26, 21, 17, 230), outline=(111, 91, 68, 230), width=3)
    d.ellipse((9, 9, 62, 62), outline=(224, 199, 157, 35), width=1)

    def hammer(cx=36, cy=37, color=metal):
        d.polygon(((cx - 20, cy - 15), (cx + 6, cy - 19), (cx + 12, cy - 10), (cx - 15, cy - 4)), fill=color, outline=ink)
        line(d, ((cx - 3, cy - 7), (cx + 15, cy + 23)), rgba("#795033"), 7)

    def flame(cx=36, cy=38, scale=1.0):
        pts = [(cx, cy - int(22*scale)), (cx + int(14*scale), cy - int(3*scale)),
               (cx + int(9*scale), cy + int(16*scale)), (cx, cy + int(23*scale)),
               (cx - int(13*scale), cy + int(12*scale)), (cx - int(12*scale), cy)]
        d.polygon(pts, fill=ember, outline=ink)
        d.polygon(((cx, cy - 5), (cx + 5, cy + 6), (cx, cy + 14), (cx - 5, cy + 5)), fill=gold)

    def horseshoe(cx=36, cy=35):
        d.arc((18, 14, 54, 58), 190, 350, fill=metal, width=9)
        d.line((20, 33, 25, 56), fill=metal, width=9)
        d.line((52, 33, 47, 56), fill=metal, width=9)
        for x, y in ((23, 30), (49, 30), (26, 46), (46, 46)):
            d.ellipse((x-2, y-2, x+2, y+2), fill=ink)

    def gear(cx=36, cy=35, color=metal):
        for a in range(0, 360, 45):
            x = cx + int(math.cos(math.radians(a)) * 20)
            y = cy + int(math.sin(math.radians(a)) * 20)
            d.rectangle((x-4, y-4, x+4, y+4), fill=color)
        d.ellipse((20, 19, 52, 51), outline=color, width=7)
        d.ellipse((31, 30, 41, 40), fill=color)

    if name == "observer":
        d.ellipse((14, 24, 58, 48), outline=metal, width=5); d.ellipse((30, 29, 42, 43), fill=gold)
    elif name == "apprentice": hammer(35, 36, muted)
    elif name == "journeyman":
        hammer(30, 35); line(d, ((48, 16), (25, 57)), gold, 6)
    elif name == "toolmaking":
        gear(38, 36); hammer(27, 39, gold)
    elif name == "farriery": horseshoe()
    elif name == "building_ironwork":
        line(d, ((16, 52), (16, 19), (53, 19)), metal, 9); d.circle((53, 52), 8, fill=gold)
        line(d, ((20, 48), (52, 20)), muted, 5)
    elif name == "master":
        d.polygon(((14, 44), (20, 31), (51, 31), (59, 43), (49, 52), (23, 52)), fill=metal, outline=ink)
        d.polygon(((22, 27), (27, 15), (35, 25), (43, 14), (51, 27)), fill=gold, outline=ink)
    elif name == "innovator":
        gear(35, 38); d.polygon(((49, 8), (44, 22), (55, 22), (45, 39), (48, 26), (38, 26)), fill=gold)
    elif name == "workshop_master":
        d.polygon(((14, 26), (22, 13), (34, 25), (46, 13), (57, 27), (52, 37), (19, 37)), fill=gold, outline=ink)
        hammer(36, 43, metal)
    elif name == "forge_safety":
        d.polygon(((36, 12), (55, 20), (51, 47), (36, 59), (21, 47), (17, 20)), fill=muted, outline=metal); flame(36, 37, .55)
    elif name == "fuel":
        for x, y in ((24, 42), (38, 43), (31, 30), (47, 34)): d.polygon(((x-8,y),(x-4,y-8),(x+6,y-6),(x+9,y+3),(x,y+8)), fill=rgba("#5f554b"), outline=ink)
    elif name == "tools": hammer();
    elif name == "heat_color":
        for x, c in ((22, rgba("#7b3529")), (36, ember), (50, gold)): d.ellipse((x-8, 27, x+8, 45), fill=c)
    elif name == "tongs":
        line(d, ((20, 14), (31, 35), (23, 58)), metal, 5); line(d, ((51, 14), (40, 35), (49, 58)), metal, 5); d.ellipse((32,31,39,38), fill=gold)
    elif name == "bellows":
        d.polygon(((16, 23), (42, 17), (53, 27), (43, 48), (18, 44)), fill=rgba("#83583a"), outline=metal); line(d, ((52,31),(62,31)), metal, 6)
    elif name == "straighten":
        line(d, ((16, 46), (56, 46)), metal, 7); line(d, ((23, 21), (49, 21)), gold, 5); hammer(32, 35, muted)
    elif name == "grind":
        d.ellipse((16, 14, 55, 53), fill=muted, outline=metal, width=4); line(d, ((36,53),(36,61)), metal, 5); d.arc((10,8,60,58), 20, 100, fill=gold, width=3)
    elif name == "nail": line(d, ((37, 17), (34, 56)), metal, 7); line(d, ((27,17),(46,17)), metal, 6)
    elif name == "staple":
        d.arc((18, 13, 54, 43), 180, 360, fill=metal, width=7); line(d, ((18,28),(20,56)), metal, 7); line(d, ((54,28),(52,56)), metal, 7)
    elif name == "hook": d.arc((17, 17, 51, 56), 280, 110, fill=metal, width=7); line(d, ((47,16),(47,35)), metal, 7)
    elif name == "repair":
        hammer(31, 36, metal); d.ellipse((43,36,60,53), fill=green, outline=ink); line(d, ((51,39),(51,50)), rgba("#f5ead2"), 3); line(d, ((46,45),(56,45)), rgba("#f5ead2"), 3)
    elif name == "quench":
        d.polygon(((17, 29), (55, 29), (50, 57), (22, 57)), fill=rgba("#607784"), outline=metal); line(d, ((36,12),(36,40)), gold, 6)
    elif name == "axe":
        line(d, ((44,14),(26,59)), rgba("#795033"), 7); d.polygon(((22,16),(43,13),(51,25),(31,34)), fill=metal, outline=ink)
    elif name == "chisel":
        d.polygon(((31,12),(43,12),(41,52),(36,61),(31,52)), fill=metal, outline=ink)
    elif name == "knife":
        d.polygon(((15,27),(50,17),(57,23),(29,37)), fill=metal, outline=ink); line(d, ((28,36),(20,54)), rgba("#795033"), 8)
    elif name == "hoe":
        line(d, ((44,12),(27,59)), rgba("#795033"), 7); d.polygon(((23,18),(50,13),(52,23),(29,31)), fill=metal, outline=ink)
    elif name == "horseshoe": horseshoe()
    elif name == "forge_weld":
        d.ellipse((12,22,40,50), outline=metal, width=7); d.ellipse((32,22,60,50), outline=gold, width=7); flame(36, 22, .4)
    elif name == "heat_treatment":
        line(d, ((28,15),(28,46)), metal, 7); d.ellipse((18,41,38,61), fill=ember, outline=metal); line(d, ((46,18),(55,53)), gold, 5)
    elif name == "material_estimate":
        for i, c in enumerate((muted, metal, gold)): d.rectangle((15+i*13, 22+i*5, 28+i*13, 52), fill=c, outline=ink)
    elif name == "defect_detection":
        d.rectangle((15,20,53,51), fill=muted, outline=metal); line(d, ((39,20),(31,31),(40,37),(28,51)), ink, 4)
    elif name == "temperature_control":
        line(d, ((28,13),(28,48)), metal, 7); d.ellipse((18,42,38,62), fill=ember, outline=metal); d.arc((39,15,60,46), 270, 90, fill=gold, width=5)
    elif name == "planning":
        d.rounded_rectangle((17,14,54,57), 5, fill=rgba(PALETTE["parchment"]), outline=metal, width=3)
        for y in (25,35,45): line(d, ((24,y),(47,y)), ink, 2)
    elif name == "resources":
        for x, y in ((22,44),(37,44),(30,31),(48,34)): d.polygon(((x-8,y),(x-3,y-8),(x+7,y-5),(x+9,y+4),(x,y+8)), fill=muted, outline=ink)
    elif name == "diagnostics":
        d.ellipse((14,13,46,45), outline=metal, width=6); line(d, ((43,42),(57,57)), gold, 7); line(d, ((26,18),(34,27),(26,39)), ember, 3)
    elif name == "teamwork":
        d.ellipse((15,15,30,30), fill=gold); d.ellipse((42,15,57,30), fill=green); line(d, ((22,34),(35,53),(50,34)), metal, 7)
    return image


def fog_overlay(kind: str, size=(360, 176)):
    image = Image.new("RGBA", size)
    d = ImageDraw.Draw(image, "RGBA")
    alpha = 36 if kind == "near" else 78
    for i in range(9):
        y = 10 + i * 21
        d.ellipse((-60 + (i % 3) * 38, y - 28, size[0] + 55, y + 30), fill=(174, 172, 166, alpha))
    return image.filter(ImageFilter.GaussianBlur(13))


@dataclass
class Packed:
    key: str
    x: int
    y: int
    w: int
    h: int


def save_sprite(records, key, image):
    path = ASSETS / f"{key}.png"
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, optimize=True)
    records[key] = image


def pack_atlas(records):
    atlas = Image.new("RGBA", ATLAS_SIZE)
    packed = []
    x = y = PAD
    row_h = 0
    for key, image in sorted(records.items(), key=lambda kv: (-kv[1].height, -kv[1].width, kv[0])):
        if x + image.width + PAD > ATLAS_SIZE[0]:
            x = PAD
            y += row_h + PAD
            row_h = 0
        if y + image.height + PAD > ATLAS_SIZE[1]:
            raise RuntimeError(f"Atlas overflow while packing {key}")
        atlas.alpha_composite(image, (x, y))
        packed.append(Packed(key, x, y, image.width, image.height))
        x += image.width + PAD
        row_h = max(row_h, image.height)
    return atlas, packed


def bezier(draw, a, b, color, width=6):
    c1 = (a[0] + (b[0] - a[0]) * .42, a[1])
    c2 = (a[0] + (b[0] - a[0]) * .58, b[1])
    pts = []
    for i in range(41):
        t = i / 40
        u = 1 - t
        x = u**3*a[0] + 3*u*u*t*c1[0] + 3*u*t*t*c2[0] + t**3*b[0]
        y = u**3*a[1] + 3*u*u*t*c1[1] + 3*u*t*t*c2[1] + t**3*b[1]
        pts.append((x, y))
    draw.line(pts, fill=color, width=width, joint="curve")


def fit_text(draw, xy, text, font, fill, max_width, anchor=None):
    if draw.textbbox((0, 0), text, font=font)[2] <= max_width:
        draw.text(xy, text, font=font, fill=fill, anchor=anchor)
        return
    while text and draw.textbbox((0, 0), text + "…", font=font)[2] > max_width:
        text = text[:-1]
    draw.text(xy, text + "…", font=font, fill=fill, anchor=anchor)


def draw_preview_node(canvas, records, box, state, icon_name, title, subtitle, progress=None, badge_name=None):
    x, y, w, h = box
    frame = records[f"nodes/{state}"].resize((w, h), Image.Resampling.LANCZOS)
    canvas.alpha_composite(frame, (x, y))
    if state == "secret":
        b = records["badges/question"].resize((38, 38), Image.Resampling.LANCZOS)
        canvas.alpha_composite(b, (x + w//2 - 19, y + h//2 - 19))
        return
    icon_img = records[f"icons/{icon_name}"].resize((48, 48), Image.Resampling.LANCZOS)
    canvas.alpha_composite(icon_img, (x + 13, y + 31))
    d = ImageDraw.Draw(canvas, "RGBA")
    fb = ImageFont.truetype(FONT_BOLD, 16)
    fr = ImageFont.truetype(FONT_REGULAR, 12)
    title_color = rgba(PALETTE["text_light"]) if "locked" not in state else rgba("#aaa59c")
    fit_text(d, (x+14, y+13), title.upper(), fb, title_color, w-42)
    fit_text(d, (x+69, y+39), subtitle, fr, rgba(PALETTE["muted"]), w-78)
    if progress is not None:
        d.rounded_rectangle((x+69, y+64, x+w-16, y+73), 4, fill=(11,9,8,230), outline=(91,77,63,220), width=1)
        fill_w = int((w-87) * progress)
        if fill_w > 2:
            color = rgba(PALETTE["positive"] if progress >= .999 else PALETTE["ember"])
            d.rounded_rectangle((x+70, y+65, x+70+fill_w, y+72), 3, fill=color)
    if badge_name:
        b = records[f"badges/{badge_name}"].resize((30, 30), Image.Resampling.LANCZOS)
        canvas.alpha_composite(b, (x+w-36, y+6))


def compose_mockup(records):
    canvas = Image.new("RGBA", (1920, 1080), (19, 13, 9, 255))
    d = ImageDraw.Draw(canvas, "RGBA")
    for y in range(1080):
        t = y / 1080
        d.line((0, y, 1920, y), fill=(29+int(13*(1-t)), 20+int(8*(1-t)), 14+int(5*(1-t)), 255))
    # editor canvas and grid
    d.rounded_rectangle((24, 88, 1472, 1048), 12, fill=(12,10,8,235), outline=(88,62,41,255), width=3)
    for x in range(44, 1472, 48): d.line((x, 90, x, 1047), fill=(116,91,68,18), width=1)
    for y in range(108, 1048, 48): d.line((25, y, 1471, y), fill=(116,91,68,18), width=1)
    d.rounded_rectangle((1490, 88, 1896, 1048), 12, fill=(45,31,21,248), outline=(139,100,61,255), width=3)

    title = ImageFont.truetype(FONT_BOLD, 30)
    small = ImageFont.truetype(FONT_REGULAR, 14)
    tiny = ImageFont.truetype(FONT_REGULAR, 12)
    section = ImageFont.truetype(FONT_BOLD, 17)
    d.text((38, 28), "KOVÁŘSTVÍ — POSTUP UČENÍ", font=title, fill=rgba(PALETTE["text_light"]))
    d.text((1112, 44), "46 %  •  TOVARYŠ", font=small, fill=rgba(PALETTE["ember"]))
    d.text((1540, 31), "DETAIL DOVEDNOSTI", font=section, fill=rgba(PALETTE["parchment_light"]))

    boxes = {
        "observer": (52, 445, 182, 94),
        "apprentice": (266, 445, 182, 94),
        "journeyman": (480, 445, 182, 94),
        "toolmaking": (710, 255, 192, 94),
        "farriery": (710, 445, 192, 94),
        "building": (710, 635, 192, 94),
        "master": (956, 445, 192, 94),
        "innovator": (1210, 350, 210, 94),
        "workshop": (1210, 540, 210, 94),
    }
    def left(b): return (b[0], b[1]+b[3]//2)
    def right(b): return (b[0]+b[2], b[1]+b[3]//2)
    # link shadows then links
    links = [
        ("observer","apprentice","completed"), ("apprentice","journeyman","active"),
        ("journeyman","toolmaking","locked"), ("journeyman","farriery","locked"),
        ("journeyman","building","locked"), ("toolmaking","master","locked"),
        ("farriery","master","locked"), ("building","master","locked"),
        ("master","innovator","secret"), ("master","workshop","secret"),
    ]
    link_colors = {"completed": rgba(PALETTE["positive"]), "active": rgba(PALETTE["ember"]), "locked": (94,90,85,185), "secret": (64,62,59,105)}
    for a,b,state in links: bezier(d, right(boxes[a]), left(boxes[b]), (5,4,3,220), 10)
    for a,b,state in links: bezier(d, right(boxes[a]), left(boxes[b]), link_colors[state], 4 if state != "active" else 6)

    draw_preview_node(canvas, records, boxes["observer"], "completed", "observer", "Pozorovatel", "Základy výhně", 1.0, "complete")
    draw_preview_node(canvas, records, boxes["apprentice"], "completed", "apprentice", "Učeň", "První výrobky", 1.0, "complete")
    draw_preview_node(canvas, records, boxes["journeyman"], "active", "journeyman", "Tovaryš", "Samostatná práce", .46)
    draw_preview_node(canvas, records, boxes["toolmaking"], "locked_near", "toolmaking", "Nástrojařství", "Volba specializace", None, "lock")
    draw_preview_node(canvas, records, boxes["farriery"], "locked_near", "farriery", "Podkovářství", "Volba specializace", None, "lock")
    draw_preview_node(canvas, records, boxes["building"], "locked_near", "building_ironwork", "Stavební kování", "Volba specializace", None, "lock")
    draw_preview_node(canvas, records, boxes["master"], "locked_far", "master", "Mistr", "Vedení dílny", None, "lock")
    draw_preview_node(canvas, records, boxes["innovator"], "secret", "innovator", "", "")
    draw_preview_node(canvas, records, boxes["workshop"], "secret", "workshop_master", "", "")

    # pins
    state_for = {"observer":"completed","apprentice":"completed","journeyman":"active","toolmaking":"locked","farriery":"locked","building":"locked","master":"locked","innovator":"secret","workshop":"secret"}
    for key, b in boxes.items():
        p = records[f"pins/{state_for[key]}"].resize((20,20),Image.Resampling.LANCZOS)
        if key != "observer": canvas.alpha_composite(p, (b[0]-10,b[1]+b[3]//2-10))
        if key not in ("innovator","workshop"): canvas.alpha_composite(p, (b[0]+b[2]-10,b[1]+b[3]//2-10))

    # node editor overlay controls
    d.rounded_rectangle((42, 108, 342, 145), 7, fill=(32,25,19,235), outline=(96,75,56,220), width=2)
    d.text((58, 118), "Kolečko: přiblížení   •   LMB: posun", font=tiny, fill=rgba(PALETTE["muted"]))
    d.rounded_rectangle((1280, 995, 1448, 1028), 6, fill=(33,25,19,230), outline=(91,70,51,220))
    d.text((1298,1004), "PŘEHLED STROMU", font=tiny, fill=rgba(PALETTE["parchment_light"]))

    # detail panel outside node editor
    x0 = 1520
    d.text((x0, 116), "TOVARYŠ", font=title, fill=rgba(PALETTE["ember"]))
    d.text((x0, 156), "Samostatná práce u výhně", font=small, fill=rgba(PALETTE["muted"]))
    icon_img = records["icons/journeyman"].resize((92,92),Image.Resampling.LANCZOS)
    canvas.alpha_composite(icon_img,(x0,188))
    d.text((1628,202), "Postup", font=small, fill=rgba(PALETTE["parchment_light"]))
    d.rounded_rectangle((1628,228,1860,245),8,fill=(13,10,8,255),outline=(103,82,61,255),width=2)
    d.rounded_rectangle((1631,231,1735,242),5,fill=rgba(PALETTE["ember"]))
    d.text((1628,254), "46 % — důkazy z reálné práce", font=tiny, fill=rgba(PALETTE["muted"]))
    d.line((x0,302,1862,302),fill=(113,83,54,220),width=2)
    d.text((x0,326), "CO SE UČÍŠ", font=section, fill=rgba(PALETTE["parchment_light"]))
    lessons = [
        ("axe","Sekery, dláta, nože, motyky a podkovy"),
        ("forge_weld","Svařování ohněm"),
        ("heat_treatment","Tepelné zpracování"),
        ("material_estimate","Odhad materiálu"),
    ]
    y = 366
    for icon_name, label in lessons:
        ii=records[f"icons/{icon_name}"].resize((42,42),Image.Resampling.LANCZOS);canvas.alpha_composite(ii,(x0,y-9))
        fit_text(d,(x0+54,y),label,small,rgba(PALETTE["text_light"]),278)
        y += 64
    d.line((x0,626,1862,626),fill=(113,83,54,220),width=2)
    d.text((x0,650), "PODMÍNKA POSTUPU", font=section, fill=rgba(PALETTE["parchment_light"]))
    wrapped = ["Prokaž správný postup při výrobě", "nebo opravě tří různých nástrojů.", "Body se nekupují — dovednost potvrzuje", "pozorování mistra a výsledek práce."]
    for i,text in enumerate(wrapped): d.text((x0,688+i*25),text,font=small,fill=rgba(PALETTE["muted"]))
    d.rounded_rectangle((x0,826,1862,884),7,fill=(31,22,16,245),outline=rgba(PALETTE["warning"]),width=2)
    d.text((x0+18,845),"DALŠÍ: VYBER SPECIALIZACI",font=section,fill=rgba(PALETTE["warning"]))
    d.text((x0,920),"Uzly nelze přesouvat. Plátno lze",font=small,fill=rgba(PALETTE["muted"]))
    d.text((x0,945),"posouvat, přibližovat a centrovat.",font=small,fill=rgba(PALETTE["muted"]))
    d.text((x0,994),"Detail zůstává mimo node canvas.",font=tiny,fill=rgba(PALETTE["positive"]))
    return canvas.convert("RGB")


def compose_overview(records):
    canvas=Image.new("RGBA",(1600,1000),(24,17,12,255));d=ImageDraw.Draw(canvas,"RGBA")
    title=ImageFont.truetype(FONT_BOLD,28); small=ImageFont.truetype(FONT_REGULAR,14); label=ImageFont.truetype(FONT_BOLD,14)
    d.text((38,28),"KOVÁŘSTVÍ — ASSETY STROMU DOVEDNOSTÍ",font=title,fill=rgba(PALETTE["text_light"]))
    d.text((40,76),"Stavy uzlů",font=label,fill=rgba(PALETTE["warning"]))
    state_labels=("HOTOVO","AKTIVNÍ","DOSTUPNÉ","BLÍZKÝ ZÁMEK","VZDÁLENÝ","TAJNÉ")
    states=("completed","active","available","locked_near","locked_far","secret")
    for i,(state,txt) in enumerate(zip(states,state_labels)):
        x=38+(i%3)*510;y=110+(i//3)*206
        frame=records[f"nodes/{state}"].resize((450,150),Image.Resampling.LANCZOS);canvas.alpha_composite(frame,(x,y))
        if state != "secret":
            ic=records[f"icons/{('observer','journeyman','toolmaking','farriery','master')[min(i,4)]}"].resize((60,60),Image.Resampling.LANCZOS);canvas.alpha_composite(ic,(x+20,y+56))
            d.text((x+22,y+18),txt,font=label,fill=rgba(PALETTE["text_light"]))
        else:
            q=records["badges/question"].resize((48,48),Image.Resampling.LANCZOS);canvas.alpha_composite(q,(x+201,y+51))
    d.text((40,530),"Ikony uzlů a milníků",font=label,fill=rgba(PALETTE["warning"]))
    for i,name in enumerate(ICON_NAMES):
        col=i%18;row=i//18;x=38+col*84;y=570+row*110
        canvas.alpha_composite(records[f"icons/{name}"],(x,y))
        text=name.replace("_"," ")
        if len(text)>11:text=text[:10]+"…"
        d.text((x+36,y+78),text,font=small,anchor="ma",fill=rgba(PALETTE["muted"]))
    d.text((40,822),"Piny, odznaky a průběh",font=label,fill=rgba(PALETTE["warning"]))
    x=40
    for state in ("completed","active","available","locked","secret"):
        canvas.alpha_composite(records[f"pins/{state}"].resize((48,48),Image.Resampling.LANCZOS),(x,860));d.text((x+24,918),state,font=small,anchor="ma",fill=rgba(PALETTE["muted"]));x+=120
    for kind in ("complete","lock","question","choice"):
        canvas.alpha_composite(records[f"badges/{kind}"],(x,860));d.text((x+24,918),kind,font=small,anchor="ma",fill=rgba(PALETTE["muted"]));x+=120
    canvas.alpha_composite(records["progress/track"],(1130,872));canvas.alpha_composite(records["progress/fill"].crop((0,0,118,18)),(1130,872));d.text((1258,918),"progress",font=small,anchor="ma",fill=rgba(PALETTE["muted"]))
    return canvas.convert("RGB")


def write_tree_json():
    nodes = [
        {"id":100,"key":"observer","input_pin":0,"output_pin":1001,"position":[0,380],"state":"completed","title_key":"skill.smithing.observer","title_cs":"Pozorovatel","rank":"observer","icon":"icons/observer","progress":1.0,"lessons":["forge_safety","fuel","tools","heat_color","tongs","bellows"],"proof_key":"proof.smithing.observer"},
        {"id":110,"key":"apprentice","input_pin":1100,"output_pin":1101,"position":[330,380],"state":"completed","title_key":"skill.smithing.apprentice","title_cs":"Učeň","rank":"apprentice","icon":"icons/apprentice","progress":1.0,"lessons":["straighten","grind","nail","staple","hook","repair","quench"],"proof_key":"proof.smithing.apprentice"},
        {"id":120,"key":"journeyman","input_pin":1200,"output_pin":1201,"position":[660,380],"state":"active","title_key":"skill.smithing.journeyman","title_cs":"Tovaryš","rank":"journeyman","icon":"icons/journeyman","progress":0.46,"lessons":["axe","chisel","knife","hoe","horseshoe","forge_weld","heat_treatment","material_estimate"],"proof_key":"proof.smithing.journeyman"},
        {"id":131,"key":"toolmaking","input_pin":1310,"output_pin":1311,"position":[1020,140],"state":"locked_near","title_key":"skill.smithing.specialist.toolmaking","title_cs":"Nástrojařství","rank":"specialist","icon":"icons/toolmaking","progress":0.0,"lessons":["defect_detection","temperature_control"],"proof_key":"proof.smithing.specialist"},
        {"id":132,"key":"farriery","input_pin":1320,"output_pin":1321,"position":[1020,380],"state":"locked_near","title_key":"skill.smithing.specialist.farriery","title_cs":"Podkovářství","rank":"specialist","icon":"icons/farriery","progress":0.0,"lessons":["defect_detection","temperature_control"],"proof_key":"proof.smithing.specialist"},
        {"id":133,"key":"building_ironwork","input_pin":1330,"output_pin":1331,"position":[1020,620],"state":"locked_near","title_key":"skill.smithing.specialist.building_ironwork","title_cs":"Stavební kování","rank":"specialist","icon":"icons/building_ironwork","progress":0.0,"lessons":["defect_detection","temperature_control"],"proof_key":"proof.smithing.specialist"},
        {"id":140,"key":"master","input_pin":1400,"output_pin":1401,"position":[1380,380],"state":"locked_far","title_key":"skill.smithing.master","title_cs":"Mistr","rank":"master","icon":"icons/master","progress":0.0,"lessons":["planning","resources","diagnostics","teamwork"],"proof_key":"proof.smithing.master"},
        {"id":151,"key":"innovator","input_pin":1510,"output_pin":0,"position":[1740,260],"state":"secret","title_key":"skill.smithing.innovator","title_cs":"Inovátor","rank":"final","icon":"icons/innovator","progress":0.0,"lessons":[],"proof_key":"proof.smithing.innovator","hidden_until_revealed":True},
        {"id":152,"key":"workshop_master","input_pin":1520,"output_pin":0,"position":[1740,500],"state":"secret","title_key":"skill.smithing.workshop_master","title_cs":"Mistr dílny","rank":"final","icon":"icons/workshop_master","progress":0.0,"lessons":[],"proof_key":"proof.smithing.workshop_master","hidden_until_revealed":True},
    ]
    raw_links = [(200,1001,1100,"completed"),(201,1101,1200,"active"),(210,1201,1310,"locked"),(211,1201,1320,"locked"),(212,1201,1330,"locked"),(220,1311,1400,"locked"),(221,1321,1400,"locked"),(222,1331,1400,"locked"),(230,1401,1510,"secret"),(231,1401,1520,"secret")]
    colors={"completed":"#8fa679","active":"#f07a31","locked":"#625f5b","secret":"#3e3d3a"}
    links=[{"id":i,"start_pin":a,"end_pin":b,"state":s,"color":colors[s],"thickness":4 if s=="active" else 3} for i,a,b,s in raw_links]
    data={
        "schema":"remeslo-stredoveku.smithing-skill-tree.v1",
        "editor":"thedmd/imgui-node-editor",
        "layout":{"mode":"fixed_gameplay_view","reference_canvas":[2100,950],"node_size":[270,132],"source_sprite_size":[360,176],"allow_node_drag":False,"allow_pan":True,"allow_zoom":True,"detail_panel_outside_canvas":True},
        "progression_rule":"Progress is proven through observation, real work and outcomes; it is not purchased with abstract points.",
        "visibility_rule":{"completed":"colored","active":"ember_glow","locked_near":"readable_with_light_fog","locked_far":"gray_with_heavy_fog","secret":"unnamed_without_outline_until_revealed"},
        "nodes":nodes,"links":links,
        "lesson_labels_cs":{
            "forge_safety":"Bezpečnost výhně","fuel":"Druhy paliva","tools":"Nástroje","heat_color":"Barvy žáru","tongs":"Správné držení materiálu","bellows":"Práce s měchy","straighten":"Rovnání","grind":"Broušení","nail":"Hřebíky","staple":"Skoby","hook":"Háky","repair":"Jednoduché opravy","quench":"Základní kalení","axe":"Sekery","chisel":"Dláta","knife":"Nože","hoe":"Motyky","horseshoe":"Podkovy","forge_weld":"Svařování ohněm","heat_treatment":"Tepelné zpracování","material_estimate":"Odhad materiálu","defect_detection":"Rozpoznání vad železa","temperature_control":"Přesnější řízení teploty","planning":"Návrh postupu","resources":"Hospodaření se železem a uhlím","diagnostics":"Diagnostika cizích výrobků","teamwork":"Vedení společné práce"
        }
    }
    (INTEGRATION/"smithing_skill_tree.json").write_text(json.dumps(data,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


def build():
    for folder in (ASSETS, INTEGRATION, PREVIEW): folder.mkdir(parents=True,exist_ok=True)
    records={}
    for state in NODE_STATES: save_sprite(records,f"nodes/{state}",node_frame(state))
    for state in ("completed","active","available","locked","secret"): save_sprite(records,f"pins/{state}",pin_sprite(state))
    for kind in ("complete","lock","question","choice"): save_sprite(records,f"badges/{kind}",badge(kind))
    save_sprite(records,"progress/track",progress_sprite("track"))
    save_sprite(records,"progress/fill",progress_sprite("fill"))
    save_sprite(records,"progress/complete_fill",progress_sprite("complete_fill"))
    save_sprite(records,"overlays/fog_near",fog_overlay("near"))
    save_sprite(records,"overlays/fog_far",fog_overlay("far"))
    for name in ICON_NAMES: save_sprite(records,f"icons/{name}",icon(name))

    atlas,packed=pack_atlas(records)
    atlas_path=ASSETS/"atlases"/"smithing_skill_tree_atlas_2048.png"
    atlas_path.parent.mkdir(parents=True,exist_ok=True);atlas.save(atlas_path,optimize=True)
    manifest={
        "schema":"remeslo-stredoveku.smithing-skill-tree-atlas.v1",
        "atlas":{"file":"assets/atlases/smithing_skill_tree_atlas_2048.png","size":list(ATLAS_SIZE),"padding":PAD,"filter":"linear","wrap":"clamp"},
        "font_policy":"Runtime sprites contain no text. Render localization strings through ImGui.",
        "palette":PALETTE,
        "recommended_sizes":{"node":[360,176],"node_compact":[270,132],"icon":[72,72],"pin":[32,32],"badge":[48,48],"progress":[256,18]},
        "sprites":{}
    }
    for p in packed:
        manifest["sprites"][p.key]={"file":f"assets/{p.key}.png","src":[p.x,p.y,p.w,p.h],"uv0":[round(p.x/ATLAS_SIZE[0],8),round(p.y/ATLAS_SIZE[1],8)],"uv1":[round((p.x+p.w)/ATLAS_SIZE[0],8),round((p.y+p.h)/ATLAS_SIZE[1],8)],"native_size":[p.w,p.h]}
    (INTEGRATION/"smithing_skill_tree_assets.json").write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    lines=["#pragma once","","// Generated by build_skill_tree_assets.py. Pixel coordinates in the 2048x2048 atlas.","namespace SmithingSkillTreeAtlas {","inline constexpr int Width = 2048;","inline constexpr int Height = 2048;","struct Rect { int x, y, w, h; };",""]
    for p in packed: lines.append(f"inline constexpr Rect {p.key.replace('/','_').replace('-','_')}{{{p.x}, {p.y}, {p.w}, {p.h}}};")
    lines.extend(["","} // namespace SmithingSkillTreeAtlas",""])
    (INTEGRATION/"SmithingSkillTreeAtlas.generated.h").write_text("\n".join(lines),encoding="utf-8")
    write_tree_json()
    compose_mockup(records).save(PREVIEW/"smithing_skill_tree_node_editor_mockup.png",optimize=True)
    compose_overview(records).save(PREVIEW/"smithing_skill_tree_assets_overview.png",optimize=True)
    print(f"Built {len(records)} sprites and {len(packed)} atlas entries.")


if __name__ == "__main__": build()
