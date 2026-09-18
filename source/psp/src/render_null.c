#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "r_types.h"
#include "r_state.h"
#include "r_draw.h"
#include "r_texture.h"

#define PSP_SCREEN_W 480
#define PSP_SCREEN_H 272
#define PSP_STRIDE   512
#define GAME_W       640.0f
#define GAME_H       480.0f
#define TPAGE_COUNT  52
#define TPAGE_PLATFORM_ICONS_PSP 51

extern int g_tpageWidth[TPAGE_COUNT];
extern int g_tpageHeight[TPAGE_COUNT];
extern void *g_tpagePixelBuf[TPAGE_COUNT];

int g_glBackingWidth = 363;
int g_glBackingHeight = PSP_SCREEN_H;
int g_glViewportOffsetX = 58;
int g_glViewportOffsetY = 0;

typedef struct {
    int textureId;
    R_BlendMode blendMode;
    int depthTest;
    int depthWrite;
    R_DepthFunc depthFunc;
    R_TexEnvMode texEnv;
    R_FilterMode filter;
    R_CullMode cull;
    int alphaTest;
    float alphaRef;
} PspRenderState;

typedef struct {
    float u;
    float v;
    uint32_t color;
    float x;
    float y;
    float z;
} PspVertex;

typedef struct {
    void *pixels;
    int w;
    int h;
    int psm;
    int bpp;
    int dirty;
    int noColorKey;
    int green6;
    int rgbaOverride;
    R_FilterMode filter;
    int hasFilter;
    int swizzled;
} PspTexture;

typedef struct {
    int scissorValid;
    int scissorX;
    int scissorY;
    int scissorW;
    int scissorH;
    int blendValid;
    R_BlendMode blendMode;
    int cullDisabled;
    int depthTestValid;
    int depthTest;
    R_DepthFunc depthFunc;
    int depthWriteValid;
    int depthWrite;
    int alphaValid;
    int alphaTest;
    int alphaRef;
    int textureValid;
    int textureEnabled;
    int textureId;
    void *texturePixels;
    int textureW;
    int textureH;
    int textureFilter;
    int textureFunc;
    int textureTcc;
    int texturePsm;
    int textureSwizzled;
} PspGuCache;

static unsigned int __attribute__((aligned(16))) s_displayList[262144];
static PspRenderState s_state;
static PspRenderState s_stack[4];
static int s_stackDepth;
static PspTexture s_tex[TPAGE_COUNT];
static PspGuCache s_cache;
static void *s_frame0;
static void *s_frame1;
static void *s_depth;
static int s_guReady;
static int s_frameOpen;
static int s_scissorEnabled;
static int s_scissorX;
static int s_scissorY;
static int s_scissorW = PSP_SCREEN_W;
static int s_scissorH = PSP_SCREEN_H;

#define BATCH_MAX_VERTS 8192
#define DEFERRED_FREE_MAX 64

static PspVertex __attribute__((aligned(16))) s_batchVerts[BATCH_MAX_VERTS];
static int s_batchCount;
static int s_batchActive;
static PspRenderState s_batchState;
static int s_batchScX;
static int s_batchScY;
static int s_batchScW;
static int s_batchScH;
static int s_batchHasTex;
static float s_batchTexW;
static float s_batchTexH;

static void *s_deferredFree[DEFERRED_FREE_MAX];
static int s_deferredFreeCount;
static int s_frameDirty = 1;

static void flush_batch(void);

static uint32_t argb_to_abgr(uint32_t c)
{
    uint32_t a = c & 0xff000000u;
    uint32_t r = (c >> 16) & 0xffu;
    uint32_t g = c & 0x0000ff00u;
    uint32_t b = (c & 0xffu) << 16;
    return a | b | g | r;
}

static uint32_t argb_to_add_signed_offset_abgr(uint32_t c)
{
    uint32_t a = c & 0xff000000u;
    int r = ((int)((c >> 16) & 0xffu)) - 0x80;
    int g = ((int)((c >> 8) & 0xffu)) - 0x80;
    int b = ((int)(c & 0xffu)) - 0x80;

    if (r < 0) {
        r = 0;
    }
    if (g < 0) {
        g = 0;
    }
    if (b < 0) {
        b = 0;
    }

    return a | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static uint32_t rgba_to_abgr(unsigned char r, unsigned char g,
                             unsigned char b, unsigned char a)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
           ((uint32_t)g << 8) | (uint32_t)r;
}

