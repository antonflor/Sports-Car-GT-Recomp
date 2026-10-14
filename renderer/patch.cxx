// In-memory patches to Spcar.exe (nothing on disk changes).
//  * Mouse: the game works in its logical 640x480 window space. GetCursorPos / ClipCursor and
//    the window's mouse messages are mapped onto the scaled picture.
//  * Widescreen (Hor+): the game's view setup (0x48F6F0, v1.551) gets a wider horizontal field
//    of view for full-screen views; the renderer stretches the 3D view to the screen width.
//    Smaller views (the mirror) keep their field of view and are drawn in the 4:3 layout.
#include "scgt.hxx"
#include <math.h>

namespace scgt
{
    static BOOL(WINAPI* realGetCursorPos)(LPPOINT) = nullptr;
    static BOOL(WINAPI* realClipCursor)(const RECT*) = nullptr;
    static BOOL(WINAPI* realSetCursorPos)(int, int) = nullptr;

    static POINT ClientOrigin()
    {
        POINT o = { 0, 0 };
        if (gameWindow) ClientToScreen(gameWindow, &o);
        return o;
    }

    static BOOL WINAPI HookGetCursorPos(LPPOINT p)
    {
        if (!realGetCursorPos(p)) return FALSE;
        if (!dev || !gameWindow) return TRUE;
        const POINT o = ClientOrigin();
        float x = (p->x - o.x - out.offsetX) / out.scaleX;
        float y = (p->y - o.y - out.offsetY) / out.scaleY;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > out.logicalW - 1) x = (float)(out.logicalW - 1);
        if (y > out.logicalH - 1) y = (float)(out.logicalH - 1);
        p->x = o.x + (LONG)x;
        p->y = o.y + (LONG)y;
        return TRUE;
    }

    static BOOL WINAPI HookSetCursorPos(int x, int y)
    {
        if (!dev || !gameWindow) return realSetCursorPos(x, y);
        const POINT o = ClientOrigin();
        return realSetCursorPos(o.x + (int)((x - o.x) * out.scaleX + out.offsetX), o.y + (int)((y - o.y) * out.scaleY + out.offsetY));
    }

    static BOOL WINAPI HookClipCursor(const RECT* r)
    {
        if (!r || !dev || !gameWindow) return realClipCursor(r);
        const POINT o = ClientOrigin();
        RECT s;
        s.left = o.x + (LONG)((r->left - o.x) * out.scaleX + out.offsetX);
        s.right = o.x + (LONG)((r->right - o.x) * out.scaleX + out.offsetX);
        s.top = o.y + (LONG)((r->top - o.y) * out.scaleY + out.offsetY);
        s.bottom = o.y + (LONG)((r->bottom - o.y) * out.scaleY + out.offsetY);
        return realClipCursor(&s);
    }

    // Mouse messages carry client coordinates: map them back to the game's logical space.
    static WNDPROC realWndProc = nullptr;
    static LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (dev && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL)
        {
            float x = ((short)LOWORD(lp) - out.offsetX) / out.scaleX;
            float y = ((short)HIWORD(lp) - out.offsetY) / out.scaleY;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x > out.logicalW - 1) x = (float)(out.logicalW - 1);
            if (y > out.logicalH - 1) y = (float)(out.logicalH - 1);
            lp = MAKELPARAM((int)x, (int)y);
            if (cfg.Trace >= 2 && msg != WM_MOUSEMOVE) Log("mouse msg %04x -> %d,%d", msg, (int)x, (int)y);
        }
        return CallWindowProcA(realWndProc, hwnd, msg, wp, lp);
    }

    void HookWindow(HWND hwnd)
    {
        if (!hwnd || realWndProc) return;
        realWndProc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)(uintptr_t)HookWndProc);
        Log("window procedure hooked (%p)", (void*)realWndProc);
    }

    // Replace an imported function in the main executable's import table.
    static void* HookImport(const char* dll, const char* name, void* hook)
    {
        u8* base = (u8*)GetModuleHandleA(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)base;
        auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) return nullptr;
        for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++)
        {
            if (_stricmp((const char*)(base + imp->Name), dll) != 0) continue;
            auto thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
            auto orig = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            for (; orig->u1.AddressOfData; orig++, thunk++)
            {
                if (orig->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
                auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
                if (strcmp((const char*)ibn->Name, name) != 0) continue;
                DWORD old;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
                void* prev = (void*)thunk->u1.Function;
                thunk->u1.Function = (uintptr_t)hook;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
                return prev;
            }
        }
        return nullptr;
    }

    // ---- widescreen -----------------------------------------------------------------------
    // View::SetFov(float fovX, float fovY) — thiscall, radians; a negative fovY means "derive
    // from fovX". Fields: +0x08 focal x, +0x0C focal y, +0x18 tan(fovX/2), +0x1C tan(fovY/2),
    // +0x50 width, +0x54 height, +0x68 fovX in degrees, +0x6C fovY in degrees.
    static const uintptr_t VIEW_SETFOV = 0x48F6F0;
    static const u8 SETFOV_PROLOGUE[7] = { 0x57, 0x56, 0x55, 0x53, 0x83, 0xEC, 0x64 };
    typedef void(__fastcall* SetFovFn)(u8* view, void* edx, float fovX, float fovY);
    static SetFovFn origSetFov = nullptr;
    static float hFactor = 1.0f; // widescreen factor: (screen aspect) / (4:3)

    static void __fastcall HookSetFov(u8* view, void* edx, float fovX, float fovY)
    {
        void* caller = __builtin_return_address(0);
        if (hFactor <= 1.0001f) { origSetFov(view, edx, fovX, fovY); return; }
        if (!(fovY >= 0.0f)) fovY = fovX * 0.8038585186f; // the game's own derivation
        const int w = *(int*)(view + 0x50);
        const int h = *(int*)(view + 0x54);
        const bool fullScreen = w >= out.logicalW * 9 / 10 && h >= out.logicalH * 9 / 10;
        if (fullScreen)
        {
            // Hor+: same vertical view, horizontally wider by the screen's extra width
            const float wide = 2.0f * atanf(tanf(fovX * 0.5f) * hFactor);
            origSetFov(view, edx, wide, fovY);
        }
        else
        {
            // smaller views (rear-view mirror) are drawn in the 4:3 layout: unchanged
            origSetFov(view, edx, fovX, fovY);
        }
        // keep the stored angles as the game set them (it re-derives views from them)
        *(float*)(view + 0x68) = fovX * 57.29578018f;
        *(float*)(view + 0x6C) = fovY * 57.29578018f;
        if (cfg.Trace) Log("view %p %dx%d fov %.1f/%.1f deg%s from %p", view, w, h, fovX * 57.2958f, fovY * 57.2958f, fullScreen ? " (widened)" : "", caller);
    }

    static void PatchWidescreen()
    {
        const float aspect = (float)out.width / out.height;
        hFactor = cfg.Widescreen ? aspect / (4.0f / 3.0f) : 1.0f;
        if (hFactor < 1.0f) hFactor = 1.0f;
        if (hFactor <= 1.0001f) return;
        u8* target = (u8*)VIEW_SETFOV;
        if (IsBadReadPtr(target, 8) || memcmp(target, SETFOV_PROLOGUE, sizeof(SETFOV_PROLOGUE)) != 0)
        {
            Log("widescreen: unknown Spcar.exe build, staying 4:3");
            hFactor = 1.0f;
            return;
        }
        // trampoline: the original prologue, then jump back past it
        u8* tramp = (u8*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        memcpy(tramp, SETFOV_PROLOGUE, 7);
        tramp[7] = 0xE9;
        *(int32_t*)(tramp + 8) = (int32_t)((target + 7) - (tramp + 12));
        origSetFov = (SetFovFn)tramp;
        DWORD old;
        VirtualProtect(target, 7, PAGE_EXECUTE_READWRITE, &old);
        target[0] = 0xE9;
        *(int32_t*)(target + 1) = (int32_t)((u8*)&HookSetFov - (target + 5));
        target[5] = 0x90;
        target[6] = 0x90;
        VirtualProtect(target, 7, old, &old);
        FlushInstructionCache(GetCurrentProcess(), target, 7);
        out.viewScaleX = hFactor;
        UpdateLayout();
        Log("widescreen: Hor+ factor %.3f", hFactor);
    }

    void PatchGame()
    {
        static bool done = false;
        if (done) return;
        done = true;
        HMODULE user = GetModuleHandleA("user32.dll");
        realGetCursorPos = (BOOL(WINAPI*)(LPPOINT))GetProcAddress(user, "GetCursorPos");
        realClipCursor = (BOOL(WINAPI*)(const RECT*))GetProcAddress(user, "ClipCursor");
        realSetCursorPos = (BOOL(WINAPI*)(int, int))GetProcAddress(user, "SetCursorPos");
        const bool a = HookImport("USER32.dll", "GetCursorPos", (void*)HookGetCursorPos) != nullptr;
        const bool b = HookImport("USER32.dll", "ClipCursor", (void*)HookClipCursor) != nullptr;
        HookImport("USER32.dll", "SetCursorPos", (void*)HookSetCursorPos);
        Log("mouse hooks: GetCursorPos %d ClipCursor %d", a, b);
        HookWindow(gameWindow);
        PatchWidescreen();
    }
}
