# Sports Car GT: 2026 Edition

**Sports Car GT** is the GT racing sim Image Space Incorporated made for Electronic Arts in 1999 (PC, v1.551). This project brings it to modern Windows with HD resolution, widescreen and AI-upscaled textures, the same way [4x4 Evolution: 2026 Edition](https://github.com/antonflor/4x4-Evo-2026-Recomp) did for 4x4 Evolution.

> Status: **phase 1**. The game's archive format is fully decoded, all 2,513 textures can be exported, AI-upscaled and packed back into working `.MAS` archives, and there's a setup guide for running the game at modern resolutions today. The custom renderer (phase 2) is next.

## How Sports Car GT is put together

| Piece | What it is |
|---|---|
| `Spcar.exe` | The game. 32-bit, 1.551 build, links `DDRAW.dll` directly and loads `GLIDE2X.DLL` at runtime |
| Renderers | Two built in: **3dfx Glide 2.x** (looked up function by function with `GetProcAddress`) and **Direct3D (DirectX 6)** |
| `MAS\*.MAS` | ISI archives: car shapes (`CDB.MAS`), car liveries (`Cmaps.mas`), track shapes (`<track>.mas`) and track textures (`<track>MAP.MAS`), menus, fonts, loading screens |
| `DAT\SCENE.MAS` | Track scene/AI files (text) |
| `DAT\VEHICLE.MAS` | Car physics/setup files (text) |

The `.MAS` format and the texture format inside it are documented at the top of [`tools/mas.py`](tools/mas.py). In short: a directory of 32-byte entries, then the payload; textures are a 16-byte header (width, height, compressed, paletted) followed by zlib-compressed 24-bit RGB, top-down. Pure magenta (255, 0, 255) is the transparency colour key. The game converts every texture to 16-bit (RGB555/565) when it loads it.

## Plan

| Phase | What you get | State |
|---|---|---|
| **1. Tools + HD textures** | Export / upscale / repack every texture. Run the game through dgVoodoo2 at native resolution with MSAA and anisotropic filtering. | ✅ tools done, test pack ready for in-game check |
| **2. `scgt2026` renderer** | A drop-in `glide2x.dll` written for this game (Glide 2 → Direct3D 11): native-resolution output, true Hor+ widescreen (in-memory patch of the camera, HUD moved to the edges), 32-bit HD texture replacement by name + CRC at upload time (gets around the game's 256×256 / 16-bit texture path), 16× AF, MSAA, vsync, borderless window, texture dumping | next |
| **3. Installer** | All-in-one Inno Setup installer like the 4x4 one (game files from a personal copy, so the repo stays **private**) | later |

Why Glide for phase 2: the Glide 2 API is small (about 100 functions), every vertex arrives already in screen space, and every texture goes through one upload call (`grTexDownloadMipMap`). That makes resolution scaling, widescreen and texture replacement much simpler and more reliable than wrapping the DirectX 6 path.

## Layout

| Path | What it is |
|---|---|
| `tools/mas.py` | MAS archive library + CLI (`list`, `extract`, `pack`). Round-trips all 65 retail archives byte-for-byte |
| `tools/mas_textures.py` | `export` every texture to PNG, `import` replacement PNGs into new MAS archives |
| `tools/upscale_textures.py` | Real-ESRGAN upscaler with colour-key handling and seamless tiling |
| `docs/SETUP.md` | Running the game on Windows 10/11 today (dgVoodoo2) and installing the HD texture pack |

## HD texture workflow

```
pip install torch --index-url https://download.pytorch.org/whl/cu128   # NVIDIA GPU; CPU works too
pip install pillow numpy

python tools\mas_textures.py export "C:\Games\Sports Car GT" textures_original
python tools\upscale_textures.py textures_original textures_hd
python tools\mas_textures.py import "C:\Games\Sports Car GT" textures_hd hd_pack
```

Then back up `C:\Games\Sports Car GT\MAS` and copy `hd_pack\MAS\*` over it.

- `--match Cmaps/01` upscales one car only (car #01), `--match SEBAMAP` one track only.
- `--model x4plus` for the sharper (heavier) model.
- Menus, fonts, loading screens and the dash are skipped by default (the game ships one image per screen resolution for those); `--include-ui` includes them.
- `import --max-size 256` caps replacements at 256×256 if the engine turns out not to accept larger textures (that still upscales every texture smaller than 256).

Keyed (transparent) textures — wheel arches, trees, fences, flags, spectators — are handled explicitly: the magenta is filled with neighbouring colours before upscaling so it can't bleed into the edges, and the mask is rebuilt at the new size with smooth edges.

## Game files

No game files are in this repo. You need your own copy of Sports Car GT (full install) with the 1.551 patch.
