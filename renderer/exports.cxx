// The Glide 2 API: every function Sports Car GT resolves from glide2x.dll.
#include "scgt.hxx"
#include <math.h>
#include <map>

using namespace scgt;

#define API extern "C"

namespace scgt
{
    void DebugSnapshot(u32 frame);
    void MarkFogDirty();
    void LfbRelease();
}

static bool opened = false;
static int triLogged = 0;
static u32 frameNo = 0;
static u32 texMemBytes = 4 * 1024 * 1024;
static std::map<const void*, State> savedStates;
static void (*errorCallback)(const char*, FxBool) = nullptr;

#define TRACE(...) do { if (cfg.Trace >= 2 && frameNo >= (u32)cfg.TraceFrom && frameNo < (u32)cfg.TraceFrom + 2) Log(__VA_ARGS__); } while (0)

template <typename T> static inline void Set(T& field, T value)
{
    if (field == value) return;
    Flush();
    field = value;
    InvalidateState();
}

// ---- init / hardware ---------------------------------------------------------------------
static void FillHardware(GrHwConfiguration* hw)
{
    if (!hw) return;
    memset(hw, 0, sizeof(*hw));
    hw->num_sst = 1;
    hw->SSTs[0].type = 0; // GR_SSTTYPE_VOODOO: the game only accepts Voodoo Graphics / Rush
    hw->SSTs[0].VoodooConfig.fbRam = 8; // enough frame buffer for the game to offer 1024x768
    hw->SSTs[0].VoodooConfig.fbiRev = 2;
    hw->SSTs[0].VoodooConfig.nTexelfx = 1;
    hw->SSTs[0].VoodooConfig.sliDetect = FALSE;
    hw->SSTs[0].VoodooConfig.tmuConfig[0].tmuRev = 1;
    hw->SSTs[0].VoodooConfig.tmuConfig[0].tmuRam = cfg.TexMemMB;
}

API void __stdcall grGlideInit(void)
{
    LoadConfig();
    texMemBytes = (u32)cfg.TexMemMB * 1024 * 1024;
    Log("scgt2026 grGlideInit (texture memory %u MB)", cfg.TexMemMB);
}

API void __stdcall grGlideShutdown(void)
{
    if (opened)
    {
        TexShutdown();
        LfbRelease();
        DeviceClose();
        opened = false;
    }
}

API void __stdcall grGlideGetVersion(char version[80])
{
    if (version) strcpy(version, "Glide Version 2.46 (scgt2026)");
}

API FxBool __stdcall grSstQueryBoards(GrHwConfiguration* hw) { LoadConfig(); FillHardware(hw); return TRUE; }
API FxBool __stdcall grSstQueryHardware(GrHwConfiguration* hw) { FillHardware(hw); return TRUE; }
API void __stdcall grSstSelect(int) {}
API void __stdcall grGlideShamelessPlug(const FxBool) {}
API void __stdcall grErrorSetCallback(void (*fn)(const char*, FxBool)) { errorCallback = fn; }

API FxBool __stdcall grSstWinOpen(FxU32 hWnd, int res, int refresh, int cformat, int org, int nColBuffers, int nAuxBuffers)
{
    static const int W[] = { 320, 320, 400, 512, 640, 640, 640, 640, 800, 960, 856, 512, 1024, 1280, 1600, 400 };
    static const int H[] = { 200, 240, 256, 384, 200, 350, 400, 480, 600, 720, 480, 256, 768, 1024, 1200, 300 };
    if (opened) grGlideShutdown();
    const int w = (res >= 0 && res < 16) ? W[res] : 640;
    const int h = (res >= 0 && res < 16) ? H[res] : 480;
    colorFormat = cformat;
    origin = org;
    st = State();
    st.clipMaxX = w;
    st.clipMaxY = h;
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) fogTable[i] = 0;
    TexInit(texMemBytes);
    if (!DeviceOpen((HWND)(uintptr_t)hWnd, w, h)) { Log("grSstWinOpen failed"); return FALSE; }
    opened = true;
    PatchGame();
    Log("grSstWinOpen hwnd %08x res %d (%dx%d) refresh %d cformat %d origin %d buffers %d/%d", hWnd, res, w, h, refresh, cformat, org, nColBuffers, nAuxBuffers);
    return TRUE;
}

