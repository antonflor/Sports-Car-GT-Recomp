# Running Sports Car GT on Windows 10/11 (phase 1)

Until the `scgt2026` renderer exists, the way to get modern resolutions is dgVoodoo2, which translates the game's Glide and DirectX 6 calls to Direct3D 11/12.

## 1. Install the game

1. Mount `Sports Car GT.iso` (double-click) and run `Setup.exe`. Choose the **full install** (graphics and vehicle data on disk). Install outside Program Files, e.g. `C:\Games\Sports Car GT`.
2. Apply the **1.551** patch: unzip `Patch-1551a.zip` into the game folder (it replaces `Spcar.exe`, `MAS\atln.mas`, `MAS\atlw.mas` and three files in `DAT\`).
3. For running without the disc, copy `CRACK\Spcar.exe` (1.551 No-CD) over the patched one. This is the exe the project targets.

Skip the DirectX 6 and video-driver installers on the disc.

## 2. dgVoodoo2

1. Download dgVoodoo2 from its official page (dege.freeweb.hu).
2. Copy from its `MS\x86` folder into the game folder: `DDraw.dll`, `D3DImm.dll`. Copy `3Dfx\x86\Glide2x.dll` too.
3. Copy `dgVoodooCpl.exe` into the game folder, run it, and pick the game folder as the config location. Suggested settings:
   - **General:** Output API `Direct3D 11`, Scaling mode `Stretched, keep aspect ratio`
   - **Glide / DirectX:** Resolution = your monitor's (or `Max`), Antialiasing `8x`, Texture filtering `Force anisotropic 16x`, VRAM `256 MB` or more
   - **DirectX:** untick "dgVoodoo Watermark"
4. In the game, Options → Graphics: pick the **Direct3D** renderer for the HD texture pack (the Glide API itself stops at 256×256 textures), and the highest resolution the menu offers (1024×768). dgVoodoo renders it at the resolution you chose above.

Widescreen today: a community fix on PCGamingWiki (by AuToMaNiAk005) ships replacement `Spcar.exe` files per aspect ratio, used together with a matching forced resolution in dgVoodoo. The project's own renderer will do this without replacing the exe.

## 3. HD texture pack

1. Back up `C:\Games\Sports Car GT\MAS`.
2. Copy the pack's `MAS\*.mas` files over the game's `MAS` folder.
3. Start a race with the BMW M3 #1 (car 01) at Sebring to see the upscaled car and track side by side with stock content.

What to check, and what it tells us:

| You see | Meaning |
|---|---|
| Sharper car and track, transparent wheel arches and trees | The engine accepts the larger textures. Do the whole pack |
| Game crashes or "Could not allocate D3D source texture surface!" | The D3D path can't take the size. Rebuild with `import --max-size 512`, then `256` |
| Some textures appear as garbage or the wrong size | The engine resamples textures somewhere. Note which ones (track, car) |
| Pink fringes around cut-outs | Colour-key edge issue; send a screenshot |

To undo, restore the `MAS` backup.
