#!/usr/bin/env python3
"""
Sports Car GT (ISI, 1999) MAS archive reader/writer.

MAS layout (little endian), reverse-engineered from the retail 1.551 files:

    u32 count                      number of entries
    u32 data_size                  bytes of payload after the directory
    count x 32-byte directory entries:
        char name[16]              8.3 name, NUL padded (e.g. "01B1.BMP")
        u32  offset                payload offset, relative to the end of the directory
        u32  size                  uncompressed size (for zlib images: width*height*bytes-per-pixel)
        u32  packed                compressed size for zlib images, 0 for plain entries
                                   (a few raw images carry other values here; kept verbatim)
        u32  reserved              always 0
    The stored length of an entry is the distance to the next entry's offset.
    payload

Image entries (.BMP / .PCX) start with a 16-byte header, then the pixels:

    u32 width, u32 height
    u32 compressed                 1 = zlib stream follows, 0 = raw pixels follow
    u32 paletted                   0 = 24-bit RGB (top-down, R,G,B order), 1 = 8-bit indexed (PCX)

Pure magenta (255, 0, 255) in RGB textures is the transparency colour key.

Usage:
    python mas.py list    <file.mas>
    python mas.py extract <file.mas> <out_dir>          raw entries, byte-exact
    python mas.py pack    <in_dir> <file.mas> [--order <original.mas>]
"""
import os
import struct
import sys
import zlib

HEADER = struct.Struct("<II")
ENTRY = struct.Struct("<16sIIII")
IMAGE = struct.Struct("<IIII")
COLOR_KEY = (255, 0, 255)


class Entry:
    __slots__ = ("name", "size", "packed", "reserved", "data")

    def __init__(self, name, size, packed, reserved, data):
        self.name, self.size, self.packed, self.reserved, self.data = name, size, packed, reserved, data

    @property
    def ext(self):
        return self.name.rsplit(".", 1)[-1].upper() if "." in self.name else ""

    # ---- images ---------------------------------------------------------------------
    def image_header(self):
        """(width, height, compressed, paletted) or None if this isn't an image entry."""
        if self.ext not in ("BMP", "PCX") or len(self.data) < IMAGE.size:
            return None
        return IMAGE.unpack_from(self.data, 0)

    def is_rgb_texture(self):
        h = self.image_header()
        if h is None or h[3] != 0:
            return False
        if h[2] == 1:
            return self.size == h[0] * h[1] * 3
        return len(self.data) >= IMAGE.size + h[0] * h[1] * 3

    def is_compressed(self):
        h = self.image_header()
        return bool(h and h[2])

    def rgb(self):
        """Decode an RGB texture -> (width, height, bytes)."""
        w, h, compressed, _ = self.image_header()
        if compressed:
            raw = zlib.decompress(self.data[IMAGE.size:])
        else:
            raw = self.data[IMAGE.size:IMAGE.size + w * h * 3]
        if len(raw) != w * h * 3:
            raise ValueError(f"{self.name}: expected {w*h*3} bytes, got {len(raw)}")
        return w, h, raw

    @classmethod
    def from_rgb(cls, name, width, height, raw, compressed=True, level=6):
        """Build a texture entry the way the game's own tools store them."""
        if len(raw) != width * height * 3:
            raise ValueError("pixel buffer size does not match dimensions")
        if compressed:
            blob = IMAGE.pack(width, height, 1, 0) + zlib.compress(raw, level)
            return cls(name, width * height * 3, len(blob), 0, blob)
        blob = IMAGE.pack(width, height, 0, 0) + raw
        return cls(name, len(blob), 0, 0, blob)


def read(path):
    with open(path, "rb") as f:
        d = f.read()
    count, data_size = HEADER.unpack_from(d, 0)
    base = HEADER.size + ENTRY.size * count
    if base + data_size != len(d):
        raise ValueError(f"{path}: not a Sports Car GT MAS file (size mismatch)")
    rows = []
    for i in range(count):
        raw_name, off, size, packed, reserved = ENTRY.unpack_from(d, HEADER.size + ENTRY.size * i)
        rows.append((raw_name.split(b"\0", 1)[0].decode("latin-1"), off, size, packed, reserved))
    # stored length = distance to the next distinct offset (or the end of the payload)
    offsets = sorted({r[1] for r in rows} | {data_size})
    nxt = {offsets[i]: offsets[i + 1] for i in range(len(offsets) - 1)}
    entries = []
    for name, off, size, packed, reserved in rows:
        end = nxt.get(off, off)
        if size == 0 and packed == 0:
            end = off  # zero-length placeholder (MASTEMP.TMP)
        entries.append(Entry(name, size, packed, reserved, d[base + off: base + end]))
    return entries


def write(path, entries):
    directory, payload, off = [], [], 0
    for e in entries:
        name = e.name.encode("latin-1")
        if len(name) > 15:
            raise ValueError(f"name too long for MAS: {e.name}")
        directory.append(ENTRY.pack(name, off, e.size, e.packed, e.reserved))
        payload.append(e.data)
        off += len(e.data)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(HEADER.pack(len(entries), off))
        f.writelines(directory)
        f.writelines(payload)
    os.replace(tmp, path)


def _main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    cmd = argv[0].lower()
    if cmd == "list":
        for e in read(argv[1]):
            h = e.image_header()
            extra = f"  {h[0]}x{h[1]}{' zlib' if h[2] else ''}{' 8bit' if h[3] else ''}" if h else ""
            print(f"{e.name:16s} {e.size:10d} {len(e.data):10d}{extra}")
    elif cmd == "extract":
        os.makedirs(argv[2], exist_ok=True)
        for e in read(argv[1]):
            with open(os.path.join(argv[2], e.name), "wb") as f:
                f.write(e.data)
    elif cmd == "pack":
        src, dst = argv[1], argv[2]
        ref = {}
        if "--order" in argv:
            ref_entries = read(argv[argv.index("--order") + 1])
            names = [e.name for e in ref_entries]
            ref = {e.name: e for e in ref_entries}
        else:
            names = sorted(os.listdir(src))
        out = []
        for n in names:
            with open(os.path.join(src, n), "rb") as f:
                data = f.read()
            if n in ref and ref[n].data == data:
                out.append(ref[n])  # unchanged: keep the original directory fields
                continue
            e = Entry(n, len(data), 0, 0, data)
            h = e.image_header()
            if h and h[2]:  # zlib image: size = pixel bytes, packed = stored length
                e.size, e.packed = h[0] * h[1] * (1 if h[3] else 3), len(data)
            out.append(e)
        write(dst, out)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    _main(sys.argv[1:])