API void __stdcall grSstWinClose(void) { grGlideShutdown(); }
API FxU32 __stdcall grSstScreenWidth(void) { return out.logicalW; }
API FxU32 __stdcall grSstScreenHeight(void) { return out.logicalH; }
API void __stdcall grSstOrigin(int org) { Flush(); origin = org; }
API FxU32 __stdcall grSstStatus(void) { return 0x0FFFF03F; }
API FxBool __stdcall grSstIsBusy(void) { return FALSE; }
API void __stdcall grSstIdle(void) { Flush(); }
API FxBool __stdcall grSstVRetraceOn(void) { return TRUE; }
API FxU32 __stdcall grSstVideoLine(void) { return 0; }
API FxBool __stdcall grSstControl(FxU32) { return TRUE; }
API void __stdcall grSstPerfStats(void* stats) { if (stats) memset(stats, 0, 20); }
API void __stdcall grSstResetPerfStats(void) {}
API void __stdcall grResetTriStats(void) {}
API void __stdcall grTriStats(FxU32* processed, FxU32* drawn) { if (processed) *processed = 0; if (drawn) *drawn = 0; }
API void __stdcall grHints(int, FxU32) {}
API void __stdcall grSplash(float, float, float, float, FxU32) {}
API void __stdcall grCheckForRoom(FxI32) {}
API int __stdcall grBufferNumPending(void) { return 0; }
API void __stdcall grDitherMode(int) {}
API void __stdcall grGammaCorrectionValue(float) {}

API void __stdcall grBufferSwap(int interval)
{
    TRACE("grBufferSwap %d", interval);
    triLogged = 0;
    if (cfg.Trace >= 3) Log("swap %u", frameNo);
    Present();
    frameNo++;
    if (cfg.Trace && frameNo % 100 == 0)
    {
        MEMORYSTATUS ms = { sizeof(ms) };
        GlobalMemoryStatus(&ms);
        Log("frame %u (address space free %u MB)", frameNo, (unsigned)(ms.dwAvailVirtual >> 20));
    }
    if (cfg.Trace >= 2 && cfg.TraceFrom > 0 && frameNo >= (u32)cfg.TraceFrom + 2) cfg.Trace = 1; // detailed trace done
    if (cfg.Trace && frameNo % 30 == 0)
    {
        // debugging aid: create "trace_now" in the game folder to trace the next frames in detail
        const std::string f = gameDir + "trace_now";
        if (GetFileAttributesA(f.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            DeleteFileA(f.c_str());
            cfg.Trace = 3;
            cfg.TraceFrom = (int)frameNo + 1;
            DebugSnapshot(frameNo);
        }
        const std::string r = gameDir + "reset_now";
        if (GetFileAttributesA(r.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            DeleteFileA(r.c_str());
            TestDeviceReset();
        }
    }
}

API void __stdcall grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU16 depth)
{
    TRACE("grBufferClear %08x %02x %04x", color, alpha, depth);
    ClearBuffers(color, alpha, depth);
}

API void __stdcall grRenderBuffer(int buffer) { Set(st.renderBuffer, buffer); }

API void __stdcall grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy)
{
    TRACE("grClipWindow %u %u %u %u", minx, miny, maxx, maxy);
    if (st.clipMinX == (int)minx && st.clipMinY == (int)miny && st.clipMaxX == (int)maxx && st.clipMaxY == (int)maxy) return;
    Flush();
    st.clipMinX = minx; st.clipMinY = miny; st.clipMaxX = maxx; st.clipMaxY = maxy;
    InvalidateState();
}

// ---- combiners ---------------------------------------------------------------------------
API void __stdcall grColorCombine(int function, int factor, int local, int other, FxBool invert)
{
    TRACE("grColorCombine %d %d %d %d %d", function, factor, local, other, invert);
    Combine c; c.function = function; c.factor = factor; c.local = local; c.other = other; c.invert = invert ? 1 : 0;
    if (!memcmp(&c, &st.color, sizeof(c))) return;
    Flush(); st.color = c; InvalidateState();
}

