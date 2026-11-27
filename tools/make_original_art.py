"""Generate the project's own artwork (no game art): launcher logos, icon and home slides, Android home images.

    python tools/make_original_art.py

Everything is drawn from shapes and the Poppins font (SIL Open Font License), so the release files hold no artwork from
the game. The emblem (a ring crowned with two horns around a gem) is the one already used by the Android app icon.
At run time the launcher and the app also show the player's own PIC1.PNG from their game files when it is available.
"""
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
WIN = ROOT / 'windows/assets'
DROID = ROOT / 'android/app/src/main/res/drawable-nodpi'
FONT = '/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf'
FONT_MEDIUM = '/usr/share/fonts/truetype/google-fonts/Poppins-Medium.ttf'
S = 4  # supersampling

RED, PINK, PURPLE, NIGHT, GOLD, CREAM = (224, 49, 79), (255, 94, 115), (106, 63, 214), (28, 18, 54), (255, 196, 64), (244, 234, 251)


def gradient(w, h, stops, horizontal=False):
    img = Image.new('RGB', (w, h))
    draw = ImageDraw.Draw(img)
    n = w if horizontal else h
    for i in range(n):
        t = i / max(1, n - 1)
        for k in range(len(stops) - 1):
            a, b = stops[k], stops[k + 1]
            if a[0] <= t <= b[0]:
                u = (t - a[0]) / max(1e-6, b[0] - a[0])
                c = tuple(round(a[1][j] + (b[1][j] - a[1][j]) * u) for j in range(3))
                break
        draw.line([(i, 0), (i, h)] if horizontal else [(0, i), (w, i)], fill=c)
    return img


