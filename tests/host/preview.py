"""Turns tests/host/preview.c's frames into previews: an animated GIF and a still PNG per sequence, and a contact
sheet per effect with the "off" state beside the "on" one.

What is the plugin's and what is a stand-in, so nobody mistakes a preview for a photo of an OP-1:
  - the picture in the middle is the plugin's own drawing code, run on the PC with its own audio code
    (opus_mock.h rasterises the core's opus_page_* calls with plain integer lines: the OS smooths them);
  - the corner text stands in for the core's effect page (the four labels in the encoders' colours over their values,
    blue top left, green bottom left, white top right, orange bottom right); the core's real font and places differ;
  - "off" is a simulation of the OS's own effect-off overlay (fx_bypass: the whole frame dimmed, an "off" badge),
    which on the OP-1 the OS draws over the page, the plugin's picture included.
The colours are the stock encoder colours; on the OP-1 they follow the user's theme.

    python3 -I tests/host/preview.py FRAMES_DIR OUT_DIR
Copyright (C) 2026 sammi. GPL-3.0-or-later with the OP-1 firmware permission."""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H, S = 320, 160, 3
VIS = 148                                  # the OP-1's window shows the drawing to about y 147
ENC = [(0x69, 0x8E, 0xFF), (0x00, 0xED, 0x95), (0xE6, 0xE1, 0xFF), (0xFF, 0x3A, 0x5D)]
CORNER = [(8, 6), (8, 112), (312, 6), (312, 112)]  # blue top left, green bottom left, white top right, orange bottom right


def font(size):
    try:
        return ImageFont.load_default(size)
    except TypeError:
        return ImageFont.load_default()


F_LABEL, F_VALUE = font(9), font(15)


def frames(path):
    data = open(path, "rb").read()
    n = len(data) // (W * H * 3)
    return [Image.frombytes("RGB", (W, H), data[i * W * H * 3:(i + 1) * W * H * 3]) for i in range(n)]


def texts(path):
    out = []
    for line in open(path):
        p = line.rstrip("\n").split("|")
        out.append([(p[2 * i], p[2 * i + 1]) for i in range(4)])
    return out


def compose(img, tx, off=False):
    im = img.copy()
    d = ImageDraw.Draw(im)
    for i, (label, value) in enumerate(tx):
        x, y = CORNER[i]
        anchor = "la" if i < 2 else "ra"
        d.text((x, y), label, fill=ENC[i], font=F_LABEL, anchor=anchor)
        d.text((x, y + 11), value, fill=(255, 255, 255), font=F_VALUE, anchor=anchor)
    if off:                                # the OS's overlay, simulated: dim everything, the badge
        im = Image.eval(im, lambda v: v * 30 // 100)
        d = ImageDraw.Draw(im)
        d.rectangle((136, 66, 184, 84), fill=(0, 0, 0), outline=(150, 150, 150))
        d.text((160, 75), "off", fill=(200, 200, 200), font=F_VALUE, anchor="mm")
    im = im.crop((0, 0, W, VIS))
    return im.resize((W * S, VIS * S), Image.NEAREST)


def lit(img):
    return sum(1 for p in (img.get_flattened_data() if hasattr(img, "get_flattened_data") else img.getdata()) if p != (0, 0, 0) and not (p[0] == p[1] == p[2]))


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    names = sorted(f[:-7] for f in os.listdir(src) if f.endswith(".frames"))
    stills = {}
    for name in names:
        fr, tx = frames(f"{src}/{name}.frames"), texts(f"{src}/{name}.txt")
        imgs = [compose(f, t) for f, t in zip(fr, tx)]
        imgs[0].save(f"{out}/{name}.gif", save_all=True, append_images=imgs[1:], duration=33, loop=0, optimize=True)
        k = max(range(len(fr) // 4, len(fr)), key=lambda i: lit(fr[i])) if len(fr) > 4 else 0   # the liveliest frame
        imgs[k].save(f"{out}/{name}.png")
        stills[name] = (fr[k], tx[k])
        print(f"{name}: {len(fr)} frames -> {name}.gif, {name}.png (frame {k})")
    for effect in ("reverb", "stutter"):
        mine = [n for n in names if n.startswith(effect)]
        if not mine:
            continue
        first = mine[0] if f"{effect}_default" not in mine else f"{effect}_default"
        if effect == "stutter":
            first = "stutter_repeat" if "stutter_repeat" in mine else first
        cells = [(n, compose(*stills[n])) for n in mine] + [(f"{first} (off)", compose(*stills[first], off=True))]
        cw, ch = W * S // 2, VIS * S // 2
        cols = 2
        rows = (len(cells) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cw, rows * (ch + 22)), (16, 16, 16))
        d = ImageDraw.Draw(sheet)
        for i, (n, im) in enumerate(cells):
            x, y = (i % cols) * cw, (i // cols) * (ch + 22)
            sheet.paste(im.resize((cw, ch), Image.NEAREST), (x, y + 22))
            d.text((x + 6, y + 4), n.replace("_", " "), fill=(200, 200, 200), font=font(13))
        sheet.save(f"{out}/{effect}_sheet.png")
        print(f"{effect}_sheet.png: {len(cells)} cells")


if __name__ == "__main__":
    main()