API void __stdcall grAlphaCombine(int function, int factor, int local, int other, FxBool invert)
{
    TRACE("grAlphaCombine %d %d %d %d %d", function, factor, local, other, invert);
    Combine c; c.function = function; c.factor = factor; c.local = local; c.other = other; c.invert = invert ? 1 : 0;
    if (!memcmp(&c, &st.alpha, sizeof(c))) return;
    Flush(); st.alpha = c; InvalidateState();
}

API void __stdcall grTexCombine(int tmu, int rgbFn, int rgbFactor, int aFn, int aFactor, FxBool rgbInvert, FxBool aInvert)
{
    TRACE("grTexCombine %d %d %d %d %d %d %d", tmu, rgbFn, rgbFactor, aFn, aFactor, rgbInvert, aInvert);
    if (tmu != 0) return;
    TexCombine t; t.rgbFunction = rgbFn; t.rgbFactor = rgbFactor; t.alphaFunction = aFn; t.alphaFactor = aFactor;
    t.rgbInvert = rgbInvert ? 1 : 0; t.alphaInvert = aInvert ? 1 : 0;
    if (!memcmp(&t, &st.tex, sizeof(t))) return;
    Flush(); st.tex = t; InvalidateState();
}

API void __stdcall grTexCombineFunction(int tmu, int fnc)
{
    switch (fnc)
    {
    case 0: grTexCombine(tmu, 0, 0, 0, 0, FALSE, FALSE); break;           // zero
    case 2: grTexCombine(tmu, 3, 8, 3, 8, FALSE, FALSE); break;           // other
    case 3: grTexCombine(tmu, 4, 8, 4, 8, FALSE, FALSE); break;           // add
    case 4: grTexCombine(tmu, 3, 1, 3, 1, FALSE, FALSE); break;           // multiply
    case 9: grTexCombine(tmu, 0, 0, 0, 0, TRUE, TRUE); break;             // one
    default: grTexCombine(tmu, 1, 0, 1, 0, FALSE, FALSE); break;          // decal / detail / trilinear (one TMU)
    }
}

API void __stdcall guTexCombineFunction(int tmu, int fnc) { grTexCombineFunction(tmu, fnc); }

API void __stdcall grConstantColorValue(GrColor_t c) { TRACE("grConstantColorValue %08x", c); Set(st.constant, c); }

API void __stdcall grConstantColorValue4(float a, float r, float g, float b)
{
    auto cl = [](float v) { return (u32)(v < 0 ? 0 : v > 255 ? 255 : v); };
    // stored in the current colour format
    GrColor_t c;
    switch (colorFormat)
    {
    case GR_COLORFORMAT_ABGR: c = cl(a) << 24 | cl(b) << 16 | cl(g) << 8 | cl(r); break;
    case GR_COLORFORMAT_RGBA: c = cl(r) << 24 | cl(g) << 16 | cl(b) << 8 | cl(a); break;
    case GR_COLORFORMAT_BGRA: c = cl(b) << 24 | cl(g) << 16 | cl(r) << 8 | cl(a); break;
    default: c = cl(a) << 24 | cl(r) << 16 | cl(g) << 8 | cl(b); break;
    }
    Set(st.constant, c);
}

API void __stdcall grAlphaBlendFunction(int rs, int rd, int as, int ad)
{
    TRACE("grAlphaBlendFunction %d %d %d %d", rs, rd, as, ad);
    if (st.blendRgbSrc == rs && st.blendRgbDst == rd && st.blendASrc == as && st.blendADst == ad) return;
    Flush();
    st.blendRgbSrc = rs; st.blendRgbDst = rd; st.blendASrc = as; st.blendADst = ad;
    InvalidateState();
}

API void __stdcall grAlphaTestFunction(int fn) { TRACE("grAlphaTestFunction %d", fn); Set(st.alphaTestFunc, fn); }
API void __stdcall grAlphaTestReferenceValue(GrAlpha_t v) { Set(st.alphaRef, (int)v); }
API void __stdcall grAlphaControlsITRGBLighting(FxBool e) { Set(st.alphaControlsIt, (int)e); }
API void __stdcall grColorMask(FxBool rgb, FxBool a)
{
    if (st.colorMaskRgb == (rgb ? 1 : 0) && st.colorMaskA == (a ? 1 : 0)) return;
    Flush(); st.colorMaskRgb = rgb ? 1 : 0; st.colorMaskA = a ? 1 : 0; InvalidateState();
}
API void __stdcall grCullMode(int mode) { TRACE("grCullMode %d", mode); Set(st.cull, mode); }
API void __stdcall grChromakeyMode(int mode) { TRACE("grChromakeyMode %d", mode); Set(st.chromaMode, mode); }
API void __stdcall grChromakeyValue(GrColor_t v) { TRACE("grChromakeyValue %08x", v); Set(st.chromaValue, v); }

