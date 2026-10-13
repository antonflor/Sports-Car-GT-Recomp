// Geometry batching and translation of Glide render state to Direct3D 9 + shader constants.
#include "scgt.hxx"
#include "shaders.hxx"
#include <math.h>

namespace scgt
{
    State st;
    FxU8 fogTable[GR_FOG_TABLE_SIZE];
    int colorFormat = GR_COLORFORMAT_ARGB;
    int origin = GR_ORIGIN_UPPER_LEFT;

    IDirect3DVertexShader9* MakeVS(const char* entry, const unsigned char* fallback);
    IDirect3DPixelShader9* MakePS(const char* entry, const unsigned char* fallback);

    struct Vtx
    {
        float x, y, z, w;
        D3DCOLOR c;
        float s, t;
    };

    static std::vector<Vtx> batch;
    static bool dirty = true;
    static IDirect3DVertexShader9* mainVS = nullptr;
    static IDirect3DPixelShader9* mainPS = nullptr;
    static IDirect3DVertexDeclaration9* decl = nullptr;
    static IDirect3DTexture9* fogTex = nullptr;
    static IDirect3DTexture9* whiteTex = nullptr;
    static bool fogDirty = true;
    static float uScale = 1.0f / 256, vScale = 1.0f / 256;

    void InitPipeline()
    {
        mainVS = MakeVS("vs_main", g_vs_main);
        mainPS = MakePS("ps_main", g_ps_main);
        const D3DVERTEXELEMENT9 el[] = {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
            { 0, 20, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            D3DDECL_END() };
        dev->CreateVertexDeclaration(el, &decl);
        dev->CreateTexture(GR_FOG_TABLE_SIZE, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &fogTex, nullptr);
        dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &whiteTex, nullptr);
        D3DLOCKED_RECT lr;
        if (whiteTex && SUCCEEDED(whiteTex->LockRect(0, &lr, nullptr, 0)))
        {
            *(u32*)lr.pBits = 0xffffffff;
            whiteTex->UnlockRect(0);
        }
        fogDirty = true;
        dirty = true;
        batch.reserve(4096);
        dev->BeginScene();
        if (!mainVS || !mainPS) Log("shader creation failed vs=%p ps=%p", mainVS, mainPS);
    }

    void ReleasePipeline()
    {
        batch.clear();
        dev->EndScene();
        if (whiteTex) { whiteTex->Release(); whiteTex = nullptr; }
        if (fogTex) { fogTex->Release(); fogTex = nullptr; }
        if (decl) { decl->Release(); decl = nullptr; }
        if (mainPS) { mainPS->Release(); mainPS = nullptr; }
        if (mainVS) { mainVS->Release(); mainVS = nullptr; }
    }

    void InvalidateState()
    {
        dirty = true;
    }

    void MarkFogDirty() { fogDirty = true; }

    // ---- combiner constants ----------------------------------------------------------------
    // function -> (k_other, k_sublocal, k_addlocal, k_addlocalalpha)
    static void FunctionWeights(int fn, float k[4])
    {
        k[0] = k[1] = k[2] = k[3] = 0;
        switch (fn)
        {
        case GR_COMBINE_FUNCTION_ZERO: break;
        case GR_COMBINE_FUNCTION_LOCAL: k[2] = 1; break;
        case GR_COMBINE_FUNCTION_LOCAL_ALPHA: k[3] = 1; break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER: k[0] = 1; break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL: k[0] = 1; k[2] = 1; break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA: k[0] = 1; k[3] = 1; break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL: k[0] = 1; k[1] = 1; break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: k[0] = 1; k[1] = 1; k[2] = 1; break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA: k[0] = 1; k[1] = 1; k[3] = 1; break;
        case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL: k[1] = 1; k[2] = 1; break;
        case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA: k[1] = 1; k[3] = 1; break;
        default: k[2] = 1; break;
        }
    }

    // factor -> base source (0 zero, 1 local, 2 other alpha, 3 local alpha, 4 texture alpha, 5 texture rgb / lod)
    // and (f0, sign): factor = f0 + sign * source
    static int FactorSource(int factor, float* f0, float* sign)
    {
        const bool minus = (factor & 8) != 0;
        *f0 = minus ? 1.0f : 0.0f;
        *sign = minus ? -1.0f : 1.0f;
        return factor & 7;
    }

