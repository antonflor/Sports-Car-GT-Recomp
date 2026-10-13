// scgt2026 shaders: Glide 2 pipeline (texture combine -> colour/alpha combine -> chroma key -> fog)
// expressed with constants so one shader covers every combiner setting.

// ---------------- vertex shader -------------------------------------------------------
// input position = (x, y, ooz, oow) in the game's logical screen space
float4 vsView  : register(c0); // scaleX, scaleY, offsetX, offsetY (logical -> output pixels)
float4 vsTarget: register(c1); // 2/width, 2/height, depthMode (0 = w, 1 = z), 0
float4 vsUV    : register(c2); // u scale, v scale (s,t in 0..256 -> 0..1)

struct VSIn
{
    float4 pos : POSITION;
    float4 col : COLOR0;
    float2 st  : TEXCOORD0;
};

struct VSOut
{
    float4 pos : POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
    float  w   : TEXCOORD1;
};

VSOut vs_main(VSIn i)
{
    VSOut o;
    float oow = max(i.pos.w, 1e-7);
    float w = 1.0 / oow;
    // D3D9 pixel centres are at integer window coordinates, Glide's at +0.5
    float x = i.pos.x * vsView.x + vsView.z - 0.5;
    float y = i.pos.y * vsView.y + vsView.w - 0.5;
    float nx = x * vsTarget.x - 1.0;
    float ny = 1.0 - y * vsTarget.y;
    // reversed depth: nearer = larger. W mode stores 1/w (exact, linear in screen space).
    float d = lerp(saturate(oow), saturate(i.pos.z / 65535.0), vsTarget.z);
    o.pos = float4(nx * w, ny * w, d * w, w);
    o.col = i.col;
    o.uv = (i.st / oow) * vsUV.xy;
    o.w = w;
    return o;
}

// ---------------- pixel shader --------------------------------------------------------
sampler2D texSampler : register(s0);
sampler2D fogSampler : register(s1);

float4 cConst    : register(c0);
float4 cChroma   : register(c1);  // rgb key, w = enabled
float4 cFog      : register(c2);  // rgb fog colour
float4 cLocal    : register(c3);  // colour local select: iterated, constant, -, factor weight of texture rgb
float4 cOther    : register(c4);  // colour other select: iterated, texture, constant
float4 cFn       : register(c5);  // colour function: k_other, k_sublocal, k_addlocal, k_addlocalalpha
float4 cFacW     : register(c6);  // colour factor source weights: local, other alpha, local alpha, texture alpha
float4 cFacInv   : register(c7);  // factor = x + y*src ; result = z + w*result (invert)
float4 aLocal    : register(c8);  // alpha local select: iterated, constant
float4 aOther    : register(c9);  // alpha other select: iterated, texture, constant
float4 aFn       : register(c10); // k_other, k_sublocal, k_addlocal
float4 aFacW     : register(c11); // local alpha, other alpha, -, texture alpha
float4 aFacInv   : register(c12);
float4 tFn       : register(c13); // texture unit rgb: k_other(0), k_sublocal, k_addlocal, k_addlocalalpha
float4 tFacW     : register(c14); // local rgb, other alpha(0), local alpha, -
float4 tFacInv   : register(c15);
float4 tAFn      : register(c16); // texture unit alpha: k_other, k_sublocal, k_addlocal
float4 tAFacW    : register(c17); // local alpha, -, local alpha, -
float4 tAFacInv  : register(c18);
float4 cTex      : register(c19); // real alpha, -, key mask in alpha, -
float4 cFogW     : register(c20); // fog weight: iterated alpha, table

struct PSIn
{
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
    float  w   : TEXCOORD1;
};