static int is_pow2(int v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

static void invalidate_gu_cache(void)
{
    memset(&s_cache, 0, sizeof(s_cache));
}

static void invalidate_texture_cache(int tpage)
{
    flush_batch();
    if (tpage < 0 || tpage == s_cache.textureId) {
        s_cache.textureValid = 0;
    }
}

static void release_pixels(void *p)
{
    if (!p) {
        return;
    }
    if (s_frameOpen && s_deferredFreeCount < DEFERRED_FREE_MAX) {
        s_deferredFree[s_deferredFreeCount++] = p;
        return;
    }
    free(p);
}

static void drain_deferred_free(void)
{
    for (int i = 0; i < s_deferredFreeCount; i++) {
        free(s_deferredFree[i]);
        s_deferredFree[i] = NULL;
    }
    s_deferredFreeCount = 0;
}

static void free_texture(int tpage)
{
    if (tpage < 0 || tpage >= TPAGE_COUNT) {
        return;
    }
    invalidate_texture_cache(tpage);
    if (s_tex[tpage].pixels) {
        release_pixels(s_tex[tpage].pixels);
    }
    s_tex[tpage].pixels = NULL;
    s_tex[tpage].w = 0;
    s_tex[tpage].h = 0;
    s_tex[tpage].dirty = 1;
    s_tex[tpage].rgbaOverride = 0;
    s_tex[tpage].swizzled = 0;
    s_tex[tpage].psm = GU_PSM_8888;
    s_tex[tpage].bpp = 4;
}

static float s_vpScaleX = (float)PSP_SCREEN_W / GAME_W;
static float s_vpScaleY = (float)PSP_SCREEN_H / GAME_H;

static void update_viewport_size(void)
{
    float scale = (float)PSP_SCREEN_H / GAME_H;
    int w = (int)(GAME_W * scale + 0.5f);
    if (w > PSP_SCREEN_W) {
        w = PSP_SCREEN_W;
    }
    g_glBackingWidth = w;
    g_glBackingHeight = PSP_SCREEN_H;
    g_glViewportOffsetX = (PSP_SCREEN_W - w) / 2;
    g_glViewportOffsetY = 0;

    s_vpScaleX = (float)g_glBackingWidth / GAME_W;
    s_vpScaleY = (float)g_glBackingHeight / GAME_H;
}

static void psp_gu_init(void)
{
    if (s_guReady) {
        return;
    }

    update_viewport_size();

    s_frame0 = guGetStaticVramBuffer(PSP_STRIDE, PSP_SCREEN_H, GU_PSM_5650);
    s_frame1 = guGetStaticVramBuffer(PSP_STRIDE, PSP_SCREEN_H, GU_PSM_5650);
    s_depth = guGetStaticVramBuffer(PSP_STRIDE, PSP_SCREEN_H, GU_PSM_4444);

    sceGuInit();
    sceGuStart(GU_DIRECT, s_displayList);
    sceGuDrawBuffer(GU_PSM_5650, s_frame0, PSP_STRIDE);
    sceGuDispBuffer(PSP_SCREEN_W, PSP_SCREEN_H, s_frame1, PSP_STRIDE);
    sceGuDepthBuffer(s_depth, PSP_STRIDE);
    sceGuOffset(2048 - (PSP_SCREEN_W / 2), 2048 - (PSP_SCREEN_H / 2));
    sceGuViewport(2048, 2048, PSP_SCREEN_W, PSP_SCREEN_H);
    sceGuDepthRange(0, 65535);
    sceGuScissor(0, 0, PSP_SCREEN_W, PSP_SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFrontFace(GU_CW);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_LEQUAL);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuClearColor(0xff000000);
    sceGuClearDepth(65535);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    s_guReady = 1;
    invalidate_gu_cache();
    R_ResetState();
}

static void begin_gu_if_needed(void)
{
    psp_gu_init();
    if (!s_frameOpen) {
        sceGuStart(GU_DIRECT, s_displayList);
        invalidate_gu_cache();
        s_frameOpen = 1;
    }
}

static void resolve_scissor(int *x, int *y, int *w, int *h)
{
    if (s_scissorEnabled) {
        *x = s_scissorX;
        *y = s_scissorY;
        *w = s_scissorW;
        *h = s_scissorH;
    }
    else {
        *x = g_glViewportOffsetX;
        *y = g_glViewportOffsetY;
        *w = g_glBackingWidth;
        *h = g_glBackingHeight;
    }
}

static void apply_scissor_rect(int x, int y, int w, int h)
{
    if (s_cache.scissorValid &&
        s_cache.scissorX == x && s_cache.scissorY == y &&
        s_cache.scissorW == w && s_cache.scissorH == h) {
        return;
    }

    sceGuScissor(x, y, w, h);
    s_cache.scissorValid = 1;
    s_cache.scissorX = x;
    s_cache.scissorY = y;
    s_cache.scissorW = w;
    s_cache.scissorH = h;
}

static void apply_scissor(void)
{
    int x;
    int y;
    int w;
    int h;

    resolve_scissor(&x, &y, &w, &h);
    apply_scissor_rect(x, y, w, h);
}

static void clear_full_frame(void)
{
    flush_batch();
    begin_gu_if_needed();
    if (!s_frameDirty) {
        apply_scissor();
        return;
    }
    sceGuScissor(0, 0, PSP_SCREEN_W, PSP_SCREEN_H);
    s_cache.scissorValid = 1;
    s_cache.scissorX = 0;
    s_cache.scissorY = 0;
    s_cache.scissorW = PSP_SCREEN_W;
    s_cache.scissorH = PSP_SCREEN_H;
    sceGuClearColor(0xff000000);
    sceGuClearDepth(65535);
    sceGuDepthMask(GU_FALSE);
    s_cache.depthWriteValid = 1;
    s_cache.depthWrite = 1;
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    s_frameDirty = 0;
    apply_scissor();
}

static uint32_t convert_565_pixel(unsigned short p, int noColorKey, int green6)
{
    int r5 = (p >> 11) & 0x1f;
    int g = green6 ? ((p >> 5) & 0x3f) : ((p >> 6) & 0x1f);
    int b5 = p & 0x1f;
    int g5 = green6 ? (g >> 1) : g;
    unsigned char a = (!noColorKey && IS_COLOR_KEY_RGB5(r5, g5, b5)) ? 0 : 255;
    unsigned char r8 = (unsigned char)((r5 << 3) | (r5 >> 2));
    unsigned char g8 = green6 ? (unsigned char)((g << 2) | (g >> 4))
                              : (unsigned char)((g5 << 3) | (g5 >> 2));
    unsigned char b8 = (unsigned char)((b5 << 3) | (b5 >> 2));
    return rgba_to_abgr(r8, g8, b8, a);
}

static int tex_format_for(int noColorKey, int green6)
{
    if (noColorKey) {
        return GU_PSM_5650;
    }
    if (green6) {
        return GU_PSM_8888;
    }
    return GU_PSM_5551;
}

static int tex_bpp_for(int psm)
{
    return (psm == GU_PSM_8888) ? 4 : 2;
}

static int tex_can_swizzle(int w, int h, int bpp)
{
    if (h < 8 || (h & 7) != 0) {
        return 0;
    }
    if (bpp == 4) {
        return w >= 4 && (w & 3) == 0;
    }
    return w >= 8 && (w & 7) == 0;
}

static size_t swizzle_index32(int x, int y, int w)
{
    return ((size_t)(y >> 3) * (size_t)(w >> 2) + (size_t)(x >> 2)) * 32u +
           (size_t)(y & 7) * 4u + (size_t)(x & 3);
}

static size_t swizzle_index16(int x, int y, int w)
{
    return ((size_t)(y >> 3) * (size_t)(w >> 3) + (size_t)(x >> 3)) * 64u +
           (size_t)(y & 7) * 8u + (size_t)(x & 7);
}

static uint16_t convert_5551_pixel(unsigned short p)
{
    int r5 = (p >> 11) & 0x1f;
    int g5 = (p >> 6) & 0x1f;
    int b5 = p & 0x1f;
    unsigned int a = IS_COLOR_KEY_RGB5(r5, g5, b5) ? 0u : 0x8000u;
    return (uint16_t)(a | ((unsigned int)b5 << 10) |
                      ((unsigned int)g5 << 5) | (unsigned int)r5);
}

static uint16_t convert_5650_pixel(unsigned short p, int green6)
{
    int r5 = (p >> 11) & 0x1f;
    int b5 = p & 0x1f;
    int g6;

    if (green6) {
        g6 = (p >> 5) & 0x3f;
    }
    else {
        int g5 = (p >> 6) & 0x1f;
        g6 = (g5 << 1) | (g5 >> 4);
    }
    return (uint16_t)(((unsigned int)b5 << 11) |
                      ((unsigned int)g6 << 5) | (unsigned int)r5);
}

static void convert_tpage_565(void *dst, const unsigned short *src,
                              int w, int h, int noColorKey, int green6,
                              int psm, int swizzled)
{
    if (psm == GU_PSM_8888) {
        uint32_t *out = (uint32_t *)dst;
        if (!swizzled) {
            int n = w * h;
            for (int i = 0; i < n; i++) {
                out[i] = convert_565_pixel(src[i], noColorKey, green6);
            }
            return;
        }
        for (int by = 0; by < h; by += 8) {
            for (int bx = 0; bx < w; bx += 4) {
                for (int row = 0; row < 8; row++) {
                    const unsigned short *p = src + (size_t)(by + row) * (size_t)w + bx;
                    out[0] = convert_565_pixel(p[0], noColorKey, green6);
                    out[1] = convert_565_pixel(p[1], noColorKey, green6);
                    out[2] = convert_565_pixel(p[2], noColorKey, green6);
                    out[3] = convert_565_pixel(p[3], noColorKey, green6);
                    out += 4;
                }
            }
        }
        return;
    }

    uint16_t *out = (uint16_t *)dst;
    if (!swizzled) {
        int n = w * h;
        if (psm == GU_PSM_5650) {
            for (int i = 0; i < n; i++) {
                out[i] = convert_5650_pixel(src[i], green6);
            }
        }
        else {
            for (int i = 0; i < n; i++) {
                out[i] = convert_5551_pixel(src[i]);
            }
        }
        return;
    }

    for (int by = 0; by < h; by += 8) {
        for (int bx = 0; bx < w; bx += 8) {
            for (int row = 0; row < 8; row++) {
                const unsigned short *p = src + (size_t)(by + row) * (size_t)w + bx;
                if (psm == GU_PSM_5650) {
                    for (int i = 0; i < 8; i++) {
                        out[i] = convert_5650_pixel(p[i], green6);
                    }
                }
                else {
                    for (int i = 0; i < 8; i++) {
                        out[i] = convert_5551_pixel(p[i]);
                    }
                }
                out += 8;
            }
        }
    }
}

static void convert_tpage_rgba(uint32_t *dst, const unsigned char *rgba,
                               int w, int h, int swizzled)
{
    if (!swizzled) {
        int n = w * h;
        for (int i = 0; i < n; i++) {
            dst[i] = rgba_to_abgr(rgba[i * 4 + 0], rgba[i * 4 + 1],
                                  rgba[i * 4 + 2], rgba[i * 4 + 3]);
        }
        return;
    }

    uint32_t *out = dst;
    for (int by = 0; by < h; by += 8) {
        for (int bx = 0; bx < w; bx += 4) {
            for (int row = 0; row < 8; row++) {
                const unsigned char *s = rgba +
                    ((size_t)(by + row) * (size_t)w + bx) * 4u;
                out[0] = rgba_to_abgr(s[0], s[1], s[2], s[3]);
                out[1] = rgba_to_abgr(s[4], s[5], s[6], s[7]);
                out[2] = rgba_to_abgr(s[8], s[9], s[10], s[11]);
                out[3] = rgba_to_abgr(s[12], s[13], s[14], s[15]);
                out += 4;
            }
        }
    }
}

static void upload_rgba_copy(int tpage, const unsigned char *rgba, int w, int h)
{
    if (tpage < 0 || tpage >= TPAGE_COUNT || !rgba || w <= 0 || h <= 0) {
        return;
    }

    size_t bytes = (size_t)w * (size_t)h * 4u;
    uint32_t *dst = (uint32_t *)memalign(16, bytes);
    if (!dst) {
        return;
    }

    int swizzled = tex_can_swizzle(w, h, 4);
    convert_tpage_rgba(dst, rgba, w, h, swizzled);

    free_texture(tpage);
    s_tex[tpage].pixels = dst;
    s_tex[tpage].w = w;
    s_tex[tpage].h = h;
    s_tex[tpage].dirty = 0;
    s_tex[tpage].swizzled = swizzled;
    s_tex[tpage].psm = GU_PSM_8888;
    s_tex[tpage].bpp = 4;
    sceKernelDcacheWritebackRange(dst, (unsigned int)bytes);
}

static void bind_texture(const PspRenderState *st)
{
    int tpage = st->textureId;
    if (tpage < 0 || tpage >= TPAGE_COUNT || !s_tex[tpage].pixels ||
        s_tex[tpage].w <= 0 || s_tex[tpage].h <= 0) {
        if (!s_cache.textureValid || s_cache.textureEnabled) {
            sceGuDisable(GU_TEXTURE_2D);
            s_cache.textureValid = 1;
            s_cache.textureEnabled = 0;
            s_cache.textureId = -1;
            s_cache.texturePixels = NULL;
        }
        return;
    }

    R_FilterMode filter = s_tex[tpage].hasFilter ? s_tex[tpage].filter : st->filter;
    int guFilter = (filter == R_FILTER_LINEAR) ? GU_LINEAR : GU_NEAREST;
    int texFunc = (st->texEnv == R_TEXENV_ADD_SIGNED) ? GU_TFX_ADD : GU_TFX_MODULATE;
    int texTcc = (s_tex[tpage].psm == GU_PSM_5650) ? GU_TCC_RGB : GU_TCC_RGBA;

    if (s_cache.textureValid && s_cache.textureEnabled &&
        s_cache.textureId == tpage &&
        s_cache.texturePixels == s_tex[tpage].pixels &&
        s_cache.textureW == s_tex[tpage].w &&
        s_cache.textureH == s_tex[tpage].h &&
        s_cache.textureFilter == guFilter &&
        s_cache.textureFunc == texFunc &&
        s_cache.textureTcc == texTcc &&
        s_cache.texturePsm == s_tex[tpage].psm &&
        s_cache.textureSwizzled == s_tex[tpage].swizzled) {
        return;
    }

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(s_tex[tpage].psm, 0, 0, s_tex[tpage].swizzled);
    sceGuTexImage(0, s_tex[tpage].w, s_tex[tpage].h, s_tex[tpage].w, s_tex[tpage].pixels);
    sceGuTexFilter(guFilter, guFilter);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFunc(texFunc, texTcc);
    sceGuTexFlush();

    s_cache.textureValid = 1;
    s_cache.textureEnabled = 1;
    s_cache.textureId = tpage;
    s_cache.texturePixels = s_tex[tpage].pixels;
    s_cache.textureW = s_tex[tpage].w;
    s_cache.textureH = s_tex[tpage].h;
    s_cache.textureFilter = guFilter;
    s_cache.textureFunc = texFunc;
    s_cache.textureTcc = texTcc;
    s_cache.texturePsm = s_tex[tpage].psm;
    s_cache.textureSwizzled = s_tex[tpage].swizzled;
}

static void fill_vertex(PspVertex *dst, const RenderVertex *src)
{
    if (s_batchHasTex) {
        float u = src->u * s_batchTexW;
        float v = src->v * s_batchTexH;
        if (src->u <= 0.0f) {
            u = 0.5f;
        }
        else if (src->u >= 1.0f) {
            u = s_batchTexW - 0.5f;
        }
        if (src->v <= 0.0f) {
            v = 0.5f;
        }
        else if (src->v >= 1.0f) {
            v = s_batchTexH - 0.5f;
        }
        dst->u = u;
        dst->v = v;
    }
    else {
        dst->u = src->u;
        dst->v = src->v;
    }
    dst->color = (s_batchState.texEnv == R_TEXENV_ADD_SIGNED)
        ? argb_to_add_signed_offset_abgr(src->color)
        : argb_to_abgr(src->color);
    dst->x = (float)g_glViewportOffsetX + src->sx * s_vpScaleX;
    dst->y = (float)g_glViewportOffsetY + src->sy * s_vpScaleY;
    dst->z = src->sz * 65535.0f;
}

void R_SetTexture(int tpageIndex)
{
    if (s_state.textureId == tpageIndex) {
        return;
    }
    s_state.textureId = tpageIndex;
    if (tpageIndex >= 0 && tpageIndex < TPAGE_COUNT && s_tex[tpageIndex].dirty) {
        R_UploadTexture(tpageIndex);
    }
}

void R_SetBlendMode(R_BlendMode mode)  { if (s_state.blendMode != mode) s_state.blendMode = mode; }
void R_SetDepthTest(int enable)        { if (s_state.depthTest != enable) s_state.depthTest = enable; }
void R_SetDepthFunc(R_DepthFunc func)  { if (s_state.depthFunc != func) s_state.depthFunc = func; }
void R_SetDepthWrite(int enable)       { if (s_state.depthWrite != enable) s_state.depthWrite = enable; }
void R_SetTexEnv(R_TexEnvMode mode)    { if (s_state.texEnv != mode) s_state.texEnv = mode; }
void R_SetFilter(R_FilterMode mode)    { if (s_state.filter != mode) s_state.filter = mode; }
void R_SetCullMode(R_CullMode mode)    { if (s_state.cull != mode) s_state.cull = mode; }
void R_SetAlphaTest(int enable)        { if (s_state.alphaTest != enable) s_state.alphaTest = enable; }
void R_SetAlphaRef(float ref)          { if (s_state.alphaRef != ref) s_state.alphaRef = ref; }

void R_SetTpageFilter(int tpage, R_FilterMode mode)
{
    if (tpage >= 0 && tpage < TPAGE_COUNT) {
        s_tex[tpage].filter = mode;
        s_tex[tpage].hasFilter = 1;
        invalidate_texture_cache(tpage);
    }
}

void R_ClearTpageFilter(int tpage)
{
    if (tpage >= 0 && tpage < TPAGE_COUNT) {
        s_tex[tpage].hasFilter = 0;
        invalidate_texture_cache(tpage);
    }
}

void R_SetTpageSatBoost(int tpage, int k256)
{
    (void)tpage;
    (void)k256;
}

void R_SetScissor(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    int px = x;
    int py = PSP_SCREEN_H - y - h;
    int pw = w;
    int ph = h;

    if (px < 0) {
        pw += px;
        px = 0;
    }
    if (py < 0) {
        ph += py;
        py = 0;
    }
    if (px + pw > PSP_SCREEN_W) {
        pw = PSP_SCREEN_W - px;
    }
    if (py + ph > PSP_SCREEN_H) {
        ph = PSP_SCREEN_H - py;
    }

    int scw = pw > 0 ? pw : 1;
    int sch = ph > 0 ? ph : 1;
    if (s_scissorEnabled && s_scissorX == px && s_scissorY == py &&
        s_scissorW == scw && s_scissorH == sch) {
        return;
    }

    if (px == 0 && py == 0 && scw == PSP_SCREEN_W && sch == PSP_SCREEN_H) {
        flush_batch();
        s_scissorEnabled = 0;
        return;
    }

    flush_batch();
    s_scissorEnabled = 1;
    s_scissorX = px;
    s_scissorY = py;
    s_scissorW = scw;
    s_scissorH = sch;
}

void R_DisableScissor(void)
{
    if (!s_scissorEnabled) {
        return;
    }
    flush_batch();
    s_scissorEnabled = 0;
}

static void apply_render_state(const PspRenderState *st,
                               int scX, int scY, int scW, int scH)
{
    int alphaRef;

    begin_gu_if_needed();
    apply_scissor_rect(scX, scY, scW, scH);

    if (!s_cache.blendValid || s_cache.blendMode != st->blendMode) {
        if (st->blendMode == R_BLEND_NONE) {
            sceGuDisable(GU_BLEND);
        }
        else {
            sceGuEnable(GU_BLEND);
            if (st->blendMode == R_BLEND_ADDITIVE) {
                sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0x00ffffff);
            }
            else {
                sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            }
        }
        s_cache.blendValid = 1;
        s_cache.blendMode = st->blendMode;
    }

    if (!s_cache.cullDisabled) {
        sceGuDisable(GU_CULL_FACE);
        s_cache.cullDisabled = 1;
    }

    if (!s_cache.depthTestValid ||
        s_cache.depthTest != st->depthTest ||
        (st->depthTest && s_cache.depthFunc != st->depthFunc)) {
        if (st->depthTest) {
            sceGuEnable(GU_DEPTH_TEST);
            switch (st->depthFunc) {
                case R_DEPTH_LESS:
                    sceGuDepthFunc(GU_LESS);
                    break;
                case R_DEPTH_ALWAYS:
                    sceGuDepthFunc(GU_ALWAYS);
                    break;
                case R_DEPTH_LEQUAL:
                default:
                    sceGuDepthFunc(GU_LEQUAL);
                    break;
            }
        }
        else {
            sceGuDisable(GU_DEPTH_TEST);
        }
        s_cache.depthTestValid = 1;
        s_cache.depthTest = st->depthTest;
        s_cache.depthFunc = st->depthFunc;
    }

    if (!s_cache.depthWriteValid || s_cache.depthWrite != st->depthWrite) {
        sceGuDepthMask(st->depthWrite ? GU_FALSE : GU_TRUE);
        s_cache.depthWriteValid = 1;
        s_cache.depthWrite = st->depthWrite;
    }

    alphaRef = (int)(st->alphaRef * 255.0f);
    if (alphaRef < 0) {
        alphaRef = 0;
    }
    if (alphaRef > 255) {
        alphaRef = 255;
    }
    if (!s_cache.alphaValid ||
        s_cache.alphaTest != st->alphaTest ||
        (st->alphaTest && s_cache.alphaRef != alphaRef)) {
        if (st->alphaTest) {
            sceGuAlphaFunc(GU_GREATER, alphaRef, 0xff);
            sceGuEnable(GU_ALPHA_TEST);
        }
        else {
            sceGuDisable(GU_ALPHA_TEST);
        }
        s_cache.alphaValid = 1;
        s_cache.alphaTest = st->alphaTest;
        s_cache.alphaRef = alphaRef;
    }
    bind_texture(st);
}

