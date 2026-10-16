// Direct3D 9 device, render targets, clears, presentation and 2D blits.
#include "scgt.hxx"
#include "shaders.hxx"
#include <d3dcompiler.h>
#include "stb_image_write.h"

namespace scgt
{
    Output out;
    IDirect3DDevice9* dev = nullptr;
    HWND gameWindow = nullptr;

    static IDirect3D9* d3d = nullptr;
    static D3DPRESENT_PARAMETERS pp = {};
    static IDirect3DSurface9* colorRT[2] = {};       // the two Glide colour buffers (MSAA); they flip on swap
    static int backIdx = 0;                          // colorRT[backIdx] is the back buffer
    static IDirect3DSurface9* depth = nullptr;
    static IDirect3DTexture9* frontTex = nullptr;    // the Glide front buffer (last presented image)
    static IDirect3DSurface9* frontSurf = nullptr;
    static IDirect3DTexture9* readTex = nullptr;     // logical-size readback
    static IDirect3DSurface9* readSurf = nullptr;
    static IDirect3DTexture9* savedTex = nullptr;   // full-resolution copy of a buffer as the game last read it
    static IDirect3DSurface9* savedSurf = nullptr;
    static RECT wantRect = {};
    static bool savedValid = false;
    static IDirect3DSurface9* readSys = nullptr;
    static IDirect3DVertexShader9* blitVS = nullptr;
    static IDirect3DPixelShader9* blitPS = nullptr;
    static IDirect3DVertexDeclaration9* blitDecl = nullptr;
    static int currentTarget = -1;
    static D3DFORMAT depthFormat = D3DFMT_D24S8;
    static LONG oldStyle = 0, oldExStyle = 0;
    static RECT oldRect = {};