API void __stdcall grDepthBufferMode(int mode) { TRACE("grDepthBufferMode %d", mode); Set(st.depthMode, mode); }
API void __stdcall grDepthBufferFunction(int fn) { TRACE("grDepthBufferFunction %d", fn); Set(st.depthFunc, fn); }
API void __stdcall grDepthMask(FxBool m) { TRACE("grDepthMask %d", m); Set(st.depthMask, m ? 1 : 0); }
API void __stdcall grDepthBiasLevel(FxI32 level) { TRACE("grDepthBiasLevel %d", (int)(short)level); Set(st.depthBias, (int)(short)level); }

API void __stdcall grFogMode(int mode) { TRACE("grFogMode %x", mode); Set(st.fogMode, mode); }
API void __stdcall grFogColorValue(GrColor_t c) { TRACE("grFogColorValue %08x", c); Set(st.fogColor, c); }
API void __stdcall grFogTable(const GrFog_t* table)
{
    if (!table) return;
    if (!memcmp(fogTable, table, GR_FOG_TABLE_SIZE)) return;
    Flush();
    memcpy(fogTable, table, GR_FOG_TABLE_SIZE);
    MarkFogDirty();
    InvalidateState();
}

API void __stdcall grDisableAllEffects(void)
{
    Flush();
    st.blendRgbSrc = 4; st.blendRgbDst = 0; st.blendASrc = 4; st.blendADst = 0;
    st.alphaTestFunc = 7;
    st.chromaMode = 0;
    st.depthMode = GR_DEPTHBUFFER_DISABLE;
    st.fogMode = GR_FOG_DISABLE;
    InvalidateState();
}

API void __stdcall grGlideGetState(void* state) { if (state) savedStates[state] = st; }
API void __stdcall grGlideSetState(const void* state)
{
    auto it = savedStates.find(state);
    if (it == savedStates.end()) return;
    Flush();
    st = it->second;
    InvalidateState();
}

// ---- drawing -----------------------------------------------------------------------------
static void TraceVerts(const char* what, int n, const GrVertex* const* v)
{
    if (!(cfg.Trace >= 2 && frameNo >= (u32)cfg.TraceFrom && frameNo < (u32)cfg.TraceFrom + 2)) return;
    if (v[0]->oow < 0.5f && triLogged > 40) return;
    if (triLogged++ > 4000) return;
    char buf[1024]; int o = sprintf(buf, "%s n=%d:", what, n);
    for (int i = 0; i < n && i < 4; i++)
        o += sprintf(buf + o, " (%.1f,%.1f ooz %.1f oow %.5f rgba %.0f,%.0f,%.0f,%.0f st %.2f,%.2f)", v[i]->x, v[i]->y, v[i]->ooz, v[i]->oow, v[i]->r, v[i]->g, v[i]->b, v[i]->a, v[i]->tmuvtx[0].sow, v[i]->tmuvtx[0].tow);
    Log("%s", buf);
}
API void __stdcall grDrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c)
{
    const GrVertex* v[3] = { a, b, c };
    TraceVerts("tri", 3, v);
    DrawTriangle(a, b, c);
}
API void __stdcall grAADrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c, FxBool, FxBool, FxBool) { DrawTriangle(a, b, c); }
API void __stdcall guDrawTriangleWithClip(const GrVertex* a, const GrVertex* b, const GrVertex* c) { DrawTriangle(a, b, c); }
API void __stdcall guAADrawTriangleWithClip(const GrVertex* a, const GrVertex* b, const GrVertex* c) { DrawTriangle(a, b, c); }

