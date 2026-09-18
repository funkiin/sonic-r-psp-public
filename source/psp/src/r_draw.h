/**
 * r_draw.h — Geometry submission API for the immediate-mode renderer.
 *
 * All functions emit geometry immediately using the current render state.
 * No batching — the backend draws to the GPU right away.
 */
#ifndef R_DRAW_H
#define R_DRAW_H

#include "r_types.h"
#include "vertex_struct.h"

/* Pre-clipped primitives (caller guarantees all verts are visible). */
void R_DrawTri(const RenderVertex v[3]);
void R_DrawQuad(const RenderVertex v[4]);
void R_DrawTriFan(const RenderVertex *v, int count);
void R_DrawQuadBatch(const RenderVertex *quads, int quadCount);

/* 2D textured quad (HUD, menus, sprites).
 * Position and UV in screen/texel coordinates. */
void R_DrawQuad2D(float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  float z, uint32_t color);

/* Untextured 2D quad (iris, sky fill, borders).
 * Temporarily disables texturing for this primitive. */
void R_DrawQuad2DSolid(float x0, float y0, float x1, float y1,
                       float z, uint32_t color);

/* Frame lifecycle */
void R_EndFrame(void);
void R_Flip(void);
void R_ClearAndReset(void);

#endif /* R_DRAW_H */
