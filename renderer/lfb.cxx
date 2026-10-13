// Linear frame buffer access.
//
// Sports Car GT draws its movies and all of its menus by writing pixels straight into the
// frame buffer, and saves/restores what is under the mouse cursor by reading it back. To keep
// that exact, every Glide colour buffer has a logical-resolution shadow (like Voodoo memory):
//  * writes land in the shadow; newly written pixels are composited into the high-resolution
//    buffer before the next draw / clear / swap (only their bounding box is uploaded);
//  * reads return the shadow where the game itself wrote the pixels, and a downscaled copy of
//    the high-resolution buffer elsewhere (3D drawn with triangles).
#include "scgt.hxx"
#include "stb_image_write.h"
#include <algorithm>

namespace scgt
{
    int BufferIndex(int buffer); // device.cxx: which of the two flipping colour buffers

    static const u16 SENTINEL16 = 0x0821;
    static const u32 SENTINEL32 = 0x01020304;

    struct Shadow
    {
        std::vector<u32> pix;      // ARGB, logical size
        std::vector<u8> mask;      // 1 = pixel defined by a frame-buffer write
        std::vector<u8> pending;   // 1 = written but not yet composited into the render target
        std::vector<u32> pixGen;   // when each pixel was last written by the game
        std::vector<u32> tileGen;  // when triangles / clears last touched each 16x16 tile
        int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; // pending bounding box (inclusive)
    };
    static const int TILE = 16;
    static u32 gen = 1;
    static int tilesX = 0, tilesY = 0;
    static Shadow shadows[2];

    static std::vector<u8> lockBuf;
    static u32 lockStride = 0;
    static int lockMode = -1;
    static int lockOrigin = 0;
    static bool writeLocked[3] = {};
    static IDirect3DTexture9* lfbTex = nullptr;
    static int lfbTexW = 0, lfbTexH = 0;
    static bool anyPending = false;

    static void EnsureShadows()
    {
        const size_t n = (size_t)out.logicalW * out.logicalH;
        for (auto& s : shadows)
            if (s.pix.size() != n)
            {
                s.pix.assign(n, 0);
                s.mask.assign(n, 0);
                s.pending.assign(n, 0);
                s.x0 = s.y0 = 1 << 30; s.x1 = s.y1 = -1;
                tilesX = (out.logicalW + TILE - 1) / TILE;
                tilesY = (out.logicalH + TILE - 1) / TILE;
                s.pixGen.assign(n, 0);
                s.tileGen.assign((size_t)tilesX * tilesY, 0);
            }
    }

    static inline bool Exact(const Shadow& s, size_t i, int W)
    {
        const int x = (int)(i % W), y = (int)(i / W);
        return s.mask[i] && s.pixGen[i] > s.tileGen[(size_t)(y / TILE) * tilesX + x / TILE];
    }

    static u32 ToArgb(u32 px, int mode, bool* written)
    {
        u32 r, g, b;
        switch (mode)
        {
        case GR_LFBWRITEMODE_555:
        case GR_LFBWRITEMODE_1555:
            *written = (u16)px != SENTINEL16;
            r = (px >> 10) & 31; g = (px >> 5) & 31; b = px & 31;
            r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2;
            break;
        case GR_LFBWRITEMODE_888:
        case GR_LFBWRITEMODE_8888:
            *written = px != SENTINEL32;
            r = (px >> 16) & 255; g = (px >> 8) & 255; b = px & 255;
            break;
        default: // 565
            *written = (u16)px != SENTINEL16;
            r = (px >> 11) & 31; g = (px >> 5) & 63; b = px & 31;
            r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2;
            break;
        }
        return 0xff000000u | r << 16 | g << 8 | b;
    }

    static inline u16 To565(u32 c) { return (u16)(((c >> 19) & 31) << 11 | ((c >> 10) & 63) << 5 | ((c >> 3) & 31)); }
    static int Bytes(int mode) { return (mode == GR_LFBWRITEMODE_888 || mode == GR_LFBWRITEMODE_8888) ? 4 : 2; }

    static bool EnsureTexture()
    {
        if (lfbTex && lfbTexW >= out.logicalW && lfbTexH >= out.logicalH) return true;
        if (lfbTex) { lfbTex->Release(); lfbTex = nullptr; }
        int tw = 64, th = 64;
        while (tw < out.logicalW) tw *= 2;
        while (th < out.logicalH) th *= 2;
        if (FAILED(dev->CreateTexture(tw, th, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &lfbTex, nullptr))) return false;
        lfbTexW = tw;
        lfbTexH = th;
        return true;
    }

