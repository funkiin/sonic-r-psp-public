/**
 * shutdown.c — Game shutdown sequence
 *
 * Called on exit — frees all resources in order.
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"

/* COM_CALL and vtable offsets defined in sonicr_types.h */
#define IUNKNOWN_RELEASE 0x08   /* alias for readability */

/* =====================================================================
 * Cleanup function implementations
 * ===================================================================== */

void QuitSonicR(void) {
    Shutdown();
}

void RestoreCDAutoRun(void) {
    /* Original restores CD autorun disabled at startup. Safe to no-op. */
}

void D3DAppDestroy(void) {
#if 0
    if (g_lpD3DDevice != NULL) {
        COM_CALL(g_lpD3DDevice, D3DDEV2_RELEASE);
        g_lpD3DDevice = NULL;
    }
    if (g_lpDD != NULL) {
        COM_CALL(g_lpDD, DD_RELEASE);
        g_lpDD = NULL;
    }
#endif
}

/**
 * CloseDirectInput — 0x00478104 — 124 bytes
 * Releases DirectInput keyboard, joystick devices, and DI object.
 */
static void *s_lpDirectInput;      /* _DAT_006758a4 — IDirectInputA* */
static void *s_lpDIKeyboard;       /* _DAT_006758a8 — IDirectInputDeviceA* */
static void *s_lpDIJoystick[4];    /* 0x006758ac — IDirectInputDeviceA* × 4 */

void CloseDirectInput(void)
{
    if (s_lpDirectInput == NULL) return;

    if (s_lpDIKeyboard != NULL) {
        COM_CALL(s_lpDIKeyboard, DIDEV_UNACQUIRE);
        COM_CALL(s_lpDIKeyboard, DIDEV_RELEASE);
        s_lpDIKeyboard = NULL;
    }

    int i;
    for (i = 0; i < 4; i++) {
        if (s_lpDIJoystick[i] != NULL) {
            COM_CALL(s_lpDIJoystick[i], DIDEV_UNACQUIRE);
            COM_CALL(s_lpDIJoystick[i], DIDEV_RELEASE);
            s_lpDIJoystick[i] = NULL;
        }
    }

    COM_CALL(s_lpDirectInput, DI_RELEASE);
    s_lpDirectInput = NULL;
}

void ClosePaletteStuff(void) {
    /* Releases palette-related DirectDraw objects. Only used in 8bpp. */
}

void FreeTrackModelData(void) {
    /* Frees track-specific model data buffers */
}

void FreeTextures(void) {
    /* Releases all D3D texture surfaces */
}

void UpdatePalette(void) {
    /* Updates the 8-bit palette. Only used in 8bpp mode. */
}

static void *s_parallaxBuffer1;     /* _DAT_008FB614 */
static void *s_parallaxBuffer2;     /* _DAT_008FB618 */

/**
 * Shutdown — 0x004CA2FC — 452 bytes
 * Frees all game resources and exits.
 * Protected by g_shutdownStarted to prevent double-shutdown.
 */
void Shutdown(void)
{
    if (g_shutdownStarted != 0) {
        return;
    }
    g_shutdownStarted = 1;

    StopCD();
    FreeTrackModelData();
    DebugLog("FreeTrackArrays done\n");

    if (g_demoMode != DEMO_NONE) {
        /* Save time trial position if in time trial mode */
        /* _DAT_008fd49c = _DAT_006da2a0; */
    }

    DebugLog("ShutDown...\n");
    SaveGameSettings();
    DebugLog("SaveGameSettings done\n");

    SavePadTypesImpl();
    DebugLog("SavePadTypes done\n");

    RestoreCDAutoRun();
    DebugLog("RestoreCDAutoRun done\n");

    CloseCDDevice();
    DebugLog("           CloseCDDevice done\n");

    if (s_parallaxBuffer1 != NULL) {
        free(s_parallaxBuffer1);
    }
    s_parallaxBuffer1 = NULL;
    DebugLog("           Parallax done\n");

    if (s_parallaxBuffer2 != NULL) {
        free(s_parallaxBuffer2);
    }
    s_parallaxBuffer2 = NULL;
    DebugLog("           Parallax2 done\n");

    DebugLog("           DrawList done\n");

    FreeTextures();
    DebugLog("           FreeTextures done\n");

    CloseDirectInput();
    DebugLog("           CloseDirectInput done\n");

    CloseDirectSound();
    DebugLog("           CloseDirectSound done\n");

    ClosePaletteStuff();
    DebugLog("           ClosePaletteStuff done\n");

    D3DAppDestroy();
    DebugLog("           D3DAppDestroy done\n");

    DebugLog("           done\n");

    exit(0);
}
