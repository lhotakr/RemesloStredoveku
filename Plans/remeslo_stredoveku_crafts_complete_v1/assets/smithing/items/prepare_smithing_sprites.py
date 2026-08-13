from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont
import numpy as np
from scipy import ndimage


ROOT = Path(__file__).resolve().parent
SOURCE_ROOT = ROOT.parent.parent / "generated_images"
OUTPUT_ROOT = ROOT / "sprites" / "smithing"

SOURCES = {
    "nail": SOURCE_ROOT / "exec-70ddefcf-46aa-4509-80f5-127d065ba392.png",
    "staple": SOURCE_ROOT / "exec-4bf43e91-d64e-4d87-a5c7-2ec9e135349d.png",
    "hook": SOURCE_ROOT / "exec-30a88257-b582-47ab-8b3c-4504289cb835.png",
    "horseshoe": SOURCE_ROOT / "exec-4e80c076-5639-4dc7-be7e-b2cd7c99cc44.png",
    "chisel": SOURCE_ROOT / "exec-182dd84f-510f-4560-88f2-3f53953b397f.png",
    "knife": SOURCE_ROOT / "exec-4ff6eae5-0409-4d7b-a9e2-b1d247467591.png",
    "sickle": SOURCE_ROOT / "exec-f994490a-60a9-4bea-b6af-d92557e1dbe8.png",
    "hoe": SOURCE_ROOT / "exec-32898c20-58e4-4a11-b71b-ba0b18c24d0b.png",
    "axe": SOURCE_ROOT / "exec-1661c7e4-98d2-4eb6-855a-f02e02694776.png",
}

STATE_IDS = (
    "raw_material",
    "forged_blank",
    "unfinished",
    "ruined",
    "provisional",
    "common",
    "honest",
    "excellent",
    "masterwork",
)

DISPLAY_NAMES = {
    "nail": "hřebík",
    "staple": "skoba",
    "hook": "hák",
    "horseshoe": "podkova",
    "chisel": "dláto",
    "knife": "nůž",
    "sickle": "srp",
    "hoe": "motyka",
    "axe": "sekera",
}

STATE_NAMES = (
    "surovina",
    "polotovar",
    "nedokončený",
    "zkažený",
    "provizorní",
    "běžný",
    "poctivý",
    "výborný",
    "mistrovský",
)


def clean_alpha(image: Image.Image) -> Image.Image:
    image = image.convert("RGBA")
    red, green, blue, alpha = image.split()
    # The generator delivered genuine alpha plus a very faint decorative glow.
    # Keep the object and its anti-aliased edge, but remove the glow so the
    # sprites remain clean on arbitrary inventory backgrounds.
    # A high cut is intentional: the generated atlas contains soft amber halos
    # whose alpha can otherwise connect neighbouring objects into one component.
    alpha = alpha.point(lambda value: 0 if value < 180 else min(255, int((value - 180) * 255 / 75)))
    return Image.merge("RGBA", (red, green, blue, alpha))


def remove_checkerboard(cell: Image.Image) -> Image.Image:
    """Remove a baked light-gray transparency checkerboard.

    The generated sheets intentionally use dark iron and warm wood.  Their
    background is bright and almost neutral, so a conservative foreground mask
    can preserve the object while rejecting both checker colors.  Small bright
    metal highlights are restored by morphological closing.  The soft fringe
    borrows the nearest foreground color, preventing a pale checker halo.
    """
    rgb = np.asarray(cell.convert("RGB")).copy()
    values = rgb.astype(np.int16)
    lightness = values.mean(axis=2)
    chroma = values.max(axis=2) - values.min(axis=2)
    foreground = (lightness < 214) | (chroma > 16)

    foreground = ndimage.binary_closing(foreground, iterations=2)
    labels, count = ndimage.label(foreground)
    keep = np.zeros_like(foreground, dtype=bool)
    for component_id in range(1, count + 1):
        component = labels == component_id
        # Keep complete item parts (including separate raw iron/wood billets and
        # genuinely broken pieces), but discard tiny checkerboard remnants.
        if int(component.sum()) >= 200:
            keep |= component

    if not keep.any():
        raise RuntimeError("Checkerboard removal produced an empty sprite")

    soft = ndimage.gaussian_filter(keep.astype(np.float32), sigma=0.7)
    soft[soft < 0.025] = 0.0
    alpha = np.clip(np.rint(soft * 255.0), 0, 255).astype(np.uint8)

    fringe = (~keep) & (alpha > 0)
    if fringe.any():
        nearest = ndimage.distance_transform_edt(~keep, return_distances=False, return_indices=True)
        rgb[fringe] = rgb[nearest[0][fringe], nearest[1][fringe]]

    rgba = np.dstack((rgb, alpha))
    return Image.fromarray(rgba, mode="RGBA")