static void flush_batch(void)
{
    int count = s_batchCount;

    if (!s_batchActive || count <= 0) {
        return;
    }

    s_batchCount = 0;
    s_batchActive = 0;

    apply_render_state(&s_batchState, s_batchScX, s_batchScY,
                       s_batchScW, s_batchScH);

    PspVertex *out = (PspVertex *)sceGuGetMemory(sizeof(PspVertex) * (size_t)count);
    if (!out) {
        return;
    }
    memcpy(out, s_batchVerts, sizeof(PspVertex) * (size_t)count);
    s_frameDirty = 1;
    sceGuDrawArray(GU_TRIANGLES,
                   GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   count, NULL, out);
}

static PspVertex *batch_reserve(int needVerts)
{
    if (needVerts <= 0) {
        return NULL;
    }

    int x;
    int y;
    int w;
    int h;

    resolve_scissor(&x, &y, &w, &h);

    if (s_batchActive) {
        int sameState = (
            s_batchState.textureId == s_state.textureId &&
            s_batchState.blendMode == s_state.blendMode &&
            s_batchState.depthTest == s_state.depthTest &&
            s_batchState.depthWrite == s_state.depthWrite &&
            s_batchState.depthFunc == s_state.depthFunc &&
            s_batchState.texEnv == s_state.texEnv &&
            s_batchState.filter == s_state.filter &&
            s_batchState.cull == s_state.cull &&
            s_batchState.alphaTest == s_state.alphaTest &&
            s_batchState.alphaRef == s_state.alphaRef
        );

        if (s_batchCount + needVerts > BATCH_MAX_VERTS ||
            x != s_batchScX || y != s_batchScY ||
            w != s_batchScW || h != s_batchScH ||
            !sameState) {
            flush_batch();
        }
    }

    if (!s_batchActive) {
        s_batchState = s_state;
        s_batchScX = x;
        s_batchScY = y;
        s_batchScW = w;
        s_batchScH = h;

        int tpage = s_state.textureId;
        if (tpage >= 0 && tpage < TPAGE_COUNT &&
            s_tex[tpage].pixels && s_tex[tpage].w > 0 && s_tex[tpage].h > 0) {
            s_batchHasTex = 1;
            s_batchTexW = (float)s_tex[tpage].w;
            s_batchTexH = (float)s_tex[tpage].h;
        }
        else {
            s_batchHasTex = 0;
            s_batchTexW = 1.0f;
            s_batchTexH = 1.0f;
        }
        s_batchActive = 1;
    }

    PspVertex *p = &s_batchVerts[s_batchCount];
    s_batchCount += needVerts;
    return p;
}