    static void ApplyConstants(const BoundTexture* bt)
    {
        float c[21][4] = {};
        ColorToFloat(st.constant, c[0]);

        // chroma key
        float key[4];
        ColorToFloat(st.chromaValue, key);
        c[1][0] = key[0]; c[1][1] = key[1]; c[1][2] = key[2]; c[1][3] = st.chromaMode ? 1.0f : 0.0f;

        // fog
        float fog[4];
        ColorToFloat(st.fogColor, fog);
        c[2][0] = fog[0]; c[2][1] = fog[1]; c[2][2] = fog[2];
        const int fogMode = st.fogMode & 0xff;
        c[20][0] = fogMode == GR_FOG_WITH_ITERATED_ALPHA ? 1.0f : 0.0f;
        c[20][1] = fogMode == GR_FOG_WITH_TABLE ? 1.0f : 0.0f;

        // colour combine
        const Combine& cc = st.color;
        c[3][0] = cc.local == GR_COMBINE_LOCAL_ITERATED ? 1.0f : 0.0f;
        c[3][1] = cc.local == GR_COMBINE_LOCAL_CONSTANT ? 1.0f : 0.0f;
        c[4][0] = cc.other == GR_COMBINE_OTHER_ITERATED ? 1.0f : 0.0f;
        c[4][1] = cc.other == GR_COMBINE_OTHER_TEXTURE ? 1.0f : 0.0f;
        c[4][2] = cc.other == GR_COMBINE_OTHER_CONSTANT ? 1.0f : 0.0f;
        FunctionWeights(cc.function, c[5]);
        {
            float f0, sg;
            const int src = FactorSource(cc.factor, &f0, &sg);
            c[6][0] = src == 1 ? 1.0f : 0.0f;
            c[6][1] = src == 2 ? 1.0f : 0.0f;
            c[6][2] = src == 3 ? 1.0f : 0.0f;
            c[6][3] = src == 4 ? 1.0f : 0.0f;
            c[3][3] = src == 5 ? 1.0f : 0.0f;
            c[7][0] = f0; c[7][1] = src == 0 ? 0.0f : sg;
            c[7][2] = cc.invert ? 1.0f : 0.0f; c[7][3] = cc.invert ? -1.0f : 1.0f;
        }

        // alpha combine
        const Combine& ac = st.alpha;
        c[8][0] = ac.local == GR_COMBINE_LOCAL_ITERATED ? 1.0f : 0.0f;
        c[8][1] = ac.local == GR_COMBINE_LOCAL_CONSTANT ? 1.0f : 0.0f;
        c[9][0] = ac.other == GR_COMBINE_OTHER_ITERATED ? 1.0f : 0.0f;
        c[9][1] = ac.other == GR_COMBINE_OTHER_TEXTURE ? 1.0f : 0.0f;
        c[9][2] = ac.other == GR_COMBINE_OTHER_CONSTANT ? 1.0f : 0.0f;
        {
            float k[4];
            FunctionWeights(ac.function, k);
            c[10][0] = k[0]; c[10][1] = k[1]; c[10][2] = (k[2] + k[3]) > 0 ? 1.0f : 0.0f;
            float f0, sg;
            const int src = FactorSource(ac.factor, &f0, &sg);
            c[11][0] = (src == 1 || src == 3) ? 1.0f : 0.0f;
            c[11][1] = src == 2 ? 1.0f : 0.0f;
            c[11][3] = src == 4 ? 1.0f : 0.0f;
            c[12][0] = f0; c[12][1] = (src == 0 || src == 5) ? 0.0f : sg;
            c[12][2] = ac.invert ? 1.0f : 0.0f; c[12][3] = ac.invert ? -1.0f : 1.0f;
        }

        // texture combine (TMU 0, nothing upstream)
        const TexCombine& tc = st.tex;
        {
            FunctionWeights(tc.rgbFunction, c[13]);
            c[13][0] = 0;
            float f0, sg;
            const int src = FactorSource(tc.rgbFactor, &f0, &sg);
            c[14][0] = src == 1 ? 1.0f : 0.0f;
            c[14][2] = src == 3 ? 1.0f : 0.0f;
            c[15][0] = f0; c[15][1] = (src == 1 || src == 3) ? sg : 0.0f;
            c[15][2] = tc.rgbInvert ? 1.0f : 0.0f; c[15][3] = tc.rgbInvert ? -1.0f : 1.0f;

            float k[4];
            FunctionWeights(tc.alphaFunction, k);
            c[16][0] = 0; c[16][1] = k[1]; c[16][2] = (k[2] + k[3]) > 0 ? 1.0f : 0.0f;
            const int asrc = FactorSource(tc.alphaFactor, &f0, &sg);
            c[17][0] = (asrc == 1 || asrc == 3) ? 1.0f : 0.0f;
            c[18][0] = f0; c[18][1] = (asrc == 1 || asrc == 3) ? sg : 0.0f;
            c[18][2] = tc.alphaInvert ? 1.0f : 0.0f; c[18][3] = tc.alphaInvert ? -1.0f : 1.0f;
        }

        // texture properties
        // textures carry a mask of black texels in alpha: valid when the key is black
        const bool keyIsBlack = key[0] < 0.02f && key[1] < 0.02f && key[2] < 0.02f;
        c[19][0] = bt && bt->realAlpha ? 1.0f : 0.0f;
        c[19][2] = (bt && bt->keyMask && keyIsBlack && st.chromaMode) ? 1.0f : 0.0f;

        dev->SetPixelShaderConstantF(0, &c[0][0], 21);
    }

