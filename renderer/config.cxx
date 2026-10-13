// Settings, logging and small helpers.
#include "scgt.hxx"
#include <stdarg.h>

namespace scgt
{
    Config cfg;
    std::string gameDir;
    static FILE* logFile = nullptr;

    static int ReadInt(const char* key, int def, const std::string& ini)
    {
        return (int)GetPrivateProfileIntA("scgt2026", key, def, ini.c_str());
    }

    static float ReadFloat(const char* key, float def, const std::string& ini)
    {
        char buf[64] = {};
        GetPrivateProfileStringA("scgt2026", key, "", buf, sizeof(buf), ini.c_str());
        return buf[0] ? (float)atof(buf) : def;
    }

    void LoadConfig()
    {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        gameDir = path;
        size_t slash = gameDir.find_last_of("\\/");
        gameDir = slash == std::string::npos ? std::string(".\\") : gameDir.substr(0, slash + 1);

        const std::string ini = gameDir + "scgt2026.ini";
        cfg.Width = ReadInt("Width", cfg.Width, ini);
        cfg.Height = ReadInt("Height", cfg.Height, ini);
        cfg.Window = ReadInt("Window", cfg.Window, ini);
        cfg.Widescreen = ReadInt("Widescreen", cfg.Widescreen, ini);
        cfg.HudToEdges = ReadInt("HudToEdges", cfg.HudToEdges, ini);
        cfg.VSync = ReadInt("VSync", cfg.VSync, ini);
        cfg.Anisotropy = ReadInt("Anisotropy", cfg.Anisotropy, ini);
        cfg.MSAA = ReadInt("MSAA", cfg.MSAA, ini);
        cfg.Filter2D = ReadInt("Filter2D", cfg.Filter2D, ini);
        cfg.ReplaceTextures = ReadInt("ReplaceTextures", cfg.ReplaceTextures, ini);
        cfg.DumpTextures = ReadInt("DumpTextures", cfg.DumpTextures, ini);
        cfg.Trace = ReadInt("Trace", cfg.Trace, ini);
        cfg.TexMemMB = ReadInt("TexMemMB", cfg.TexMemMB, ini);
        cfg.Trilinear = ReadInt("Trilinear", cfg.Trilinear, ini);
        cfg.Gamma = ReadInt("Gamma", cfg.Gamma, ini);
        cfg.LodBias = ReadFloat("LodBias", cfg.LodBias, ini);
        cfg.TraceFrom = ReadInt("TraceFrom", cfg.TraceFrom, ini);
        if (cfg.TexMemMB < 2) cfg.TexMemMB = 2;
        if (cfg.TexMemMB > 64) cfg.TexMemMB = 64;
    }

    void Log(const char* fmt, ...)
    {
        if (!cfg.Trace) return;
        if (!logFile)
        {
            logFile = fopen((gameDir + "scgt2026.log").c_str(), "w");
            if (!logFile) return;
        }
        va_list ap;
        va_start(ap, fmt);
        vfprintf(logFile, fmt, ap);
        va_end(ap);
        fputc('\n', logFile);
        fflush(logFile);
    }

    void ColorToFloat(GrColor_t c, float o[4])
    {
        u32 a, r, g, b;
        switch (colorFormat)
        {
        case GR_COLORFORMAT_ABGR: a = c >> 24; b = (c >> 16) & 255; g = (c >> 8) & 255; r = c & 255; break;
        case GR_COLORFORMAT_RGBA: r = c >> 24; g = (c >> 16) & 255; b = (c >> 8) & 255; a = c & 255; break;
        case GR_COLORFORMAT_BGRA: b = c >> 24; g = (c >> 16) & 255; r = (c >> 8) & 255; a = c & 255; break;
        default: a = c >> 24; r = (c >> 16) & 255; g = (c >> 8) & 255; b = c & 255; break;
        }
        o[0] = r / 255.0f; o[1] = g / 255.0f; o[2] = b / 255.0f; o[3] = a / 255.0f;
    }

    u32 Crc32(const void* data, size_t len, u32 crc)
    {
        static u32 table[256];
        static bool ready = false;
        if (!ready)
        {
            for (u32 i = 0; i < 256; i++)
            {
                u32 c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            ready = true;
        }
        const u8* p = (const u8*)data;
        crc = ~crc;
        for (size_t i = 0; i < len; i++) crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
        return ~crc;
    }
}