void R_FlushState(void)
{
    flush_batch();
}

void R_PushState(void)
{
    if (s_stackDepth < 4) {
        s_stack[s_stackDepth++] = s_state;
    }
}

void R_PopState(void)
{
    if (s_stackDepth > 0) {
        s_state = s_stack[--s_stackDepth];
    }
}

void R_ResetState(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.textureId = -1;
    s_state.blendMode = R_BLEND_ALPHA;
    s_state.depthTest = 1;
    s_state.depthWrite = 1;
    s_state.depthFunc = R_DEPTH_LEQUAL;
    s_state.texEnv = R_TEXENV_MODULATE;
    s_state.filter = R_FILTER_NEAREST;
    s_state.cull = R_CULL_NONE;
    s_state.alphaTest = 1;
    s_state.alphaRef = 0.01f;
}

void R_DebugGetState(int *depthTest, int *depthWrite, int *depthFunc,
                     int *blendMode, int *alphaTest, float *alphaRef)
{
    if (depthTest) {
        *depthTest = s_state.depthTest;
    }
    if (depthWrite) {
        *depthWrite = s_state.depthWrite;
    }
    if (depthFunc) {
        *depthFunc = s_state.depthFunc;
    }
    if (blendMode) {
        *blendMode = s_state.blendMode;
    }
    if (alphaTest) {
        *alphaTest = s_state.alphaTest;
    }
    if (alphaRef) {
        *alphaRef = s_state.alphaRef;
    }
}