    static D3DBLEND BlendSrc(int f)
    {
        switch (f)
        {
        case 0: return D3DBLEND_ZERO;
        case 1: return D3DBLEND_SRCALPHA;
        case 2: return D3DBLEND_DESTCOLOR;
        case 3: return D3DBLEND_DESTALPHA;
        case 4: return D3DBLEND_ONE;
        case 5: return D3DBLEND_INVSRCALPHA;
        case 6: return D3DBLEND_INVDESTCOLOR;
        case 7: return D3DBLEND_INVDESTALPHA;
        case 15: return D3DBLEND_SRCALPHASAT;
        default: return D3DBLEND_ONE;
        }
    }

    static D3DBLEND BlendDst(int f)
    {
        switch (f)
        {
        case 0: return D3DBLEND_ZERO;
        case 1: return D3DBLEND_SRCALPHA;
        case 2: return D3DBLEND_SRCCOLOR;
        case 3: return D3DBLEND_DESTALPHA;
        case 4: return D3DBLEND_ONE;
        case 5: return D3DBLEND_INVSRCALPHA;
        case 6: return D3DBLEND_INVSRCCOLOR;
        case 7: return D3DBLEND_INVDESTALPHA;
        case 15: return D3DBLEND_SRCCOLOR; // pre-fog colour
        default: return D3DBLEND_ZERO;
        }
    }

    static bool UsesTexture()
    {
        auto fac = [](int f) { int b = f & 7; return b == 4 || b == 5; };
        return st.color.other == GR_COMBINE_OTHER_TEXTURE || st.alpha.other == GR_COMBINE_OTHER_TEXTURE || fac(st.color.factor) || fac(st.alpha.factor);
    }

