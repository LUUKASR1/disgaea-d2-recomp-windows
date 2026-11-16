"""Regenerate the launcher artwork (cartoon buttons, home banner, logo). Needs Pillow; not part of the build.

    python tools/make_launcher_art.py <folder with in-game screenshots> <disgaea-logo.png with transparency>

Writes into windows/assets: hero-1..4.png (banner slides), logo-disgaea.png, pill.png and btn-*.png (n = normal,
h = hover, p = pressed, d = disabled).
"""
import math
from pathlib import Path
import sys

from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'windows/assets'
BG = (19, 14, 29)
S = 3  # supersampling


def gradient(w, h, top, bottom):
    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return img


def rounded_mask(size, box, radius):
    mask = Image.new('L', size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(box, radius, fill=255)
    return mask


def draw_icon(layer, kind, cx, cy, size, edge):
    d = ImageDraw.Draw(layer)
    if kind == 'play':
        pts = [(cx - size * 0.35, cy - size * 0.5), (cx - size * 0.35, cy + size * 0.5), (cx + size * 0.5, cy)]
        grow = [(cx + (x - cx) * 1.0 + (-4 if x < cx else 5) * S, cy + (y - cy) + (-4 if y < cy else (4 if y > cy else 0)) * S) for x, y in pts]
        d.polygon(grow, fill=edge)
        d.polygon(pts, fill=(255, 255, 255, 255))
    elif kind == 'folder':
        w, h = size * 1.1, size * 0.8
        x0, y0 = cx - w / 2, cy - h / 2
        for pad, fill in ((3 * S, edge), (0, (255, 255, 255, 255))):
            d.rounded_rectangle((x0 - pad, y0 + h * 0.16 - pad, x0 + w + pad, y0 + h + pad), 4 * S, fill=fill)
            d.rounded_rectangle((x0 - pad, y0 - pad, x0 + w * 0.46 + pad, y0 + h * 0.3 + pad), 3 * S, fill=fill)
    elif kind == 'gear':
        r = size * 0.38
        for pad, fill in ((3 * S, edge), (0, (255, 255, 255, 255))):
            for k in range(8):
                a = k * math.pi / 4
                tx, ty = cx + math.cos(a) * r * 1.05, cy + math.sin(a) * r * 1.05
                d.ellipse((tx - r * 0.32 - pad, ty - r * 0.32 - pad, tx + r * 0.32 + pad, ty + r * 0.32 + pad), fill=fill)
            d.ellipse((cx - r - pad, cy - r - pad, cx + r + pad, cy + r + pad), fill=fill)
        d.ellipse((cx - r * 0.42, cy - r * 0.42, cx + r * 0.42, cy + r * 0.42), fill=(0, 0, 0, 0))
        d.ellipse((cx - r * 0.42, cy - r * 0.42, cx + r * 0.42, cy + r * 0.42), outline=edge, width=2 * S)


def cartoon_button(w, h, top, bottom, edge, icon=None, state='n'):
    W, H = w * S, h * S
    body = (3 * S, 2 * S, W - 3 * S, H - 9 * S)
    radius = (body[3] - body[1]) * 0.34
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    shadow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((body[0], body[1] + 6 * S, body[2], body[3] + 6 * S), radius, fill=(0, 0, 0, 140))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(3 * S)))
    outline = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(outline).rounded_rectangle(body, radius, fill=edge + (255,))
    img.alpha_composite(outline)
    inner = (body[0] + 3 * S, body[1] + 3 * S, body[2] - 3 * S, body[3] - 3 * S)
    inner_radius = radius - 3 * S
    fill = gradient(W, H, top, bottom).convert('RGBA')
    fill.putalpha(rounded_mask((W, H), inner, inner_radius))
    img.alpha_composite(fill)
    # darker lower band
    band = Image.new('RGBA', (W, H), bottom + (0,))
    mask = Image.new('L', (W, H), 0)
    ImageDraw.Draw(mask).rectangle((0, inner[1] + (inner[3] - inner[1]) * 0.66, W, H), fill=70)
    band.putalpha(ImageChops.multiply(mask, rounded_mask((W, H), inner, inner_radius)))
    img.alpha_composite(band)
    # glossy highlight on the upper half
    gloss = Image.new('RGBA', (W, H), (255, 255, 255, 0))
    gm = Image.new('L', (W, H), 0)
    gx0, gx1 = inner[0] + 7 * S, inner[2] - 7 * S
    gy0, gy1 = inner[1] + 3 * S, inner[1] + (inner[3] - inner[1]) * 0.5
    ImageDraw.Draw(gm).rounded_rectangle((gx0, gy0, gx1, gy1), radius * 0.6, fill=255)
    fade = gradient(W, H, (110,) * 3, (22,) * 3).convert('L')
    gloss.putalpha(ImageChops.multiply(gm, fade))
    img.alpha_composite(gloss)
    if icon:
        layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        draw_icon(layer, icon, body[0] + (body[3] - body[1]) * 0.62, (body[1] + body[3]) / 2, (body[3] - body[1]) * 0.46, edge + (255,))
        img.alpha_composite(layer)
    img = img.resize((w, h), Image.LANCZOS)
    rgb, alpha = img.convert('RGB'), img.getchannel('A')
    if state == 'h':
        rgb = ImageEnhance.Brightness(rgb).enhance(1.14)
    elif state == 'p':
        rgb = ImageEnhance.Brightness(rgb).enhance(0.86)
    elif state == 'd':
        rgb = ImageEnhance.Brightness(ImageEnhance.Color(rgb).enhance(0.0)).enhance(0.8)
    out = rgb.convert('RGBA')
    out.putalpha(alpha)
    if state == 'p':  # pressed buttons sink 2 px
        shifted = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        shifted.paste(out, (0, 2))
        out = shifted
    return out


