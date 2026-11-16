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
BG = (244, 247, 253)  # light launcher background (matches BG in launcher_gui.py)
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
    ImageDraw.Draw(shadow).rounded_rectangle((body[0], body[1] + 6 * S, body[2], body[3] + 6 * S), radius, fill=(40, 52, 96, 95))
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
    'yellow': ((255, 234, 104), (250, 176, 24), (112, 60, 6)),
}


def make_buttons():
    spec = [('play', 'red', 340, 84, 'play'), ('iso', 'blue', 164, 60, 'folder'), ('gear', 'blue', 164, 60, 'gear'),
            ('small', 'purple', 170, 46, None),
            ('nav', 'yellow', 176, 48, None), ('navs', 'red', 176, 48, None)]
    for name, style, w, h, icon in spec:
        top, bottom, edge = STYLES[style]
        for state in 'nhpd':
            cartoon_button(w, h, top, bottom, edge, icon, state).save(OUT / ('btn-%s-%s.png' % (name, state)), optimize=True)
    pill = Image.new('RGBA', (340 * S, 34 * S), (0, 0, 0, 0))
    ImageDraw.Draw(pill).rounded_rectangle((0, 0, 340 * S - 1, 34 * S - 1), 17 * S, fill=(255, 255, 255, 215), outline=(150, 165, 205, 160), width=S)
    pill.resize((340, 34), Image.LANCZOS).save(OUT / 'pill.png', optimize=True)


def make_logo(path):
    logo = Image.open(path).convert('RGBA')
    box = logo.getchannel('A').getbbox()
    logo = logo.crop(box)
    logo.thumbnail((336, 190), Image.LANCZOS)
    logo.save(OUT / 'logo-disgaea.png', optimize=True)
    print('logo', logo.size)


SLIDES = [7, 2, 4, 5, 1]  # indexes into the sorted screenshot folder: title, city, battle target, attack, room
W, H = 760, 430
PANEL_BOX = (14, 14, 366, 416)  # frosted glass panel baked into each slide


def make_slides(folder):
    files = sorted(Path(folder).glob('*.png'))
    for number, index in enumerate(SLIDES, 1):
        shot = Image.open(files[index]).convert('RGB')
        crop_h = round(shot.width * H / W)
        y0 = max(0, (shot.height - crop_h) // 2)
        art = shot.crop((0, y0, shot.width, y0 + crop_h)).resize((W, H), Image.LANCZOS)
        art = ImageEnhance.Color(art).enhance(1.08)
        # frosted glass: blur + whiten the area behind the panel
        glass = art.filter(ImageFilter.GaussianBlur(14))
        glass = Image.blend(glass, Image.new('RGB', (W, H), (255, 255, 255)), 0.66)
        mask = rounded_mask((W * S, H * S), tuple(v * S for v in PANEL_BOX), 26 * S).resize((W, H), Image.LANCZOS)
        art = Image.composite(glass, art, mask)
        ring = Image.new('RGBA', (W * S, H * S), (0, 0, 0, 0))
        ImageDraw.Draw(ring).rounded_rectangle(tuple(v * S for v in PANEL_BOX), 26 * S, outline=(255, 255, 255, 235), width=2 * S)
        art = Image.alpha_composite(art.convert('RGBA'), ring.resize((W, H), Image.LANCZOS)).convert('RGB')
        # rounded outer corners in the launcher colour
        corners = rounded_mask((W * S, H * S), (0, 0, W * S - 1, H * S - 1), 22 * S).resize((W, H), Image.LANCZOS)
        art = Image.composite(art, Image.new('RGB', (W, H), BG), corners)
        art.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(OUT / ('hero-%d.png' % number), optimize=True)
        print('hero', number, (OUT / ('hero-%d.png' % number)).stat().st_size // 1024, 'KB')


def make_sidebar():
    """Tall frosted-glass gradient for the side menu (220 px wide)."""
    w, h = 220, 1400
    img = gradient(w, h, (255, 255, 255), (214, 224, 250)).convert('RGBA')
    orbs = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(orbs)
    d.ellipse((-70, 360, 150, 580), fill=(255, 201, 92, 90))
    d.ellipse((90, 700, 300, 910), fill=(255, 120, 150, 70))
    d.ellipse((-40, 1040, 190, 1270), fill=(150, 170, 255, 90))
    img.alpha_composite(orbs.filter(ImageFilter.GaussianBlur(38)))
    ImageDraw.Draw(img).line((w - 1, 0, w - 1, h), fill=(255, 255, 255, 255), width=1)
    img.convert('RGB').save(OUT / 'sidebar-glass.png', optimize=True)


if __name__ == '__main__':
    OUT.mkdir(parents=True, exist_ok=True)
    make_buttons()
    make_logo(sys.argv[2])
    make_slides(sys.argv[1])
    make_sidebar()
