#!/usr/bin/env python3
"""Generate Steam Library Grid artwork for a Non-Steam Disgaea D2 entry.

Creates the 5 Steam custom artwork assets from user-supplied local images:
  - <appid>.png       (920x430   horizontal grid capsule)
  - <appid>p.png      (600x900   vertical library poster)
  - <appid>_hero.png  (1920x620  library details hero banner)
  - <appid>_logo.png  (1200w     trimmed transparent clear logo)
  - <appid>_icon.png  (256x256   square shortcut icon)

Requires Pillow and NumPy (`pip install pillow numpy`). No artwork is bundled
in the repository; supply your own scans/keyart paths.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageFilter
except ImportError as exc:
    sys.stderr.write(
        f"Missing required image dependency ({exc}). "
        "Install with: python3 -m pip install pillow numpy\n"
    )
    sys.exit(1)

Resampling = Image.Resampling.LANCZOS
DEFAULT_SHORTCUT_APPID = 3332139769


def add_shadow(im: Image.Image, radius: int = 14, opacity: float = 0.72, offset: tuple[int, int] = (0, 6)) -> tuple[Image.Image, int]:
    pad = radius * 3
    w, h = im.size
    canvas = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    alpha = im.getchannel("A")
    shadow_alpha = alpha.point(lambda p: int(p * opacity))
    shadow = Image.new("RGBA", im.size, (10, 6, 18, 255))
    shadow.putalpha(shadow_alpha)
    canvas.paste(shadow, (pad + offset[0], pad + offset[1]))
    canvas = canvas.filter(ImageFilter.GaussianBlur(radius))
    canvas.alpha_composite(im, (pad, pad))
    return canvas, pad


def build_logo(logo_raw: Image.Image, target_width: int = 1200) -> tuple[Image.Image, Image.Image]:
    alpha_mask = logo_raw.getchannel("A").point(lambda p: 255 if p > 8 else 0)
    bbox = alpha_mask.getbbox()
    logo_trimmed = logo_raw.crop(bbox) if bbox else logo_raw.copy()
    pad = 24
    padded = Image.new("RGBA", (logo_trimmed.width + pad * 2, logo_trimmed.height + pad * 2), (0, 0, 0, 0))
    padded.paste(logo_trimmed, (pad, pad))
    target_h = max(1, round(padded.height * (target_width / max(1, padded.width))))
    return padded.resize((target_width, target_h), Resampling), logo_trimmed


def build_capsule(keyart: Image.Image, logo_trimmed: Image.Image) -> Image.Image:
    kw, kh = keyart.size
    crop_h = min(kh, round(kw * (430.0 / 920.0)))
    bg = keyart.crop((0, 0, kw, crop_h)).resize((920, 430), Resampling).convert("RGBA")

    arr = np.zeros((430, 920, 4), dtype=np.float32)
    yy, xx = np.mgrid[0:430, 0:920]
    g_bot = np.clip((yy - 230) / 200.0, 0.0, 1.0) ** 1.35
    g_left = np.clip((520 - xx) / 450.0, 0.0, 1.0) * 0.55 + 0.45
    arr[..., 0], arr[..., 1], arr[..., 2] = 16, 10, 28
    arr[..., 3] = np.clip(g_bot * g_left * 170.0, 0, 175)
    bg = Image.alpha_composite(bg, Image.fromarray(arr.astype(np.uint8), "RGBA"))

    hl_w = 355
    hl_h = max(1, round(logo_trimmed.height * (hl_w / max(1, logo_trimmed.width))))
    hl_img = logo_trimmed.resize((hl_w, hl_h), Resampling)
    hl_shad, hl_pad = add_shadow(hl_img, radius=10, opacity=0.82, offset=(0, 4))
    bg.alpha_composite(hl_shad, (22 - hl_pad, 430 - hl_h - 14 - hl_pad))
    return bg.convert("RGB")


def build_hero(keyart: Image.Image) -> Image.Image:
    ha_h = 620
    ha_w = max(1, round(keyart.width * (ha_h / max(1, keyart.height))))
    hero_art_rs = keyart.resize((ha_w, ha_h), Resampling).convert("RGBA")

    # Continuous full-width dusk cloud wash sampled from upper-right sky
    sx0 = round(keyart.width * 0.78)
    sy1 = round(keyart.height * 0.58)
    sky_tile = (
        keyart.crop((sx0, 0, keyart.width, max(1, sy1)))
        .resize((1920, 620), Resampling)
        .filter(ImageFilter.GaussianBlur(48))
        .convert("RGBA")
    )
    bg_np = np.array(sky_tile, dtype=np.float32)
    hy, hx = np.mgrid[0:620, 0:1920]
    t_bot = np.clip((hy - 180) / 440.0, 0.0, 1.0) ** 1.35
    t_left = np.clip((1120 - hx) / 960.0, 0.0, 1.0) ** 1.15
    amt = np.clip(0.35 * t_left + 0.55 * t_bot + 0.25 * t_left * t_bot, 0.0, 0.90)[..., None]
    dark_col = np.array([[[18.0, 13.0, 28.0]]])
    bg_np[..., :3] = bg_np[..., :3] * (1.0 - amt) + dark_col * amt
    base_sky = Image.fromarray(np.clip(bg_np, 0, 255).astype(np.uint8), "RGBA")

    art_x = max(0, 1920 - ha_w)
    if ha_w > 1920:
        hero_art_rs = hero_art_rs.crop((ha_w - 1920, 0, ha_w, ha_h))
        ha_w = 1920
    mask_np = np.ones((ha_h, ha_w), dtype=np.float32) * 255.0
    feather = min(115, ha_w // 4)
    if feather > 1:
        mask_np[:, :feather] = (np.linspace(0.0, 1.0, feather)[None, :] ** 1.4) * 255.0
    hero_feathered = hero_art_rs.copy()
    hero_feathered.putalpha(Image.fromarray(mask_np.astype(np.uint8), "L"))
    base_sky.alpha_composite(hero_feathered, (art_x, 0))

    vig = np.zeros((620, 1920, 4), dtype=np.float32)
    v_y = np.clip((hy - 220) / 400.0, 0.0, 1.0) ** 1.35
    v_x = np.clip((960 - hx) / 760.0, 0.0, 1.0) ** 1.25
    vig[..., 0], vig[..., 1], vig[..., 2] = 12, 8, 22
    vig[..., 3] = np.clip(v_y * v_x * 130.0, 0, 135)
    return Image.alpha_composite(base_sky, Image.fromarray(vig.astype(np.uint8), "RGBA")).convert("RGB")


def build_poster(box: Image.Image, crop_ps3_banner: bool, fade_badges: bool) -> Image.Image:
    # Normalize reference box art coordinate space (1546x1596 artwork area)
    ref_w, ref_h = 1546, 1596
    bw, bh = box.size
    top_y = round(bh * (149.0 / 1745.0)) if crop_ps3_banner else 0
    box_art = box.crop((0, top_y, bw, bh)).resize((ref_w, ref_h), Resampling).convert("RGBA")

    if fade_badges:
        # Smoothly clean upper-right console corner badge via full-width twilight cloud blend
        sky_r = box_art.crop((1340, 96, 1546, 260)).resize((656, 96), Resampling).filter(ImageFilter.GaussianBlur(12))
        sky_l = (
            box_art.crop((40, 0, 260, 96))
            .transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            .resize((656, 96), Resampling)
            .filter(ImageFilter.GaussianBlur(12))
        )
        patch_np = np.array(sky_r, dtype=np.float32)
        sl_np = np.array(sky_l, dtype=np.float32)
        lx_t = np.clip(np.linspace(1.0, 0.0, 656), 0.0, 1.0)[None, :, None]
        patch_np[..., :3] = sl_np[..., :3] * lx_t + patch_np[..., :3] * (1.0 - lx_t)

        c_np = np.array(box_art, dtype=np.float32)
        w_m = np.ones((90, 650, 1), dtype=np.float32)
        w_m[:, :40, 0] = np.linspace(0.0, 1.0, 40)[None, :]
        w_m[74:90, :, 0] *= np.linspace(1.0, 0.0, 16)[:, None]
        c_np[0:90, 896:1546, :3] = c_np[0:90, 896:1546, :3] * (1.0 - w_m) + patch_np[0:90, :650, :3] * w_m
        box_art = Image.fromarray(np.clip(c_np, 0, 255).astype(np.uint8), "RGBA")

    bf_crop = box_art.crop((33, 0, 1513, box_art.height))  # 1480 x 1596
    w_p, h_p = 1480, 2220
    top_h = 360
    p_canvas = Image.new("RGBA", (w_p, h_p), (14, 10, 22, 255))

    sky_src_l = box_art.crop((33, 18, 250, 260)).resize((w_p, top_h + 80), Resampling).filter(ImageFilter.GaussianBlur(36))
    sky_src_r = box_art.crop((1310, 18, 1513, 260)).resize((w_p, top_h + 80), Resampling).filter(ImageFilter.GaussianBlur(36))
    tl_np = np.array(sky_src_l, dtype=np.float32)
    tr_np = np.array(sky_src_r, dtype=np.float32)
    hx_p = np.clip(np.linspace(0.0, 1.0, w_p), 0.0, 1.0)[None, :, None]
    top_sky_np = tl_np * (1.0 - hx_p) + tr_np * hx_p
    grad_up = np.clip(np.linspace(0.42, 0.92, top_h + 80), 0.42, 0.92)[:, None, None]
    top_sky_np[..., :3] = top_sky_np[..., :3] * grad_up + np.array([[[22.0, 16.0, 36.0]]]) * (1.0 - grad_up)
    p_canvas.paste(Image.fromarray(np.clip(top_sky_np, 0, 255).astype(np.uint8), "RGBA"), (0, 0))

    bf_mask = np.ones((bf_crop.height, w_p), dtype=np.float32) * 255.0
    f_top = 44
    bf_mask[:f_top, :] = (np.linspace(0.0, 1.0, f_top)[:, None] ** 1.15) * 255.0
    bf_rgba = bf_crop.copy()
    bf_rgba.putalpha(Image.fromarray(bf_mask.astype(np.uint8), "L"))
    p_canvas.alpha_composite(bf_rgba, (0, top_h))

    if fade_badges:
        py, px_idx = np.mgrid[0:h_p, 0:w_p]
        overlay = np.zeros((h_p, w_p, 4), dtype=np.float32)
        overlay[..., 0], overlay[..., 1], overlay[..., 2] = 14, 10, 22
        bot_fade = np.clip((py - (top_h + 1482)) / 90.0, 0.0, 1.0)
        esrb_mask = np.clip((270 - px_idx) / 36.0, 0.0, 1.0) * np.clip((py - (top_h + 1272)) / 36.0, 0.0, 1.0)
        nisa_mask = np.maximum(
            np.clip((px_idx - 1110) / 36.0, 0.0, 1.0) * np.clip((py - (top_h + 1470)) / 24.0, 0.0, 1.0),
            np.clip((px_idx - 1236) / 28.0, 0.0, 1.0) * np.clip((py - (top_h + 1420)) / 28.0, 0.0, 1.0),
        )
        top_vig = (np.clip((210 - py) / 210.0, 0.0, 1.0) ** 1.3) * 0.45
        overlay[..., 3] = np.clip(np.maximum.reduce([bot_fade, esrb_mask, nisa_mask, top_vig]) * 255.0, 0, 255)
        p_canvas = Image.alpha_composite(p_canvas, Image.fromarray(overlay.astype(np.uint8), "RGBA"))

    return p_canvas.resize((600, 900), Resampling).convert("RGB")


def build_icon(box: Image.Image) -> Image.Image:
    bw, bh = box.size
    crop_box = (
        round(bw * (265.0 / 1546.0)),
        round(bh * (165.0 / 1745.0)),
        round(bw * (890.0 / 1546.0)),
        round(bh * (790.0 / 1745.0)),
    )
    return box.crop(crop_box).resize((256, 256), Resampling).convert("RGB")


def write_64bit_symlinks(out_dir: Path, appid32: int, generated_suffixes: list[str]) -> None:
    appid64 = (appid32 << 32) | 0x02000000
    for suffix in generated_suffixes:
        target = f"{appid32}{suffix}"
        link_path = out_dir / f"{appid64}{suffix}"
        if link_path.exists() or link_path.is_symlink():
            link_path.unlink()
        try:
            os.symlink(target, link_path)
        except OSError:
            data = (out_dir / target).read_bytes()
            link_path.write_bytes(data)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--box", type=Path, required=True, help="Path to PS3 front box art image (portrait)")
    parser.add_argument("--keyart", type=Path, required=True, help="Path to wide promotional/fan art image (16:9)")
    parser.add_argument("--logo", type=Path, required=True, help="Path to transparent PNG clear logo")
    parser.add_argument("--out-dir", type=Path, required=True, help="Output Steam grid directory")
    parser.add_argument(
        "--appid",
        type=int,
        default=DEFAULT_SHORTCUT_APPID,
        help=f"32-bit Non-Steam shortcut AppID (default: {DEFAULT_SHORTCUT_APPID})",
    )
    parser.add_argument(
        "--no-ps3-banner-crop",
        action="store_true",
        help="Do not crop the top PS3 header strip (~8.5%%) from --box",
    )
    parser.add_argument(
        "--no-badge-fade",
        action="store_true",
        help="Do not fade bottom-corner rating/publisher badges or top-right corner tag",
    )
    parser.add_argument(
        "--create-64bit-links",
        action="store_true",
        help="Also create 64-bit SteamOS Big Picture grid symlinks ((appid << 32) | 0x02000000)",
    )
    args = parser.parse_args(argv)

    box = Image.open(args.box).convert("RGBA")
    keyart = Image.open(args.keyart).convert("RGBA")
    logo_raw = Image.open(args.logo).convert("RGBA")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    aid = args.appid

    logo_out, logo_trimmed = build_logo(logo_raw)
    logo_out.save(args.out_dir / f"{aid}_logo.png", optimize=True)
    build_icon(box).save(args.out_dir / f"{aid}_icon.png", optimize=True)
    build_capsule(keyart, logo_trimmed).save(args.out_dir / f"{aid}.png", optimize=True)
    build_hero(keyart).save(args.out_dir / f"{aid}_hero.png", optimize=True)
    build_poster(
        box,
        crop_ps3_banner=not args.no_ps3_banner_crop,
        fade_badges=not args.no_badge_fade,
    ).save(args.out_dir / f"{aid}p.png", optimize=True)

    if args.create_64bit_links:
        write_64bit_symlinks(args.out_dir, aid, [".png", "p.png", "_hero.png", "_logo.png"])

    print(f"Generated Steam Grid assets for appid={aid} in {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
