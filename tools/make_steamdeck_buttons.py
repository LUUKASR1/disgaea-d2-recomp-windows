#!/usr/bin/env python3
"""Generate an optional Steam Deck button-prompt texture pack for Disgaea D2.

Patches user-supplied Disgaea D2 button prompt atlases (extracted from your own
START.dat and/or dumped via D2_TEXTURE_DUMP=1 / F1 -> System -> Save Textures)
with crisp Steam Deck A/B/X/Y/VIEW glyphs while preserving SKIP/AUTO badge
overlays, layout, and surrounding UI sprites:

  - 266b3459747d8fe3_button.png        (512x512   menus, HUD bars, VIEW pill)
  - 5910b2ce06e8b0d1_event_buttons.png (1024x2560 event/cutscene SKIP & AUTO)

Requires Pillow and NumPy (`pip install pillow numpy`). No copyrighted game
textures are bundled; original transparent glyphs default to
tools/assets/steamdeck-buttons/.
"""

from __future__ import annotations

import argparse
import struct
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
Nearest = Image.Resampling.NEAREST

BUTTON_HASH = "266b3459747d8fe3"
EVENT_HASH = "5910b2ce06e8b0d1"
DEFAULT_GLYPH_DIR = Path(__file__).resolve().parent / "assets" / "steamdeck-buttons"


def load_rgba(path: Path) -> Image.Image:
    return Image.open(path).convert("RGBA")


def crop_alpha_bbox(im: Image.Image, alpha_thresh: int = 4) -> Image.Image:
    arr = np.array(im)
    ys, xs = np.where(arr[:, :, 3] > alpha_thresh)
    if len(xs) == 0:
        return im
    return im.crop((int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1))


