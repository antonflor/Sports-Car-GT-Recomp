// Glide 2.x types and constants (the subset of glide.h / sst1vid.h that Sports Car GT uses).
#pragma once
#include <stdint.h>

typedef int32_t FxI32;
typedef uint32_t FxU32;
typedef int16_t FxI16;
typedef uint16_t FxU16;
typedef uint8_t FxU8;
typedef int FxBool;
typedef FxU32 GrColor_t;
typedef FxU8 GrAlpha_t;
typedef FxU8 GrFog_t;

#define GR_FOG_TABLE_SIZE 64

typedef struct
{
    float sow, tow, oow;
} GrTmuVertex;

typedef struct
{
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    GrTmuVertex tmuvtx[2];
} GrVertex;

typedef struct
{
    FxI32 smallLod;
    FxI32 largeLod;
    FxI32 aspectRatio;
    FxI32 format;
    void* data;
} GrTexInfo;

typedef struct
{
    FxI32 size;
    void* lfbPtr;
    FxU32 strideInBytes;
    FxI32 writeMode;
    FxI32 origin;
} GrLfbInfo_t;

// Hardware description (Glide 2): 4 + 4 * 36 bytes
typedef struct { int tmuRev; int tmuRam; } GrTMUConfig_t;
typedef struct
{
    int fbRam;
    int fbiRev;
    int nTexelfx;
    FxBool sliDetect;
    GrTMUConfig_t tmuConfig[2];
} GrVoodooConfig_t;
typedef struct
{
    int num_sst;
    struct
    {
        int type;
        GrVoodooConfig_t VoodooConfig;
    } SSTs[4];
} GrHwConfiguration;

// LODs (Glide 2: 0 = 256 ... 8 = 1)
#define GR_LOD_256 0
#define GR_LOD_1 8

// aspect ratios (width x height)
#define GR_ASPECT_8x1 0
#define GR_ASPECT_4x1 1
#define GR_ASPECT_2x1 2
#define GR_ASPECT_1x1 3
#define GR_ASPECT_1x2 4
#define GR_ASPECT_1x4 5
#define GR_ASPECT_1x8 6

// texture formats
#define GR_TEXFMT_RGB_332 0x0
#define GR_TEXFMT_YIQ_422 0x1
#define GR_TEXFMT_ALPHA_8 0x2
#define GR_TEXFMT_INTENSITY_8 0x3
#define GR_TEXFMT_ALPHA_INTENSITY_44 0x4
#define GR_TEXFMT_P_8 0x5
#define GR_TEXFMT_ARGB_8332 0x8
#define GR_TEXFMT_AYIQ_8422 0x9
#define GR_TEXFMT_RGB_565 0xa
#define GR_TEXFMT_ARGB_1555 0xb
#define GR_TEXFMT_ARGB_4444 0xc
#define GR_TEXFMT_ALPHA_INTENSITY_88 0xd
#define GR_TEXFMT_AP_88 0xe

// combine
#define GR_COMBINE_FUNCTION_ZERO 0x0
#define GR_COMBINE_FUNCTION_LOCAL 0x1
#define GR_COMBINE_FUNCTION_LOCAL_ALPHA 0x2
#define GR_COMBINE_FUNCTION_SCALE_OTHER 0x3
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL 0x4
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA 0x5
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL 0x6
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL 0x7
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x8
#define GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL 0x9
#define GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x10

#define GR_COMBINE_FACTOR_ZERO 0x0
#define GR_COMBINE_FACTOR_LOCAL 0x1
#define GR_COMBINE_FACTOR_OTHER_ALPHA 0x2
#define GR_COMBINE_FACTOR_LOCAL_ALPHA 0x3
#define GR_COMBINE_FACTOR_TEXTURE_ALPHA 0x4
#define GR_COMBINE_FACTOR_TEXTURE_RGB 0x5
#define GR_COMBINE_FACTOR_ONE 0x8
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL 0x9
#define GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA 0xa
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA 0xb
#define GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA 0xc
#define GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION 0xd

#define GR_COMBINE_LOCAL_ITERATED 0x0
#define GR_COMBINE_LOCAL_CONSTANT 0x1
#define GR_COMBINE_LOCAL_DEPTH 0x2

#define GR_COMBINE_OTHER_ITERATED 0x0
#define GR_COMBINE_OTHER_TEXTURE 0x1
#define GR_COMBINE_OTHER_CONSTANT 0x2

// depth
#define GR_DEPTHBUFFER_DISABLE 0x0
#define GR_DEPTHBUFFER_ZBUFFER 0x1
#define GR_DEPTHBUFFER_WBUFFER 0x2
#define GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS 0x3
#define GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS 0x4

// fog
#define GR_FOG_DISABLE 0x0
#define GR_FOG_WITH_ITERATED_ALPHA 0x1
#define GR_FOG_WITH_TABLE 0x2
#define GR_FOG_MULT2 0x100
#define GR_FOG_ADD2 0x200

// buffers / lfb
#define GR_BUFFER_FRONTBUFFER 0x0
#define GR_BUFFER_BACKBUFFER 0x1
#define GR_BUFFER_AUXBUFFER 0x2
#define GR_LFB_READ_ONLY 0x00
#define GR_LFB_WRITE_ONLY 0x01
#define GR_LFBWRITEMODE_565 0x0
#define GR_LFBWRITEMODE_555 0x1
#define GR_LFBWRITEMODE_1555 0x2
#define GR_LFBWRITEMODE_888 0x4
#define GR_LFBWRITEMODE_8888 0x5
#define GR_LFBWRITEMODE_ANY 0xFF

#define GR_ORIGIN_UPPER_LEFT 0x0
#define GR_ORIGIN_LOWER_LEFT 0x1

#define GR_COLORFORMAT_ARGB 0x0
#define GR_COLORFORMAT_ABGR 0x1
#define GR_COLORFORMAT_RGBA 0x2
#define GR_COLORFORMAT_BGRA 0x3

#define GR_CULL_DISABLE 0x0
#define GR_CULL_NEGATIVE 0x1
#define GR_CULL_POSITIVE 0x2

#define GR_MIPMAP_DISABLE 0x0
#define GR_TEXTUREFILTER_POINT_SAMPLED 0x0
#define GR_TEXTURECLAMP_WRAP 0x0
#define GR_TEXTURECLAMP_CLAMP 0x1

#define GR_CHROMAKEY_DISABLE 0x0
#define GR_CHROMAKEY_ENABLE 0x1

#define GR_TEXTABLE_NCC0 0x0
#define GR_TEXTABLE_NCC1 0x1
#define GR_TEXTABLE_PALETTE 0x2
