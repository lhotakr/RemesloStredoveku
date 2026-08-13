#!/usr/bin/env python3
from __future__ import annotations

import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps


ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "generated_images"
OUT = Path(__file__).resolve().parent / "package"
OBJECT_DIR = OUT / "assets" / "Objects" / "Smithy"
TEXTURE_DIR = OUT / "assets" / "Textures" / "Smithy"
EFFECT_DIR = OUT / "assets" / "Effects" / "Smithy"
PREVIEW_DIR = OUT / "preview"


OBJECTS = [
    {
        "id": "smithy_hearth",
        "name": "Vyhen s kominovou kapotou",
        "source": "exec-26d70fb5-554a-4ee6-9467-ca9ac3b6c05c.png",
        "target": (384, 384),
        "solid": True,
        "tags": ["smithy", "forge", "workstation", "hearth", "interactable"],
        "collision_rects": [[0.11, 0.70, 0.78, 0.25]],
        "interaction": {"id": "smithy_hearth", "radius": 74, "prompt": "Pouzit vyhen"},
    },
    {
        "id": "smithy_anvil",
        "name": "Kovadlina na spalku",
        "source": "exec-c080db2e-2052-4297-b0f8-c03dfcb0233e.png",
        "target": (256, 320),
        "solid": True,
        "tags": ["smithy", "anvil", "workstation", "interactable"],
        "collision_rects": [[0.22, 0.70, 0.56, 0.25]],
        "interaction": {"id": "smithy_anvil", "radius": 62, "prompt": "Pracovat na kovadline"},
    },
    {
        "id": "smithy_bellows",
        "name": "Velke kovarske mechy",
        "source": "exec-8365d02e-70d4-4c8e-87dd-96ece04ea6ba.png",
        "target": (384, 288),
        "solid": True,
        "tags": ["smithy", "bellows", "workstation", "interactable"],
        "collision_rects": [[0.10, 0.67, 0.80, 0.25]],
        "interaction": {"id": "smithy_bellows", "radius": 72, "prompt": "Obsluhovat mechy"},
    },
    {
        "id": "smithy_quench_tub",
        "name": "Kalici kad",
        "source": "exec-c8fcd6b1-a941-479f-b76b-cb0fe50f51bc.png",
        "target": (288, 256),
        "solid": True,
        "tags": ["smithy", "quench", "water", "workstation", "interactable"],
        "collision_rects": [[0.10, 0.56, 0.80, 0.33]],
        "interaction": {"id": "smithy_quench", "radius": 60, "prompt": "Kalit vyrobek"},
    },
    {
        "id": "smithy_grindstone",
        "name": "Rucni brusny kamen",
        "source": "exec-88564225-1c4c-4e0f-bcab-d2af6c185202.png",
        "target": (320, 256),
        "solid": True,
        "tags": ["smithy", "grindstone", "workstation", "interactable"],
        "collision_rects": [[0.12, 0.68, 0.76, 0.24]],
        "interaction": {"id": "smithy_grindstone", "radius": 64, "prompt": "Brousit"},
    },
    {
        "id": "smithy_workbench",
        "name": "Kovarsky pracovni stul",
        "source": "exec-3fcd1696-8423-4e17-9eb8-a16130980304.png",
        "target": (384, 288),
        "solid": True,
        "tags": ["smithy", "workbench", "workstation", "interactable"],
        "collision_rects": [[0.08, 0.68, 0.84, 0.24]],
        "interaction": {"id": "smithy_workbench", "radius": 72, "prompt": "Pouzit pracovni stul"},
    },
    {
        "id": "smithy_tool_rack",
        "name": "Nastenny drzak kovarskeho naradi",
        "source": "exec-879ec325-a883-4b18-b2d0-0086abaee133.png",
        "target": (384, 256),
        "solid": False,
        "tags": ["smithy", "tools", "wall_prop", "decoration"],
        "collision_rects": [],
    },
    {
        "id": "smithy_charcoal",
        "name": "Kosiky s drevenym uhlim",
        "source": "exec-88dfeb24-c0c2-4553-b63e-4862793aae44.png",
        "target": (288, 256),
        "solid": True,
        "tags": ["smithy", "charcoal", "fuel", "storage"],
        "collision_rects": [[0.12, 0.62, 0.76, 0.29]],
    },
    {
        "id": "smithy_iron_rack",
        "name": "Stojan se zeleznymi polotovary",
        "source": "exec-6bc914a6-f018-44b3-a596-b76d25f8bb54.png",
        "target": (384, 256),
        "solid": True,
        "tags": ["smithy", "iron_stock", "storage"],
        "collision_rects": [[0.07, 0.70, 0.86, 0.21]],
    },
    {
        "id": "smithy_firewood",
        "name": "Palivove drivi a stipaci spalek",
        "source": "exec-d1865d82-fc1f-4abc-8e57-fa59fe9d78bc.png",
        "target": (320, 256),
        "solid": True,
        "tags": ["smithy", "firewood", "fuel", "storage"],
        "collision_rects": [[0.08, 0.62, 0.84, 0.30]],
    },
    {
        "id": "smithy_storage",
        "name": "Drobny sklad kovarny",
        "source": "exec-f476e491-e917-4186-8d93-465d3fbfe303.png",
        "target": (320, 256),
        "solid": True,
        "tags": ["smithy", "storage", "containers"],
        "collision_rects": [[0.07, 0.58, 0.86, 0.34]],
    },
    {
        "id": "smithy_exit_door",
        "name": "Otevrene dvere z kovarny",
        "source": "exec-31703efc-f7a4-4309-a9d7-0c37d4f39083.png",
        "target": (256, 384),
        "solid": False,
        "tags": ["smithy", "door", "portal", "map_link", "interactable"],
        "collision_rects": [],
        "interaction": {"id": "smithy_exit", "radius": 54, "prompt": "Odejit do Blatcu"},
    },
]