void R_DrawTri(const RenderVertex v[3])
{
    PspVertex *out = batch_reserve(3);

    fill_vertex(&out[0], &v[0]);
    fill_vertex(&out[1], &v[1]);
    fill_vertex(&out[2], &v[2]);
}

void R_DrawQuad(const RenderVertex v[4])
{
    PspVertex *out = batch_reserve(6);

    fill_vertex(&out[0], &v[0]);
    fill_vertex(&out[1], &v[1]);
    fill_vertex(&out[2], &v[2]);
    out[3] = out[0];
    out[4] = out[2];
    fill_vertex(&out[5], &v[3]);
}

void R_DrawQuadBatch(const RenderVertex *quads, int quadCount)
{
    if (!quads || quadCount <= 0) {
        return;
    }

    PspVertex *out = batch_reserve(quadCount * 6);
    int dst = 0;

    for (int i = 0; i < quadCount; i++) {
        const RenderVertex *v = &quads[i * 4];
        fill_vertex(&out[dst + 0], &v[0]);
        fill_vertex(&out[dst + 1], &v[1]);
        fill_vertex(&out[dst + 2], &v[2]);
        out[dst + 3] = out[dst + 0];
        out[dst + 4] = out[dst + 2];
        fill_vertex(&out[dst + 5], &v[3]);
        dst += 6;
    }
}