STYLES = {
    'red': ((255, 98, 118), (206, 30, 72), (86, 12, 38)),
    'blue': ((96, 176, 255), (40, 100, 226), (16, 34, 98)),
    'purple': ((170, 124, 255), (104, 62, 214), (38, 20, 96)),
}


def make_buttons():
    spec = [('play', 'red', 340, 84, 'play'), ('iso', 'blue', 164, 60, 'folder'), ('gear', 'blue', 164, 60, 'gear'),
            ('small', 'purple', 170, 46, None)]
    for name, style, w, h, icon in spec:
        top, bottom, edge = STYLES[style]
        for state in 'nhpd':
            cartoon_button(w, h, top, bottom, edge, icon, state).save(OUT / ('btn-%s-%s.png' % (name, state)), optimize=True)
    pill = Image.new('RGBA', (340 * S, 34 * S), (0, 0, 0, 0))
    ImageDraw.Draw(pill).rounded_rectangle((0, 0, 340 * S - 1, 34 * S - 1), 17 * S, fill=(14, 8, 24, 215), outline=(255, 255, 255, 70), width=S)
    pill.resize((340, 34), Image.LANCZOS).save(OUT / 'pill.png', optimize=True)


def make_logo(path):
    logo = Image.open(path).convert('RGBA')
    box = logo.getchannel('A').getbbox()
    logo = logo.crop(box)
    logo.thumbnail((336, 190), Image.LANCZOS)
    logo.save(OUT / 'logo-disgaea.png', optimize=True)
    print('logo', logo.size)


SLIDES = [('DisgaeaD2_20261115_212458_257.png', 0.30), ('DisgaeaD2_20261115_212602_217.png', 0.52),
          ('DisgaeaD2_20261115_211801_367.png', 0.40), ('DisgaeaD2_20261115_212608_877.png', 0.42)]
W, H, ART_LEFT = 760, 430, 240


def make_slides(folder):
    for number, (name, cx) in enumerate(SLIDES, 1):
        shot = Image.open(Path(folder) / name).convert('RGB')
        aw = W - ART_LEFT
        crop_w = round(shot.height * aw / H)
        x0 = int(max(0, min(shot.width - crop_w, cx * shot.width - crop_w / 2)))
        art = shot.crop((x0, 0, x0 + crop_w, shot.height)).resize((aw, H), Image.LANCZOS)
        art = ImageEnhance.Color(art).enhance(1.1)
        canvas = Image.new('RGB', (W, H), BG)
        canvas.paste(art, (ART_LEFT, 0))
        # horizontal fade into the launcher colour: solid up to x=250, clear again from x~520
        mask = Image.new('L', (W, H), 0)
        mp = mask.load()
        for x in range(W):
            if x <= 250: v = 255
            elif x >= 560: v = 0
            else:
                t = (x - 250) / 310
                v = round(255 * (1 - t) ** 1.6)
            for y in range(H): mp[x, y] = v
        # soft bottom fade and top fade
        for y in range(H):
            edge = max(0, (y - (H - 46)) / 46)
            top = max(0, (30 - y) / 30)
            k = max(edge, top) * 255
            if k:
                for x in range(W): mp[x, y] = max(mp[x, y], round(k))
        canvas = Image.composite(Image.new('RGB', (W, H), BG), canvas, mask)
        canvas.save(OUT / ('hero-%d.png' % number), optimize=True)
        print('hero', number, (OUT / ('hero-%d.png' % number)).stat().st_size // 1024, 'KB')


if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    make_buttons()
    make_logo(sys.argv[2])
    make_slides(sys.argv[1])
