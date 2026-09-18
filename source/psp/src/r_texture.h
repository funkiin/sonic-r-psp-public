/**
 * r_texture.h — Texture management API for the immediate-mode renderer.
 *
 * Abstracts backend-specific texture upload, dirty tracking, and per-tpage
 * flags. Game-side pixel buffers (g_tpagePixelBuf[], texture.c) are read
 * by the backend during upload — they stay in texture.c.
 */
#ifndef R_TEXTURE_H
#define R_TEXTURE_H

/* Initialize texture system (creates backend texture objects). */
void R_InitTextures(void);

/* Upload a tpage's 16bpp pixel buffer to the backend.
 * Handles color-key transparency, shadow softening, pending RGBA overrides.
 * Called lazily by R_SetTexture() when the tpage is dirty. */
void R_UploadTexture(int tpage);

/* Upload pre-built RGBA data directly (bypasses 16bpp conversion).
 * Used for tinted backgrounds. */
void R_UploadTextureRGBA(int tpage, unsigned char *rgba, int w, int h);

/* Upload a sub-rectangle of a tpage (for minimap patches). */
void R_UploadTextureSubRect(int tpage, int x, int y, int w, int h);

/* Mark a tpage as needing re-upload on next use. */
void R_MarkTextureDirty(int tpage);

/* Clear dirty flag without uploading (for freeze-then-patch flow). */
void R_ClearTextureDirty(int tpage);

/* Upload current pixel buffer and lock it (freeze for sub-rect patching). */
void R_FreezeTexture(int tpage);

/* Set pending full-quality RGBA data for lazy upload (TintBackgroundTPage). */
void R_SetPendingRGBA(int tpage, unsigned char *rgba, int w, int h);

/* Disable color-key transparency for a tpage (env maps, wallpapers). */
void R_SetNoColorKey(int tpage);

/* Re-enable color-key transparency for a tpage (after credits wallpaper). */
void R_ClearNoColorKey(int tpage);

/* Mark a tpage's pixel buffer as carrying true 6-bit green at bits 10:5 rather
 * than the game's R5 G5 pad B5. Set before upload, after ClaimTpage (which
 * clears it). Only the no-key upload path honours it. */
void R_SetTpageGreen6(int tpage, int on);

/* Register a persistent full-quality RGBA8 buffer for a tpage (32-bit sky).
 * Desktop GL only; DC keeps the 16bpp tpage. Takes ownership of rgba. */
void R_SetTpageRGBA8(int tpage, unsigned char *rgba);

#endif /* R_TEXTURE_H */
