#!/usr/bin/env python3
"""Generate the CIA HOME Menu banner + icon from YOUR OWN ROM, at build time.

The title-screen logo and the DS icon are Nintendo artwork: they are never
stored in this repository. This script reads them from the user's own
Super Mario 64 DS (Europe) dump and writes, into a git-ignored directory:

  banner.png   256x128  title logo (1:1, as on the DS top screen) on a
                        transparent background (--sky: over a generated sky
                        gradient)          -> bannertool makebanner
  icon.png     48x48    the DS cartridge icon (32x32) upscaled (Scale3x then
                        2x2 area average)          -> bannertool makesmdh
  banner.wav   short silent clip (bannertool needs a banner sound)
  preview.png  banner + icon side by side, for a quick visual check
  assets.json  what was generated and from where (logo source or fallback)

Where the logo lives: overlay 7 (title/menu scene) carries the top-screen logo
as a raw 256x128 8bpp tile sheet (32x16 tiles, row-major, tile 0 at file
offset 0x3d030) and its 256-colour BGR555 palette (offset 0x2bf28); index 0 is
transparent, the logo occupies x 19..254 / y 5..121 of the sheet (the 16-px
column on the left holds unrelated tiles). The overlay is read from the ROM
with ndspy (BLZ-decompressed). Both blocks are checked against SHA-1 digests
of the Europe ROM before use; on any mismatch (other region/revision) the
script falls back to a generated text banner containing no Nintendo artwork.

Usage: make_cia_assets.py --rom ROM --out DIR [--fallback] [--sky]
"""
import argparse
import hashlib
import json
import struct
import sys
import wave
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageFilter, ImageFont
except ImportError:
    sys.exit("make_cia_assets.py needs Pillow and numpy: python3 -m pip install pillow numpy")
try:
    import ndspy.rom
except ImportError:
    sys.exit("make_cia_assets.py needs ndspy: python3 -m pip install ndspy")

BANNER_W, BANNER_H = 256, 128
ICON = 48

OVERLAY = 7
OVERLAY_SIZE = 351456
PAL_OFFSET, PAL_SIZE = 0x2bf28, 512
SHEET_OFFSET, SHEET_SIZE = 0x3d030, 256 * 128
LOGO_BOX = (19, 5, 255, 122)  # x0, y0, x1, y1 (exclusive) inside the sheet
# Digests of the Europe (En,Fr,De,Es,It) data blocks (not the data itself).
PAL_SHA1 = "53780d0fdcca42f48f8fd39190a4bdb0d04dfdbb"
SHEET_SHA1 = "8996883ca57612f482090e6fae2df2f15f96941c"

# Title-screen sky (top -> bottom), plain colours measured on the menu.
SKY_TOP = (56, 186, 251)
SKY_BOTTOM = (227, 251, 251)