void R_DrawTriFan(const RenderVertex *v, int count)
{
    if (!v || count < 3) {
        return;
    }

    int triCount = count - 2;
    PspVertex *out = batch_reserve(triCount * 3);
    int dst = 0;

    for (int i = 1; i + 1 < count; i++) {
        fill_vertex(&out[dst + 0], &v[0]);
        fill_vertex(&out[dst + 1], &v[i]);
        fill_vertex(&out[dst + 2], &v[i + 1]);
        dst += 3;
    }
}

void R_DrawQuad2D(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  float z, uint32_t color)
{
    RenderVertex v[4] = {
        { x0, y0, z, 1.0f, color, 0, u0, v0 },
        { x1, y0, z, 1.0f, color, 0, u1, v0 },
        { x1, y1, z, 1.0f, color, 0, u1, v1 },
        { x0, y1, z, 1.0f, color, 0, u0, v1 },
    };
    R_DrawQuad(v);
}

void R_DrawQuad2DSolid(float x0, float y0, float x1, float y1,
                       float z, uint32_t color)
{
    int oldTex = s_state.textureId;
    s_state.textureId = -1;
    R_DrawQuad2D(x0, y0, x1, y1, 0.0f, 0.0f, 0.0f, 0.0f, z, color);
    s_state.textureId = oldTex;
}

void R_InitTextures(void)
{
    for (int i = 0; i < TPAGE_COUNT; i++) {
        s_tex[i].dirty = 1;
        s_tex[i].filter = R_FILTER_NEAREST;
        s_tex[i].psm = GU_PSM_8888;
        s_tex[i].bpp = 4;
    }
}

void R_UploadTexture(int tpage)
{
    if (tpage < 0 || tpage >= TPAGE_COUNT) {
        return;
    }

    int w = g_tpageWidth[tpage];
    int h = g_tpageHeight[tpage];
    unsigned short *src = (unsigned short *)g_tpagePixelBuf[tpage];
    if (!src || !is_pow2(w) || !is_pow2(h) || w > 512 || h > 512) {
        free_texture(tpage);
        return;
    }

    int noColorKey = s_tex[tpage].noColorKey;
    int green6 = s_tex[tpage].green6;
    int psm = tex_format_for(noColorKey, green6);
    int bpp = tex_bpp_for(psm);
    int swizzled = tex_can_swizzle(w, h, bpp);
    size_t bytes = (size_t)w * (size_t)h * (size_t)bpp;

    void *dst = memalign(16, bytes);
    if (!dst) {
        return;
    }
    convert_tpage_565(dst, src, w, h, noColorKey, green6, psm, swizzled);

    free_texture(tpage);
    s_tex[tpage].pixels = dst;
    s_tex[tpage].w = w;
    s_tex[tpage].h = h;
    s_tex[tpage].dirty = 0;
    s_tex[tpage].noColorKey = noColorKey;
    s_tex[tpage].green6 = green6;
    s_tex[tpage].swizzled = swizzled;
    s_tex[tpage].psm = psm;
    s_tex[tpage].bpp = bpp;
    sceKernelDcacheWritebackRange(dst, (unsigned int)bytes);
}

