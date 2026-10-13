#!/usr/bin/env python3
"""
Sports Car GT HD texture pipeline: AI-upscale textures exported with mas_textures.py.

    1. python mas_textures.py export "<game>" textures_original
    2. python upscale_textures.py textures_original textures_hd
    3. python mas_textures.py import "<game>" textures_hd hd_pack

Requires: Python 3.9+, torch (CUDA build for NVIDIA GPUs), pillow, numpy.
    pip install torch --index-url https://download.pytorch.org/whl/cu128
    pip install pillow numpy

Models (Real-ESRGAN, BSD-3-Clause, by Xintao Wang et al.) are downloaded on first use:
    general  realesr-general-x4v3.pth   fast, natural textures (default)
    x4plus   RealESRGAN_x4plus.pth      heavier, sharper, can look "painted"

Per texture:
  * Colour-key transparency (pure magenta) is handled explicitly: keyed pixels are filled with
    the nearest real colour before upscaling (so no pink fringes bleed in), and the key mask is
    rebuilt at the new size with smooth, exact edges.
  * Tileable textures (asphalt, grass, walls) are wrap-padded so they still tile without seams;
    everything else (car liveries, signs) is edge-padded.
  * By default only the 3D textures are processed (car liveries in Cmaps and the track *MAP
    archives). Menu art, fonts, loading screens and the dash ship one image per screen
    resolution and are left alone unless you pass --include-ui.
"""
import argparse
import math
import os
import sys
import time
import urllib.request

import numpy as np
from PIL import Image

try:
    import torch
    import torch.nn as nn
    import torch.nn.functional as F
except ImportError:  # pragma: no cover
    sys.exit("PyTorch is required: pip install torch (see the header of this script)")

KEY = np.array([255, 0, 255], dtype=np.uint8)
UI_ARCHIVES = {"font", "gizmo", "options", "loading", "dash", "wpedit"}

MODELS = {
    "general": ("realesr-general-x4v3.pth",
                "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth"),
    "x4plus": ("RealESRGAN_x4plus.pth",
               "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.1.0/RealESRGAN_x4plus.pth"),
}


# ----------------------------------------------------------------------------------------
# Network definitions (re-implemented from Real-ESRGAN / BasicSR so no extra packages
# are needed; parameter names match the published checkpoints).
# ----------------------------------------------------------------------------------------
class SRVGGNetCompact(nn.Module):
    def __init__(self, num_in_ch=3, num_out_ch=3, num_feat=64, num_conv=32, upscale=4):
        super().__init__()
        self.upscale = upscale
        self.body = nn.ModuleList()
        self.body.append(nn.Conv2d(num_in_ch, num_feat, 3, 1, 1))
        self.body.append(nn.PReLU(num_parameters=num_feat))
        for _ in range(num_conv):
            self.body.append(nn.Conv2d(num_feat, num_feat, 3, 1, 1))
            self.body.append(nn.PReLU(num_parameters=num_feat))
        self.body.append(nn.Conv2d(num_feat, num_out_ch * upscale * upscale, 3, 1, 1))
        self.upsampler = nn.PixelShuffle(upscale)

    def forward(self, x):
        out = x
        for layer in self.body:
            out = layer(out)
        out = self.upsampler(out)
        return out + F.interpolate(x, scale_factor=self.upscale, mode="nearest")


class ResidualDenseBlock(nn.Module):
    def __init__(self, nf=64, gc=32):
        super().__init__()
        self.conv1 = nn.Conv2d(nf, gc, 3, 1, 1)
        self.conv2 = nn.Conv2d(nf + gc, gc, 3, 1, 1)
        self.conv3 = nn.Conv2d(nf + 2 * gc, gc, 3, 1, 1)
        self.conv4 = nn.Conv2d(nf + 3 * gc, gc, 3, 1, 1)
        self.conv5 = nn.Conv2d(nf + 4 * gc, nf, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, True)

    def forward(self, x):
        x1 = self.lrelu(self.conv1(x))
        x2 = self.lrelu(self.conv2(torch.cat((x, x1), 1)))
        x3 = self.lrelu(self.conv3(torch.cat((x, x1, x2), 1)))
        x4 = self.lrelu(self.conv4(torch.cat((x, x1, x2, x3), 1)))
        x5 = self.conv5(torch.cat((x, x1, x2, x3, x4), 1))
        return x5 * 0.2 + x