def bgr555_to_rgb(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


def sky(w, h):
    t = np.linspace(0.0, 1.0, h)[:, None, None]
    top = np.array(SKY_TOP, float)[None, None, :]
    bottom = np.array(SKY_BOTTOM, float)[None, None, :]
    rgb = (top + (bottom - top) * t ** 0.9).repeat(w, axis=1)
    alpha = np.full((h, w, 1), 255.0)
    return Image.fromarray(np.concatenate([rgb, alpha], 2).round().astype(np.uint8), "RGBA")


def logo_from_rom(rom, check=True):
    """Return (RGBA logo image, info) or raise ValueError."""
    ov = rom.loadArm9Overlays([OVERLAY])[OVERLAY].data
    if len(ov) != OVERLAY_SIZE:
        raise ValueError(f"overlay {OVERLAY} is {len(ov)} bytes, expected {OVERLAY_SIZE} (not the Europe ROM?)")
    pal_raw = ov[PAL_OFFSET:PAL_OFFSET + PAL_SIZE]
    sheet_raw = ov[SHEET_OFFSET:SHEET_OFFSET + SHEET_SIZE]
    digests = {"palette": hashlib.sha1(pal_raw).hexdigest(), "sheet": hashlib.sha1(sheet_raw).hexdigest()}
    if check and (digests["palette"] != PAL_SHA1 or digests["sheet"] != SHEET_SHA1):
        raise ValueError(f"logo data digests {digests} do not match the Europe ROM")
    pal = [bgr555_to_rgb(c) for c in struct.unpack("<256H", pal_raw)]
    idx = np.frombuffer(sheet_raw, np.uint8).reshape(16, 32, 8, 8).transpose(0, 2, 1, 3).reshape(128, 256)
    x0, y0, x1, y1 = LOGO_BOX
    idx = idx[y0:y1, x0:x1]
    lut = np.array([p + (255,) for p in pal], np.uint8)
    lut[0, 3] = 0
    return Image.fromarray(lut[idx], "RGBA"), digests


def text_logo():
    """Fallback: plain generated text, no Nintendo artwork."""
    img = Image.new("RGBA", (236, 117), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    try:
        big = ImageFont.load_default(size=34)
        small = ImageFont.load_default(size=16)
    except TypeError:  # Pillow < 10.1
        big = small = ImageFont.load_default()
    for text, font, y, fill in (("SM64DS", big, 22, (255, 255, 255)),
                                ("for Nintendo 3DS", small, 72, (255, 255, 255))):
        w = d.textlength(text, font=font)
        x = (236 - w) / 2
        for dx, dy in ((2, 2), (1, 1)):
            d.text((x + dx, y + dy), text, font=font, fill=(20, 40, 90, 255))
        d.text((x, y), text, font=font, fill=fill + (255,))
    return img


def make_banner(logo, with_sky=False):
    banner = sky(BANNER_W, BANNER_H) if with_sky else Image.new("RGBA", (BANNER_W, BANNER_H), (0, 0, 0, 0))
    x = (BANNER_W - logo.width) // 2
    y = (BANNER_H - logo.height) // 2
    banner.alpha_composite(logo, (x, y))
    return banner


def nds_icon(rom):
    b = rom.iconBanner
    if len(b) < 0x240:
        raise ValueError("ROM has no icon/banner block")
    bmp = b[0x20:0x220]
    pal = struct.unpack_from("<16H", b, 0x220)
    img = np.zeros((32, 32, 4), np.uint8)
    for t in range(16):
        tx, ty = (t % 4) * 8, (t // 4) * 8
        for y in range(8):
            for x in range(8):
                v = bmp[t * 32 + y * 4 + x // 2]
                v = (v >> 4) if x & 1 else (v & 15)
                img[ty + y, tx + x] = bgr555_to_rgb(pal[v]) + ((0 if v == 0 else 255),)
    return img


def scale3x(a):
    """Scale3x (AdvMAME3x) on an RGBA array: edge-preserving pixel-art 3x."""
    h, w = a.shape[:2]
    p = np.pad(a, ((1, 1), (1, 1), (0, 0)), mode="edge")
    key = p.astype(np.uint32)
    key = (key[..., 0] << 24) | (key[..., 1] << 16) | (key[..., 2] << 8) | key[..., 3]

    def K(dy, dx):
        return key[1 + dy:1 + dy + h, 1 + dx:1 + dx + w]

    def P(dy, dx):
        return p[1 + dy:1 + dy + h, 1 + dx:1 + dx + w]

    A, B, C, D, E, F, G, H, I = (K(-1, -1), K(-1, 0), K(-1, 1), K(0, -1), K(0, 0), K(0, 1), K(1, -1), K(1, 0), K(1, 1))
    pA, pB, pC, pD, pE, pF, pG, pH, pI = (P(-1, -1), P(-1, 0), P(-1, 1), P(0, -1), P(0, 0), P(0, 1), P(1, -1), P(1, 0), P(1, 1))
    core = (B != H) & (D != F)

    def pick(cond, src):
        return np.where((core & cond)[..., None], src, pE)

    out = np.zeros((h * 3, w * 3, 4), a.dtype)
    out[0::3, 0::3] = pick(D == B, pD)
    out[0::3, 1::3] = pick(((D == B) & (E != C)) | ((B == F) & (E != A)), pB)
    out[0::3, 2::3] = pick(B == F, pF)
    out[1::3, 0::3] = pick(((D == B) & (E != G)) | ((D == H) & (E != A)), pD)
    out[1::3, 1::3] = pE
    out[1::3, 2::3] = pick(((B == F) & (E != I)) | ((H == F) & (E != C)), pF)
    out[2::3, 0::3] = pick(D == H, pD)
    out[2::3, 1::3] = pick(((D == H) & (E != I)) | ((H == F) & (E != G)), pH)
    out[2::3, 2::3] = pick(H == F, pF)
    return out


def area_down2(a):
    """2x2 area average with premultiplied alpha."""
    f = a.astype(np.float64)
    rgb, al = f[..., :3] * f[..., 3:4] / 255.0, f[..., 3:4]
    def box(x):
        return (x[0::2, 0::2] + x[1::2, 0::2] + x[0::2, 1::2] + x[1::2, 1::2]) / 4.0
    rgb, al = box(rgb), box(al)
    out = np.where(al > 0, rgb * 255.0 / np.maximum(al, 1e-9), 0)
    return np.concatenate([out, al], 2).round().clip(0, 255).astype(np.uint8)


def make_icon(rom, logo):
    try:
        icon = area_down2(scale3x(nds_icon(rom)))  # 32 -> 96 -> 48
        return Image.fromarray(icon, "RGBA"), "rom-nds-icon"
    except ValueError:
        img = sky(ICON, ICON)
        small = logo.copy()
        small.thumbnail((ICON - 4, ICON - 4), Image.LANCZOS)
        img.alpha_composite(small, ((ICON - small.width) // 2, (ICON - small.height) // 2))
        return img, "banner-logo"


def silent_wav(path, seconds=0.5, rate=32728):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"\0\0" * int(seconds * rate))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--fallback", action="store_true", help="force the generated text banner (no ROM artwork)")
    ap.add_argument("--sky", action="store_true", help="put the logo over a generated sky instead of a transparent background")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    rom = ndspy.rom.NintendoDSRom.fromFile(str(a.rom))

    info = {"rom": a.rom.name, "banner_size": [BANNER_W, BANNER_H], "icon_size": [ICON, ICON]}
    logo = None
    if not a.fallback:
        try:
            logo, digests = logo_from_rom(rom)
            info.update(logo_source=f"rom overlay {OVERLAY} (palette 0x{PAL_OFFSET:x}, sheet 0x{SHEET_OFFSET:x})", digests=digests)
        except (ValueError, IndexError, KeyError) as e:
            print(f"warning: title logo not found in this ROM ({e}); using a generated text banner", file=sys.stderr)
            info["logo_error"] = str(e)
    if logo is None:
        logo = text_logo()
        info["logo_source"] = "generated text fallback (no Nintendo artwork)"

    banner = make_banner(logo, a.sky)
    info["background"] = "sky" if a.sky else "transparent"
    banner.save(a.out / "banner.png")
    icon, info["icon_source"] = make_icon(rom, logo)
    icon.save(a.out / "icon.png")
    silent_wav(a.out / "banner.wav")

    preview = Image.new("RGBA", (BANNER_W * 2 + ICON * 2 + 24, BANNER_H * 2), (40, 40, 40, 255))
    preview.alpha_composite(banner.resize((BANNER_W * 2, BANNER_H * 2), Image.NEAREST), (0, 0))
    bg = Image.new("RGBA", (ICON * 2, ICON * 2), (255, 255, 255, 255))
    bg.alpha_composite(icon.resize((ICON * 2, ICON * 2), Image.NEAREST))
    preview.alpha_composite(bg, (BANNER_W * 2 + 12, (BANNER_H * 2 - ICON * 2) // 2))
    preview.save(a.out / "preview.png")
    (a.out / "assets.json").write_text(json.dumps(info, indent=2) + "\n")
    print(f"CIA assets in {a.out}: banner.png ({info['logo_source']}), icon.png ({info['icon_source']}), banner.wav")


if __name__ == "__main__":
    main()