static void PolyList(int n, const GrVertex* v)
{
    if (n < 3 || !v) return;
    std::vector<const GrVertex*> p(n);
    for (int i = 0; i < n; i++) p[i] = &v[i];
    TraceVerts("poly", n, p.data());
    DrawFan(n, p.data());
}

static void PolyIndexed(int n, const int* idx, const GrVertex* v)
{
    if (n < 3 || !v || !idx) return;
    std::vector<const GrVertex*> p(n);
    for (int i = 0; i < n; i++) p[i] = &v[idx[i]];
    TraceVerts("ipoly", n, p.data());
    DrawFan(n, p.data());
}

API void __stdcall grDrawPolygon(int n, const int* idx, const GrVertex* v) { PolyIndexed(n, idx, v); }
API void __stdcall grDrawPlanarPolygon(int n, const int* idx, const GrVertex* v) { PolyIndexed(n, idx, v); }
API void __stdcall grAADrawPolygon(int n, const int* idx, const GrVertex* v) { PolyIndexed(n, idx, v); }
API void __stdcall grDrawPolygonVertexList(int n, const GrVertex* v) { PolyList(n, v); }
API void __stdcall grDrawPlanarPolygonVertexList(int n, const GrVertex* v) { PolyList(n, v); }
API void __stdcall grAADrawPolygonVertexList(int n, const GrVertex* v) { PolyList(n, v); }
API void __stdcall guDrawPolygonVertexListWithClip(int n, const GrVertex* v) { PolyList(n, v); }
API void __stdcall grDrawLine(const GrVertex* a, const GrVertex* b) { DrawLine(a, b); }
API void __stdcall grAADrawLine(const GrVertex* a, const GrVertex* b) { DrawLine(a, b); }
API void __stdcall grDrawPoint(const GrVertex* a) { DrawPoint(a); }
API void __stdcall grAADrawPoint(const GrVertex* a) { DrawPoint(a); }

// ---- textures ----------------------------------------------------------------------------
API FxU32 __stdcall grTexMinAddress(int) { return 0; }
API FxU32 __stdcall grTexMaxAddress(int) { return texMemBytes - 8; }
API FxU32 __stdcall grTexCalcMemRequired(int smallLod, int largeLod, int aspect, int format) { return TexMemRequired(smallLod, largeLod, aspect, format); }
API FxU32 __stdcall grTexTextureMemRequired(FxU32, GrTexInfo* info)
{
    return info ? TexMemRequired(info->smallLod, info->largeLod, info->aspectRatio, info->format) : 0;
}

API void __stdcall grTexDownloadMipMap(int tmu, FxU32 address, FxU32, GrTexInfo* info)
{
    TRACE("grTexDownloadMipMap %d %08x lod %d-%d aspect %d fmt %d", tmu, address, info ? info->largeLod : -1, info ? info->smallLod : -1, info ? info->aspectRatio : -1, info ? info->format : -1);
    Flush();
    TexDownload(address, info);
    InvalidateState();
}

API void __stdcall grTexDownloadMipMapLevel(int, FxU32 address, int thisLod, int largeLod, int aspect, int format, FxU32, void* data)
{
    Flush();
    TexDownloadLevel(address, thisLod, largeLod, aspect, format, data);
    InvalidateState();
}

API void __stdcall grTexDownloadMipMapLevelPartial(int, FxU32 address, int thisLod, int largeLod, int aspect, int format, FxU32, void* data, int start, int end)
{
    Flush();
    TexDownloadLevelPartial(address, thisLod, largeLod, aspect, format, data, start, end);
    InvalidateState();
}

API void __stdcall grTexDownloadTable(int, int type, void* data)
{
    if (type != GR_TEXTABLE_PALETTE || !data) return;
    Flush();
    TexDownloadPalette((const u32*)data, 0, 255);
    InvalidateState();
}

API void __stdcall grTexDownloadTablePartial(int, int type, void* data, int start, int end)
{
    if (type != GR_TEXTABLE_PALETTE || !data) return;
    Flush();
    TexDownloadPalette((const u32*)data + start, start, end);
    InvalidateState();
}

API void __stdcall grTexNCCTable(int, int) {}
API void __stdcall grTexMultibase(int, FxBool) {}
API void __stdcall grTexMultibaseAddress(int, int, FxU32, FxU32, GrTexInfo*) {}
API void __stdcall grTexDetailControl(int, int, FxU8, float) {}

