from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageFilter
import random


ROOT = Path(__file__).resolve().parent
SPRITES = ROOT / "sprites" / "smithing"

W, H = 1600, 900
FONT_SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONT_SERIF = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"


def font(size: int, serif: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT_SERIF if serif else FONT_SANS, size)


def text(draw: ImageDraw.ImageDraw, xy, value: str, size: int, fill, *, serif=False, anchor=None):
    draw.text(xy, value, font=font(size, serif), fill=fill, anchor=anchor)


def panel(draw: ImageDraw.ImageDraw, box, fill, outline=(93, 67, 39, 255), radius=14, width=2):
    draw.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)


def load_sprite(item: str, state: str) -> Image.Image:
    return Image.open(SPRITES / item / f"{item}_{state}_128.png").convert("RGBA")


def paste_center(canvas: Image.Image, sprite: Image.Image, center, size: int, *, circular_clip: bool = False):
    sprite = sprite.resize((size, size), Image.Resampling.LANCZOS)
    if circular_clip:
        clip = Image.new("L", (size, size), 0)
        ImageDraw.Draw(clip).ellipse((0, 0, size - 1, size - 1), fill=255)
        sprite.putalpha(ImageChops.multiply(sprite.getchannel("A"), clip))
    canvas.alpha_composite(sprite, (int(center[0] - size / 2), int(center[1] - size / 2)))


def draw_node(canvas: Image.Image, draw: ImageDraw.ImageDraw, center, sprite: Image.Image, title: str, subtitle: str, state: str):
    colors = {
        "proven": ((91, 64, 27, 255), (212, 171, 78, 255)),
        "current": ((76, 78, 65, 255), (167, 113, 45, 255)),
        "heard": ((64, 68, 65, 255), (115, 121, 112, 255)),
        "unknown": ((53, 57, 56, 255), (85, 91, 89, 255)),
    }
    fill, border = colors[state]
    x, y = center
    draw.ellipse((x - 60, y - 60, x + 60, y + 60), fill=fill, outline=border, width=5)
    paste_center(canvas, sprite, center, 104, circular_clip=state in {"heard", "unknown"})
    text(draw, (x, y + 77), title, 19, (238, 225, 193, 255), serif=True, anchor="mm")
    text(draw, (x, y + 101), subtitle, 13, (176, 168, 149, 255), anchor="mm")
    if state == "proven":
        draw.ellipse((x + 38, y + 35, x + 63, y + 60), fill=(149, 51, 38, 255), outline=(231, 184, 95, 255), width=2)
        text(draw, (x + 51, y + 48), "✓", 17, (247, 225, 172, 255), anchor="mm")