class RRDB(nn.Module):
    def __init__(self, nf, gc=32):
        super().__init__()
        self.rdb1 = ResidualDenseBlock(nf, gc)
        self.rdb2 = ResidualDenseBlock(nf, gc)
        self.rdb3 = ResidualDenseBlock(nf, gc)

    def forward(self, x):
        return self.rdb3(self.rdb2(self.rdb1(x))) * 0.2 + x


class RRDBNet(nn.Module):
    def __init__(self, num_in_ch=3, num_out_ch=3, nf=64, nb=23, gc=32):
        super().__init__()
        self.conv_first = nn.Conv2d(num_in_ch, nf, 3, 1, 1)
        self.body = nn.Sequential(*[RRDB(nf, gc) for _ in range(nb)])
        self.conv_body = nn.Conv2d(nf, nf, 3, 1, 1)
        self.conv_up1 = nn.Conv2d(nf, nf, 3, 1, 1)
        self.conv_up2 = nn.Conv2d(nf, nf, 3, 1, 1)
        self.conv_hr = nn.Conv2d(nf, nf, 3, 1, 1)
        self.conv_last = nn.Conv2d(nf, num_out_ch, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, True)

    def forward(self, x):
        feat = self.conv_first(x)
        feat = feat + self.conv_body(self.body(feat))
        feat = self.lrelu(self.conv_up1(F.interpolate(feat, scale_factor=2, mode="nearest")))
        feat = self.lrelu(self.conv_up2(F.interpolate(feat, scale_factor=2, mode="nearest")))
        return self.conv_last(self.lrelu(self.conv_hr(feat)))


def load_model(name, model_dir, device, half):
    filename, url = MODELS[name]
    path = os.path.join(model_dir, filename)
    if not os.path.exists(path):
        os.makedirs(model_dir, exist_ok=True)
        print(f"downloading {filename} ...")
        urllib.request.urlretrieve(url, path)
    state = torch.load(path, map_location="cpu", weights_only=True)
    state = state.get("params_ema", state.get("params", state))
    net = SRVGGNetCompact() if name == "general" else RRDBNet()
    net.load_state_dict(state, strict=True)
    net.eval().to(device)
    if half:
        net.half()
    return net


# ----------------------------------------------------------------------------------------
def is_tileable(rgb):
    """True if the image's opposite edges continue into each other (terrain tiles)."""
    a = rgb.astype(np.float32)
    inner_x = np.abs(a[:, 1:] - a[:, :-1]).mean()
    inner_y = np.abs(a[1:] - a[:-1]).mean()
    wrap_x = np.abs(a[:, 0] - a[:, -1]).mean()
    wrap_y = np.abs(a[0] - a[-1]).mean()
    return wrap_x <= 2.0 * inner_x + 2 and wrap_y <= 2.0 * inner_y + 2


@torch.no_grad()
def upscale_rgb(net, rgb, device, half, tile_wrap, pad):
    h, w, _ = rgb.shape
    mode = "wrap" if tile_wrap else "edge"
    padded = np.pad(rgb, ((pad, pad), (pad, pad), (0, 0)), mode=mode)
    t = torch.from_numpy(padded).permute(2, 0, 1).unsqueeze(0).float().div(255.0).to(device)
    if half:
        t = t.half()
    out = net(t).float().clamp(0, 1)
    scale = out.shape[-1] // padded.shape[1]
    out = out[..., pad * scale:(pad + h) * scale, pad * scale:(pad + w) * scale]
    return (out.squeeze(0).permute(1, 2, 0).cpu().numpy() * 255.0 + 0.5).astype(np.uint8), scale


