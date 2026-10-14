# Sports Car GT: 2026 Edition

Sports Car GT (Image Space Incorporated / EA, 1999) on modern Windows, in native HD, true widescreen, with AI-upscaled textures. The heart of it is **scgt2026**, a renderer written for this game that replaces its 3dfx Glide driver and draws with Direct3D 9. The game's exe isn't changed on disk.

- Native resolution (1080p, 1440p, 4K), borderless fullscreen
- True widescreen (Hor+): more of the track at the sides, nothing stretched
- Race HUD moved to the screen edges; menus stay in their 4:3 layout
- Widescreen cockpit: the dash stays true to the original, its roof and door panel continue to the screen edges, and the side windows open onto the wider view
- HD textures: every car livery and track texture, 4× AI-upscaled, full 32-bit colour
- 4× MSAA, 16× anisotropic filtering, vsync

## Install

You need your own Sports Car GT disc: the extracted CD folder or the ISO, plus `Patch-1551a.zip` (the official 1.551 update, included in the kit).

1. Mount the ISO (double-click it), or keep the extracted CD folder next to this kit.
2. Double-click **Install Sports Car GT 2026.cmd** and allow it administrator rights (the game keeps its folder paths in the registry, like the original setup).
3. Start the game from the **Sports Car GT 2026** shortcut on your desktop.

The installer copies the game to `C:\Games\Sports Car GT`, applies the 1.551 patch and the 1.551 exe that runs without the disc, then adds the renderer and the HD texture pack. Other options:

```
powershell -ExecutionPolicy Bypass -File install.ps1 -GamePath "D:\Games\Sports Car GT"
powershell -ExecutionPolicy Bypass -File install.ps1 -Source "E:\"            # where the disc is
powershell -ExecutionPolicy Bypass -File install.ps1 -RendererOnly             # update an existing install
powershell -ExecutionPolicy Bypass -File install.ps1 -NoTextures               # original textures
```

### First start

The game opens with its launcher window. Under **Select Video Driver** pick **3Dfx Voodoo** (that's scgt2026) and click **Play Sports Car**.

Recommended in **Options → Graphics** (they're saved with your player):

- **Screen Resolution: 1024×768** — sharper menus, fonts, HUD and cockpit (the 3D view is always drawn at your monitor's resolution, whatever the game is set to)
- **E-Mapping** on — reflections on the cars
- **MIP Mapping** on
- all detail levels on **Max**

Tracks and cars unlock as you play, or type `isi-cheeseman` on the first screen, then `isi-tbone` on the Race Setup screen.

## Settings

`scgt2026.ini` in the game folder, section `[scgt2026]`:

| Key | Default | Meaning |
|---|---|---|
| `Width` / `Height` | `0` | Output resolution. `0` = the monitor's |
| `Window` | `2` | `0` exclusive fullscreen · `1` window · `2` borderless fullscreen |
| `Widescreen` | `1` | `1` true widescreen (wider view) · `0` classic 4:3 with side bars |
| `HudToEdges` | `1` | In widescreen, race HUD at the screen edges · `0` keep it in the 4:3 area |
| `VSync` | `1` | Sync to the monitor's refresh rate |
| `MSAA` | `4` | Anti-aliasing samples (`0`, `2`, `4`, `8`) |
| `Anisotropy` | `16` | Anisotropic filtering (`0` = off) |
| `Trilinear` | `1` | Smooth transitions between texture detail levels |
| `Filter2D` | `1` | Smooth scaling of movies and menus · `0` sharp pixels |
| `ReplaceTextures` | `1` | Load HD textures from `textures\` |
| `CompressHD` | `1` | Keep HD textures compressed in memory (DXT); leave on — the game is 32-bit |
| `DumpTextures` | `0` | Save every texture the game uploads to `textures_dump\` (for making your own) |
| `Trace` | `0` | Write a log to `scgt2026.log` |

## HD textures

The pack is in `textures\`, one PNG per texture, named after a checksum of the texture as the game uploads it. To make your own (another model, a different scale, hand-painted fixes), see the tools in the project repository (`tools/mas_textures.py`, `tools/upscale_textures.py`). A replacement can be any size and is loaded in 32-bit colour.

## Uninstall

`powershell -ExecutionPolicy Bypass -File uninstall.ps1` removes the renderer and the HD textures. Add `-RemoveGame` to delete the game, its registry key and the shortcuts.

## Troubleshooting

- **Windows asks to install DirectPlay.** That's for the game's 1999 network mode; you can decline, single player doesn't need it.
- **Antivirus flags Spcar.exe.** The 1.551 exe that runs without the disc is a patched build from 2008 (three calls to the CD check replaced). Restore it, or use the official 1.551 `Spcar.exe` from `Patch-1551a.zip` and keep the ISO mounted.
- **Black screen or the wrong monitor.** Set `Window=1` to run in a window, or set `Width`/`Height` explicitly.
- **Anything else.** Set `Trace=1`, reproduce, and look at `scgt2026.log`.

## Credits

- Sports Car GT © 1999 Image Space Incorporated / Electronic Arts. You need your own copy; the kit contains no game data beyond the official patch and the two runtime DLLs from the original setup.
- PNG loading and saving: stb_image / stb_image_write; texture compression: stb_dxt (Sean Barrett and contributors, public domain).
- Upscaling model: Real-ESRGAN by Xintao Wang et al. (BSD-3-Clause).
