// In-memory patches to Spcar.exe (nothing on disk changes).
//  * Mouse: the game works in its logical 640x480 window space. GetCursorPos / ClipCursor are
//    redirected so the cursor maps onto the scaled picture.
#include "scgt.hxx"

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
    }
}