def main() -> None:
    random.seed(1400)
    canvas = Image.new("RGBA", (W, H), (18, 21, 20, 255))
    pixels = canvas.load()
    for _ in range(50000):
        x = random.randrange(W)
        y = random.randrange(H)
        base = pixels[x, y]
        delta = random.choice((-5, -3, 2, 4))
        pixels[x, y] = tuple(max(0, min(255, base[i] + delta)) for i in range(3)) + (255,)

    draw = ImageDraw.Draw(canvas)
    panel(draw, (45, 35, 1555, 865), (35, 38, 34, 247), (112, 80, 43, 255), 18, 3)

    # Left craft navigation.
    panel(draw, (70, 65, 335, 835), (47, 45, 36, 255), (101, 75, 42, 255), 13, 2)
    text(draw, (95, 92), "ŘEMESLA", 26, (229, 205, 151, 255), serif=True)
    text(draw, (95, 128), "13 světských oborů", 14, (156, 149, 132, 255))
    craft_names = [
        "Kovářství", "Tesařství", "Kamenictví", "Kolářství", "Hrnčířství",
        "Koželužství", "Tkalcovství", "Luky a šípy", "Pivovarnictví",
        "Písařství", "Bylinkářství", "Ranhojičství", "Vaření",
    ]
    y = 168
    for index, name in enumerate(craft_names):
        selected = index == 0
        if selected:
            draw.rounded_rectangle((85, y - 7, 319, y + 27), radius=7, fill=(107, 73, 35, 255), outline=(201, 156, 76, 255), width=2)
        text(draw, (103, y + 10), name, 16, (244, 226, 188, 255) if selected else (188, 180, 159, 255), anchor="lm")
        if index == 0:
            text(draw, (303, y + 10), "46 %", 13, (236, 198, 108, 255), anchor="rm")
        y += 43

    draw.line((91, 754, 313, 754), fill=(92, 80, 58, 255), width=1)
    draw.rounded_rectangle((85, 772, 319, 818), radius=9, fill=(55, 59, 57, 255), outline=(85, 91, 89, 255), width=2)
    text(draw, (202, 795), "NEZNÁMÁ VĚTEV", 14, (128, 134, 131, 255), anchor="mm")

    # Header.
    text(draw, (380, 78), "KOVÁŘSTVÍ", 34, (234, 205, 139, 255), serif=True)
    text(draw, (382, 120), "Beneš — vesnický kovář", 16, (173, 163, 142, 255))
    panel(draw, (785, 76, 1025, 125), (54, 54, 44, 255), (119, 91, 48, 255), 10, 2)
    text(draw, (810, 90), "Hodnost: Tovaryš", 15, (221, 206, 170, 255))
    draw.rounded_rectangle((810, 111, 995, 120), radius=5, fill=(42, 42, 38, 255))
    draw.rounded_rectangle((810, 111, 895, 120), radius=5, fill=(174, 111, 43, 255))

    # Main tree and branches.
    line_color = (115, 87, 50, 255)
    dim_line = (74, 78, 73, 255)
    segments = [((485, 235), (675, 235), line_color), ((675, 235), (865, 235), line_color), ((865, 295), (865, 420), dim_line), ((805, 480), (675, 575), dim_line), ((675, 635), (525, 735), dim_line), ((735, 635), (870, 735), dim_line)]
    for a, b, color in segments:
        draw.line((a[0], a[1], b[0], b[1]), fill=(31, 30, 26, 255), width=9)
        draw.line((a[0], a[1], b[0], b[1]), fill=color, width=4)

    draw_node(canvas, draw, (485, 235), load_sprite("sickle", "honest"), "Pozorovatel", "prokázáno", "proven")
    draw_node(canvas, draw, (675, 235), load_sprite("axe", "common"), "Učeň", "prokázáno", "proven")
    draw_node(canvas, draw, (865, 235), load_sprite("hoe", "common"), "Tovaryš", "46 %", "current")
    draw_node(canvas, draw, (865, 480), load_sprite("sickle", "heard_blurred"), "Specialista", "zaslechnuto", "heard")
    draw_node(canvas, draw, (675, 595), load_sprite("axe", "unknown_fogged"), "Mistr", "neznámé", "unknown")
    draw_node(canvas, draw, (525, 755), load_sprite("sickle", "unknown_fogged"), "Inovátor", "neznámé", "unknown")
    draw_node(canvas, draw, (870, 755), load_sprite("hoe", "unknown_fogged"), "Mistr dílny", "neznámé", "unknown")

    # Selected milestone detail.
    panel(draw, (1070, 66, 1528, 835), (48, 47, 39, 255), (112, 82, 45, 255), 13, 2)
    text(draw, (1100, 95), "TOVARYŠ", 16, (190, 132, 59, 255))
    text(draw, (1100, 124), "Celý pracovní nástroj", 25, (238, 220, 180, 255), serif=True)
    text(draw, (1100, 166), "Co už Patrik zvládá", 17, (218, 197, 153, 255), serif=True)
    bullets = [
        "• odhad vhodného kusu železa",
        "• tepelná úprava běžného ostří",
        "• oprava vlastního přehřátí",
        "• srp a sekera v běžné jakosti",
    ]
    yy = 198
    for item in bullets:
        text(draw, (1110, yy), item, 15, (199, 192, 174, 255))
        yy += 29

    text(draw, (1100, 334), "Praktický důkaz", 17, (218, 197, 153, 255), serif=True)
    panel(draw, (1098, 366, 1500, 437), (59, 58, 48, 255), (93, 81, 57, 255), 9, 1)
    text(draw, (1115, 382), "Vyrob nebo oprav celý nástroj a", 15, (205, 198, 179, 255))
    text(draw, (1115, 407), "obhaj volbu postupu před Benešem.", 15, (205, 198, 179, 255))

    text(draw, (1100, 472), "Známé vady", 17, (218, 197, 153, 255), serif=True)
    text(draw, (1110, 505), "Prasklina u trnu", 15, (213, 170, 137, 255))
    text(draw, (1110, 532), "Měkké nebo přepálené ostří", 15, (213, 170, 137, 255))
    text(draw, (1110, 559), "Křivé nasazení topůrka", 15, (213, 170, 137, 255))

    text(draw, (1100, 610), "Výsledek práce", 17, (218, 197, 153, 255), serif=True)
    quality = [
        ("ruined", "Zkažený"), ("provisional", "Provizorní"), ("common", "Běžný"),
        ("honest", "Poctivý"), ("excellent", "Výborný"), ("masterwork", "Mistrovský"),
    ]
    x = 1110
    for index, (state, label) in enumerate(quality):
        sprite = load_sprite("sickle", state).resize((58, 58), Image.Resampling.LANCZOS)
        canvas.alpha_composite(sprite, (x, 645))
        text(draw, (x + 29, 713), label, 10, (177, 169, 151, 255), anchor="mm")
        x += 64

    panel(draw, (1098, 750, 1500, 823), (69, 54, 38, 255), (154, 104, 48, 255), 9, 2)
    text(draw, (1116, 768), "Nejlepší výsledek: Běžný srp", 15, (235, 210, 159, 255))
    text(draw, (1116, 796), "Další pokrok: 2 samostatné zakázky", 13, (184, 174, 153, 255))

    canvas.convert("RGB").save(ROOT / "ui_mockup_smithing_craft_page.png", quality=95)


if __name__ == "__main__":
    main()