def emblem(size, color=CREAM, gem=PINK):
    """The app-icon emblem drawn on a transparent square (same geometry as ic_launcher_foreground.xml, 108 units)."""
    k = size * S / 108.0
    img = Image.new('RGBA', (size * S, size * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def bez(p0, p1, p2, p3, steps=40):
        return [((1 - t) ** 3 * p0[0] + 3 * (1 - t) ** 2 * t * p1[0] + 3 * (1 - t) * t * t * p2[0] + t ** 3 * p3[0],
                 (1 - t) ** 3 * p0[1] + 3 * (1 - t) ** 2 * t * p1[1] + 3 * (1 - t) * t * t * p2[1] + t ** 3 * p3[1])
                for t in [i / steps for i in range(steps + 1)]]

    for side in (1, -1):
        cx = 54
        def m(p):
            return ((cx + side * (p[0] - cx)) * k, p[1] * k)
        pts = bez((38, 46), (30, 40), (28, 30), (32, 22)) + bez((32, 22), (38, 28), (46, 34), (48, 42))
        d.polygon([m(p) for p in pts], fill=color)
    r = 22 * k
    d.ellipse([54 * k - r, 60 * k - r, 54 * k + r, 60 * k + r], outline=color, width=round(4 * k))
    g = [(54, 46), (64, 60), (54, 74), (44, 60)]
    d.polygon([(x * k, y * k) for x, y in g], fill=gem)
    d.polygon([(x * k, y * k) for x, y in [(54, 46), (64, 60), (54, 60)]], fill=tuple(min(255, c + 40) for c in gem))
    return img.resize((size, size), Image.LANCZOS)


def text_layer(text, font_path, size, fill, stroke=0, stroke_fill=(0, 0, 0), shadow=True):
    font = ImageFont.truetype(font_path, size * S)
    box = font.getbbox(text, stroke_width=stroke * S)
    w, h = box[2] - box[0] + 8 * S, box[3] - box[1] + 8 * S
    img = Image.new('RGBA', (w + 6 * S, h + 6 * S), (0, 0, 0, 0))
    if shadow:
        sh = Image.new('RGBA', img.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).text((4 * S - box[0] + 3 * S, 4 * S - box[1] + 4 * S), text, font=font, fill=(20, 8, 40, 150),
                                stroke_width=stroke * S, stroke_fill=(20, 8, 40, 150))
        img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(3 * S)))
    ImageDraw.Draw(img).text((4 * S - box[0], 4 * S - box[1]), text, font=font, fill=fill, stroke_width=stroke * S,
                             stroke_fill=stroke_fill)
    return img.resize((img.width // S, img.height // S), Image.LANCZOS)


def wordmark(width, height, subtitle, sub_color, with_emblem=True, title='DISGAEA D2', vertical=False):
    """Plain-type title with the emblem: the game's name in an ordinary font, not the game's logo."""
    img = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    if vertical:
        em = emblem(round(height * 0.46))
        top = wordmark(width, round(height * 0.56), subtitle, sub_color, with_emblem=False, title=title)
        glow = Image.new('RGBA', img.size, (0, 0, 0, 0))
        cx, cy, r = width / 2, em.height / 2 + 2, em.width * 0.5
        ImageDraw.Draw(glow).ellipse([cx - r, cy - r, cx + r, cy + r], fill=PURPLE + (200,))
        img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(height * 0.04)))
        img.alpha_composite(em, ((width - em.width) // 2, 2))
        img.alpha_composite(top, (0, height - top.height))
        return img
    em = emblem(round(height * 0.62)) if with_emblem else None
    title_layer = text_layer(title, FONT, max(10, round(height * 0.24)), (255, 255, 255), stroke=max(1, round(height * 0.02)),
                             stroke_fill=RED)
    sub_layer = text_layer(subtitle, FONT, max(8, round(height * 0.17)), sub_color, stroke=max(1, round(height * 0.012)),
                           stroke_fill=NIGHT)
    block_w = max(title_layer.width, sub_layer.width)
    total_w = block_w + ((em.width + round(height * 0.04)) if em else 0)
    scale = min(1.0, (width * 0.98) / total_w)
    if scale < 1:
        title_layer = title_layer.resize((round(title_layer.width * scale), round(title_layer.height * scale)), Image.LANCZOS)
        sub_layer = sub_layer.resize((round(sub_layer.width * scale), round(sub_layer.height * scale)), Image.LANCZOS)
        if em:
            em = em.resize((round(em.width * scale), round(em.height * scale)), Image.LANCZOS)
        block_w = max(title_layer.width, sub_layer.width)
        total_w = block_w + ((em.width + round(height * 0.04 * scale)) if em else 0)
    x = (width - total_w) // 2
    if em:
        glow = Image.new('RGBA', img.size, (0, 0, 0, 0))
        r = em.width * 0.55
        cx, cy = x + em.width / 2, height / 2
        ImageDraw.Draw(glow).ellipse([cx - r, cy - r, cx + r, cy + r], fill=PURPLE + (170,))
        img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(height * 0.05)))
        img.alpha_composite(em, (x, (height - em.height) // 2))
        x += em.width + round(height * 0.04 * scale)
    block_h = title_layer.height + sub_layer.height
    y = (height - block_h) // 2
    img.alpha_composite(title_layer, (x + (block_w - title_layer.width) // 2, y))
    img.alpha_composite(sub_layer, (x + (block_w - sub_layer.width) // 2, y + title_layer.height - round(height * 0.03)))
    return img


def night_scene(w, h, seed, top, bottom, moon=True):
    """An original night sky with a moon, stars and layered hills crowned by a little castle silhouette."""
    rnd = random.Random(seed)
    W, H = w * 2, h * 2
    img = gradient(W, H, [(0, top), (0.65, bottom), (1, tuple(max(0, c - 40) for c in bottom))]).convert('RGBA')
    d = ImageDraw.Draw(img)
    for _ in range(140):
        x, y, r = rnd.uniform(0, W), rnd.uniform(0, H * 0.6), rnd.choice((1, 1, 1.5, 2, 3))
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, rnd.randint(110, 230)))
    if moon:
        mx, my, mr = W * rnd.uniform(0.62, 0.82), H * rnd.uniform(0.18, 0.3), H * 0.13
        glow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(glow).ellipse([mx - mr * 2, my - mr * 2, mx + mr * 2, my + mr * 2], fill=(255, 210, 230, 90))
        img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(mr)))
        d = ImageDraw.Draw(img)
        crescent = Image.new('L', (W, H), 0)
        cd = ImageDraw.Draw(crescent)
        cd.ellipse([mx - mr, my - mr, mx + mr, my + mr], fill=255)
        cd.ellipse([mx - mr * 0.55, my - mr * 0.95, mx + mr * 1.45, my + mr * 0.85], fill=0)
        img.paste(Image.new('RGBA', (W, H), (255, 236, 214, 255)), (0, 0), crescent)
        d = ImageDraw.Draw(img)
    layers = [(0.62, tuple(min(255, c + 25) for c in bottom)), (0.74, tuple(max(0, c - 20) for c in bottom)), (0.86, NIGHT)]
    for li, (base, color) in enumerate(layers):
        pts = [(0, H)]
        phase, amp, freq = rnd.uniform(0, 6), H * rnd.uniform(0.03, 0.06), rnd.uniform(1.5, 3.0)
        for i in range(0, W + 20, 20):
            pts.append((i, H * base + amp * math.sin(phase + i / W * freq * math.pi * 2)))
        pts.append((W, H))
        d.polygon(pts, fill=color + (255,))
        if li == 1:
            cx = W * rnd.uniform(0.55, 0.85)
            cy = H * base + amp * math.sin(phase + cx / W * freq * math.pi * 2)
            u = H * 0.025
            for tx, tw, th in ((-3, 1.6, 6), (0, 2.2, 8.5), (3, 1.6, 6)):
                x0 = cx + tx * u * 1.4
                d.rectangle([x0 - tw * u / 2, cy - th * u, x0 + tw * u / 2, cy + 2], fill=color + (255,))
                d.polygon([(x0 - tw * u * 0.7, cy - th * u), (x0, cy - th * u - 2.2 * u), (x0 + tw * u * 0.7, cy - th * u)],
                          fill=color + (255,))
            for wx in (-3, 0, 3):
                x0 = cx + wx * u * 1.4
                d.rectangle([x0 - u * 0.25, cy - u * 4, x0 + u * 0.25, cy - u * 3.2], fill=GOLD + (255,))
    for _ in range(6):
        x, y = rnd.uniform(0, W), rnd.uniform(H * 0.1, H * 0.55)
        em = emblem(round(H * rnd.uniform(0.05, 0.09)), color=(255, 255, 255), gem=PINK)
        faded = em.copy()
        faded.putalpha(em.getchannel('A').point(lambda a: a * 0.22))
        img.alpha_composite(faded, (round(x), round(y)))
    return img.resize((w, h), Image.LANCZOS).convert('RGB')


def glass_panel(img, box, radius):
    """Frosted panel at the left of the home slides (where the launcher draws the logo and buttons)."""
    x0, y0, x1, y1 = box
    blurred = img.crop(box).filter(ImageFilter.GaussianBlur(10))
    tint = Image.new('RGB', blurred.size, (244, 247, 253))
    panel = Image.blend(blurred, tint, 0.55)
    mask = Image.new('L', (blurred.width * S, blurred.height * S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, mask.width - 1, mask.height - 1], radius * S, fill=255)
    mask = mask.resize(blurred.size, Image.LANCZOS)
    img.paste(panel, (x0, y0), mask)
    ImageDraw.Draw(img).rounded_rectangle(box, radius, outline=(255, 255, 255), width=2)
    return img


def icon(size):
    s = size * S
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    bg = gradient(s, s, [(0, (126, 70, 232)), (0.55, (190, 46, 120)), (1, RED)])
    mask = Image.new('L', (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], round(s * 0.22), fill=255)
    img.paste(bg, (0, 0), mask)
    em = emblem(round(size * 0.86)).resize((round(s * 0.86), round(s * 0.86)), Image.LANCZOS)
    img.alpha_composite(em, ((s - em.width) // 2, round(s * 0.0)))
    tag = text_layer('D2', FONT, size // 4, (255, 255, 255), stroke=max(1, size // 64), stroke_fill=NIGHT, shadow=False)
    tag = tag.resize((tag.width * S, tag.height * S), Image.LANCZOS)
    img.alpha_composite(tag, ((s - tag.width) // 2, s - tag.height - round(s * 0.02)))
    return img.resize((size, size), Image.LANCZOS)


SLIDES = [((60, 30, 120), (176, 54, 104)), ((24, 44, 108), (92, 70, 190)), ((92, 24, 60), (224, 90, 80)),
          ((20, 60, 90), (60, 150, 150)), ((48, 20, 90), (130, 60, 170))]


def main():
    for i, (top, bottom) in enumerate(SLIDES, 1):
        scene = night_scene(760, 430, 100 + i, top, bottom)
        glass_panel(scene, (8, 10, 378, 420), 18).save(WIN / ('hero-%d.png' % i), optimize=True)
    panel = Image.new('RGBA', (370 * S, 410 * S), (0, 0, 0, 0))
    ImageDraw.Draw(panel).rounded_rectangle([0, 0, 370 * S - 1, 410 * S - 1], 18 * S, fill=(244, 247, 253, 150),
                                            outline=(255, 255, 255, 255), width=2 * S)
    panel.resize((370, 410), Image.LANCZOS).save(WIN / 'glass-panel.png', optimize=True)
    wordmark(336, 163, 'PC PORT', GOLD).save(WIN / 'logo-disgaea.png', optimize=True)
    wordmark(480, 270, 'WIN PORT', (90, 170, 255)).save(WIN / 'logo-winport.png', optimize=True)
    wordmark(175, 134, 'WIN PORT', (90, 170, 255), vertical=True).save(WIN / 'logo-sidebar.png', optimize=True)
    icon(256).save(WIN / 'launcher-icon.png', optimize=True)
    icon(256).save(WIN / 'launcher.ico', sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    wordmark(900, 433, 'A BRIGHTER DARKNESS', CREAM, with_emblem=False).save(DROID / 'home_title.png', optimize=True)
    wordmark(1161, 893, 'ANDROID PORT', (110, 220, 120)).save(DROID / 'home_logo.png', optimize=True)
    night_scene(960, 540, 7, (40, 26, 80), (110, 50, 120)).filter(ImageFilter.GaussianBlur(3)).save(DROID / 'home_art.jpg', quality=88)
    print('original artwork written')


if __name__ == '__main__':
    main()