    typedef HRESULT(WINAPI* PD3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

    // Compile with Microsoft's compiler when the system has it; otherwise use the
    // bytecode built into the DLL.
    static const DWORD* ShaderCode(const char* entry, const char* profile, const unsigned char* fallback, std::vector<u8>& storage)
    {
        static PD3DCompile compile = nullptr;
        static bool looked = false;
        if (!looked)
        {
            looked = true;
            HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
            if (!m) m = LoadLibraryA("d3dcompiler_43.dll");
            if (m) compile = (PD3DCompile)GetProcAddress(m, "D3DCompile");
        }
        if (compile)
        {
            ID3DBlob* code = nullptr;
            ID3DBlob* err = nullptr;
            HRESULT hr = compile(SHADER_SOURCE, strlen(SHADER_SOURCE), "scgt2026", nullptr, nullptr, entry, profile, 0, 0, &code, &err);
            if (err) { Log("shader %s: %s", entry, (const char*)err->GetBufferPointer()); err->Release(); }
            if (SUCCEEDED(hr) && code)
            {
                storage.assign((u8*)code->GetBufferPointer(), (u8*)code->GetBufferPointer() + code->GetBufferSize());
                code->Release();
                return (const DWORD*)storage.data();
            }
        }
        return (const DWORD*)fallback;
    }

    IDirect3DVertexShader9* MakeVS(const char* entry, const unsigned char* fallback)
    {
        std::vector<u8> s;
        IDirect3DVertexShader9* vs = nullptr;
        if (FAILED(dev->CreateVertexShader(ShaderCode(entry, "vs_3_0", fallback, s), &vs)))
        {
            s.clear();
            dev->CreateVertexShader((const DWORD*)fallback, &vs);
        }
        return vs;
    }

    IDirect3DPixelShader9* MakePS(const char* entry, const unsigned char* fallback)
    {
        std::vector<u8> s;
        IDirect3DPixelShader9* ps = nullptr;
        if (FAILED(dev->CreatePixelShader(ShaderCode(entry, "ps_3_0", fallback, s), &ps)))
        {
            dev->CreatePixelShader((const DWORD*)fallback, &ps);
        }
        return ps;
    }

    static void ComputeLayout()
    {
        // 2D content keeps the game's 4:3 shape, centred. The 3D view may be widened (see patch.cxx).
        const float sx = (float)out.width / out.logicalW;
        const float sy = (float)out.height / out.logicalH;
        const float s = sx < sy ? sx : sy;
        out.scaleX = s;
        out.scaleY = s;
        out.offsetX = (out.width - out.logicalW * s) * 0.5f;
        out.offsetY = (out.height - out.logicalH * s) * 0.5f;
        out.view3DScaleX = s * out.viewScaleX;
        out.view3DOffsetX = out.width * 0.5f - out.logicalW * 0.5f * out.view3DScaleX;
    }

    void UpdateLayout() { ComputeLayout(); InvalidateState(); }

    static bool CreateTargets()
    {
        D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE;
        if (cfg.MSAA > 1)
        {
            for (int n = cfg.MSAA; n >= 2; n--)
            {
                if (SUCCEEDED(d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, (D3DMULTISAMPLE_TYPE)n, nullptr)) &&
                    SUCCEEDED(d3d->CheckDeviceMultiSampleType(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, depthFormat, TRUE, (D3DMULTISAMPLE_TYPE)n, nullptr)))
                {
                    ms = (D3DMULTISAMPLE_TYPE)n;
                    break;
                }
            }
        }
        for (int i = 0; i < 2; i++)
            if (FAILED(dev->CreateRenderTarget(out.width, out.height, D3DFMT_A8R8G8B8, ms, 0, FALSE, &colorRT[i], nullptr))) return false;
        backIdx = 0;
        if (FAILED(dev->CreateDepthStencilSurface(out.width, out.height, depthFormat, ms, 0, TRUE, &depth, nullptr))) return false;
        if (FAILED(dev->CreateTexture(out.width, out.height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &frontTex, nullptr))) return false;
        frontTex->GetSurfaceLevel(0, &frontSurf);
        dev->CreateTexture(out.logicalW, out.logicalH, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &readTex, nullptr);
        if (readTex) readTex->GetSurfaceLevel(0, &readSurf);
        dev->CreateOffscreenPlainSurface(out.logicalW, out.logicalH, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readSys, nullptr);
        if (SUCCEEDED(dev->CreateTexture(out.width, out.height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &savedTex, nullptr)))
            savedTex->GetSurfaceLevel(0, &savedSurf);
        savedValid = false;
        Log("targets %dx%d msaa %d depth %d", out.width, out.height, (int)ms, (int)depthFormat);
        dev->SetDepthStencilSurface(depth);
        for (int i = 0; i < 2; i++)
        {
            dev->SetRenderTarget(0, colorRT[i]);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 0.0f, 0);
        }
        currentTarget = -1;
        SetRenderBuffer(st.renderBuffer);
        return true;
    }

    static void ReleaseTargets()
    {
        if (readSys) { readSys->Release(); readSys = nullptr; }
        if (savedSurf) { savedSurf->Release(); savedSurf = nullptr; }
        if (savedTex) { savedTex->Release(); savedTex = nullptr; }
        savedValid = false;
        if (readSurf) { readSurf->Release(); readSurf = nullptr; }
        if (readTex) { readTex->Release(); readTex = nullptr; }
        if (frontSurf) { frontSurf->Release(); frontSurf = nullptr; }
        if (frontTex) { frontTex->Release(); frontTex = nullptr; }
        if (depth) { depth->Release(); depth = nullptr; }
        for (auto& rt : colorRT) if (rt) { rt->Release(); rt = nullptr; }
    }

    bool DeviceOpen(HWND hwnd, int logicalW, int logicalH)
    {
        if (!hwnd) hwnd = GetActiveWindow();
        if (!hwnd) hwnd = GetForegroundWindow();
        gameWindow = hwnd;

        // work in real pixels on high-DPI screens (otherwise Windows scales the picture again)
        static bool dpiSet = false;
        if (!dpiSet)
        {
            dpiSet = true;
            HMODULE user = GetModuleHandleA("user32.dll");
            typedef BOOL(WINAPI * PCtx)(HANDLE);
            typedef BOOL(WINAPI * PAware)(void);
            auto ctx = (PCtx)GetProcAddress(user, "SetProcessDpiAwarenessContext");
            if (!ctx || !ctx((HANDLE)-4)) // per-monitor v2
            {
                auto aware = (PAware)GetProcAddress(user, "SetProcessDPIAware");
                if (aware) aware();
            }
        }
        out.logicalW = logicalW;
        out.logicalH = logicalH;

        if (!d3d) d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) return false;

        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoA(mon, &mi);
        const int monW = mi.rcMonitor.right - mi.rcMonitor.left;
        const int monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

        int w = cfg.Width > 0 ? cfg.Width : monW;
        int h = cfg.Height > 0 ? cfg.Height : monH;
        if (cfg.Window == 1 && cfg.Width <= 0)
        {
            h = monH * 3 / 4;
            w = h * 4 / 3;
        }

        if (!oldStyle)
        {
            oldStyle = GetWindowLongA(hwnd, GWL_STYLE);
            oldExStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
            GetWindowRect(hwnd, &oldRect);
        }
        if (cfg.Window == 1)
        {
            SetWindowLongA(hwnd, GWL_STYLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE);
            RECT r = { 0, 0, w, h };
            AdjustWindowRectEx(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);
            SetWindowPos(hwnd, HWND_NOTOPMOST, mi.rcMonitor.left + (monW - (r.right - r.left)) / 2, mi.rcMonitor.top + (monH - (r.bottom - r.top)) / 2,
                r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        else
        {
            SetWindowLongA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowLongA(hwnd, GWL_EXSTYLE, oldExStyle & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
            if (cfg.Window == 2) { w = cfg.Width > 0 ? cfg.Width : monW; h = cfg.Height > 0 ? cfg.Height : monH; }
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, cfg.Window == 2 ? monW : w, cfg.Window == 2 ? monH : h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        out.width = w;
        out.height = h;
        GetWindowRect(hwnd, &wantRect);

        pp = {};
        pp.BackBufferWidth = w;
        pp.BackBufferHeight = h;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = hwnd;
        pp.Windowed = cfg.Window != 0;
        pp.EnableAutoDepthStencil = FALSE;
        pp.PresentationInterval = cfg.VSync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
        if (!pp.Windowed) pp.FullScreen_RefreshRateInHz = 0;

        // reversed float depth gives Voodoo-like W-buffer precision
        const D3DFORMAT candidates[] = { D3DFMT_D24FS8, D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D16 };
        for (D3DFORMAT f : candidates)
        {
            if (SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, f)) &&
                SUCCEEDED(d3d->CheckDepthStencilMatch(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, f)))
            {
                depthFormat = f;
                break;
            }
        }

        DWORD flags = D3DCREATE_FPU_PRESERVE | D3DCREATE_HARDWARE_VERTEXPROCESSING;
        HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags, &pp, &dev);
        if (FAILED(hr))
        {
            flags = D3DCREATE_FPU_PRESERVE | D3DCREATE_SOFTWARE_VERTEXPROCESSING;
            hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags, &pp, &dev);
        }
        if (FAILED(hr))
        {
            Log("CreateDevice failed %08x", (unsigned)hr);
            return false;
        }
        ComputeLayout();
        if (!CreateTargets()) { Log("render targets failed"); return false; }

        blitVS = MakeVS("vs_blit", g_vs_blit);
        blitPS = MakePS("ps_blit", g_ps_blit);
        const D3DVERTEXELEMENT9 el[] = {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            D3DDECL_END() };
        dev->CreateVertexDeclaration(el, &blitDecl);
        InitPipeline();
        Log("device open: logical %dx%d output %dx%d window %d", logicalW, logicalH, w, h, cfg.Window);
        return true;
    }

    void DeviceClose()
    {
        if (!dev) return;
        ReleasePipeline();
        ReleaseTargets();
        if (blitDecl) { blitDecl->Release(); blitDecl = nullptr; }
        if (blitPS) { blitPS->Release(); blitPS = nullptr; }
        if (blitVS) { blitVS->Release(); blitVS = nullptr; }
        dev->Release();
        dev = nullptr;
        if (gameWindow && oldStyle)
        {
            SetWindowLongA(gameWindow, GWL_STYLE, oldStyle);
            SetWindowLongA(gameWindow, GWL_EXSTYLE, oldExStyle);
            SetWindowPos(gameWindow, nullptr, oldRect.left, oldRect.top, oldRect.right - oldRect.left, oldRect.bottom - oldRect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
            oldStyle = 0;
        }
    }

    // The window size the renderer chose; the game's own window moves (e.g. when a replay
    // starts) are held to it while the device is open.
    bool WantedWindowRect(RECT* r)
    {
        if (!dev || wantRect.right <= wantRect.left) return false;
        *r = wantRect;
        return true;
    }

    IDirect3DSurface9* TargetSurface(int buffer)
    {
        return buffer == GR_BUFFER_FRONTBUFFER ? colorRT[backIdx ^ 1] : colorRT[backIdx];
    }

    void SetRenderBuffer(int buffer)
    {
        if (!dev || buffer == currentTarget) return;
        currentTarget = buffer;
        dev->SetRenderTarget(0, TargetSurface(buffer));
        dev->SetDepthStencilSurface(depth);
    }

    void LfbDeviceReset(); // lfb.cxx: the render targets were recreated empty

    // Reset the device after it was lost (screen lock, Ctrl+Alt+Del, a UAC prompt, a display
    // mode change). Nothing may hold a default-pool resource during Reset, so the targets are
    // unbound and released first, then recreated; everything else is managed and survives.
    static bool ResetDevice()
    {
        for (DWORD i = 0; i < 8; i++) dev->SetTexture(i, nullptr);
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
        {
            dev->SetRenderTarget(0, bb);
            bb->Release();
        }
        dev->SetDepthStencilSurface(nullptr);
        ReleaseTargets();
        const HRESULT hr = dev->Reset(&pp);
        if (FAILED(hr))
        {
            Log("device reset failed %08lx", (unsigned long)hr);
            return false;
        }
        if (!CreateTargets())
        {
            Log("device reset: recreating render targets failed");
            return false;
        }
        currentTarget = -1;
        InvalidateState();
        LfbDeviceReset();
        Log("device reset");
        return true;
    }

    static bool HandleLost()
    {
        HRESULT hr = dev->TestCooperativeLevel();
        if (hr == D3D_OK) return true;
        if (hr == D3DERR_DEVICELOST) return false; // not yet: try again next frame
        if (hr == D3DERR_DEVICENOTRESET) return ResetDevice();
        return false;
    }

    // debugging aid: run the device-lost recovery on demand (create "reset_now" in the game folder)
    void TestDeviceReset()
    {
        if (!dev) return;
        Flush();
        dev->EndScene();
        Log("test: forcing a device reset");
        ResetDevice();
        dev->BeginScene();
    }

    static void DebugDump()
    {
        // Trace >= 5: every 200 frames, save the full-resolution frame as shown (debugging aid)
        static unsigned n = 0;
        if (cfg.Trace < 5 || (++n % 200) != 0) return;
        IDirect3DSurface9* sys = nullptr;
        if (FAILED(dev->CreateOffscreenPlainSurface(out.width, out.height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr))) return;
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(dev->GetRenderTargetData(frontSurf, sys)) && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
        {
            std::vector<u8> rgb((size_t)out.width * out.height * 3);
            for (int y = 0; y < out.height; y++)
            {
                const u32* row = (const u32*)((const u8*)lr.pBits + (size_t)y * lr.Pitch);
                for (int x = 0; x < out.width; x++)
                {
                    u8* d = &rgb[((size_t)y * out.width + x) * 3];
                    d[0] = (row[x] >> 16) & 255; d[1] = (row[x] >> 8) & 255; d[2] = row[x] & 255;
                }
            }
            sys->UnlockRect();
            char name[64];
            sprintf(name, "frame_%05u.png", n);
            stbi_write_png((gameDir + name).c_str(), out.width, out.height, 3, rgb.data(), out.width * 3);
        }
        sys->Release();
    }

    static void Show(IDirect3DSurface9* src)
    {
        // resolve into the display copy, then to the swap chain
        dev->StretchRect(src, nullptr, frontSurf, nullptr, D3DTEXF_NONE);
        DebugDump();
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)))
        {
            dev->StretchRect(frontSurf, nullptr, bb, nullptr, D3DTEXF_NONE);
            bb->Release();
        }
        dev->EndScene();
        HRESULT hr = dev->Present(nullptr, nullptr, nullptr, nullptr);
        if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) HandleLost();
        dev->BeginScene();
    }

    void Present()
    {
        if (!dev) return;
        Flush();
        LfbFlush();
        DrawCockpitSides();
        if (!frameHas3D)
        {
            // menus are 4:3: keep the side (or top/bottom) bars black
            D3DRECT bars[4];
            int n = 0;
            const LONG ox = (LONG)out.offsetX, oy = (LONG)out.offsetY;
            if (ox > 0)
            {
                bars[n++] = { 0, 0, ox, out.height };
                bars[n++] = { out.width - ox, 0, out.width, out.height };
            }
            if (oy > 0)
            {
                bars[n++] = { 0, 0, out.width, oy };
                bars[n++] = { 0, out.height - oy, out.width, out.height };
            }
            if (n)
            {
                SetRenderBuffer(GR_BUFFER_BACKBUFFER);
                dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
                dev->Clear(n, bars, D3DCLEAR_TARGET, 0, 0.0f, 0);
            }
        }
        Show(colorRT[backIdx]);
        EndFrame();
        hudCenterPrev.swap(hudCenter);
        hudCenter.clear();
        backIdx ^= 1; // flip: the old front buffer becomes the back buffer, like a Voodoo
        currentTarget = -1;
        InvalidateState();
    }

    // the game drew into the front buffer: show it right away
    void PresentFrontNow()
    {
        Show(colorRT[backIdx ^ 1]);
    }

    int BufferIndex(int buffer) { return buffer == GR_BUFFER_FRONTBUFFER ? (backIdx ^ 1) : backIdx; }

    void ClearBuffers(GrColor_t color, GrAlpha_t alpha, FxU16 depthValue)
    {
        if (!dev) return;
        Flush();
        SetRenderBuffer(st.renderBuffer);
        float c[4];
        ColorToFloat(color, c);
        const D3DCOLOR dc = D3DCOLOR_ARGB(alpha, (int)(c[0] * 255 + 0.5f), (int)(c[1] * 255 + 0.5f), (int)(c[2] * 255 + 0.5f));
        DWORD flags = 0;
        if (st.colorMaskRgb || st.colorMaskA) flags |= D3DCLEAR_TARGET;
        if (st.depthMask) flags |= D3DCLEAR_ZBUFFER;
        if (!flags) return;
        if (cfg.Trace >= 4) Log("  clear buffer %d clip %d,%d-%d,%d flags %lx", st.renderBuffer, st.clipMinX, st.clipMinY, st.clipMaxX, st.clipMaxY, flags);
        NoteClear();
        float z;
        if (st.depthMode == GR_DEPTHBUFFER_ZBUFFER || st.depthMode == GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS)
            z = depthValue / 65535.0f;
        else
            z = depthValue >= 0xff00 ? 0.0f : 1.0f - depthValue / 65535.0f; // reversed W

        if (flags & D3DCLEAR_TARGET) LfbNoteClear(st.renderBuffer, st.clipMinX, st.clipMinY, st.clipMaxX, st.clipMaxY);
        LfbFlush();
        SetRenderBuffer(st.renderBuffer);
        // a full-screen clip window clears the whole target (including pillarbox bars)
        const bool full = st.clipMinX <= 0 && st.clipMinY <= 0 && st.clipMaxX >= out.logicalW && st.clipMaxY >= out.logicalH;
        if (full)
        {
            dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            dev->Clear(0, nullptr, flags, dc, z, 0);
        }
        else
        {
            D3DRECT r;
            r.x1 = (LONG)(st.clipMinX * out.view3DScaleX + out.view3DOffsetX);
            r.x2 = (LONG)(st.clipMaxX * out.view3DScaleX + out.view3DOffsetX);
            r.y1 = (LONG)(st.clipMinY * out.scaleY + out.offsetY);
            r.y2 = (LONG)(st.clipMaxY * out.scaleY + out.offsetY);
            dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            dev->Clear(1, &r, flags, dc, z, 0);
        }
        InvalidateState();
    }

    // Draw a texture into a buffer at logical coordinates (x, y, w, h); keyed = skip alpha-0 pixels.
    void DrawFullscreenTexture(IDirect3DTexture9* tex, int buffer, float x, float y, float w, float h, bool keyed)
    {
        DrawTexture(tex, buffer, x, y, w, h, 0, 0, 1, 1, keyed);
    }

    void DrawTexture(IDirect3DTexture9* tex, int buffer, float x, float y, float w, float h, float u0, float v0, float u1, float v1, bool keyed, float shiftX)
    {
        if (!dev || !tex) return;
        Flush();
        SetRenderBuffer(buffer);
        const float x0 = x * out.scaleX + out.offsetX + shiftX, y0 = y * out.scaleY + out.offsetY;
        const float x1 = (x + w) * out.scaleX + out.offsetX + shiftX, y1 = (y + h) * out.scaleY + out.offsetY;
        const float v[4][6] = {
            { x0, y0, 0, 1, u0, v0 }, { x1, y0, 0, 1, u1, v0 }, { x0, y1, 0, 1, u0, v1 }, { x1, y1, 0, 1, u1, v1 } };
        dev->SetVertexShader(blitVS);
        dev->SetPixelShader(blitPS);
        dev->SetVertexDeclaration(blitDecl);
        const float target[4] = { 2.0f / out.width, 2.0f / out.height, 0, 0 };
        dev->SetVertexShaderConstantF(0, target, 1);
        const float key[4] = { keyed ? 1.0f : 0.0f, 0, 0, 0 };
        dev->SetPixelShaderConstantF(0, key, 1);
        dev->SetTexture(0, tex);
        const DWORD filter = cfg.Filter2D ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, filter);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, filter);
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetRenderState(D3DRS_ZENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xf);
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(v[0]));
        InvalidateState();
    }

    // Put back, at full resolution, a logical rectangle as it was when the game last read the
    // buffer (the menu cursor restoring what was under it).
    bool RestoreSaved(int buffer, int x0, int y0, int x1, int y1)
    {
        if (!dev || !savedValid) return false;
        const float ox0 = x0 * out.scaleX + out.offsetX, oy0 = y0 * out.scaleY + out.offsetY;
        const float ox1 = (x1 + 1) * out.scaleX + out.offsetX, oy1 = (y1 + 1) * out.scaleY + out.offsetY;
        DrawTexture(savedTex, buffer, (float)x0, (float)y0, (float)(x1 - x0 + 1), (float)(y1 - y0 + 1),
                    ox0 / out.width, oy0 / out.height, ox1 / out.width, oy1 / out.height, false);
        return true;
    }

    bool ReadBuffer(int buffer, std::vector<u32>& pixels)
    {
        if (!dev || !readSurf || !readSys) return false;
        Flush();
        dev->StretchRect(TargetSurface(buffer), nullptr, frontSurf, nullptr, D3DTEXF_NONE); // resolve (the display copy is rebuilt at every present)
        savedValid = savedSurf && SUCCEEDED(dev->StretchRect(frontSurf, nullptr, savedSurf, nullptr, D3DTEXF_NONE));
        RECT src = { (LONG)out.offsetX, (LONG)out.offsetY, (LONG)(out.offsetX + out.logicalW * out.scaleX), (LONG)(out.offsetY + out.logicalH * out.scaleY) };
        HRESULT hr = dev->StretchRect(frontSurf, &src, readSurf, nullptr, D3DTEXF_LINEAR);
        if (FAILED(hr)) Log("ReadBuffer: StretchRect %08x", (unsigned)hr);
        hr = dev->GetRenderTargetData(readSurf, readSys);
        if (FAILED(hr)) { Log("ReadBuffer: GetRenderTargetData %08x", (unsigned)hr); return false; }
        D3DLOCKED_RECT lr;
        if (FAILED(readSys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        pixels.resize((size_t)out.logicalW * out.logicalH);
        for (int y = 0; y < out.logicalH; y++)
            memcpy(&pixels[(size_t)y * out.logicalW], (u8*)lr.pBits + (size_t)y * lr.Pitch, out.logicalW * 4);
        readSys->UnlockRect();
        return true;
    }
}