def split_objects(image: Image.Image) -> list[Image.Image]:
    """Assign connected alpha components to the nearest intended grid cell.

    Long handles can cross an atlas cell boundary. Connected-component grouping
    keeps the complete object while excluding fragments from its neighbours.
    """
    width, height = image.size
    alpha = np.asarray(image.getchannel("A"))
    labels, count = ndimage.label(alpha > 0)
    grouped_masks = [np.zeros_like(alpha, dtype=bool) for _ in range(9)]

    for component_id in range(1, count + 1):
        component = labels == component_id
        area = int(component.sum())
        if area < 12:
            continue
        ys, xs = np.nonzero(component)
        center_x = float(xs.mean())
        center_y = float(ys.mean())
        column = min(2, max(0, int(center_x * 3 / width)))
        row = min(2, max(0, int(center_y * 3 / height)))
        grouped_masks[row * 3 + column] |= component

    sprites: list[Image.Image] = []
    rgba = np.asarray(image).copy()
    for index, group_mask in enumerate(grouped_masks):
        isolated = rgba.copy()
        isolated[..., 3] = np.where(group_mask, isolated[..., 3], 0)
        cell = Image.fromarray(isolated, mode="RGBA")
        bbox = cell.getchannel("A").getbbox()
        if bbox is None:
            raise RuntimeError(f"Empty sprite cell at index={index}")
        sprites.append(fit_sprite(cell.crop(bbox)))
    return sprites


def split_checkerboard_sheet(image: Image.Image) -> list[Image.Image]:
    width, height = image.size
    if width % 3 or height % 3:
        raise RuntimeError(f"Expected a sheet divisible into 3x3 cells, got {image.size}")
    cell_width = width // 3
    cell_height = height // 3
    sprites: list[Image.Image] = []
    for row in range(3):
        for column in range(3):
            box = (
                column * cell_width,
                row * cell_height,
                (column + 1) * cell_width,
                (row + 1) * cell_height,
            )
            isolated = remove_checkerboard(image.crop(box))
            bbox = isolated.getchannel("A").getbbox()
            if bbox is None:
                raise RuntimeError(f"Empty checkerboard cell at row={row}, column={column}")
            sprites.append(fit_sprite(isolated.crop(bbox)))
    return sprites


def fit_sprite(cell: Image.Image) -> Image.Image:
    bbox = cell.getchannel("A").getbbox()
    if bbox is None:
        raise RuntimeError("Cannot fit an empty sprite")

    cell = cell.crop(bbox)
    target_content = 112
    scale = min(target_content / cell.width, target_content / cell.height)
    target_size = (
        max(1, round(cell.width * scale)),
        max(1, round(cell.height * scale)),
    )
    cell = cell.resize(target_size, Image.Resampling.LANCZOS)
    cell = ImageEnhance.Sharpness(cell).enhance(1.15)

    canvas = Image.new("RGBA", (128, 128), (0, 0, 0, 0))
    x = (128 - cell.width) // 2
    y = (128 - cell.height) // 2
    canvas.alpha_composite(cell, (x, y))
    return canvas