API void __stdcall grTexSource(int tmu, FxU32 address, FxU32, GrTexInfo* info)
{
    if (tmu != 0 || !info) return;
    TRACE("grTexSource %08x lod %d-%d aspect %d fmt %d", address, info->largeLod, info->smallLod, info->aspectRatio, info->format);
    if (st.texValid && st.texAddress == address && !memcmp(&st.texInfo, info, sizeof(GrTexInfo) - sizeof(void*))) return;
    Flush();
    st.texAddress = address;
    st.texInfo = *info;
    st.texInfo.data = nullptr;
    st.texValid = 1;
    InvalidateState();
}

API void __stdcall grTexFilterMode(int tmu, int minf, int magf)
{
    if (tmu != 0 || (st.minFilter == minf && st.magFilter == magf)) return;
    Flush(); st.minFilter = minf; st.magFilter = magf; InvalidateState();
}

API void __stdcall grTexClampMode(int tmu, int s, int t)
{
    if (tmu != 0 || (st.clampS == s && st.clampT == t)) return;
    Flush(); st.clampS = s; st.clampT = t; InvalidateState();
}

API void __stdcall grTexMipMapMode(int tmu, int mode, FxBool blend)
{
    if (tmu != 0 || (st.mipMode == mode && st.lodBlend == (blend ? 1 : 0))) return;
    Flush(); st.mipMode = mode; st.lodBlend = blend ? 1 : 0; InvalidateState();
}

API void __stdcall grTexLodBiasValue(int tmu, float bias) { if (tmu == 0) st.lodBias = bias; }

// ---- frame buffer ------------------------------------------------------------------------
API FxBool __stdcall grLfbLock(int type, int buffer, int writeMode, int org, FxBool pipeline, GrLfbInfo_t* info)
{
    TRACE("grLfbLock %d %d %d %d %d", type, buffer, writeMode, org, pipeline);
    return LfbLock(type, buffer, writeMode, org, pipeline, info);
}
API FxBool __stdcall grLfbUnlock(int type, int buffer) { return LfbUnlock(type, buffer); }
API FxBool __stdcall grLfbWriteRegion(int buffer, FxU32 x, FxU32 y, int fmt, FxU32 w, FxU32 h, FxI32 stride, void* data)
{
    TRACE("grLfbWriteRegion %d %u %u %d %u %u", buffer, x, y, fmt, w, h);
    return LfbWriteRegion(buffer, x, y, fmt, w, h, stride, data);
}
API FxBool __stdcall grLfbReadRegion(int buffer, FxU32 x, FxU32 y, FxU32 w, FxU32 h, FxU32 stride, void* data)
{
    TRACE("grLfbReadRegion %d %u %u %u %u", buffer, x, y, w, h);
    return LfbReadRegion(buffer, x, y, w, h, stride, data);
}
API void __stdcall grLfbConstantAlpha(GrAlpha_t) {}
API void __stdcall grLfbConstantDepth(FxU16) {}
API void __stdcall grLfbWriteColorFormat(int) {}
API void __stdcall grLfbWriteColorSwizzle(FxBool, FxBool) {}

// ---- gu utilities ------------------------------------------------------------------------
API float __stdcall guFogTableIndexToW(int i)
{
    return (float)(pow(2.0, 3.0 + (double)(i >> 2)) / (8 - (i & 3)));
}

API void __stdcall guFogGenerateExp(GrFog_t* table, float density)
{
    const float dpMax = density * guFogTableIndexToW(GR_FOG_TABLE_SIZE - 1);
    const float scale = 1.0f / (1.0f - expf(-dpMax));
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++)
    {
        float f = (1.0f - expf(-density * guFogTableIndexToW(i))) * scale;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        table[i] = (GrFog_t)(f * 255.0f);
    }
}

API void __stdcall guFogGenerateExp2(GrFog_t* table, float density)
{
    const float dpMax = density * guFogTableIndexToW(GR_FOG_TABLE_SIZE - 1);
    const float scale = 1.0f / (1.0f - expf(-dpMax * dpMax));
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++)
    {
        const float dp = density * guFogTableIndexToW(i);
        float f = (1.0f - expf(-dp * dp)) * scale;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        table[i] = (GrFog_t)(f * 255.0f);
    }
}

