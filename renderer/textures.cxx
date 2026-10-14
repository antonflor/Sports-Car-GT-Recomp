// Texture memory emulation, format conversion, texture cache, HD replacement and dumping.
//
// The game manages TMU memory itself (addresses from grTexMinAddress..grTexMaxAddress), so
// downloads are written into an emulated TMU RAM. A texture used by grTexSource is looked up
// by (address, format, LODs, aspect) and, on a miss, by a hash of its bytes, so re-downloads
// of the same image never create a new Direct3D texture.
#include "scgt.hxx"
#include <unordered_map>
#include <list>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <string.h>
#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include "stb_dxt.h"

namespace scgt
{
    static std::vector<u8> ram;
    static u32 palette[256];
    static u32 paletteGen = 1;

    struct Entry
    {
        BoundTexture bt;
        u64 contentKey = 0;
        u32 lastUse = 0;
    };

    struct Slot
    {
        u32 start = 0, end = 0;
        Entry* entry = nullptr;
        u32 paletteGen = 0;
    };

    static std::unordered_map<u64, Entry*> contentCache;
    static std::unordered_map<u64, Slot> slots;
    static u32 useClock = 0;
    static std::unordered_map<u32, bool> hdMissing;

    void TexLodSize(int lod, int aspect, int* w, int* h)
    {
        const int s = 256 >> lod;
        int ww = s, hh = s;
        if (aspect < GR_ASPECT_1x1) hh = s >> (GR_ASPECT_1x1 - aspect);
        else if (aspect > GR_ASPECT_1x1) ww = s >> (aspect - GR_ASPECT_1x1);
        *w = ww < 1 ? 1 : ww;
        *h = hh < 1 ? 1 : hh;
    }

    static int Bpp(int format) { return format >= GR_TEXFMT_ARGB_8332 ? 2 : 1; }

    static u32 LodBytes(int lod, int aspect, int format)
    {
        int w, h;
        TexLodSize(lod, aspect, &w, &h);
        return (u32)(w * h * Bpp(format));
    }

    static u32 LodOffset(int thisLod, int largeLod, int aspect, int format)
    {
        u32 off = 0;
        for (int l = largeLod; l < thisLod; l++) off += LodBytes(l, aspect, format);
        return off;
    }

    u32 TexMemRequired(int smallLod, int largeLod, int aspect, int format)
    {
        u32 total = 0;
        for (int l = largeLod; l <= smallLod; l++) total += LodBytes(l, aspect, format);
        return (total + 7) & ~7u;
    }

    void TexInit(u32 bytes)
    {
        ram.assign(bytes + 1024 * 1024, 0); // slack for downloads that run past the end
        memset(palette, 0, sizeof(palette));
    }

    void TexShutdown()
    {
        for (auto& kv : contentCache)
        {
            if (kv.second->bt.tex) kv.second->bt.tex->Release();
            delete kv.second;
        }
        contentCache.clear();
        slots.clear();
        hdMissing.clear();
    }

    static void Invalidate(u32 start, u32 end)
    {
        for (auto it = slots.begin(); it != slots.end();)
        {
            if (it->second.start < end && start < it->second.end) it = slots.erase(it);
            else ++it;
        }
    }

    static void Write(u32 address, const void* data, u32 bytes)
    {
        if (!data || !bytes) return;
        if ((size_t)address + bytes > ram.size()) return;
        memcpy(&ram[address], data, bytes);
        Invalidate(address, address + bytes);
    }

    void TexDownload(u32 address, const GrTexInfo* info)
    {
        if (!info) return;
        u32 bytes = 0;
        for (int l = info->largeLod; l <= info->smallLod; l++) bytes += LodBytes(l, info->aspectRatio, info->format);
        Write(address, info->data, bytes);
    }

    void TexDownloadLevel(u32 address, int thisLod, int largeLod, int aspect, int format, const void* data)
    {
        Write(address + LodOffset(thisLod, largeLod, aspect, format), data, LodBytes(thisLod, aspect, format));
    }