TEXTURES = [
    ("smithy_wall_plaster_stone.png", "exec-3b9c5a8e-b6e6-4f8c-9671-9822092cfd02.png", False),
    ("smithy_wall_sooty_stone.png", "exec-50ca7928-1416-4b83-a305-6ab689c48594.png", False),
    ("smithy_floor_earth_stone.png", "exec-02e6db42-6f38-47dd-918a-c9170863db0c.png", True),
    ("smithy_ceiling_oak_beams.png", "exec-1076c02c-ee0a-4914-b04c-ae837042f1db.png", False),
]


def ensure_dirs() -> None:
    for directory in (OBJECT_DIR, TEXTURE_DIR, EFFECT_DIR, PREVIEW_DIR):
        directory.mkdir(parents=True, exist_ok=True)


def alpha_bbox(image: Image.Image, threshold: int = 8):
    alpha = image.getchannel("A")
    mask = alpha.point(lambda value: 255 if value >= threshold else 0)
    return mask.getbbox()


def fit_transparent(source: Path, target_size: tuple[int, int], margin: int = 8) -> Image.Image:
    image = Image.open(source).convert("RGBA")
    bbox = alpha_bbox(image)
    if not bbox:
        raise ValueError(f"No visible alpha in {source}")
    image = image.crop(bbox)
    max_w = target_size[0] - 2 * margin
    max_h = target_size[1] - 2 * margin
    scale = min(max_w / image.width, max_h / image.height)
    size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    image = image.resize(size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", target_size, (0, 0, 0, 0))
    x = (target_size[0] - image.width) // 2
    y = target_size[1] - margin - image.height
    canvas.alpha_composite(image, (x, y))
    return canvas


def make_seamless(source: Path, out_path: Path, floor: bool) -> Image.Image:
    image = Image.open(source).convert("RGB")
    crop_size = min(image.size)
    left = (image.width - crop_size) // 2
    top = (image.height - crop_size) // 2
    image = image.crop((left, top, left + crop_size, top + crop_size))
    # Offset first, then hide the new central joins with a feathered cross blend.
    shifted = ImageChops.offset(image, crop_size // 2, crop_size // 2)
    original = image.copy()
    mask = Image.new("L", image.size, 0)
    draw = ImageDraw.Draw(mask)
    feather = max(48, crop_size // 12)
    cx = crop_size // 2
    cy = crop_size // 2
    draw.rectangle((cx - feather, 0, cx + feather, crop_size), fill=255)
    draw.rectangle((0, cy - feather, crop_size, cy + feather), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(feather / 2))
    blended = Image.composite(original, shifted, mask)
    # Make edges identical by mirroring a narrow band, while retaining organic detail.
    band = max(16, crop_size // 64)
    px = blended.load()
    for y in range(crop_size):
        for i in range(band):
            t = i / max(1, band - 1)
            lx = i
            rx = crop_size - 1 - i
            a = px[lx, y]
            b = px[rx, y]
            mix = tuple(round((a[c] + b[c]) * 0.5) for c in range(3))
            px[lx, y] = tuple(round(mix[c] * (1 - t) + a[c] * t) for c in range(3))
            px[rx, y] = tuple(round(mix[c] * (1 - t) + b[c] * t) for c in range(3))
    for x in range(crop_size):
        for i in range(band):
            t = i / max(1, band - 1)
            ty = i
            by = crop_size - 1 - i
            a = px[x, ty]
            b = px[x, by]
            mix = tuple(round((a[c] + b[c]) * 0.5) for c in range(3))
            px[x, ty] = tuple(round(mix[c] * (1 - t) + a[c] * t) for c in range(3))
            px[x, by] = tuple(round(mix[c] * (1 - t) + b[c] * t) for c in range(3))
    texture = blended.resize((512, 512), Image.Resampling.LANCZOS)
    if floor:
        texture = ImageOps.autocontrast(texture, cutoff=1)
    texture.save(out_path, optimize=True)
    return texture


def pack_atlas(sprites: list[tuple[dict, Image.Image]]) -> tuple[Image.Image, list[dict]]:
    atlas_w = 1024
    padding = 4
    x = padding
    y = padding
    row_h = 0
    placements = []
    for meta, sprite in sprites:
        if x + sprite.width + padding > atlas_w:
            x = padding
            y += row_h + padding
            row_h = 0
        placements.append((meta, sprite, x, y))
        x += sprite.width + padding
        row_h = max(row_h, sprite.height)
    atlas_h = 1 << math.ceil(math.log2(y + row_h + padding))
    atlas = Image.new("RGBA", (atlas_w, atlas_h), (0, 0, 0, 0))
    manifest = []
    for meta, sprite, px, py in placements:
        atlas.alpha_composite(sprite, (px, py))
        manifest.append({"id": meta["id"], "x": px, "y": py, "w": sprite.width, "h": sprite.height})
    return atlas, manifest


def split_fire() -> list[Image.Image]:
    sheet = Image.open(SRC / "exec-2120883c-3e01-42de-9bdc-8e9a4b29a65e.png").convert("RGBA")
    frames = []
    cell_w = sheet.width // 2
    cell_h = sheet.height // 2
    for row in range(2):
        for col in range(2):
            frame = sheet.crop((col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h))
            bbox = alpha_bbox(frame)
            if not bbox:
                raise ValueError("Empty fire frame")
            frame = frame.crop(bbox)
            frame.thumbnail((240, 184), Image.Resampling.LANCZOS)
            canvas = Image.new("RGBA", (256, 192), (0, 0, 0, 0))
            canvas.alpha_composite(frame, ((256 - frame.width) // 2, 192 - 4 - frame.height))
            frames.append(canvas)
    atlas = Image.new("RGBA", (1024, 192), (0, 0, 0, 0))
    for index, frame in enumerate(frames):
        atlas.alpha_composite(frame, (index * 256, 0))
        frame.save(EFFECT_DIR / f"smithy_forge_fire_{index:02d}.png", optimize=True)
    atlas.save(EFFECT_DIR / "smithy_forge_fire_4frames.png", optimize=True)
    return frames


def edge_error(texture: Image.Image) -> dict[str, float]:
    arr = texture.convert("RGB")
    left = arr.crop((0, 0, 1, arr.height))
    right = arr.crop((arr.width - 1, 0, arr.width, arr.height))
    top = arr.crop((0, 0, arr.width, 1))
    bottom = arr.crop((0, arr.height - 1, arr.width, arr.height))
    def mean(diff):
        hist = ImageChops.difference(*diff).histogram()
        pixels = diff[0].width * diff[0].height * 3
        return sum((i % 256) * count for i, count in enumerate(hist)) / pixels
    return {"horizontal": round(mean((left, right)), 3), "vertical": round(mean((top, bottom)), 3)}


def create_preview(sprites: list[tuple[dict, Image.Image]], textures: list[tuple[str, Image.Image]]) -> None:
    bg = (24, 27, 25, 255)
    card = (37, 42, 39, 255)
    accent = (207, 151, 74, 255)
    text_col = (232, 225, 207, 255)
    width = 1600
    cell_w = 384
    cell_h = 360
    cols = 4
    rows = math.ceil(len(sprites) / cols)
    height = 92 + rows * cell_h + 640
    canvas = Image.new("RGBA", (width, height), bg)
    draw = ImageDraw.Draw(canvas)
    try:
        font_title = ImageFont.truetype("DejaVuSans-Bold.ttf", 38)
        font_name = ImageFont.truetype("DejaVuSans.ttf", 19)
    except OSError:
        font_title = font_name = ImageFont.load_default()
    draw.text((28, 22), "Kovarna - objekty pro 2.5D interier", font=font_title, fill=text_col)
    for index, (meta, sprite) in enumerate(sprites):
        row, col = divmod(index, cols)
        x = 20 + col * cell_w
        y = 84 + row * cell_h
        draw.rounded_rectangle((x, y, x + cell_w - 16, y + cell_h - 16), radius=14, fill=card)
        show = sprite.copy()
        show.thumbnail((cell_w - 42, cell_h - 72), Image.Resampling.LANCZOS)
        canvas.alpha_composite(show, (x + (cell_w - 16 - show.width) // 2, y + cell_h - 66 - show.height))
        label = meta["name"]
        max_text_w = cell_w - 46
        if draw.textbbox((0, 0), label, font=font_name)[2] > max_text_w:
            words = label.split()
            first, second = [], []
            for word in words:
                candidate = " ".join(first + [word])
                if first and draw.textbbox((0, 0), candidate, font=font_name)[2] > max_text_w:
                    second.append(word)
                elif second:
                    second.append(word)
                else:
                    first.append(word)
            draw.text((x + 14, y + 10), " ".join(first), font=font_name, fill=accent)
            draw.text((x + 14, y + 33), " ".join(second), font=font_name, fill=accent)
        else:
            draw.text((x + 14, y + 12), label, font=font_name, fill=accent)
    y0 = 102 + rows * cell_h
    draw.text((28, y0), "Opakovatelne povrchy", font=font_title, fill=text_col)
    tile_y = y0 + 58
    for i, (name, texture) in enumerate(textures):
        x = 28 + i * 386
        tiled = Image.new("RGB", (352, 352))
        small = texture.resize((176, 176), Image.Resampling.LANCZOS)
        for yy in (0, 176):
            for xx in (0, 176):
                tiled.paste(small, (xx, yy))
        canvas.paste(tiled, (x, tile_y))
        label = name.replace("smithy_", "").replace(".png", "").replace("_", " ")
        draw.text((x, tile_y + 364), label, font=font_name, fill=accent)
    canvas.convert("RGB").save(PREVIEW_DIR / "smithy_environment_contact_sheet.jpg", quality=94, optimize=True)


def create_room_mockup(sprites: list[tuple[dict, Image.Image]], textures: list[tuple[str, Image.Image]]) -> None:
    by_id = {meta["id"]: sprite for meta, sprite in sprites}
    tex = {name: image for name, image in textures}
    width, height = 1536, 1024
    scene = Image.new("RGBA", (width, height), (20, 19, 17, 255))

    wall = tex["smithy_wall_plaster_stone.png"].resize((512, 512), Image.Resampling.LANCZOS)
    soot = tex["smithy_wall_sooty_stone.png"].resize((512, 512), Image.Resampling.LANCZOS)
    floor = tex["smithy_floor_earth_stone.png"].resize((512, 512), Image.Resampling.LANCZOS)
    for y in range(0, 560, 512):
        for x in range(0, width, 512):
            scene.paste(wall, (x, y))
    for y in range(0, 560, 512):
        for x in range(896, width, 512):
            scene.paste(soot, (x, y))
    for y in range(560, height, 512):
        for x in range(0, width, 512):
            scene.paste(floor, (x, y))

    shade = Image.new("RGBA", scene.size, (0, 0, 0, 0))
    sd = ImageDraw.Draw(shade)
    sd.rectangle((0, 0, width, 560), fill=(18, 12, 8, 70))
    sd.rectangle((0, 545, width, 585), fill=(0, 0, 0, 55))
    scene.alpha_composite(shade)

    placements = [
        ("smithy_tool_rack", 210, 190, 0.80),
        ("smithy_workbench", 260, 565, 0.86),
        ("smithy_iron_rack", 270, 835, 0.74),
        ("smithy_storage", 80, 920, 0.64),
        ("smithy_bellows", 1050, 520, 0.74),
        ("smithy_hearth", 1280, 585, 0.92),
        ("smithy_charcoal", 1400, 810, 0.60),
        ("smithy_anvil", 880, 700, 0.73),
        ("smithy_quench_tub", 1075, 790, 0.65),
        ("smithy_grindstone", 1300, 920, 0.67),
        ("smithy_firewood", 1120, 980, 0.55),
        ("smithy_exit_door", 610, 565, 0.88),
    ]
    for obj_id, pivot_x, pivot_y, scale in placements:
        obj = by_id[obj_id]
        size = (max(1, round(obj.width * scale)), max(1, round(obj.height * scale)))
        show = obj.resize(size, Image.Resampling.LANCZOS)
        scene.alpha_composite(show, (round(pivot_x - show.width / 2), round(pivot_y - show.height + 4 * scale)))

    fire = Image.open(EFFECT_DIR / "smithy_forge_fire_00.png").convert("RGBA")
    fire.thumbnail((126, 92), Image.Resampling.LANCZOS)
    scene.alpha_composite(fire, (1224, 490))

    darkness = Image.new("RGBA", scene.size, (0, 0, 0, 115))
    light_mask = Image.new("L", scene.size, 0)
    lm = ImageDraw.Draw(light_mask)
    lm.ellipse((1030, 285, 1515, 865), fill=205)
    lm.ellipse((400, 330, 820, 920), fill=90)
    light_mask = light_mask.filter(ImageFilter.GaussianBlur(90))
    darkness.putalpha(ImageOps.invert(light_mask).point(lambda p: min(150, p)))
    scene.alpha_composite(darkness)

    glow = Image.new("RGBA", scene.size, (0, 0, 0, 0))
    glow_mask = Image.new("L", scene.size, 0)
    gd = ImageDraw.Draw(glow_mask)
    gd.ellipse((1080, 300, 1510, 820), fill=72)
    glow_mask = glow_mask.filter(ImageFilter.GaussianBlur(100))
    glow.paste((255, 126, 35, 0), (0, 0, width, height))
    glow.putalpha(glow_mask)
    scene = Image.alpha_composite(scene, glow)

    scene.convert("RGB").save(PREVIEW_DIR / "smithy_room_mockup.jpg", quality=94, optimize=True)


def main() -> None:
    ensure_dirs()
    sprite_entries = []
    for meta in OBJECTS:
        sprite = fit_transparent(SRC / meta["source"], meta["target"])
        sprite.save(OBJECT_DIR / f"{meta['id']}.png", optimize=True)
        sprite_entries.append((meta, sprite))

    atlas, placements = pack_atlas(sprite_entries)
    atlas_path = OBJECT_DIR / "SmithyEnvironmentAtlas.png"
    atlas.save(atlas_path, optimize=True)
    placement_by_id = {entry["id"]: entry for entry in placements}

    objects_json = {"image": "Smithy/SmithyEnvironmentAtlas.png", "objects": []}
    for meta, sprite in sprite_entries:
        src = placement_by_id[meta["id"]]
        obj = {
            "id": meta["id"],
            "name": meta["name"],
            "src": {k: src[k] for k in ("x", "y", "w", "h")},
            "pivot": {"x": sprite.width // 2, "y": sprite.height - 4},
            "collider": {"x": 0, "y": 0, "w": 1, "h": 1, "enabled": False},
            "collision_rects": [
                {"x": r[0], "y": r[1], "w": r[2], "h": r[3]} for r in meta["collision_rects"]
            ],
            "tags": meta["tags"],
            "solid": meta["solid"],
            "has_sprite": True,
            "scale": 1.0,
        }
        if "interaction" in meta:
            obj["interaction"] = meta["interaction"]
        objects_json["objects"].append(obj)
    with (OBJECT_DIR / "SmithyObjects.json").open("w", encoding="utf-8") as handle:
        json.dump(objects_json, handle, ensure_ascii=False, indent=2)

    textures = []
    errors = {}
    for name, source, floor in TEXTURES:
        texture = make_seamless(SRC / source, TEXTURE_DIR / name, floor)
        textures.append((name, texture))
        errors[name] = edge_error(texture)

    fire_frames = split_fire()
    fire_data = {
        "image": "Effects/Smithy/smithy_forge_fire_4frames.png",
        "frame_width": 256,
        "frame_height": 192,
        "frame_count": 4,
        "frame_time_ms": 110,
        "loop": True,
        "pivot": {"x": 128, "y": 188},
        "recommended_blend": "alpha_or_additive",
    }
    with (EFFECT_DIR / "smithy_forge_fire.json").open("w", encoding="utf-8") as handle:
        json.dump(fire_data, handle, ensure_ascii=False, indent=2)

    validation = {
        "objects": len(sprite_entries),
        "atlas_size": list(atlas.size),
        "object_sprite_modes": {m["id"]: im.mode for m, im in sprite_entries},
        "object_sprite_sizes": {m["id"]: list(im.size) for m, im in sprite_entries},
        "transparent_corners": {
            m["id"]: all(im.getpixel(xy)[3] == 0 for xy in ((0, 0), (im.width - 1, 0), (0, im.height - 1), (im.width - 1, im.height - 1)))
            for m, im in sprite_entries
        },
        "texture_edge_mean_absolute_error": errors,
        "fire_frames": len(fire_frames),
    }
    with (OUT / "validation.json").open("w", encoding="utf-8") as handle:
        json.dump(validation, handle, ensure_ascii=False, indent=2)

    create_preview(sprite_entries, textures)
    create_room_mockup(sprite_entries, textures)


if __name__ == "__main__":
    main()
