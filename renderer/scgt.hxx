// scgt2026 — Glide 2 renderer for Sports Car GT on Direct3D 9.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "glide.hxx"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;

namespace scgt
{
    // ---- settings (scgt2026.ini, section [scgt2026]) ---------------------------------
    struct Config
    {
        int Width = 0, Height = 0;     // output size, 0 = monitor
        int Window = 2;                // 0 fullscreen, 1 window, 2 borderless
        int Widescreen = 1;            // Hor+ 3D view
        int HudToEdges = 1;
        int VSync = 1;
        int Anisotropy = 16;
        int MSAA = 4;
        int Filter2D = 1;              // smooth scaling of framebuffer writes (movies, menus)
        int ReplaceTextures = 1;
        int CompressHD = 1;            // store HD textures DXT-compressed (much less memory)
        int DumpTextures = 0;
        int Trace = 0;
        int TexMemMB = 4;              // reported texture memory per TMU
        int Trilinear = 1;             // upgrade mip filtering
        int Gamma = 0;                 // honour grGammaCorrectionValue
        int TraceFrom = 0;             // first frame of the detailed call trace (Trace=2)
        float LodBias = 0.0f;          // added to the game's LOD bias
    };
    extern Config cfg;
    extern std::string gameDir;        // with trailing backslash

    void LoadConfig();
    void Log(const char* fmt, ...);

    // ---- render state ------------------------------------------------------------------
    struct Combine
    {
        int function = GR_COMBINE_FUNCTION_LOCAL, factor = 0, local = 0, other = 0, invert = 0;
    };

    struct TexCombine
    {
        int rgbFunction = GR_COMBINE_FUNCTION_LOCAL, rgbFactor = 0, alphaFunction = GR_COMBINE_FUNCTION_LOCAL, alphaFactor = 0;
        int rgbInvert = 0, alphaInvert = 0;
    };

    struct State
    {
        Combine color, alpha;
        TexCombine tex;
        GrColor_t constant = 0xffffffff;
        int blendRgbSrc = 4, blendRgbDst = 0, blendASrc = 4, blendADst = 0; // ONE, ZERO
        int alphaTestFunc = 7; // ALWAYS
        int alphaRef = 0;
        int depthMode = GR_DEPTHBUFFER_DISABLE;
        int depthFunc = 1; // LESS
        int depthMask = 0;
        int depthBias = 0;
        int colorMaskRgb = 1, colorMaskA = 1;
        int cull = GR_CULL_DISABLE;
        int chromaMode = 0;
        GrColor_t chromaValue = 0;
        int fogMode = GR_FOG_DISABLE;
        GrColor_t fogColor = 0;
        int clipMinX = 0, clipMinY = 0, clipMaxX = 640, clipMaxY = 480;
        int renderBuffer = GR_BUFFER_BACKBUFFER;
        // texture unit 0
        u32 texAddress = 0;
        GrTexInfo texInfo = {};
        int texValid = 0;
        int minFilter = 1, magFilter = 1;
        int clampS = 0, clampT = 0;
        int mipMode = 0, lodBlend = 0;
        float lodBias = 0.0f;
        int alphaControlsIt = 0;
    };
    extern State st;
    extern FxU8 fogTable[GR_FOG_TABLE_SIZE];
    extern int colorFormat;
    extern int origin;

    // ---- device ------------------------------------------------------------------------
    struct Output
    {
        int logicalW = 640, logicalH = 480;   // what the game thinks the screen is
        int width = 0, height = 0;            // render target / back buffer size
        float scaleX = 1, scaleY = 1;         // logical -> output, for 2D and the 3D view
        float offsetX = 0, offsetY = 0;
        float viewScaleX = 1;                 // extra horizontal scale of the 3D view (widescreen)
        float view3DScaleX = 1, view3DOffsetX = 0; // logical x -> output x for the 3D view
        bool draw3D = true;                   // current geometry belongs to the 3D view (vs 2D overlay)
    };
    extern Output out;
    extern IDirect3DDevice9* dev;
    extern HWND gameWindow;