    void TexDownloadLevelPartial(u32 address, int thisLod, int largeLod, int aspect, int format, const void* data, int start, int end)
    {
        int w, h;
        TexLodSize(thisLod, aspect, &w, &h);
        const u32 row = (u32)(w * Bpp(format));
        if (end < start) return;
        Write(address + LodOffset(thisLod, largeLod, aspect, format) + row * start, data, row * (end - start + 1));
    }

    void TexDownloadPalette(const u32* pal, int start, int end)
    {
        for (int i = start; i <= end && i < 256; i++) palette[i] = pal[i - start] & 0xffffff;
        paletteGen++;
    }

    // ---- conversion ------------------------------------------------------------------------
    static inline u32 Expand5(u32 v) { return (v << 3) | (v >> 2); }
    static inline u32 Expand6(u32 v) { return (v << 2) | (v >> 4); }
    static inline u32 Expand4(u32 v) { return (v << 4) | v; }

    static void ConvertLod(const u8* src, int w, int h, int format, u32* dst)
    {
        const int n = w * h;
        const u16* s16 = (const u16*)src;
        for (int i = 0; i < n; i++)
        {
            u32 a = 255, r = 0, g = 0, b = 0;
            switch (format)
            {
            case GR_TEXFMT_RGB_565: { u32 p = s16[i]; r = Expand5(p >> 11); g = Expand6((p >> 5) & 63); b = Expand5(p & 31); break; }
            case GR_TEXFMT_ARGB_1555: { u32 p = s16[i]; a = (p & 0x8000) ? 255 : 0; r = Expand5((p >> 10) & 31); g = Expand5((p >> 5) & 31); b = Expand5(p & 31); break; }
            case GR_TEXFMT_ARGB_4444: { u32 p = s16[i]; a = Expand4(p >> 12); r = Expand4((p >> 8) & 15); g = Expand4((p >> 4) & 15); b = Expand4(p & 15); break; }
            case GR_TEXFMT_ARGB_8332: { u32 p = s16[i]; a = p >> 8; r = ((p >> 5) & 7) * 255 / 7; g = ((p >> 2) & 7) * 255 / 7; b = (p & 3) * 85; break; }
            case GR_TEXFMT_ALPHA_INTENSITY_88: { u32 p = s16[i]; a = p >> 8; r = g = b = p & 255; break; }
            case GR_TEXFMT_AP_88: { u32 p = s16[i]; a = p >> 8; u32 c = palette[p & 255]; r = (c >> 16) & 255; g = (c >> 8) & 255; b = c & 255; break; }
            case GR_TEXFMT_RGB_332: { u32 p = src[i]; r = ((p >> 5) & 7) * 255 / 7; g = ((p >> 2) & 7) * 255 / 7; b = (p & 3) * 85; break; }
            case GR_TEXFMT_ALPHA_8: { a = src[i]; r = g = b = 255; break; }
            case GR_TEXFMT_INTENSITY_8: { r = g = b = src[i]; break; }
            case GR_TEXFMT_ALPHA_INTENSITY_44: { u32 p = src[i]; a = Expand4(p >> 4); r = g = b = Expand4(p & 15); break; }
            case GR_TEXFMT_P_8: { u32 c = palette[src[i]]; r = (c >> 16) & 255; g = (c >> 8) & 255; b = c & 255; break; }
            default: { r = g = b = src[i]; break; }
            }
            dst[i] = a << 24 | r << 16 | g << 8 | b;
        }
    }

    static bool FormatHasAlpha(int f)
    {
        switch (f)
        {
        case GR_TEXFMT_ARGB_1555: case GR_TEXFMT_ARGB_4444: case GR_TEXFMT_ARGB_8332: case GR_TEXFMT_ALPHA_INTENSITY_88:
        case GR_TEXFMT_AP_88: case GR_TEXFMT_ALPHA_8: case GR_TEXFMT_ALPHA_INTENSITY_44:
            return true;
        default:
            return false;
        }
    }