API void __stdcall guFogGenerateLinear(GrFog_t* table, float nearZ, float farZ)
{
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++)
    {
        float w = guFogTableIndexToW(i);
        if (w > 65535.0f) w = 65535.0f;
        float f = (w - nearZ) / (farZ - nearZ);
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        table[i] = (GrFog_t)(f * 255.0f);
    }
}

API void __stdcall guColorCombineFunction(int fnc)
{
    switch (fnc)
    {
    case 0: grColorCombine(0, 0, 0, 0, FALSE); break;
    case 1: grColorCombine(1, 0, 1, 0, FALSE); break;
    case 2: case 3: grColorCombine(1, 0, 0, 0, FALSE); break;
    case 4: grColorCombine(3, 8, 0, 1, FALSE); break;
    case 5: grColorCombine(3, 1, 1, 1, FALSE); break;
    case 6: case 7: grColorCombine(3, 1, 0, 1, FALSE); break;
    case 8: grColorCombine(5, 1, 0, 1, FALSE); break;
    case 9: grColorCombine(3, 3, 0, 1, FALSE); break;
    case 10: grColorCombine(4, 3, 0, 1, FALSE); break;
    case 11: grColorCombine(4, 8, 0, 1, FALSE); break;
    case 12: grColorCombine(6, 8, 0, 1, FALSE); break;
    case 13: grColorCombine(7, 4, 0, 2, FALSE); break;
    case 14: grColorCombine(5, 1, 0, 1, FALSE); break;
    case 15: grColorCombine(4, 3, 0, 1, FALSE); break;
    case 16: grColorCombine(0, 0, 0, 0, TRUE); break;
    default: break;
    }
}

API void __stdcall guAlphaSource(int mode)
{
    switch (mode)
    {
    case 0: grAlphaCombine(1, 0, 1, 2, FALSE); break;
    case 1: grAlphaCombine(1, 0, 0, 2, FALSE); break;
    case 2: grAlphaCombine(3, 8, 0, 1, FALSE); break;
    case 3: grAlphaCombine(3, 1, 0, 1, FALSE); break;
    default: break;
    }
}

API FxU16 __stdcall guEndianSwapBytes(FxU16 v) { return (FxU16)((v >> 8) | (v << 8)); }
API FxU32 __stdcall guEndianSwapWords(FxU32 v) { return (v >> 16) | (v << 16); }
API int __stdcall guEncodeRLE16(void*, void*, FxU32, FxU32) { return 0; }
API FxBool __stdcall gu3dfGetInfo(const char*, void*) { return FALSE; }
API FxBool __stdcall gu3dfLoad(const char*, void*) { return FALSE; }
API FxBool __stdcall ConvertAndDownloadRle(int, FxU32, int, int, int, int, FxU32, FxU8*, long, FxU32, FxU32, FxU32, FxU32, FxU32, FxU32, FxU16*) { return FALSE; }

// gu texture manager (Glide 2 GrMipMapInfo layout)
struct GuNccTable { FxU8 yRGB[16]; FxI16 iRGB[4][3]; FxI16 qRGB[4][3]; FxU32 packed[12]; };
struct GrMipMapInfo
{
    int sst; FxBool valid; int width, height; int aspect_ratio; void* data; int format; int mipmap_mode;
    int magfilter_mode, minfilter_mode, s_clamp_mode, t_clamp_mode;
    FxU32 tLOD, tTextureMode, lod_bias; int lod_min, lod_max; int tmu; FxU32 odd_even_mask; FxU32 tmu_base_address;
    FxBool trilinear; GuNccTable ncc_table;
};
static std::vector<GrMipMapInfo> mipmaps;
static FxU32 guNextFree = 0;
static int guCurrent = -1;

API void __stdcall guTexMemReset(void) { mipmaps.clear(); guNextFree = 0; guCurrent = -1; }
API FxU32 __stdcall guTexMemQueryAvail(int) { return texMemBytes > guNextFree ? texMemBytes - guNextFree : 0; }