def make_locked_preview(sprite: Image.Image, fog_strength: int) -> Image.Image:
    # Runtime UI should preferably create this effect lazily. These previews are
    # included only as a visual reference for the proposed discovery states.
    tiny = sprite.resize((20, 20), Image.Resampling.BILINEAR)
    blurred = tiny.resize((128, 128), Image.Resampling.BILINEAR)
    grayscale = blurred.convert("L")
    muted = Image.merge("RGBA", (grayscale, grayscale, grayscale, blurred.getchannel("A")))
    fog = Image.new("RGBA", (128, 128), (196, 202, 194, fog_strength))
    fog_alpha = Image.new("L", (128, 128), fog_strength)
    fog_alpha = fog_alpha.filter(ImageFilter.GaussianBlur(18))
    fog.putalpha(fog_alpha)
    return Image.alpha_composite(muted, fog)


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path("/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"),
    ]
    for path in candidates:
        if path.exists():
            return ImageFont.truetype(path, size=size)
    return ImageFont.load_default()


def make_contact_sheet(all_rows: list[list[Image.Image]]) -> None:
    label_width = 150
    header_height = 54
    cell_size = 128
    width = label_width + cell_size * len(STATE_IDS)
    height = header_height + cell_size * len(SOURCES)
    sheet = Image.new("RGBA", (width, height), (24, 27, 25, 255))
    draw = ImageDraw.Draw(sheet)
    font = load_font(15)
    bold = load_font(18, bold=True)
    small = load_font(13)

    draw.rectangle((0, 0, width, header_height), fill=(44, 42, 35, 255))
    draw.text((12, 15), "Kovářství", font=bold, fill=(235, 222, 184, 255))
    for column, state_name in enumerate(STATE_NAMES):
        x = label_width + column * cell_size
        draw.text((x + 8, 18), state_name, font=small, fill=(224, 221, 207, 255))

    for row, (item_id, sprites) in enumerate(zip(SOURCES, all_rows)):
        y = header_height + row * cell_size
        if row % 2:
            draw.rectangle((0, y, width, y + cell_size), fill=(29, 33, 30, 255))
        draw.text((12, y + 51), DISPLAY_NAMES[item_id].capitalize(), font=font, fill=(236, 229, 204, 255))
        for column, sprite in enumerate(sprites):
            sheet.alpha_composite(sprite, (label_width + column * cell_size, y))

    sheet.save(ROOT / "smithing_items_contact_sheet.png", optimize=True)


def main() -> None:
    OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
    all_rows: list[list[Image.Image]] = []

    for item_id, source_path in SOURCES.items():
        original = Image.open(source_path)
        item_dir = OUTPUT_ROOT / item_id
        item_dir.mkdir(parents=True, exist_ok=True)
        row_sprites: list[Image.Image] = []

        if "A" in original.mode:
            extracted = split_objects(clean_alpha(original))
        else:
            extracted = split_checkerboard_sheet(original)
        for index, state_id in enumerate(STATE_IDS):
            sprite = extracted[index]
            sprite.save(item_dir / f"{item_id}_{state_id}_128.png", optimize=True)
            row_sprites.append(sprite)

        # Representative discovery previews for UI validation.
        common = row_sprites[5]
        make_locked_preview(common, 54).save(item_dir / f"{item_id}_heard_blurred_128.png", optimize=True)
        make_locked_preview(common, 108).save(item_dir / f"{item_id}_unknown_fogged_128.png", optimize=True)
        all_rows.append(row_sprites)

    atlas = Image.new("RGBA", (128 * len(STATE_IDS), 128 * len(SOURCES)), (0, 0, 0, 0))
    for row, sprites in enumerate(all_rows):
        for column, sprite in enumerate(sprites):
            atlas.alpha_composite(sprite, (column * 128, row * 128))
    atlas.save(OUTPUT_ROOT / "smithing_items_9states_atlas_1152x1152.png", optimize=True)

    # Compact visual comparison used by the README and for quick review.
    preview = Image.new("RGBA", (128 * 5, 128 * len(SOURCES)), (24, 27, 25, 255))
    preview_indices = (0, 3, 4, 5, 8)
    for row, sprites in enumerate(all_rows):
        for column, sprite_index in enumerate(preview_indices):
            preview.alpha_composite(sprites[sprite_index], (column * 128, row * 128))
    preview.save(ROOT / "smithing_items_preview.png", optimize=True)
    make_contact_sheet(all_rows)


if __name__ == "__main__":
    main()
