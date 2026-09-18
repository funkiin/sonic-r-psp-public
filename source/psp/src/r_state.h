/**
 * r_state.h — Render state API for the immediate-mode renderer.
 *
 * State changes take effect on the very next geometry submitted.
 * The backend uses a lazy state tracker to minimize redundant GL calls.
 */
#ifndef R_STATE_H
#define R_STATE_H

typedef enum { R_BLEND_NONE, R_BLEND_ALPHA, R_BLEND_ADDITIVE } R_BlendMode;
typedef enum { R_DEPTH_LEQUAL, R_DEPTH_LESS, R_DEPTH_ALWAYS } R_DepthFunc;
typedef enum { R_TEXENV_MODULATE, R_TEXENV_ADD_SIGNED } R_TexEnvMode;
typedef enum { R_FILTER_NEAREST, R_FILTER_LINEAR } R_FilterMode;
typedef enum { R_CULL_NONE, R_CULL_BACK, R_CULL_FRONT } R_CullMode;

/* Bind a texture page for subsequent geometry. -1 = untextured. */
void R_SetTexture(int tpageIndex);

/* Set blend mode. */
void R_SetBlendMode(R_BlendMode mode);

/* Enable/disable depth test. */
void R_SetDepthTest(int enable);

/* Set depth comparison function. */
void R_SetDepthFunc(R_DepthFunc func);

/* Enable/disable depth buffer writes. */
void R_SetDepthWrite(int enable);

/* Set texture environment (MODULATE vs ADD_SIGNED overbright). */
void R_SetTexEnv(R_TexEnvMode mode);

/* Set texture filter mode for the currently bound texture. */
void R_SetFilter(R_FilterMode mode);

/* Per-tpage filter override — pin a specific tpage to a filter mode
 * regardless of the global R_SetFilter setting. Used to keep sprite/
 * character atlases on point-sampling while bilinear is on globally,
 * avoiding atlas-edge bleed. SDL build is currently a no-op stub. */
void R_SetTpageFilter(int tpage, R_FilterMode mode);
void R_ClearTpageFilter(int tpage);

/* Per-tpage chroma gain applied when the texture is converted for upload,
 * 8.8 fixed point (256 = 1.0x, 0 = off). Offsets the saturation loss from the
 * additive offset-colour lift the DC uses in place of Add Signed. Must be set
 * before the tpage is uploaded. SDL build is a no-op stub. */
void R_SetTpageSatBoost(int tpage, int k256);

/* Set face culling mode. */
void R_SetCullMode(R_CullMode mode);

/* Enable/disable alpha test. */
void R_SetAlphaTest(int enable);

/* Set alpha test reference value. */
void R_SetAlphaRef(float ref);

/* Set scissor rectangle (for split-screen viewports). */
void R_SetScissor(int x, int y, int w, int h);

/* Disable scissor test. */
void R_DisableScissor(void);

/* Save/restore state (4-deep stack). */
void R_PushState(void);
void R_PopState(void);

/* Reset all state to defaults.
 *
 * SDL/GL calls this at the end of its own BeginFrame() (render_gl.c), so
 * render state there does not survive a frame boundary. The DC backend has NO
 * per-frame reset — PVR_BeginFrame touches no render state — so on DC any draw
 * that does not set its own inherits whatever the previous frame's last draw
 * left behind. That asymmetry is why the state-inheritance bugs are DC-only. */
void R_ResetState(void);

/* Flush pending state — apply only GL calls for state that changed. */
void R_FlushState(void);

/* DEBUG: read back the pending state a draw is about to use. Diagnostic only —
 * for confirming what state an unscoped draw call inherits. */
void R_DebugGetState(int *depthTest, int *depthWrite, int *depthFunc,
                     int *blendMode, int *alphaTest, float *alphaRef);

#endif /* R_STATE_H */
