// Reading ISI .MAS archives (format: tools/mas.py).
#include "scgt.hxx"
#include "stb_image.h"
#include <string.h>

namespace scgt
{
    bool MasRead(const std::string& path, const char* name, std::vector<u8>& data)
    {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) return false;
        u32 head[2];
        bool ok = false;
        if (fread(head, 4, 2, f) == 2 && head[0] < 100000)
        {
            std::vector<u8> dir((size_t)head[0] * 32);
            if (fread(dir.data(), 1, dir.size(), f) == dir.size())
            {
                const long base = 8 + (long)dir.size();
                for (u32 i = 0; i < head[0] && !ok; i++)
                {
                    const u8* e = &dir[(size_t)i * 32];
                    if (_strnicmp((const char*)e, name, 16) != 0) continue;
                    // stored length: distance to the next entry's offset (or the end of the data)
                    const u32 off = *(const u32*)(e + 16);
                    u32 end = head[1];
                    for (u32 j = 0; j < head[0]; j++)
                    {
                        const u32 o = *(const u32*)(&dir[(size_t)j * 32] + 16);
                        if (o > off && o < end) end = o;
                    }
                    data.resize(end - off);
                    ok = fseek(f, base + (long)off, SEEK_SET) == 0 && fread(data.data(), 1, data.size(), f) == data.size();
                }
            }
        }
        fclose(f);
        return ok;
    }

    bool MasImage(const std::string& path, const char* name, int* w, int* h, std::vector<u8>& bgr)
    {
        std::vector<u8> d;
        if (!MasRead(path, name, d) || d.size() < 16) return false;
        const u32* hd = (const u32*)d.data();
        *w = (int)hd[0];
        *h = (int)hd[1];
        const size_t n = (size_t)hd[0] * hd[1] * 3;
        if (hd[3] != 0 || n == 0 || n > (64u << 20)) return false; // 24-bit images only
        if (hd[2])
        {
            int len = 0;
            char* raw = stbi_zlib_decode_malloc_guesssize_headerflag((const char*)d.data() + 16, (int)d.size() - 16, (int)n, &len, 1);
            if (!raw) return false;
            const bool good = (size_t)len >= n;
            if (good) bgr.assign((u8*)raw, (u8*)raw + n);
            free(raw);
            return good;
        }
        if (d.size() < 16 + n) return false;
        bgr.assign(d.begin() + 16, d.begin() + 16 + n);
        return true;
    }
}
