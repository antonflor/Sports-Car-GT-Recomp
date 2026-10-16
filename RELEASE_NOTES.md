# v1.0.0 — Sports Car GT: 2026 Edition

Sports Car GT (ISI / EA, 1999) on modern Windows: native resolution, true widescreen and AI-upscaled HD textures, through **scgt2026**, a renderer written for this game (replaces its 3dfx Glide driver, draws with Direct3D 9). The game's exe isn't changed.

## What's in it
- Native-resolution output (1080p, 1440p, 4K), borderless fullscreen, 4× MSAA, 16× anisotropic filtering, vsync
- True widescreen (Hor+) with the race HUD at the screen edges; 16:9 and 21:9 tested
- Widescreen cockpit: the dash stays as drawn, roof and door panel continue to the screen edges
- HD textures for every car and track (4× Real-ESRGAN), 32-bit colour, compressed in memory
- Replays in widescreen, with the rear-view mirror kept in its frame
- Recovers from a lost graphics device (screen lock, Ctrl+Alt+Del, UAC prompts)

## Install
1. Download **SportsCarGT2026-Kit-v1.0.0.zip** and unzip it next to your Sports Car GT CD folder (or mount the ISO).
2. Double-click **Install Sports Car GT 2026.cmd** and allow administrator rights.
3. Start **Sports Car GT 2026** from the desktop, pick **3Dfx Voodoo** in the launcher, click **Play Sports Car**.

Already installed? Replace `glide2x.dll` in the game folder with the one attached here.

## Assets
- `SportsCarGT2026-Kit-v1.0.0.zip` — full install kit: renderer, installer, settings, official 1.551 patch, runtime DLLs, HD texture pack (sha256 `c04960163070bc4ea7a6890983d6c1cccc67a74d7c71c5eb1e723c13e1614e25`)
- `glide2x.dll` — the renderer on its own (sha256 `931163bbd82712d95bf4602fee973a2e7af4e3ea14d905ec0e02bc405bc76247`)

You need your own copy of the game. The kit contains no game data beyond the official patch, the two runtime DLLs the original setup unpacks, and textures derived from the game's own — keep the repository and its releases private.

Tested: full race, menus, replays and forced device resets on Windows (NVIDIA, 3840×2160); every track, wet and night variants, cockpit and chase views, 4:3 / 16:9 / 21:9 in the Wine test harness.