float4 ps_main(PSIn i) : COLOR
{
    float4 texel = tex2D(texSampler, i.uv);
    // colour-keyed (black = transparent) textures: alpha is the key mask and the key texels are
    // black, so dividing by the filtered mask gives a clean edge without dark fringes
    texel.rgb = lerp(texel.rgb, texel.rgb / max(texel.a, 0.004), cTex.z);
    float ta = lerp(1.0, texel.a, cTex.x);

    // texture combine unit (one TMU: the upstream "other" is zero)
    float3 tx = tFacW.x * texel.rgb + (tFacW.z * ta).xxx;
    float3 tfac = tFacInv.x + tFacInv.y * tx;
    float3 trgb = (-tFn.y * texel.rgb) * tfac + tFn.z * texel.rgb + (tFn.w * ta).xxx;
    trgb = saturate(tFacInv.z + tFacInv.w * trgb);
    float tfa = tAFacInv.x + tAFacInv.y * (tAFacW.x * ta);
    float tal = (-tAFn.y * ta) * tfa + tAFn.z * ta;
    tal = saturate(tAFacInv.z + tAFacInv.w * tal);
    float4 tex = float4(trgb, tal);

    float4 it = i.col;
    float3 cl = cLocal.x * it.rgb + cLocal.y * cConst.rgb;
    float3 co = cOther.x * it.rgb + cOther.y * tex.rgb + cOther.z * cConst.rgb;
    float al = aLocal.x * it.a + aLocal.y * cConst.a;
    float ao = aOther.x * it.a + aOther.y * tex.a + aOther.z * cConst.a;

    // chroma key compares the colour selected as "other"
    float3 d = abs(co - cChroma.rgb);
    float keyCompare = max(d.r, max(d.g, d.b)) - 0.02;
    float keyTest = lerp(keyCompare, texel.a - 0.5, cTex.z * cOther.y);
    clip(lerp(1.0, keyTest, cChroma.w));

    float3 x = cFacW.x * cl + (cFacW.y * ao + cFacW.z * al + cFacW.w * tex.a).xxx + cLocal.w * tex.rgb;
    float3 fac = cFacInv.x + cFacInv.y * x;
    float3 rgb = (cFn.x * co - cFn.y * cl) * fac + cFn.z * cl + (cFn.w * al).xxx;
    rgb = saturate(cFacInv.z + cFacInv.w * rgb);

    float xa = aFacW.x * al + aFacW.y * ao + aFacW.w * tex.a;
    float fa = aFacInv.x + aFacInv.y * xa;
    float a = (aFn.x * ao - aFn.y * al) * fa + aFn.z * al;
    a = saturate(aFacInv.z + aFacInv.w * a);

    // Glide fog table: entry i is at w = 2^(3+(i>>2)) / (8-(i&3))
    float w = max(i.w, 1.0);
    float k = floor(log2(w));
    float r = w / exp2(k);
    float idx = 4.0 * k + (8.0 - 8.0 / r);
    float tableFog = tex2D(fogSampler, float2((idx + 0.5) / 64.0, 0.5)).a;
    float f = cFogW.x * it.a + cFogW.y * tableFog;
    rgb = lerp(rgb, cFog.rgb, f);
    return float4(rgb, a);
}

// ---------------- 2D blit (frame buffer writes, present) -------------------------------
sampler2D blitSampler : register(s0);
float4 blitKey : register(c0); // x = discard pixels whose alpha is 0
struct BlitIn { float2 uv : TEXCOORD0; };
float4 ps_blit(BlitIn i) : COLOR
{
    float4 c = tex2D(blitSampler, i.uv);
    clip(lerp(1.0, c.a - 0.5, blitKey.x));
    return float4(c.rgb, 1.0);
}

struct BlitVSIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct BlitVSOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
float4 blitTarget : register(c0); // 2/width, 2/height
BlitVSOut vs_blit(BlitVSIn i)
{
    BlitVSOut o;
    float x = i.pos.x - 0.5;
    float y = i.pos.y - 0.5;
    o.pos = float4(x * blitTarget.x - 1.0, 1.0 - y * blitTarget.y, 0.0, 1.0);
    o.uv = i.uv;
    return o;
}
