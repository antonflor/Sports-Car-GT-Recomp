#!/usr/bin/env python3
"""
Assemble the Sports Car GT: 2026 Edition install kit.

    python tools/make_kit.py <out_dir> --game "<installed game>" [--textures <folder of <crc>.png>]
                                       [--patch <Patch-1551a.zip>]

The kit holds the renderer (build/glide2x.dll), its settings, the installer scripts and file
manifest, the two runtime DLLs the original setup unpacks (Mss32.dll, Smackw32.dll, taken from an
installed copy of the game), the official 1.551 patch, and the HD texture pack split into zips
of at most --chunk MB. Game files are from your own copy: keep the kit private.
"""
import argparse
import os
import shutil
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--game", required=True, help="an installed Sports Car GT (for Mss32.dll / Smackw32.dll)")
    ap.add_argument("--textures", default="", help="HD texture pack (<crc>.png files, from mas_textures.py glide)")
    ap.add_argument("--patch", default="", help="Patch-1551a.zip")
    ap.add_argument("--chunk", type=int, default=14, help="max size of each texture zip, MB")
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    dist = os.path.join(ROOT, "dist")
    for f in ("install.ps1", "uninstall.ps1", "Install Sports Car GT 2026.cmd", "files.txt", "scgt2026.ini", "README.md"):
        shutil.copy2(os.path.join(dist, f), a.out)
    shutil.copy2(os.path.join(ROOT, "build", "glide2x.dll"), a.out)

    rt = os.path.join(a.out, "runtime")
    os.makedirs(rt, exist_ok=True)
    for name in ("Mss32.dll", "Smackw32.dll"):
        src = next((os.path.join(a.game, f) for f in os.listdir(a.game) if f.lower() == name.lower()), None)
        if not src:
            raise SystemExit(f"{name} not found in {a.game}")
        shutil.copy2(src, os.path.join(rt, name))

    if a.patch:
        os.makedirs(os.path.join(a.out, "patch"), exist_ok=True)
        shutil.copy2(a.patch, os.path.join(a.out, "patch", "Patch-1551a.zip"))

    if a.textures:
        files = sorted(f for f in os.listdir(a.textures) if f.lower().endswith(".png"))
        limit = a.chunk * 1024 * 1024
        part, size, z = 0, 0, None
        for f in files:
            p = os.path.join(a.textures, f)
            s = os.path.getsize(p)
            if z is None or size + s > limit:
                if z:
                    z.close()
                part += 1
                size = 0
                z = zipfile.ZipFile(os.path.join(a.out, "textures-%02d.zip" % part), "w", zipfile.ZIP_STORED)
            z.write(p, f)  # PNGs are already compressed
            size += s
        if z:
            z.close()
        print(f"{len(files)} textures in {part} zips")

    total = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(a.out) for f in fs)
    print(f"kit: {a.out} ({total / 1e6:.0f} MB)")


if __name__ == "__main__":
    main()