    // Merge w x h pixels written by the game (rows of `stride` bytes) at logical (x, y) into a buffer's shadow.
    static void Merge(int buffer, int x, int y, int w, int h, const u8* src, u32 stride, int mode, bool flip)
    {
        EnsureShadows();
        Shadow& s = shadows[BufferIndex(buffer)];
        const int W = out.logicalW, H = out.logicalH;
        const int bpp = Bytes(mode);
        int count = 0;
        const u32 g = ++gen;
        for (int row = 0; row < h; row++)
        {
            const int ty = y + row;
            if (ty < 0 || ty >= H) continue;
            const u8* line = src + (size_t)(flip ? (h - 1 - row) : row) * stride;
            for (int col = 0; col < w; col++)
            {
                const int tx = x + col;
                if (tx < 0 || tx >= W) continue;
                const u32 px = bpp == 4 ? ((const u32*)line)[col] : ((const u16*)line)[col];
                bool written;
                const u32 c = ToArgb(px, mode, &written);
                if (!written) continue;
                const size_t i = (size_t)ty * W + tx;
                s.pix[i] = c;
                s.mask[i] = 1;
                s.pending[i] = 1;
                s.pixGen[i] = g;
                if (tx < s.x0) s.x0 = tx;
                if (tx > s.x1) s.x1 = tx;
                if (ty < s.y0) s.y0 = ty;
                if (ty > s.y1) s.y1 = ty;
                count++;
            }
        }
        if (count)
        {
            anyPending = true;
            InvalidateState(); // the next draw composites these pixels first
        }
        if (cfg.Trace >= 3)
        {
            u32 sr = 0, sg = 0, sb = 0; int n = 0, minx = 9999, miny = 9999;
            for (int row = 0; row < h; row++) for (int col = 0; col < w; col++)
            {
                const u8* line = src + (size_t)(flip ? (h - 1 - row) : row) * stride;
                const u32 px = bpp == 4 ? ((const u32*)line)[col] : ((const u16*)line)[col];
                bool wr; const u32 c = ToArgb(px, mode, &wr);
                if (wr) { sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255; n++; if (x + col < minx) minx = x + col; if (y + row < miny) miny = y + row; }
            }
            if (n && n < 5000) Log("  lfb write buffer %d (rt %d): %d px at %d,%d avg %u,%u,%u", buffer, BufferIndex(buffer), count, minx, miny, sr / n, sg / n, sb / n);
            else Log("  lfb write buffer %d (rt %d): %d px", buffer, BufferIndex(buffer), count);
        }
    }

    // Composite pending pixels of one buffer into its render target.
    static void FlushBuffer(int buffer)
    {
        Shadow& s = shadows[BufferIndex(buffer)];
        if (s.x1 < 0 || s.pix.empty()) return;
        if (!EnsureTexture()) return;
        const int W = out.logicalW, H = out.logicalH;
        // one texel of margin so filtering at the edges sees the real neighbours
        const int x0 = s.x0 > 0 ? s.x0 - 1 : 0, y0 = s.y0 > 0 ? s.y0 - 1 : 0;
        const int x1 = s.x1 < W - 1 ? s.x1 + 1 : W - 1, y1 = s.y1 < H - 1 ? s.y1 + 1 : H - 1;
        RECT r = { x0, y0, x1 + 1, y1 + 1 };
        D3DLOCKED_RECT lr;
        bool all = true;
        if (SUCCEEDED(lfbTex->LockRect(0, &lr, &r, 0)))
        {
            for (int y = y0; y <= y1; y++)
            {
                u32* d = (u32*)((u8*)lr.pBits + (size_t)(y - y0) * lr.Pitch);
                const size_t base = (size_t)y * W;
                for (int x = x0; x <= x1; x++)
                {
                    const bool p = s.pending[base + x] != 0;
                    d[x - x0] = (s.pix[base + x] & 0xffffff) | (p ? 0xff000000u : 0);
                    if (!p && x >= s.x0 && x <= s.x1 && y >= s.y0 && y <= s.y1) all = false;
                    s.pending[base + x] = 0;
                }
            }
            lfbTex->UnlockRect(0);
            const float u0 = (float)s.x0 / lfbTexW, v0 = (float)s.y0 / lfbTexH;
            const float u1 = (float)(s.x1 + 1) / lfbTexW, v1 = (float)(s.y1 + 1) / lfbTexH;
            DrawTexture(lfbTex, buffer, (float)s.x0, (float)s.y0, (float)(s.x1 - s.x0 + 1), (float)(s.y1 - s.y0 + 1), u0, v0, u1, v1, !all);
        }
        s.x0 = s.y0 = 1 << 30;
        s.x1 = s.y1 = -1;
    }