    bool DeviceOpen(HWND hwnd, int logicalW, int logicalH);
    void DeviceClose();
    void UpdateLayout();
    void Present();
    void ClearBuffers(GrColor_t color, GrAlpha_t alpha, FxU16 depth);
    void SetRenderBuffer(int buffer);
    IDirect3DSurface9* TargetSurface(int buffer);
    void DrawFullscreenTexture(IDirect3DTexture9* tex, int buffer, float x, float y, float w, float h, bool keyed);
    void DrawTexture(IDirect3DTexture9* tex, int buffer, float x, float y, float w, float h, float u0, float v0, float u1, float v1, bool keyed, float shiftX = 0.0f);
    extern bool frameHas3D;   // the game is showing a full-screen 3D view (race), decided per frame
    extern bool cockpitView;  // a cockpit image covers the screen (dash view): its 2D stays 4:3, sides extended
    extern bool subView;      // drawing a second 3D view in this frame (rear-view mirror)
    void NoteCockpit();       // lfb.cxx: a cockpit-sized frame-buffer image was drawn
    void NoteClear();         // a buffer clear: after 3D was drawn it starts a sub-view
    void EndFrame();
    struct HudBox { float x0, y0, x1, y1; };
    extern std::vector<HudBox> hudCenter, hudCenterPrev;
    bool ReadBuffer(int buffer, std::vector<u32>& pixels); // logical-size ARGB readback
    bool RestoreSaved(int buffer, int x0, int y0, int x1, int y1);

    // ---- drawing -----------------------------------------------------------------------
    void DrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c);
    void DrawFan(int count, const GrVertex* const* verts);
    void DrawLine(const GrVertex* a, const GrVertex* b);
    void DrawPoint(const GrVertex* a);
    void Flush();
    void InvalidateState(); // state changed: flush pending geometry and re-upload constants
    void InitPipeline();
    void ReleasePipeline();

    // ---- textures ----------------------------------------------------------------------
    void TexInit(u32 bytes);
    void TexShutdown();
    u32 TexMemRequired(int smallLod, int largeLod, int aspect, int format);
    void TexDownload(u32 address, const GrTexInfo* info);
    void TexDownloadLevel(u32 address, int thisLod, int largeLod, int aspect, int format, const void* data);
    void TexDownloadLevelPartial(u32 address, int thisLod, int largeLod, int aspect, int format, const void* data, int start, int end);
    void TexDownloadPalette(const u32* palette, int start, int end);
    struct BoundTexture
    {
        IDirect3DTexture9* tex = nullptr;
        float uScale = 1, vScale = 1;  // multiply s,t (0..256) to get 0..1
        bool realAlpha = false;        // texture alpha is meaningful
        bool keyMask = false;          // alpha channel holds the colour-key mask
    };
    const BoundTexture* TexBind(); // the texture for st.texAddress / st.texInfo
    void TexLodSize(int lod, int aspect, int* w, int* h);

    // ---- frame buffer access -----------------------------------------------------------
    FxBool LfbLock(int type, int buffer, int writeMode, int origin, int pipeline, GrLfbInfo_t* info);
    FxBool LfbUnlock(int type, int buffer);
    FxBool LfbWriteRegion(int buffer, int x, int y, int srcFormat, int w, int h, int stride, const void* data);
    FxBool LfbReadRegion(int buffer, int x, int y, int w, int h, int stride, void* data);
    void LfbFlush();
    void DrawCockpitSides();
    void LfbFrameStart();
    void LfbCockpitEntered();
    void LfbNoteDraw(int buffer, float x0, float y0, float x1, float y1);
    void LfbNoteClear(int buffer, int x0, int y0, int x1, int y1);

    // ---- game patches (widescreen) -----------------------------------------------------
    void PatchGame();

    // ---- MAS archives (mas.cxx) ---------------------------------------------------------
    bool MasRead(const std::string& path, const char* name, std::vector<u8>& data);
    bool MasImage(const std::string& path, const char* name, int* w, int* h, std::vector<u8>& bgr); // 24-bit, top-down

    // helpers
    void ColorToFloat(GrColor_t c, float out[4]);
    u32 Crc32(const void* data, size_t len, u32 crc = 0);
}