void R_UploadTextureRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    upload_rgba_copy(tpage, rgba, w, h);
}

void R_UploadTextureSubRect(int tpage, int x, int y, int w, int h)
{
    if (tpage < 0 || tpage >= TPAGE_COUNT || x < 0 || y < 0 || w <= 0 || h <= 0) {
        return;
    }

    unsigned short *src = (unsigned short *)g_tpagePixelBuf[tpage];
    if (!src || !s_tex[tpage].pixels || s_tex[tpage].w <= 0 || s_tex[tpage].h <= 0) {
        R_UploadTexture(tpage);
        return;
    }

    int srcW = g_tpageWidth[tpage] > 0 ? g_tpageWidth[tpage] : 256;
    int srcH = g_tpageHeight[tpage] > 0 ? g_tpageHeight[tpage] : 256;
    if (x >= srcW || y >= srcH || x >= s_tex[tpage].w || y >= s_tex[tpage].h) {
        return;
    }
    if (x + w > srcW) {
        w = srcW - x;
    }
    if (y + h > srcH) {
        h = srcH - y;
    }
    if (x + w > s_tex[tpage].w) {
        w = s_tex[tpage].w - x;
    }
    if (y + h > s_tex[tpage].h) {
        h = s_tex[tpage].h - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    invalidate_texture_cache(tpage);

    void *dst = s_tex[tpage].pixels;
    int texW = s_tex[tpage].w;
    int noColorKey = s_tex[tpage].noColorKey;
    int green6 = s_tex[tpage].green6;
    int psm = s_tex[tpage].psm;
    int bpp = s_tex[tpage].bpp;
    int swizzled = s_tex[tpage].swizzled;

    for (int row = 0; row < h; row++) {
        unsigned short *srcRow = src + (size_t)(y + row) * (size_t)srcW + x;
        int dy = y + row;
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            size_t idx;

            if (swizzled) {
                idx = (bpp == 4) ? swizzle_index32(dx, dy, texW)
                                 : swizzle_index16(dx, dy, texW);
            }
            else {
                idx = (size_t)dy * (size_t)texW + (size_t)dx;
            }

            if (psm == GU_PSM_8888) {
                ((uint32_t *)dst)[idx] =
                    convert_565_pixel(srcRow[col], noColorKey, green6);
            }
            else if (psm == GU_PSM_5650) {
                ((uint16_t *)dst)[idx] = convert_5650_pixel(srcRow[col], green6);
            }
            else {
                ((uint16_t *)dst)[idx] = convert_5551_pixel(srcRow[col]);
            }
        }
    }

    size_t first;
    size_t last;

    if (swizzled) {
        size_t blockRow = (bpp == 4) ? (size_t)(texW >> 2) * 32u
                                     : (size_t)(texW >> 3) * 64u;
        first = (size_t)(y >> 3) * blockRow;
        last = ((size_t)((y + h - 1) >> 3) + 1u) * blockRow;
    }
    else {
        first = (size_t)y * (size_t)texW + (size_t)x;
        last = first + (size_t)(h - 1) * (size_t)texW + (size_t)w;
    }

    sceKernelDcacheWritebackRange((char *)dst + first * (size_t)bpp,
                                  (unsigned int)((last - first) * (size_t)bpp));
}

void R_MarkTextureDirty(int tpage)
{
    if (tpage >= 0 && tpage < TPAGE_COUNT) {
        s_tex[tpage].dirty = 1;
        invalidate_texture_cache(tpage);
    }
}

void R_ClearTextureDirty(int tpage)
{
    if (tpage >= 0 && tpage < TPAGE_COUNT) {
        s_tex[tpage].dirty = 0;
    }
}

void R_FreezeTexture(int tpage)
{
    R_UploadTexture(tpage);
}

void R_ThawTexture(int tpage)
{
    R_MarkTextureDirty(tpage);
}

void R_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h)
{
    upload_rgba_copy(tpage, rgba, w, h);
    free(rgba);
}

void R_SetNoColorKey(int tpage)
{
    if (tpage >= 0 && tpage < TPAGE_COUNT) {
        s_tex[tpage].noColorKey = 1;
        s_tex[tpage].dirty = 1;
        invalidate_texture_cache(tpage);
    }
}

void R_ClearNoColorKey(int tpage)
{
    if (tpage >= 0 && tpage < TPAGE_COUNT) {
        s_tex[tpage].noColorKey = 0;
        s_tex[tpage].dirty = 1;
        invalidate_texture_cache(tpage);
    }
}

void R_SetTpageGreen6(int tpage, int on)
{
    if (tpage >= 0 && tpage < TPAGE_COUNT) {
        s_tex[tpage].green6 = on;
        s_tex[tpage].dirty = 1;
        invalidate_texture_cache(tpage);
    }
}

void R_SetTpageRGBA8(int tpage, unsigned char *rgba)
{
    if (tpage < 0 || tpage >= TPAGE_COUNT) {
        free(rgba);
        return;
    }
    if (rgba) {
        int w = g_tpageWidth[tpage] > 0 ? g_tpageWidth[tpage] : 256;
        int h = g_tpageHeight[tpage] > 0 ? g_tpageHeight[tpage] : 256;
        upload_rgba_copy(tpage, rgba, w, h);
        s_tex[tpage].rgbaOverride = 1;
    }
    else if (s_tex[tpage].rgbaOverride) {
        free_texture(tpage);
        if (g_tpagePixelBuf[tpage] != NULL) {
            R_UploadTexture(tpage);
        }
        else {
            R_MarkTextureDirty(tpage);
        }
    }
    free(rgba);
}

void GL_KeepPixels(int tpage)
{
    (void)tpage;
}

