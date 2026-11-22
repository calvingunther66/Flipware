#!/usr/bin/env python3
"""
One-shot generator for the Flipware brand art that replaced Momentum's.

Writes:
  assets/icons/MainMenu/Momentum_14/frame_XX.png   main-menu icon (a flipping "F")
  assets/icons/ControlCenter/CC_Momentum_16x16.png lock-menu tile icon
  assets/slideshow/firstboot/frame_0{0,1,4,5,6}.png intro slides
  assets/slideshow/update_default/frame_00.png     "updated" splash after an update

The icon file and symbol names stay the same on purpose: they are part of
the app API (A_Momentum_14, I_CC_Momentum_16x16), so renaming them would
break existing apps.

The slides are edited in place (only the branded regions are redrawn), so
run it once, on Momentum's original slides.
"""
import pathlib

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
SLIDES = ROOT / "assets/slideshow/firstboot"
MENU_ICON = ROOT / "assets/icons/MainMenu/Momentum_14"
CC_ICON = ROOT / "assets/icons/ControlCenter/CC_Momentum_16x16.png"
README_LOGO = ROOT / ".github/assets"

BLACK, WHITE = 0, 1

# Wordmark: 5x7 bold capitals, drawn at 2x (10x14, 2px stems).
BIG = {
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "I": ["###", ".#.", ".#.", ".#.", ".#.", ".#.", "###"],
    "P": ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
    "A": [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    "R": ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    "E": ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
}

# Small text: glyphs lifted from the slides' own font (rows: 2 ascender
# rows, 5 x-height rows, 2 descender rows, 1 spare).
SMALL = {
    "F": ["####", "#...", "#...", "####", "#...", "#...", "#...", "....", "....", "...."],
    "l": ["#", "#", "#", "#", "#", "#", "#", ".", ".", "."],
    "i": ["#", ".", "#", "#", "#", "#", "#", ".", ".", "."],
    "p": ["....", "....", "###.", "#..#", "#..#", "#..#", "###.", "#...", "#...", "...."],
    "w": [".....", ".....", "#.#.#", "#.#.#", "#.#.#", ".#.#.", ".#.#.", ".....", ".....", "....."],
    "a": ["....", "....", "###.", "...#", ".###", "#..#", ".###", "....", "....", "...."],
    "r": ["..", "..", "##", "#.", "#.", "#.", "#.", "..", "..", ".."],
    "e": ["....", "....", ".##.", "#..#", "####", "#...", ".###", "....", "....", "...."],
    "m": [".......", ".......", "###.##.", "#..#..#", "#..#..#", "#..#..#", "#..#..#", ".......", ".......", "......."],
    "-": ["..", "..", "..", "..", "##", "..", "..", "..", "..", ".."],
}

# Hollow "F", same outline style as the old tile glyph (11x14).
HOLLOW_F = [
    "###########",
    "#.........#",
    "#.#########",
    "#.#........",
    "#.#........",
    "#.######...",
    "#......#...",
    "#.######...",
    "#.#........",
    "#.#........",
    "#.#........",
    "#.#........",
    "#.#........",
    "###........",
]


def fill(img, box, color=WHITE):
    x0, y0, x1, y1 = box
    px = img.load()
    for y in range(y0, y1):
        for x in range(x0, x1):
            px[x, y] = color


def blit(img, rows, x, y, scale=1):
    px = img.load()
    for dy, row in enumerate(rows):
        for dx, c in enumerate(row):
            if c != "#":
                continue
            for sy in range(scale):
                for sx in range(scale):
                    xx, yy = x + dx * scale + sx, y + dy * scale + sy
                    if 0 <= xx < img.width and 0 <= yy < img.height:
                        px[xx, yy] = BLACK


def big_width(text, gap):
    return sum(len(BIG[c][0]) * 2 for c in text) + gap * (len(text) - 1)


def draw_big(img, text, x, y, gap=2):
    for c in text:
        blit(img, BIG[c], x, y, 2)
        x += len(BIG[c][0]) * 2 + gap


def small_width(text):
    w = 0
    for c in text:
        w += 4 if c == " " else len(SMALL[c][0]) + 1
    return w - 1


def draw_small(img, text, x, y):
    for c in text:
        if c == " ":
            x += 4
            continue
        blit(img, SMALL[c], x, y)
        x += len(SMALL[c][0]) + 1
    return x


def slide(n):
    return Image.open(SLIDES / f"frame_{n:02d}.png").convert("1")


def save_slide(img, n):
    img.save(SLIDES / f"frame_{n:02d}.png")


def make_slides():
    # 0: "Welcome to" + wordmark + "Firmware"; NEXT button untouched.
    img = slide(0)
    fill(img, (0, 15, 128, 46))
    w = big_width("FLIPWARE", 2)
    draw_big(img, "FLIPWARE", (128 - w) // 2, 19)
    t = small_width("Firmware")
    tx = (128 - t) // 2
    draw_small(img, "Firmware", tx, 36)
    fill(img, ((128 - w) // 2, 40, tx - 4, 41), BLACK)
    fill(img, (tx + t + 4, 40, (128 + w) // 2, 41), BLACK)
    save_slide(img, 0)

    # 1 and 4: the third lock-menu tile shows the app glyph.
    for n, (x, y) in ((1, (75, 14)), (4, (91, 41))):
        img = slide(n)
        fill(img, (x - 5, y, x + 17, y + 14))
        blit(img, HOLLOW_F, x, y)
        save_slide(img, n)

    # 4: "- Momentum Settings" -> "- Flipware Settings", re-centred.
    img = slide(4)
    settings = img.crop((77, 22, 109, 32))
    fill(img, (0, 22, 128, 32))
    lead = "- Flipware "
    total = small_width(lead) + settings.width
    x = (128 - total) // 2
    x = draw_small(img, lead, x, 22)
    img.paste(settings, (x, 22))
    save_slide(img, 4)

    # 5: big logo glyph on the left.
    img = slide(5)
    fill(img, (0, 0, 50, 46))
    blit(img, BIG["F"], 6, 2, 6)
    save_slide(img, 5)

    # 6: wordmark left of the OK button; drop the ribbon and old tagline.
    img = slide(6)
    fill(img, (0, 21, 88, 39))
    fill(img, (0, 39, 128, 64))
    w = big_width("FLIPWARE", 1)
    draw_big(img, "FLIPWARE", 2, 24, gap=1)
    fill(img, (2, 42, 126, 43), BLACK)
    fill(img, (2, 44, 126, 45), BLACK)
    save_slide(img, 6)


def make_update_splash():
    # Wordmark + "Firmware" in place of the old logo; the OK/Updated box stays.
    path = ROOT / "assets/slideshow/update_default/frame_00.png"
    img = Image.open(path).convert("1")
    fill(img, (0, 0, 128, 38))
    w = big_width("FLIPWARE", 2)
    x = (128 - w) // 2
    draw_big(img, "FLIPWARE", x, 9)
    t = small_width("Firmware")
    tx = (128 - t) // 2
    draw_small(img, "Firmware", tx, 27)
    fill(img, (x, 31, tx - 4, 32), BLACK)
    fill(img, (tx + t + 4, 31, x + w, 32), BLACK)
    img.save(path)


def f_frame(width, back=False):
    """10x14 "F" squeezed to `width` around the centre; back = card back."""
    img = Image.new("1", (14, 14), WHITE)
    full = Image.new("1", (10, 14), WHITE)
    if back:
        fill(full, (0, 0, 10, 14), BLACK)
        fill(full, (2, 2, 8, 12), WHITE)
    else:
        blit(full, BIG["F"], 0, 0, 2)
    if width <= 1:
        fill(img, (6, 0, 7, 14), BLACK)
        return img
    squeezed = full.resize((width, 14), Image.NEAREST)
    img.paste(squeezed, (7 - width // 2 - (width % 2), 0))
    return img


def make_menu_icon():
    # A card flip: front, edge, back, edge, front again (11 frames, 3 fps).
    frames = [
        f_frame(10),
        f_frame(10),
        f_frame(7),
        f_frame(3),
        f_frame(1),
        f_frame(4, back=True),
        f_frame(8, back=True),
        f_frame(4, back=True),
        f_frame(1),
        f_frame(3),
        f_frame(7),
    ]
    for i, fr in enumerate(frames, 1):
        fr.save(MENU_ICON / f"frame_{i:02d}.png")


def make_cc_icon():
    img = Image.new("1", (16, 16), WHITE)
    blit(img, HOLLOW_F, 3, 1)
    img.save(CC_ICON)


def make_readme_logo():
    # Wordmark at 2x, then scaled 6x for the README (light and dark).
    w = big_width("FLIPWARE", 2)
    img = Image.new("1", (w + 8, 22), WHITE)
    draw_big(img, "FLIPWARE", 4, 4)
    big = img.resize((img.width * 6, img.height * 6), Image.NEAREST).convert("RGBA")
    px = big.load()
    for name, ink in (("flipware_logo_light.png", (0, 0, 0, 255)), ("flipware_logo_dark.png", (255, 255, 255, 255))):
        out = Image.new("RGBA", big.size, (0, 0, 0, 0))
        opx = out.load()
        for y in range(big.height):
            for x in range(big.width):
                if px[x, y][0] == 0:
                    opx[x, y] = ink
        out.save(README_LOGO / name)


if __name__ == "__main__":
    make_readme_logo()
    make_slides()
    make_update_splash()
    make_menu_icon()
    make_cc_icon()