    void LfbFlush()
    {
        if (!anyPending || !dev) return;
        anyPending = false;
        FlushBuffer(GR_BUFFER_BACKBUFFER);
        FlushBuffer(GR_BUFFER_FRONTBUFFER);
    }

    // Triangles cover this logical rectangle: the render target is now newer there.
    void LfbNoteDraw(int buffer, float fx0, float fy0, float fx1, float fy1)
    {
        if (shadows[0].pix.empty()) return;
        Shadow& s = shadows[BufferIndex(buffer)];
        int tx0 = (int)fx0 / TILE, ty0 = (int)fy0 / TILE, tx1 = (int)fx1 / TILE, ty1 = (int)fy1 / TILE;
        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        if (tx1 >= tilesX) tx1 = tilesX - 1;
        if (ty1 >= tilesY) ty1 = tilesY - 1;
        const u32 g = ++gen;
        for (int ty = ty0; ty <= ty1; ty++)
            for (int tx = tx0; tx <= tx1; tx++) s.tileGen[(size_t)ty * tilesX + tx] = g;
    }

    void LfbNoteClear(int buffer, int x0, int y0, int x1, int y1)
    {
        EnsureShadows();
        Shadow& s = shadows[BufferIndex(buffer)];
        const int W = out.logicalW, H = out.logicalH;
        for (int y = y0 < 0 ? 0 : y0; y < y1 && y < H; y++)
            for (int x = x0 < 0 ? 0 : x0; x < x1 && x < W; x++) s.pending[(size_t)y * W + x] = 0;
        LfbNoteDraw(buffer, (float)x0, (float)y0, (float)(x1 - 1), (float)(y1 - 1));
    }

    // Logical-resolution ARGB content of a buffer, as the game would read it.
    static bool Snapshot(int buffer, std::vector<u32>& px)
    {
        if (cfg.Trace >= 3) Log("  lfb read buffer %d (rt %d)", buffer, BufferIndex(buffer));
        EnsureShadows();
        const Shadow& s = shadows[BufferIndex(buffer)];
        const size_t n = s.pix.size();
        const int W = out.logicalW;
        bool needRT = false;
        for (size_t i = 0; i < n; i++) if (!Exact(s, i, W)) { needRT = true; break; }
        if (!needRT)
        {
            px = s.pix;
            return true;
        }
        Flush();
        LfbFlush();
        if (!ReadBuffer(buffer, px)) return false;
        for (size_t i = 0; i < n; i++) if (Exact(s, i, W)) px[i] = s.pix[i]; // exact where the game wrote last
        return true;
    }

    FxBool LfbLock(int type, int buffer, int writeMode, int org, int pipeline, GrLfbInfo_t* info)
    {
        if (!info || !dev) return FALSE;
        EnsureShadows();
        const int w = out.logicalW, h = out.logicalH;
        const bool write = (type & 1) != 0;
        int mode = writeMode;
        if (!write || mode == GR_LFBWRITEMODE_ANY) mode = GR_LFBWRITEMODE_565;
        const int bpp = Bytes(mode);
        lockStride = (u32)(w <= 1024 ? 1024 : w) * bpp;
        lockBuf.resize((size_t)lockStride * (h + 1));

        if (write)
        {
            for (int y = 0; y < h; y++)
            {
                u8* row = &lockBuf[(size_t)y * lockStride];
                if (bpp == 2) { u16* p = (u16*)row; for (int x = 0; x < w; x++) p[x] = SENTINEL16; }
                else { u32* p = (u32*)row; for (int x = 0; x < w; x++) p[x] = SENTINEL32; }
            }
            lockMode = mode;
            lockOrigin = org;
            if (buffer >= 0 && buffer < 3) writeLocked[buffer] = true;
        }
        else
        {
            std::vector<u32> px;
            if (buffer != GR_BUFFER_AUXBUFFER && Snapshot(buffer, px))
            {
                for (int y = 0; y < h; y++)
                {
                    const int sy = org == GR_ORIGIN_LOWER_LEFT ? h - 1 - y : y;
                    u16* d = (u16*)&lockBuf[(size_t)y * lockStride];
                    for (int x = 0; x < w; x++) d[x] = To565(px[(size_t)sy * w + x]);
                }
            }
            else
            {
                memset(lockBuf.data(), buffer == GR_BUFFER_AUXBUFFER ? 0xff : 0, lockBuf.size());
            }
        }
        info->lfbPtr = lockBuf.data();
        info->strideInBytes = lockStride;
        info->writeMode = mode;
        info->origin = org;
        return TRUE;
    }