def fill_key(rgb, mask):
    """Replace keyed pixels with the nearest unkeyed colour (iterative 8-neighbour growth)."""
    out = rgb.astype(np.float32)
    known = ~mask
    if not known.any():
        return rgb
    h, w = mask.shape
    for _ in range(h + w):
        if known.all():
            break
        acc = np.zeros_like(out)
        cnt = np.zeros((h, w), np.float32)
        pk = np.pad(known, 1)
        po = np.pad(out * known[..., None], ((1, 1), (1, 1), (0, 0)))
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy == 0 and dx == 0:
                    continue
                k = pk[1 + dy:1 + dy + h, 1 + dx:1 + dx + w]
                acc += po[1 + dy:1 + dy + h, 1 + dx:1 + dx + w]
                cnt += k
        grow = ~known & (cnt > 0)
        out[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    return np.clip(out + 0.5, 0, 255).astype(np.uint8)


def upscale_mask(mask, size):
    """Smoothly upscale a binary key mask and re-binarise it (no blocky staircase edges)."""
    m = Image.fromarray((mask * 255).astype(np.uint8), "L").resize(size, Image.BICUBIC)
    return np.asarray(m) >= 128


def process(net, rgb, device, half, max_out, passes):
    mask = (rgb == KEY).all(axis=2)
    keyed = mask.any()
    src = fill_key(rgb, mask) if keyed else rgb
    tile_wrap = is_tileable(src) and not keyed
    out = src
    for _ in range(max(1, passes)):
        if max(out.shape[:2]) * 4 > max_out:
            break
        pad = max(4, min(out.shape[:2]) // 8)
        out, _ = upscale_rgb(net, out, device, half, tile_wrap, pad)
    if out is src:
        return None
    out = out.copy()
    stray = (out == KEY).all(axis=2)
    out[stray] = (254, 0, 254)          # real colours must never equal the key
    if keyed:
        out[upscale_mask(mask, (out.shape[1], out.shape[0]))] = KEY
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="folder written by 'mas_textures.py export'")
    ap.add_argument("output", help="folder for the HD textures (same <archive>/<texture>.png layout)")
    ap.add_argument("--model", choices=sorted(MODELS), default="general")
    ap.add_argument("--model-dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "models"))
    ap.add_argument("--min-size", type=int, default=8, help="skip textures smaller than this")
    ap.add_argument("--max-out", type=int, default=1024, help="cap on output size (pixels per side)")
    ap.add_argument("--passes", type=int, default=1, help="run the 4x model this many times (2 = 16x)")
    ap.add_argument("--match", default="", help="only process paths containing this text (e.g. Cmaps/01)")
    ap.add_argument("--include-ui", action="store_true", help="also upscale menus, fonts, loading screens, dash")
    ap.add_argument("--overwrite", action="store_true", help="redo textures that already exist in output")
    ap.add_argument("--cpu", action="store_true", help="force CPU even if a GPU is available")
    args = ap.parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() and not args.cpu else "cpu")
    half = device.type == "cuda"
    print(f"device: {device}{' (fp16)' if half else ''}, model: {args.model}")
    net = load_model(args.model, args.model_dir, device, half)

    files = []
    for root, _, names in os.walk(args.input):
        archive = os.path.basename(root).lower()
        if archive in UI_ARCHIVES and not args.include_ui:
            continue
        for n in names:
            rel = os.path.relpath(os.path.join(root, n), args.input)
            if n.lower().endswith(".png") and args.match.lower() in rel.replace("\\", "/").lower():
                files.append(rel)
    files.sort()

    done = skipped = 0
    started = time.time()
    for i, rel in enumerate(files, 1):
        dst = os.path.join(args.output, rel)
        if os.path.exists(dst) and not args.overwrite:
            skipped += 1
            continue
        rgb = np.asarray(Image.open(os.path.join(args.input, rel)).convert("RGB"))
        if min(rgb.shape[:2]) < args.min_size:
            skipped += 1
            continue
        out = process(net, rgb, device, half, args.max_out, args.passes)
        if out is None:
            skipped += 1
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        Image.fromarray(out, "RGB").save(dst)
        done += 1
        if done % 25 == 0 or i == len(files):
            rate = done / max(1e-6, time.time() - started)
            print(f"[{i}/{len(files)}] {rel} -> {out.shape[1]}x{out.shape[0]}  ({rate:.1f} textures/s)")

    print(f"finished: {done} upscaled, {skipped} skipped, {time.time() - started:.0f}s")


if __name__ == "__main__":
    main()