    // The game keys transparency on black (grChromakeyValue 0): put a mask of the black texels
    // in alpha (rgb untouched). With the key enabled the shader uses it for clean filtered edges;
    // with it disabled the texture looks exactly like the original.
    static bool KeyMask(u32* p, int w, int h)
    {
        bool any = false;
        for (int i = 0; i < w * h; i++)
        {
            if ((p[i] & 0xffffff) == 0) { p[i] = 0; any = true; }
            else p[i] |= 0xff000000;
        }
        return any;
    }

    static void BoxDown(const u32* src, int w, int h, u32* dst, int dw, int dh)
    {
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++)
            {
                u32 acc[4] = {}, n = 0, wa = 0;
                for (int yy = y * h / dh; yy < (y + 1) * h / dh || yy == y * h / dh; yy++)
                    for (int xx = x * w / dw; xx < (x + 1) * w / dw || xx == x * w / dw; xx++)
                    {
                        const u32 c = src[yy * w + xx];
                        const u32 a = c >> 24;
                        acc[0] += a;
                        acc[1] += ((c >> 16) & 255) * (a + 1);
                        acc[2] += ((c >> 8) & 255) * (a + 1);
                        acc[3] += (c & 255) * (a + 1);
                        wa += a + 1;
                        n++;
                    }
                dst[y * dw + x] = (acc[0] / n) << 24 | (acc[1] / wa) << 16 | (acc[2] / wa) << 8 | (acc[3] / wa);
            }
    }

    static IDirect3DTexture9* Upload(const std::vector<std::vector<u32>>& levels, const std::vector<std::pair<int, int>>& dims)
    {
        IDirect3DTexture9* t = nullptr;
        if (FAILED(dev->CreateTexture(dims[0].first, dims[0].second, (UINT)levels.size(), 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr)))
            return nullptr;
        for (size_t l = 0; l < levels.size(); l++)
        {
            D3DLOCKED_RECT lr;
            if (FAILED(t->LockRect((UINT)l, &lr, nullptr, 0))) continue;
            const int w = dims[l].first, h = dims[l].second;
            for (int y = 0; y < h; y++) memcpy((u8*)lr.pBits + (size_t)y * lr.Pitch, &levels[l][(size_t)y * w], w * 4);
            t->UnlockRect((UINT)l);
        }
        return t;
    }

    // HD textures are block-compressed (DXT1 opaque, DXT5 with transparency): 4-8x less memory,
    // which matters in a 32-bit game holding a whole track and grid of HD textures.
    static IDirect3DTexture9* UploadCompressed(const std::vector<std::vector<u32>>& levels, const std::vector<std::pair<int, int>>& dims, bool alpha)
    {
        if ((dims[0].first & 3) || (dims[0].second & 3)) return Upload(levels, dims);
        IDirect3DTexture9* t = nullptr;
        if (FAILED(dev->CreateTexture(dims[0].first, dims[0].second, (UINT)levels.size(), 0, alpha ? D3DFMT_DXT5 : D3DFMT_DXT1, D3DPOOL_MANAGED, &t, nullptr)))
            return Upload(levels, dims);
        const int blockBytes = alpha ? 16 : 8;
        for (size_t l = 0; l < levels.size(); l++)
        {
            D3DLOCKED_RECT lr;
            if (FAILED(t->LockRect((UINT)l, &lr, nullptr, 0))) continue;
            const int w = dims[l].first, h = dims[l].second;
            const u32* src = levels[l].data();
            u8 block[64];
            for (int by = 0; by < h; by += 4)
            {
                u8* row = (u8*)lr.pBits + (size_t)(by / 4) * lr.Pitch;
                for (int bx = 0; bx < w; bx += 4)
                {
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                        {
                            // small mip levels: repeat the edge texels to fill the 4x4 block
                            const int sx = bx + x < w ? bx + x : w - 1, sy = by + y < h ? by + y : h - 1;
                            const u32 c = src[(size_t)sy * w + sx];
                            u8* d = &block[(y * 4 + x) * 4];
                            d[0] = (c >> 16) & 255; d[1] = (c >> 8) & 255; d[2] = c & 255; d[3] = c >> 24;
                        }
                    stb_compress_dxt_block(row + (size_t)(bx / 4) * blockBytes, block, alpha ? 1 : 0, STB_DXT_NORMAL);
                }
            }
            t->UnlockRect((UINT)l);
        }
        return t;
    }

    static void FullChain(std::vector<std::vector<u32>>& levels, std::vector<std::pair<int, int>>& dims)
    {
        while (dims.back().first > 1 || dims.back().second > 1)
        {
            const int w = dims.back().first, h = dims.back().second;
            const int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
            std::vector<u32> next((size_t)nw * nh);
            BoxDown(levels.back().data(), w, h, next.data(), nw, nh);
            levels.push_back(std::move(next));
            dims.push_back({ nw, nh });
        }
    }

    static bool LoadReplacement(u32 crc, Entry* e)
    {
        if (!cfg.ReplaceTextures) return false;
        if (hdMissing.count(crc)) return false;
        char name[64];
        sprintf(name, "textures\\%08x.png", crc);
        const std::string path = gameDir + name;
        int w = 0, h = 0, n = 0;
        stbi_uc* px = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!px)
        {
            hdMissing[crc] = true;
            if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) Log("hd texture %08x: can't load %s", crc, stbi_failure_reason());
            return false;
        }
        std::vector<std::vector<u32>> levels(1, std::vector<u32>((size_t)w * h));
        bool alpha = false;
        for (int i = 0; i < w * h; i++)
        {
            const u8* q = px + i * 4;
            levels[0][i] = (u32)q[3] << 24 | (u32)q[0] << 16 | (u32)q[1] << 8 | q[2];
            if (q[3] != 255) alpha = true;
        }
        stbi_image_free(px);
        if (!alpha)
        {
            // HD sources use magenta for transparency; the game keys black. Pure black is
            // nudged off the key so only the intended texels become transparent.
            for (auto& c : levels[0])
            {
                if ((c & 0xffffff) == 0xff00ff) c = 0xff000000;
                else if ((c & 0xffffff) == 0) c = 0xff010101;
            }
            KeyMask(levels[0].data(), w, h);
        }
        bool translucent = false; // any texel with alpha below 255 (cut-outs included)
        for (u32 c : levels[0]) if ((c >> 24) != 255) { translucent = true; break; }
        std::vector<std::pair<int, int>> dims(1, { w, h });
        FullChain(levels, dims);
        IDirect3DTexture9* t = cfg.CompressHD ? UploadCompressed(levels, dims, translucent) : Upload(levels, dims);
        if (!t)
        {
            Log("hd texture %08x: can't create %dx%d", crc, w, h);
            hdMissing[crc] = true;
            return false;
        }
        if (e->bt.tex) e->bt.tex->Release();
        e->bt.tex = t;
        e->bt.realAlpha = alpha;
        e->bt.keyMask = !alpha;
        Log("hd texture %08x %dx%d", crc, w, h);
        return true;
    }

    static void Dump(u32 crc, const u32* p, int w, int h)
    {
        static bool made = false;
        const std::string dir = gameDir + "textures_dump\\";
        if (!made) { CreateDirectoryA(dir.c_str(), nullptr); made = true; }
        char name[32];
        sprintf(name, "%08x.png", crc);
        const std::string path = dir + name;
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
        std::vector<u8> rgba((size_t)w * h * 4);
        for (int i = 0; i < w * h; i++)
        {
            rgba[i * 4 + 0] = (p[i] >> 16) & 255;
            rgba[i * 4 + 1] = (p[i] >> 8) & 255;
            rgba[i * 4 + 2] = p[i] & 255;
            rgba[i * 4 + 3] = p[i] >> 24;
        }
        stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4);
    }

    static Entry* Create(u32 address, const GrTexInfo& info, u64 contentKey)
    {
        Entry* e = new Entry();
        e->contentKey = contentKey;
        int w0, h0;
        TexLodSize(info.largeLod, info.aspectRatio, &w0, &h0);
        const int maxd = w0 > h0 ? w0 : h0;
        e->bt.uScale = (float)maxd / (256.0f * w0);
        e->bt.vScale = (float)maxd / (256.0f * h0);

        // identify by the bytes of the largest LOD, as the game uploads it
        const u32 crc = Crc32(&ram[address], LodBytes(info.largeLod, info.aspectRatio, info.format));

        std::vector<std::vector<u32>> levels;
        std::vector<std::pair<int, int>> dims;
        u32 off = address;
        bool alphaUsed = false;
        for (int l = info.largeLod; l <= info.smallLod; l++)
        {
            int w, h;
            TexLodSize(l, info.aspectRatio, &w, &h);
            std::vector<u32> px((size_t)w * h);
            ConvertLod(&ram[off], w, h, info.format, px.data());
            off += LodBytes(l, info.aspectRatio, info.format);
            if (FormatHasAlpha(info.format))
                for (u32 c : px) if ((c >> 24) != 255) { alphaUsed = true; break; }
            levels.push_back(std::move(px));
            dims.push_back({ w, h });
        }
        if (cfg.DumpTextures) Dump(crc, levels[0].data(), dims[0].first, dims[0].second);

        if (!LoadReplacement(crc, e))
        {
            e->bt.realAlpha = alphaUsed;
            e->bt.keyMask = !alphaUsed;
            if (!alphaUsed)
                for (size_t l = 0; l < levels.size(); l++) KeyMask(levels[l].data(), dims[l].first, dims[l].second);
            // give the GPU a complete chain so trilinear/anisotropic filtering works at any distance
            if (info.smallLod > info.largeLod || cfg.Trilinear) FullChain(levels, dims);
            e->bt.tex = Upload(levels, dims);
        }
        return e;
    }

    const BoundTexture* TexBind()
    {
        const GrTexInfo& info = st.texInfo;
        if (info.largeLod < 0 || info.largeLod > 8 || info.smallLod < info.largeLod || info.smallLod > 8 || info.aspectRatio < 0 || info.aspectRatio > 6)
            return nullptr;
        const u32 address = st.texAddress;
        u32 bytes = 0;
        for (int l = info.largeLod; l <= info.smallLod; l++) bytes += LodBytes(l, info.aspectRatio, info.format);
        if ((size_t)address + bytes > ram.size()) return nullptr;

        const bool paletted = info.format == GR_TEXFMT_P_8 || info.format == GR_TEXFMT_AP_88;
        const u64 slotKey = (u64)address << 24 | (u64)(info.format & 15) << 16 | (u64)(info.smallLod & 15) << 12 | (u64)(info.largeLod & 15) << 8 | (u64)(info.aspectRatio & 15);
        auto it = slots.find(slotKey);
        if (it != slots.end() && (!paletted || it->second.paletteGen == paletteGen))
        {
            it->second.entry->lastUse = ++useClock;
            return &it->second.entry->bt;
        }

        u32 h = Crc32(&ram[address], bytes);
        if (paletted) h = Crc32(palette, sizeof(palette), h);
        const u64 contentKey = (u64)h << 32 | (u64)(info.format & 15) << 16 | (u64)(info.smallLod & 15) << 12 | (u64)(info.largeLod & 15) << 8 | (u64)(info.aspectRatio & 15);
        Entry* e;
        auto ct = contentCache.find(contentKey);
        if (ct != contentCache.end()) e = ct->second;
        else
        {
            e = Create(address, info, contentKey);
            contentCache[contentKey] = e;
            // keep the cache bounded
            if (contentCache.size() > 6000)
            {
                std::vector<std::pair<u32, u64>> ages;
                for (auto& kv : contentCache) ages.push_back({ kv.second->lastUse, kv.first });
                std::sort(ages.begin(), ages.end());
                for (size_t i = 0; i < ages.size() / 4; i++)
                {
                    Entry* old = contentCache[ages[i].second];
                    for (auto s = slots.begin(); s != slots.end();) { if (s->second.entry == old) s = slots.erase(s); else ++s; }
                    if (old->bt.tex) old->bt.tex->Release();
                    delete old;
                    contentCache.erase(ages[i].second);
                }
            }
        }
        Slot s;
        s.start = address;
        s.end = address + bytes;
        s.entry = e;
        s.paletteGen = paletteGen;
        slots[slotKey] = s;
        e->lastUse = ++useClock;
        return &e->bt;
    }
}