API int __stdcall guTexAllocateMemory(int tmu, FxU8 oddEven, int w, int h, int fmt, int mmMode, int smallLod, int largeLod, int aspect, int sClamp, int tClamp, int minF, int magF, float lodBias, FxBool trilinear)
{
    const FxU32 need = TexMemRequired(smallLod, largeLod, aspect, fmt);
    if (guNextFree + need > texMemBytes) return -1;
    GrMipMapInfo m = {};
    m.valid = TRUE; m.width = w; m.height = h; m.aspect_ratio = aspect; m.format = fmt; m.mipmap_mode = mmMode;
    m.magfilter_mode = magF; m.minfilter_mode = minF; m.s_clamp_mode = sClamp; m.t_clamp_mode = tClamp;
    m.lod_min = smallLod; m.lod_max = largeLod; m.tmu = tmu; m.odd_even_mask = oddEven; m.tmu_base_address = guNextFree;
    m.trilinear = trilinear; m.lod_bias = (FxU32)(lodBias * 4);
    guNextFree += need;
    mipmaps.push_back(m);
    return (int)mipmaps.size() - 1;
}

API FxBool __stdcall guTexChangeAttributes(int id, int w, int h, int fmt, int mmMode, int smallLod, int largeLod, int aspect, int sClamp, int tClamp, int minF, int magF)
{
    if (id < 0 || id >= (int)mipmaps.size()) return FALSE;
    GrMipMapInfo& m = mipmaps[id];
    if (w != -1) m.width = w;
    if (h != -1) m.height = h;
    if (fmt != -1) m.format = fmt;
    if (mmMode != -1) m.mipmap_mode = mmMode;
    if (smallLod != -1) m.lod_min = smallLod;
    if (largeLod != -1) m.lod_max = largeLod;
    if (aspect != -1) m.aspect_ratio = aspect;
    if (sClamp != -1) m.s_clamp_mode = sClamp;
    if (tClamp != -1) m.t_clamp_mode = tClamp;
    if (minF != -1) m.minfilter_mode = minF;
    if (magF != -1) m.magfilter_mode = magF;
    return TRUE;
}

static GrTexInfo InfoOf(const GrMipMapInfo& m, void* data)
{
    GrTexInfo i;
    i.smallLod = m.lod_min; i.largeLod = m.lod_max; i.aspectRatio = m.aspect_ratio; i.format = m.format; i.data = data;
    return i;
}

API void __stdcall guTexDownloadMipMap(int id, const void* src, const void*)
{
    if (id < 0 || id >= (int)mipmaps.size()) return;
    GrTexInfo i = InfoOf(mipmaps[id], (void*)src);
    grTexDownloadMipMap(0, mipmaps[id].tmu_base_address, 3, &i);
}

API void __stdcall guTexDownloadMipMapLevel(int id, int lod, const void** src)
{
    if (id < 0 || id >= (int)mipmaps.size() || !src) return;
    const GrMipMapInfo& m = mipmaps[id];
    grTexDownloadMipMapLevel(0, m.tmu_base_address, lod, m.lod_max, m.aspect_ratio, m.format, 3, (void*)*src);
    int w, h;
    TexLodSize(lod, m.aspect_ratio, &w, &h);
    *src = (const u8*)*src + w * h * (m.format >= GR_TEXFMT_ARGB_8332 ? 2 : 1);
}

API void __stdcall guTexSource(int id)
{
    if (id < 0 || id >= (int)mipmaps.size()) return;
    const GrMipMapInfo& m = mipmaps[id];
    GrTexInfo i = InfoOf(m, nullptr);
    grTexSource(0, m.tmu_base_address, 3, &i);
    grTexFilterMode(0, m.minfilter_mode, m.magfilter_mode);
    grTexClampMode(0, m.s_clamp_mode, m.t_clamp_mode);
    grTexMipMapMode(0, m.mipmap_mode, m.trilinear);
    guCurrent = id;
}

API int __stdcall guTexGetCurrentMipMap(int) { return guCurrent; }
API GrMipMapInfo* __stdcall guTexGetMipMapInfo(int id) { return (id >= 0 && id < (int)mipmaps.size()) ? &mipmaps[id] : nullptr; }
API int __stdcall guTexCreateColorMipMap(void) { return -1; }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) LoadConfig();
    return TRUE;
}