    void PresentFrontNow(); // device.cxx

    static void AfterWrite(int buffer)
    {
        if (buffer == GR_BUFFER_FRONTBUFFER && anyPending)
        {
            Flush();
            LfbFlush();
            PresentFrontNow();
        }
    }

    FxBool LfbUnlock(int type, int buffer)
    {
        if (!dev) return FALSE;
        if ((type & 1) && buffer >= 0 && buffer < 3 && writeLocked[buffer])
        {
            writeLocked[buffer] = false;
            if (buffer != GR_BUFFER_AUXBUFFER)
            {
                Merge(buffer, 0, 0, out.logicalW, out.logicalH, lockBuf.data(), lockStride, lockMode, lockOrigin == GR_ORIGIN_LOWER_LEFT);
                AfterWrite(buffer);
            }
        }
        return TRUE;
    }

    FxBool LfbWriteRegion(int buffer, int x, int y, int srcFormat, int w, int h, int stride, const void* data)
    {
        if (!data || buffer == GR_BUFFER_AUXBUFFER || !dev || w <= 0 || h <= 0) return TRUE;
        int mode = GR_LFBWRITEMODE_565;
        switch (srcFormat)
        {
        case 1: mode = GR_LFBWRITEMODE_555; break;
        case 2: mode = GR_LFBWRITEMODE_1555; break;
        case 4: mode = GR_LFBWRITEMODE_888; break;
        case 5: mode = GR_LFBWRITEMODE_8888; break;
        default: break;
        }
        // every pixel of a region write counts: make sure none equals the sentinel
        const int bpp = Bytes(mode);
        std::vector<u8> tmp((size_t)w * h * bpp);
        for (int row = 0; row < h; row++) memcpy(&tmp[(size_t)row * w * bpp], (const u8*)data + (size_t)row * stride, (size_t)w * bpp);
        if (bpp == 2) { u16* p = (u16*)tmp.data(); for (size_t i = 0; i < (size_t)w * h; i++) if (p[i] == SENTINEL16) p[i] ^= 1; }
        else { u32* p = (u32*)tmp.data(); for (size_t i = 0; i < (size_t)w * h; i++) if (p[i] == SENTINEL32) p[i] ^= 1; }
        Merge(buffer, x, y, w, h, tmp.data(), (u32)(w * bpp), mode, false);
        AfterWrite(buffer);
        return TRUE;
    }

    FxBool LfbReadRegion(int buffer, int x, int y, int w, int h, int stride, void* data)
    {
        if (!data) return FALSE;
        std::vector<u32> px;
        const bool ok = buffer != GR_BUFFER_AUXBUFFER && Snapshot(buffer, px);
        for (int row = 0; row < h; row++)
        {
            u16* d = (u16*)((u8*)data + (size_t)row * stride);
            for (int col = 0; col < w; col++)
            {
                const int sx = x + col, sy = y + row;
                u32 c = 0;
                if (ok && sx >= 0 && sy >= 0 && sx < out.logicalW && sy < out.logicalH) c = px[(size_t)sy * out.logicalW + sx];
                d[col] = To565(c);
            }
        }
        return TRUE;
    }

    // debug: save what the game would read from the back buffer as PNG
    void DebugSnapshot(u32 frame)
    {
        std::vector<u32> px;
        if (Snapshot(GR_BUFFER_BACKBUFFER, px))
        {
            std::vector<u8> rgb(px.size() * 3);
            for (size_t i = 0; i < px.size(); i++) { rgb[i * 3] = (px[i] >> 16) & 255; rgb[i * 3 + 1] = (px[i] >> 8) & 255; rgb[i * 3 + 2] = px[i] & 255; }
            char name[64];
            sprintf(name, "snap_%05u.png", frame);
            stbi_write_png((gameDir + name).c_str(), out.logicalW, out.logicalH, 3, rgb.data(), out.logicalW * 3);
        }
    }

    void LfbRelease()
    {
        if (lfbTex) { lfbTex->Release(); lfbTex = nullptr; }
        lfbTexW = lfbTexH = 0;
        for (auto& s : shadows) { s.pix.clear(); s.mask.clear(); s.pending.clear(); s.pixGen.clear(); s.tileGen.clear(); }
        anyPending = false;
    }
}