    static void ApplyState()
    {
        SetRenderBuffer(st.renderBuffer);
        dev->SetVertexShader(mainVS);
        dev->SetPixelShader(mainPS);
        dev->SetVertexDeclaration(decl);

        // depth
        const bool zmode = st.depthMode == GR_DEPTHBUFFER_ZBUFFER || st.depthMode == GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS;
        const bool depthOn = st.depthMode != GR_DEPTHBUFFER_DISABLE;
        dev->SetRenderState(D3DRS_ZENABLE, depthOn ? D3DZB_TRUE : D3DZB_FALSE);
        int fn = st.depthFunc & 7;
        if (!zmode)
        {
            // W is stored reversed (1/w): swap the direction of the comparison
            static const int rev[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
            fn = rev[fn];
        }
        dev->SetRenderState(D3DRS_ZFUNC, (D3DCMPFUNC)(fn + 1));
        dev->SetRenderState(D3DRS_ZWRITEENABLE, (depthOn && st.depthMask) ? TRUE : FALSE);
        float bias = 0.0f;
        if (st.depthBias)
            bias = (zmode ? 1.0f : -1.0f) * (float)(short)st.depthBias * (1.0f / 65536.0f);
        dev->SetRenderState(D3DRS_DEPTHBIAS, *(DWORD*)&bias);

        // blending
        const bool blend = !(st.blendRgbSrc == 4 && st.blendRgbDst == 0 && st.blendASrc == 4 && st.blendADst == 0);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, blend);
        if (blend)
        {
            dev->SetRenderState(D3DRS_SRCBLEND, BlendSrc(st.blendRgbSrc));
            dev->SetRenderState(D3DRS_DESTBLEND, BlendDst(st.blendRgbDst));
            dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_SRCBLENDALPHA, BlendSrc(st.blendASrc));
            dev->SetRenderState(D3DRS_DESTBLENDALPHA, BlendDst(st.blendADst));
        }
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, (st.alphaTestFunc & 7) != 7);
        dev->SetRenderState(D3DRS_ALPHAFUNC, (st.alphaTestFunc & 7) + 1);
        dev->SetRenderState(D3DRS_ALPHAREF, st.alphaRef & 255);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, (st.colorMaskRgb ? 7 : 0) | (st.colorMaskA ? 8 : 0));
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);

        // clip window -> scissor (in the 3D view's mapping)
        RECT r;
        r.left = (LONG)floorf(st.clipMinX * out.view3DScaleX + out.view3DOffsetX + 0.5f);
        r.right = (LONG)floorf(st.clipMaxX * out.view3DScaleX + out.view3DOffsetX + 0.5f);
        r.top = (LONG)floorf(st.clipMinY * out.scaleY + out.offsetY + 0.5f);
        r.bottom = (LONG)floorf(st.clipMaxY * out.scaleY + out.offsetY + 0.5f);
        if (r.left < 0) r.left = 0;
        if (r.top < 0) r.top = 0;
        if (r.right > out.width) r.right = out.width;
        if (r.bottom > out.height) r.bottom = out.height;
        dev->SetScissorRect(&r);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);

        // texture
        const BoundTexture* bt = nullptr;
        if (st.texValid && UsesTexture()) bt = TexBind();
        if (bt && bt->tex)
        {
            dev->SetTexture(0, bt->tex);
            uScale = bt->uScale;
            vScale = bt->vScale;
        }
        else
        {
            dev->SetTexture(0, whiteTex);
            uScale = vScale = 1.0f / 256;
        }
        const bool bilinear = st.minFilter || st.magFilter;
        if (bilinear && cfg.Anisotropy > 1)
        {
            dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
            dev->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, cfg.Anisotropy);
        }
        else
        {
            dev->SetSamplerState(0, D3DSAMP_MINFILTER, st.minFilter ? D3DTEXF_LINEAR : D3DTEXF_POINT);
        }
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, st.magFilter ? D3DTEXF_LINEAR : D3DTEXF_POINT);
        DWORD mip = D3DTEXF_NONE;
        if (st.mipMode != GR_MIPMAP_DISABLE) mip = (cfg.Trilinear || st.lodBlend) ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        dev->SetSamplerState(0, D3DSAMP_MIPFILTER, mip);
        float lodBias = cfg.LodBias;
        dev->SetSamplerState(0, D3DSAMP_MIPMAPLODBIAS, *(DWORD*)&lodBias);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, st.clampS ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, st.clampT ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP);

        // fog table
        if (fogDirty && fogTex)
        {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(fogTex->LockRect(0, &lr, nullptr, 0)))
            {
                u32* p = (u32*)lr.pBits;
                for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) p[i] = (u32)fogTable[i] << 24 | 0xffffff;
                fogTex->UnlockRect(0);
            }
            fogDirty = false;
        }
        dev->SetTexture(1, fogTex);
        dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

        ApplyConstants(bt);

        const float vs[3][4] = {
            { out.view3DScaleX, out.scaleY, out.view3DOffsetX, out.offsetY },
            { 2.0f / out.width, 2.0f / out.height, zmode ? 1.0f : 0.0f, 0 },
            { uScale, vScale, 0, 0 } };
        dev->SetVertexShaderConstantF(0, &vs[0][0], 3);
        dirty = false;
    }

    void Flush()
    {
        if (batch.empty() || !dev) { batch.clear(); return; }
        dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(batch.size() / 3), batch.data(), sizeof(Vtx));
        batch.clear();
    }

    static inline float Clamp255(float v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

    // Games add 3<<18 to x/y so the FPU rounds them to Voodoo's 1/16 pixel grid; the hardware
    // only looks at the low bits. Remove the bias.
    static inline float Unsnap(float f)
    {
        if (f >= 393216.0f) return f - 786432.0f;
        return f;
    }

    static inline void Convert(const GrVertex* v, Vtx* o)
    {
        o->x = Unsnap(v->x);
        const float y = Unsnap(v->y);
        o->y = origin == GR_ORIGIN_LOWER_LEFT ? out.logicalH - y : y;
        o->z = v->ooz;
        o->w = (v->oow > 0.0f && v->oow == v->oow) ? v->oow : 1.0f;
        o->c = D3DCOLOR_ARGB((int)Clamp255(v->a), (int)Clamp255(v->r), (int)Clamp255(v->g), (int)Clamp255(v->b));
        const float s = v->tmuvtx[0].sow, t = v->tmuvtx[0].tow;
        o->s = (s == s) ? s : 0.0f;
        o->t = (t == t) ? t : 0.0f;
    }

    static bool Culled(const Vtx& a, const Vtx& b, const Vtx& c)
    {
        if (st.cull == GR_CULL_DISABLE) return false;
        // signed area in Glide's (lower-left origin) convention
        const float area = (b.x - a.x) * (a.y - c.y) - (c.x - a.x) * (a.y - b.y);
        if (st.cull == GR_CULL_NEGATIVE) return area < 0;
        return area > 0;
    }

    static inline void Prepare()
    {
        if (!dev) return;
        if (dirty)
        {
            Flush();
            LfbFlush();
            ApplyState();
        }
    }

    void DrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c)
    {
        if (!dev) return;
        Prepare();
        Vtx v[3];
        Convert(a, &v[0]);
        Convert(b, &v[1]);
        Convert(c, &v[2]);
        if (Culled(v[0], v[1], v[2])) return;
        {
            float x0 = v[0].x, x1 = v[0].x, y0 = v[0].y, y1 = v[0].y;
            for (int i = 1; i < 3; i++)
            {
                if (v[i].x < x0) x0 = v[i].x; if (v[i].x > x1) x1 = v[i].x;
                if (v[i].y < y0) y0 = v[i].y; if (v[i].y > y1) y1 = v[i].y;
            }
            LfbNoteDraw(st.renderBuffer, x0, y0, x1, y1);
        }
        batch.push_back(v[0]);
        batch.push_back(v[1]);
        batch.push_back(v[2]);
        if (batch.size() > 30000) Flush();
    }

    void DrawFan(int count, const GrVertex* const* verts)
    {
        for (int i = 2; i < count; i++) DrawTriangle(verts[0], verts[i - 1], verts[i]);
    }

    static void Quad(const GrVertex* src, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3)
    {
        GrVertex q[4];
        for (int i = 0; i < 4; i++) q[i] = *src;
        q[0].x = x0; q[0].y = y0; q[1].x = x1; q[1].y = y1; q[2].x = x2; q[2].y = y2; q[3].x = x3; q[3].y = y3;
        DrawTriangle(&q[0], &q[1], &q[2]);
        DrawTriangle(&q[0], &q[2], &q[3]);
    }

    void DrawLine(const GrVertex* a, const GrVertex* b)
    {
        // a one-logical-pixel wide quad (scales with the output like the rest of the frame)
        const float dx = b->x - a->x, dy = b->y - a->y;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-4f) { DrawPoint(a); return; }
        const float nx = -dy / len * 0.5f, ny = dx / len * 0.5f;
        GrVertex v[4] = { *a, *b, *b, *a };
        v[0].x += nx; v[0].y += ny;
        v[1].x += nx; v[1].y += ny;
        v[2].x -= nx; v[2].y -= ny;
        v[3].x -= nx; v[3].y -= ny;
        DrawTriangle(&v[0], &v[1], &v[2]);
        DrawTriangle(&v[0], &v[2], &v[3]);
    }

    void DrawPoint(const GrVertex* a)
    {
        Quad(a, a->x - 0.5f, a->y - 0.5f, a->x + 0.5f, a->y - 0.5f, a->x + 0.5f, a->y + 0.5f, a->x - 0.5f, a->y + 0.5f);
    }
}
