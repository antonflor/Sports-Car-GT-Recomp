#!/usr/bin/env python3
"""
Export Sports Car GT textures to PNG, and build HD MAS archives from replacement PNGs.

    python mas_textures.py export "<game>" textures_original
    python upscale_textures.py textures_original textures_hd
    python mas_textures.py glide "<game>" textures_hd "<game>\\textures"
    -> HD textures for the scgt2026 renderer (recommended): one PNG per texture, named by the
       CRC of the pixels the game uploads, loaded at full size and 32-bit colour.
    python mas_textures.py import "<game>" textures_hd hd_pack
    -> alternatively, new MAS archives with the HD textures inside (for other renderers;
       copy hd_pack\\MAS\\*.mas over "<game>\\MAS" after backing up the originals)

Layout of the PNG folders: <archive>/<texture>.png, e.g. Cmaps/01B1.png is texture 01B1.BMP
inside MAS\\Cmaps.mas. Only the archives that contain at least one replacement are written,
and every other entry is copied byte-for-byte, so the result loads exactly like the original.

Transparency: the game uses pure magenta (255,0,255) as its colour key. Export keeps it as-is,
and upscale_textures.py rebuilds the key mask exactly at the new size. Import only guards
textures that had no transparency from gaining stray key pixels.

Replacements may be any size. --max-size caps the long side (power-of-two sizes are kept),
in case a size turns out to be too large for the engine.
"""
import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mas  # noqa: E402

KEY = np.array(mas.COLOR_KEY, dtype=np.int16)


def archives(game):
    found = []
    for sub in ("MAS", "DAT"):
        found += glob.glob(os.path.join(game, sub, "*.[mM][aA][sS]"))
    return sorted(found, key=str.lower)


def export(game, out):
    total = 0
    for path in archives(game):
        stem = os.path.splitext(os.path.basename(path))[0]
        textures = [e for e in mas.read(path) if e.is_rgb_texture()]
        if not textures:
            continue
        dst = os.path.join(out, stem)
        os.makedirs(dst, exist_ok=True)
        for e in textures:
            w, h, raw = e.rgb()
            rgb = np.frombuffer(raw, np.uint8).reshape(h, w, 3)[..., ::-1]  # stored B,G,R
            Image.fromarray(np.ascontiguousarray(rgb)).save(os.path.join(dst, os.path.splitext(e.name)[0] + ".png"))
        total += len(textures)
        print(f"{stem:12s} {len(textures):4d} textures")
    print(f"exported {total} textures to {out}")


def clean_key(rgb, src_has_key):
    """A texture with no transparency in the original must not gain any: nudge stray key pixels.
    (Textures that do use the key are passed through untouched; upscale_textures.py writes
    their key pixels exactly.)"""
    if src_has_key:
        return rgb
    a = rgb.copy()
    a[(a == mas.COLOR_KEY).all(axis=2)] = (254, 0, 254)
    return a


def fit(img, max_size):
    w, h = img.size
    if max_size and max(w, h) > max_size:
        s = max_size / max(w, h)
        img = img.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
    return img


def do_import(game, hd, out, max_size, level):
    written = replaced = 0
    for path in archives(game):
        stem = os.path.splitext(os.path.basename(path))[0]
        src_dir = os.path.join(hd, stem)
        if not os.path.isdir(src_dir):
            continue
        pngs = {os.path.splitext(f)[0].upper(): os.path.join(src_dir, f)
                for f in os.listdir(src_dir) if f.lower().endswith(".png")}
        entries = mas.read(path)
        n = 0
        for i, e in enumerate(entries):
            key = os.path.splitext(e.name)[0].upper()
            if key not in pngs or not e.is_rgb_texture():
                continue
            _, _, raw = e.rgb()
            has_key = (np.frombuffer(raw, np.uint8).reshape(-1, 3) == mas.COLOR_KEY).all(axis=1).any()
            img = fit(Image.open(pngs[key]).convert("RGB"), max_size)
            rgb = clean_key(np.asarray(img), has_key)
            bgr = np.ascontiguousarray(rgb[..., ::-1])
            entries[i] = mas.Entry.from_rgb(e.name, rgb.shape[1], rgb.shape[0], bgr.tobytes(),
                                            e.is_compressed(), level)
            n += 1
        if not n:
            continue
        rel = os.path.relpath(path, game)
        dst = os.path.join(out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        mas.write(dst, entries)
        written += 1
        replaced += n
        print(f"{rel:24s} {n:4d} textures replaced  ({os.path.getsize(dst) / 1e6:.1f} MB)")
    print(f"wrote {written} archives, {replaced} textures, to {out}")


def glide_pack(game, hd, out):
    """Write <out>/<crc>.png for every HD texture, keyed by the CRC32 of the game's upload."""
    import zlib
    os.makedirs(out, exist_ok=True)
    written = 0
    for path in archives(game):
        stem = os.path.splitext(os.path.basename(path))[0]
        src_dir = os.path.join(hd, stem)
        if not os.path.isdir(src_dir):
            continue
        pngs = {os.path.splitext(f)[0].upper(): os.path.join(src_dir, f)
                for f in os.listdir(src_dir) if f.lower().endswith(".png")}
        for e in mas.read(path):
            key = os.path.splitext(e.name)[0].upper()
            if key not in pngs or not e.is_rgb_texture():
                continue
            _, _, raw = e.rgb()
            crc = zlib.crc32(mas.game_rgb565(raw)) & 0xFFFFFFFF
            dst = os.path.join(out, "%08x.png" % crc)
            if os.path.exists(dst):
                continue
            Image.open(pngs[key]).convert("RGB").save(dst)
            written += 1
    print(f"wrote {written} textures to {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("export", help="game folder -> PNGs")
    e.add_argument("game")
    e.add_argument("out")
    i = sub.add_parser("import", help="replacement PNGs -> new MAS archives")
    i.add_argument("game")
    i.add_argument("hd")
    i.add_argument("out")
    i.add_argument("--max-size", type=int, default=0, help="cap the long side of replacements (0 = no cap)")
    i.add_argument("--level", type=int, default=6, help="zlib level (1-9)")
    g = sub.add_parser("glide", help="replacement PNGs -> CRC-named textures for the scgt2026 renderer")
    g.add_argument("game")
    g.add_argument("hd")
    g.add_argument("out")
    a = ap.parse_args()
    if a.cmd == "glide":
        glide_pack(a.game, a.hd, a.out)
    elif a.cmd == "export":
        export(a.game, a.out)
    else:
        do_import(a.game, a.hd, a.out, a.max_size, a.level)


if __name__ == "__main__":
    main()