def fit_centered(
    im: Image.Image,
    box_w: int,
    box_h: int,
    max_w: int | None = None,
    max_h: int | None = None,
) -> Image.Image:
    """Resize preserving aspect ratio and center inside (box_w, box_h)."""
    im = crop_alpha_bbox(im)
    mw = max_w if max_w is not None else box_w
    mh = max_h if max_h is not None else box_h
    w, h = im.size
    scale = min(mw / max(1, w), mh / max(1, h))
    nw = max(1, int(round(w * scale)))
    nh = max(1, int(round(h * scale)))
    resized = im.resize((nw, nh), Resampling)
    canvas = Image.new("RGBA", (box_w, box_h), (0, 0, 0, 0))
    canvas.paste(resized, ((box_w - nw) // 2, (box_h - nh) // 2), resized)
    return canvas


def add_subtle_rim(
    im: Image.Image,
    rim_color: tuple[int, int, int, int] = (135, 146, 162, 255),
    thickness: int = 4,
) -> Image.Image:
    """Add a crisp outer rim so dark capsules read cleanly on dark UI panels."""
    im = crop_alpha_bbox(im)
    w, h = im.size
    pad = thickness + 2
    padded = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
    padded.paste(im, (pad, pad), im)
    alpha = padded.split()[3]
    dilated = alpha.filter(ImageFilter.MaxFilter(thickness * 2 + 1))
    rim = Image.new("RGBA", padded.size, rim_color)
    rim.putalpha(dilated)
    outer_a = dilated.filter(ImageFilter.MaxFilter(3))
    outer = Image.new("RGBA", padded.size, (18, 20, 28, 255))
    outer.putalpha(outer_a)
    comp = Image.alpha_composite(outer, rim)
    comp = Image.alpha_composite(comp, padded)
    return crop_alpha_bbox(comp)


def extract_button_txf_from_start_dat(start_dat: Path) -> Image.Image:
    """Read button.txf (256x256 ARGB8) directly from a NISPACK START.dat archive."""
    with start_dat.open("rb") as f:
        hdr = f.read(16)
        magic = hdr[:8]
        if magic != b"NISPACK\x00":
            raise ValueError(f"{start_dat}: expected NISPACK magic, got {magic!r}")
        count = struct.unpack(">I", hdr[12:16])[0]
        target = None
        for _ in range(count):
            raw_name, off, sz, _crc = struct.unpack(">32sIII", f.read(44))
            name = raw_name.split(b"\x00", 1)[0].decode("ascii", errors="replace")
            if name == "button.txf":
                target = (off, sz)
                break
        if target is None:
            raise FileNotFoundError(f"button.txf not found inside {start_dat}")
        f.seek(target[0])
        blob = f.read(target[1])

    if len(blob) < 16:
        raise ValueError(f"button.txf in {start_dat} is truncated ({len(blob)} bytes)")
    fmt = blob[0]
    if fmt != 0x00:
        raise ValueError(f"Unsupported TXF pixel format 0x{fmt:02X} in button.txf")
    data = blob[16:]
    w, h = 256, 256
    if len(data) != w * h * 4:
        raise ValueError(f"Expected {w * h * 4} ARGB bytes for button.txf, got {len(data)}")

    rgba = bytearray(len(data))
    rgba[0::4] = data[1::4]
    rgba[1::4] = data[2::4]
    rgba[2::4] = data[3::4]
    rgba[3::4] = data[0::4]
    return Image.frombytes("RGBA", (w, h), bytes(rgba))


def extract_skip_auto_overlays(orig_btn: Image.Image) -> tuple[Image.Image, Image.Image]:
    """Extract clean SKIP and AUTO lettering+outline overlays at 2x scale."""
    orig_1x = orig_btn if orig_btn.size == (256, 256) else orig_btn.resize((256, 256), Nearest)
    arr = np.array(orig_1x)

    # Triangle orb [3:29, 45:75] sits at (6, 0) inside SKIP [194:230, 5:49] (44x36).
    tri_orb = arr[3:29, 45:75]
    tri_skip = arr[194:230, 5:49]

    # Square orb [3:29, 5:35] sits at (9, 0) inside AUTO [210:247, 58:107] (49x37).
    sq_orb = arr[3:29, 5:35]
    sq_auto = arr[210:247, 58:107]

    def build_overlay(patch: np.ndarray, orb: np.ndarray, ox: int, oy: int, y_cutoff: int) -> Image.Image:
        h, w, _ = patch.shape
        orb_full = np.zeros_like(patch)
        oh, ow, _ = orb.shape
        orb_full[oy : oy + oh, ox : ox + ow] = orb

        ys = np.arange(h)[:, None] >= y_cutoff
        bright = (patch[:, :, 3] > 64) & (patch[:, :, :3].max(axis=2) > 150) & ys
        diff = (np.abs(patch.astype(int) - orb_full.astype(int)).sum(axis=2) > 12) & ys

        bright_img = Image.fromarray((bright.astype(np.uint8) * 255), mode="L")
        dil2 = np.array(bright_img.filter(ImageFilter.MaxFilter(5))) > 0
        dark_patch = (patch[:, :, 3] > 64) & (patch[:, :, :3].max(axis=2) < 95)

        mask = (bright | diff | (dil2 & dark_patch)) & ys
        out = np.zeros_like(patch)
        out[mask] = patch[mask]
        im1x = Image.fromarray(out, mode="RGBA")
        return im1x.resize((w * 2, h * 2), Nearest)

    return (
        build_overlay(tri_skip, tri_orb, ox=6, oy=0, y_cutoff=19),
        build_overlay(sq_auto, sq_orb, ox=9, oy=0, y_cutoff=20),
    )


def clear_and_paste(base: Image.Image, patch: Image.Image, x0: int, y0: int, x1: int, y1: int) -> None:
    blank = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
    base.paste(blank, (x0, y0))
    base.paste(patch, (x0, y0), patch)


def find_dump_texture(dump_dir: Path, tex_hash: str) -> Path | None:
    matches = sorted(dump_dir.glob(f"{tex_hash}_*.png"))
    return matches[0] if matches else None


def load_fitted_glyphs(glyph_dir: Path, add_view_rim: bool) -> dict[str, Image.Image]:
    required = ("A", "B", "X", "Y", "VIEW")
    raw: dict[str, Image.Image] = {}
    for name in required:
        path = glyph_dir / f"{name}.png"
        if not path.is_file():
            raise FileNotFoundError(f"Missing required button glyph: {path}")
        raw[name] = load_rgba(path)

    fitted = {
        k: fit_centered(raw[k], 60, 52, max_w=56, max_h=52)
        for k in ("A", "B", "X", "Y")
    }
    if add_view_rim:
        rimmed = add_subtle_rim(raw["VIEW"], rim_color=(135, 146, 162, 255), thickness=4)
        fitted["VIEW"] = fit_centered(rimmed, 136, 40, max_w=120, max_h=38)
    else:
        fitted["VIEW"] = fit_centered(raw["VIEW"], 136, 40, max_w=134, max_h=40)
    return fitted


def build_composites(
    fitted: dict[str, Image.Image],
    orig_btn: Image.Image,
) -> tuple[Image.Image, Image.Image]:
    skip_2x, auto_2x = extract_skip_auto_overlays(orig_btn)
    y_skip = Image.new("RGBA", (88, 72), (0, 0, 0, 0))
    y_skip.paste(fitted["Y"], (12, 0), fitted["Y"])
    y_skip = Image.alpha_composite(y_skip, skip_2x)

    x_auto = Image.new("RGBA", (98, 74), (0, 0, 0, 0))
    x_auto.paste(fitted["X"], (18, 0), fitted["X"])
    x_auto = Image.alpha_composite(x_auto, auto_2x)
    return y_skip, x_auto


def patch_button_atlas(
    orig_btn: Image.Image,
    fitted: dict[str, Image.Image],
    y_skip: Image.Image,
    x_auto: Image.Image,
) -> Image.Image:
    btn_2x = orig_btn.resize((512, 512), Nearest).copy()
    clear_and_paste(btn_2x, fitted["X"],    10,   6,  70,  58)
    clear_and_paste(btn_2x, fitted["Y"],    90,   6, 150,  58)
    clear_and_paste(btn_2x, fitted["B"],   170,   6, 230,  58)
    clear_and_paste(btn_2x, fitted["A"],    10, 134,  70, 186)
    clear_and_paste(btn_2x, fitted["VIEW"],  2, 198, 138, 238)
    clear_and_paste(btn_2x, y_skip,         10, 388,  98, 460)
    clear_and_paste(btn_2x, x_auto,        116, 420, 214, 494)
    return btn_2x


def patch_event_atlas(
    orig_evt: Image.Image,
    fitted: dict[str, Image.Image],
    y_skip: Image.Image | None,
    x_auto: Image.Image | None,
) -> Image.Image:
    evt_2x = orig_evt.resize((1024, 2560), Resampling).copy()
    clear_and_paste(evt_2x, fitted["B"], 826, 246, 886, 298)
    clear_and_paste(evt_2x, fitted["A"], 826, 326, 886, 378)
    if y_skip is not None and x_auto is not None:
        clear_and_paste(evt_2x, y_skip, 910, 246,  998, 318)
        clear_and_paste(evt_2x, x_auto, 904, 326, 1002, 400)
    else:
        print(
            "Note: --start-dat or --button-atlas not supplied; "
            "patched A/B on event atlas without updating SKIP/AUTO overlays."
        )
    return evt_2x


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        required=True,
        help="Output texture-pack directory (e.g. settings/texture-pack)",
    )
    parser.add_argument(
        "--start-dat",
        type=Path,
        default=None,
        help="Path to PS3_GAME/USRDIR/Data/START.dat (extracts button.txf cleanly)",
    )
    parser.add_argument(
        "--dump-dir",
        type=Path,
        default=None,
        help="Optional texture dump directory to auto-discover dumped button atlases",
    )
    parser.add_argument(
        "--button-atlas",
        type=Path,
        default=None,
        help=f"Explicit dumped menu/HUD atlas PNG ({BUTTON_HASH}_*.png)",
    )
    parser.add_argument(
        "--event-atlas",
        type=Path,
        default=None,
        help=f"Explicit dumped event/cutscene atlas PNG ({EVENT_HASH}_*.png)",
    )
    parser.add_argument(
        "--glyph-dir",
        type=Path,
        default=DEFAULT_GLYPH_DIR,
        help=f"Directory containing A.png, B.png, X.png, Y.png, VIEW.png (default: {DEFAULT_GLYPH_DIR})",
    )
    parser.add_argument(
        "--add-view-rim",
        action="store_true",
        help="Add a bright inner rim + dark outline around VIEW.png (helpful for dark capsules)",
    )
    args = parser.parse_args(argv)

    button_src: Image.Image | None = None
    if args.start_dat is not None:
        button_src = extract_button_txf_from_start_dat(args.start_dat)
    elif args.button_atlas is not None:
        button_src = load_rgba(args.button_atlas)
    elif args.dump_dir is not None:
        found_btn = find_dump_texture(args.dump_dir, BUTTON_HASH)
        if found_btn is not None:
            button_src = load_rgba(found_btn)

    event_path = args.event_atlas
    if event_path is None and args.dump_dir is not None:
        event_path = find_dump_texture(args.dump_dir, EVENT_HASH)
    event_src = load_rgba(event_path) if event_path is not None else None

    if button_src is None and event_src is None:
        parser.error(
            "No source button atlas found. Pass --start-dat START.dat, "
            "--dump-dir <dump>, --button-atlas <png>, and/or --event-atlas <png>."
        )

    fitted = load_fitted_glyphs(args.glyph_dir, add_view_rim=args.add_view_rim)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    y_skip: Image.Image | None = None
    x_auto: Image.Image | None = None
    if button_src is not None:
        y_skip, x_auto = build_composites(fitted, button_src)
        btn_out = args.out_dir / f"{BUTTON_HASH}_button.png"
        patch_button_atlas(button_src, fitted, y_skip, x_auto).save(btn_out)
        print(f"Wrote {btn_out}")

    if event_src is not None:
        evt_out = args.out_dir / f"{EVENT_HASH}_event_buttons.png"
        patch_event_atlas(event_src, fitted, y_skip, x_auto).save(evt_out)
        print(f"Wrote {evt_out}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