void FinalizeMenuTexturesD3D(void)
{
}

void BeginFrame(void)
{
    flush_batch();
    begin_gu_if_needed();
    R_ResetState();
    s_scissorEnabled = 0;
}

void EndFrame(void)
{
}

static void submit_frame(void)
{
    flush_batch();
    if (!s_frameOpen) {
        drain_deferred_free();
        return;
    }
    sceGuFinish();
    sceGuSync(0, 0);
    s_frameOpen = 0;
    drain_deferred_free();
}

int R_IsFrameOpen(void)
{
    return s_frameOpen;
}

void R_EndFrame(void)
{
    EndFrame();
}

void FlipD3D(void)
{
    submit_frame();
    if (!s_guReady) {
        return;
    }
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    s_frameDirty = 1;
}

void R_Flip(void)
{
    FlipD3D();
}

void R_ClearDepth(void)
{
    flush_batch();
    begin_gu_if_needed();
    sceGuClearDepth(65535);
    sceGuDepthMask(GU_FALSE);
    sceGuClear(GU_DEPTH_BUFFER_BIT);
}

void R_ClearAndReset(void)
{
    clear_full_frame();
    R_ResetState();
    s_scissorEnabled = 0;
}

void RenderBackground(void)
{
    clear_full_frame();
}

void RenderWavingMenuBackground(void)
{
    int tpage = g_uiTexPage;

    if (tpage < 0 || tpage >= TPAGE_COUNT) {
        return;
    }
    if (g_tpageStateArray[tpage] != 4 || g_tpagePixelBuf[tpage] == NULL) {
        return;
    }

    struct IntVert {
        int sx;
        int sy;
        int uvU;
        int uvV;
    };
    struct IntVert buf[8][8];

    int sinAngle = (g_totalFrames & 0x7F) << 5;
    int yWorld = 0x483;
    int uvV = 0x8000;

    for (int row = 0; row < 8; row++) {
        int vertAngle = sinAngle;
        int xWorld = -0x604;
        int uvU = 0x8000;

        for (int col = 0; col < 8; col++) {
            int depth = 0x8CA - (g_sinTable[vertAngle] >> 7);

            buf[row][col].sx = g_screenCenterX +
                (g_projScaleXCurrent * xWorld) / depth;
            buf[row][col].sy = g_screenCenterY -
                (g_projScaleY * yWorld) / depth;
            buf[row][col].uvU = uvU;
            buf[row][col].uvV = uvV;

            vertAngle = (vertAngle - 0x14D) & 0xFFF;
            xWorld += 0x1B8;
            uvU += 0x246DB6;
        }

        sinAngle = (sinAngle - 0xDE) & 0xFFF;
        yWorld -= 0x14A;
        uvV += 0x246DB6;
    }

    R_PushState();
    R_SetTexture(tpage);
    R_SetTexEnv(R_TEXENV_MODULATE);
    R_SetBlendMode(R_BLEND_ALPHA);
    R_SetDepthTest(1);
    R_SetDepthWrite(0);
    R_SetDepthFunc(R_DEPTH_LEQUAL);

    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 7; col++) {
            const struct IntVert *src[4] = {
                &buf[row][col],
                &buf[row][col + 1],
                &buf[row + 1][col + 1],
                &buf[row + 1][col]
            };
            RenderVertex rv[4];

            for (int i = 0; i < 4; i++) {
                rv[i].sx = (float)src[i]->sx;
                rv[i].sy = (float)src[i]->sy;
                rv[i].sz = 0.99f;
                rv[i].rhw = 1.0f;
                rv[i].color = VERTEX_WHITE;
                rv[i].specular = 0;
                rv[i].u = g_uvLUT256[src[i]->uvU >> 16];
                rv[i].v = g_uvLUT256[src[i]->uvV >> 16];
            }

            R_DrawQuad(rv);
        }
    }

    R_PopState();
}

void ProcessTpageStates(void)
{
    int changed;

    clear_full_frame();

    do {
        changed = 0;
        for (int i = 0; i < TPAGE_COUNT; i++) {
            unsigned char state = (unsigned char)g_tpageStateArray[i];
            switch (state) {
                case 6:
                case 7:
                    free_texture(i);
                    if (state == 6) {
                        free(g_tpagePixelBuf[i]);
                        g_tpagePixelBuf[i] = NULL;
                    }
                    g_tpageStateArray[i] = 0;
                    break;

                case 1:
                case 2:
                    g_tpageStateArray[i] = 3;
                    changed = 1;
                    break;

                case 3:
                case 5:
                    R_MarkTextureDirty(i);
                    g_tpageStateArray[i] = 4;
                    changed = 1;
                    break;

                default:
                    break;
            }
        }
    } while (changed);
}

void CleanupD3DTPages(void)
{
    for (int i = 0; i < TPAGE_COUNT; i++) {
        char state = g_tpageStateArray[i];
        if (state == 0 || state == 5 || i == TPAGE_PLATFORM_ICONS_PSP) {
            continue;
        }
        g_tpageStateArray[i] = 6;
    }
    ProcessTpageStates();
}

void SetViewportFromConfig(int *config)
{
    if (!config) {
        return;
    }
    g_clipLeft = config[0];
    g_clipLeftDouble = g_clipLeft * 2;
    g_clipTop = config[1];
    g_clipRight = config[2];
    g_clipBottom = config[3];
    g_projScaleX = config[4];
    g_projScaleXCurrent = config[5];
    g_projScaleY = config[6];
    g_screenCenterX = config[7];
    g_screenCenterY = config[8];
    g_screenWidthFull = config[9];
    g_screenHeightFull = config[0xa];
    g_vpClipLeft10 = config[0xb];
    g_vpClipRight10 = config[0xc];
    g_vpClipLeft16 = config[0xd];
    g_vpClipRight16 = config[0xe];
    g_vpParam0F = config[0xf];
    g_vpParam10 = config[0x10];
}
