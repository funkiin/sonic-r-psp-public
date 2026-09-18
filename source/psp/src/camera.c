/**
 * camera.c — Camera system
 *
 * BuildChaseCamera (FUN_004240bc) @ 0x004240bc — 608 bytes
 * ComputeLookAtAngles @ 0x004233c0 — 247 bytes
 * SetViewportClipRect @ 0x004CC298 — 215 bytes
 *
 * Per-frame camera pipeline:
 * 1. BuildChaseCamera computes camera position (behind + above player)
 * 2. ComputeLookAtAngles builds yaw/pitch from camera→player direction
 * 3. View matrix is built from the angles
 * 4. SetViewportClipRect copies camera struct into global view matrix
 *
 * SetViewportClipRect reads from a camera struct with this layout:
 *   [0]-[2]:  camera orientation vectors (DAT_006e9c84-8c)
 *   [3]:      g_camIntX (camera world X)
 *   [4]:      g_camIntY (camera world Y)
 *   [5]:      g_camIntZ (camera world Z)
 *   [6]-[8]:  more orientation (DAT_006e9c9c-a4)
 *   [9]-[11]: additional (DAT_006e9cc0-c8)
 *   [0xC]-[0x11]: secondary vectors (DAT_006e9ca8-bc)
 *   [0x12]+:  16 ints → view matrix at DAT_006e9c44
 */

#include "sonicr_types.h"
#include "sonicr_globals.h"
#include "sonicr_functions.h"
#include "player_struct.h"
#include <math.h>

extern int SampleTerrainAtCamera(int *pos, int param);
extern void ComputeLookAtAngles(Player *player, int *target, CamStateEntry *cam);

extern int g_camExtra6;
extern int g_camExtra7;
extern int g_camExtra8;
extern int g_camExtra9;
extern int g_camExtra10;
extern int g_camExtra11;
extern float g_camYawRad;
extern float g_camPitchRad;
extern float g_camFloatExtra5;

/* ROM waypoint sequence at 0x4FBEA4 — pairs of (type, param) as shorts */
static const short s_flyoverWaypoints[] = {
    2,0, 0,0, 1,-1, 0,0, 2,0, 0,0, 3,1, 0,0,
    2,0, 0,0, 1,1, 0,0, 3,-1, -1,0, 6,0, 7,0
};

/* Per-viewport camera state — 200 bytes (50 ints) per viewport.
 * Original: DAT_006e9924 + viewportIdx * 200 (0xC8).
 * Layout:
 *   [0]-[2]:   camera position (int, worldCoord << 12)
 *   [3]-[5]:   camera position (int, world units)
 *   [0x22]-[0x2D]: 3×4 view matrix (int, from sine table, stride 4 per row)
 *   [0x3C]:    camera yaw angle (stored as float bits via union)
 * Non-static so sky_render.c / ComputeParallaxAndPlayfieldState can access it. */
int s_cameraStruct[50];

/**
 * BuildChaseCamera — FUN_004240bc — 608 bytes
 * Faithful translation from binary. SOFTWARE PATH ONLY (called from
 * UpdateChaseCamera → SoftwareRenderScene). The D3D path uses
 * BuildCameraView (render_scene.c) for camera positioning.
 * Also called from track_init.c and player_init.c for initialization.
 *
 * Watcom fastcall: in_EAX = player, param_2 (EDX) = camStruct, unaff_EBX = (unused after save)
 *
 * Two modes:
 *   g_introCountdown > 0x1E (flyover): chase position + intro spline offset
 *   g_introCountdown <= 0x1E (gameplay): chase position + terrain-sampled Y
 *
 * Player positions are NEGATED: player[0] = -worldX * 4096.
 * Binary does (-player[0])>>12 to get worldX.
 */
void BuildChaseCamera(Player *player, CamStateEntry *camStruct)
{
    /* 0x4240cb: cam+0x16 = 0x10 (fixed constant) */
    camStruct->fovDetail = 0x10;

    /* Character distance: 0x4B0 for Eggman (4), 0x380 otherwise */
    int charId = player->charId;                              /* EAX at 0x4240d1 */
    int dist = (charId == CHAR_EGGMAN) ? 0x4B0 : 0x380;               /* EBX */

    /* Vertical offset: -0x30 for GP single-player, 0 otherwise */
    int vertOffset = 0;                                       /* [ebp-0x10] */
    if (g_isMultiRace != 0) {
        vertOffset = -0x30;               /* 0x4240f8 */
    }

    /* Player yaw → inverted yaw for "behind player" direction */
    int playerYaw = player->moveMode;
    int invYaw = (0xFFF - playerYaw) & 0xFFF;

    /* Signed divide by 65536 helper (matches binary pattern:
     * sar edx,0x1f; shl edx,0x10; sbb eax,edx; sar eax,0x10) */
    #define SDIV65536(val) ((int)(val) / 65536)

    int camX, camY, camZ;

    if (g_introCountdown > 0x1E) {
        /*- FLYOVER: chase position + intro spline offset- */
        /* 0x42410c-0x42414d: read spline position for this frame */
        int maxFrame;
        if (g_raceType == RACE_TIMEATTACK && g_raceSubMode == SUBMODE_REVERSE) {
            maxFrame = 0x186;  /* 390 */
        }
        else {
            maxFrame = 0xD2;   /* 210 */
        }

        int frameIdx = maxFrame - g_introCountdown;
        int *spline = (int *)g_introSplineBase;

        /* Spline: 3 ints per point (X, Y, Z). Negate X and Z. */
        int splineX = -spline[frameIdx * 3 + 0];             /* [ebp-0x14] */
        int splineY =  spline[frameIdx * 3 + 1];             /* [ebp-0x1c] */
        int splineZ = -spline[frameIdx * 3 + 2];             /* [ebp-0x18] */

        /* Camera X: worldX - sin(invYaw)*dist/65536 + splineX */
        int sinOff = SDIV65536(g_sinTable[invYaw] * dist);
        camX = ((-player->posX) >> 12) - sinOff + splineX;    /* [ebp-0x30] */

        /* Camera Y: vertOffset + 0x70 - player[1]>>12 + splineY */
        camY = (vertOffset + 0x70) - (player->posY >> 12) + splineY; /* [ebp-0x2c] */

        /* Camera Z: worldZ + cos(invYaw)*dist/65536 + splineZ */
        int cosOff = SDIV65536(g_cosTable[invYaw] * dist);
        camZ = cosOff + ((-player->posZ) >> 12) + splineZ;   /* [ebp-0x28] */
    }
    else {
        /*- GAMEPLAY: chase camera behind player- */
        /* Camera X: worldX - sin(invYaw)*dist/65536 */
        int sinOff = SDIV65536(g_sinTable[invYaw] * dist);
        camX = ((-player->posX) >> 12) - sinOff;              /* [ebp-0x30] */

        /* Camera Z: worldZ + cos(invYaw)*dist/65536 */
        int cosOff = SDIV65536(g_cosTable[invYaw] * dist);
        camZ = ((-player->posZ) >> 12) + cosOff;             /* [ebp-0x28] */

        /* Camera Y: sample terrain at camera XZ, clamp above player Y */
        /* 0x42427e-0x424291: call SampleTerrainAtCamera */
        int tempPos[3];
        tempPos[0] = camX;
        tempPos[2] = camZ;
        tempPos[1] = 0;  /* Y doesn't matter for XZ terrain sample */
        int terrainResult = SampleTerrainAtCamera(tempPos,
                            player->collisionLayer);
        camY = (-terrainResult) >> 12;                        /* 0x42428c-0x42428e */

        /* Clamp: camera Y must be at least as high as player world Y */
        int worldY = (-player->posY) >> 12;                   /* 0x424294-0x42429c */
        if (worldY > camY) {
            camY = worldY;                     /* 0x42429f-0x4242a3 */
        }

        /* Add vertical offset */
        camY += vertOffset + 0x70;                             /* 0x4242a6-0x4242b1 */
    }

    #undef SDIV65536

    /*- Common: compute look-at angles- */
    /* 0x4242b4-0x4242bb: call ComputeLookAtAngles(EAX=player, EDX=&camPos, EBX=camStruct) */
    int camPos[3] = { camX, camY, camZ };
    /* Binary: EAX=player (0x4242b9: mov eax,esi ; esi=player param from
     * 0x4240c5), EDX=&camPos, EBX=camStruct — NOT g_playerBase/player[0]. */
    ComputeLookAtAngles(player, camPos, camStruct);

    /* Store world-unit camera position into int slots [3],[4],[5].
     * In the binary these overwrote the angle shorts at +0x0C-0x17 with
     * 32-bit position values.  The struct now has named short fields there,
     * so decompose each int into its low/high 16 to match LE binary layout.
     * ComputeLookAtAngles set posX/Y/Z (<<4); these set world-unit coords.
     * BuildCameraView re-computes angles each frame, so these seed values
     * only affect the first smoothing frame. */
    camStruct->smoothYaw    = (short)camX;
    camStruct->smoothPitch  = (short)(camX >> 16);
    camStruct->_unk_0x10    = (short)camY;
    camStruct->targetYaw    = (short)(camY >> 16);
    camStruct->targetPitch  = (short)camZ;
    camStruct->fovDetail    = (short)(camZ >> 16);

    /* 0x4242c0-0x424302: save smoothed camera state for race_dispatch.c distance calcs */
    g_savedCamPitch = g_gpSmoothedCam.smoothYaw;              /* 0x4242C0 */
    g_savedCamYaw   = g_gpSmoothedCam.smoothPitch;            /* 0x4242CC */
    g_savedCamX     = g_gpSmoothedCam.posX;                   /* 0x4242D8 */
    g_savedCamY     = g_gpSmoothedCam.posY;                   /* 0x4242E2 */
    g_savedCamZ     = g_gpSmoothedCam.posZ;                   /* 0x4242F3 */

    /* 0x4242e7: cam+0x20 = 0; cam+0x24 = 0 */
    camStruct->stateCounter = 0;
    camStruct->initFlag = 0;

    /* 0x424304-0x424311: write default yaw/pitch indices from ROM
     * cam+0x18 = 0x300 (768 = default yaw), cam+0x1C = 0 (default pitch)
     * These get overwritten by the rendering pipeline (BuildCameraView). */
    camStruct->camDist = 0x300;
    camStruct->camHeight = 0;
}

#ifdef SONICR_DC
#define RADSCALE 651.8986469f
#else
#define RADSCALE 651.8986469
#endif

/**
 * ComputeLookAtAngles — 0x004233C0 — 247 bytes — VALIDATED
 * Computes yaw and pitch angles from player position to a target,
 * stores target (<<4) and angles into camera struct.
 *
 * Watcom fastcall: EAX=player, EDX=target (int[3]), EBX=cam struct.
 *
 * Binary uses atan2 wrapper at 0x4E08FD (swapped args: fild A; fild B; call = atan2(A,B))
 * and Watcom float truncation at 0x4E08CA. Scale: atan2 * 4096 * 1/(2π).
 */
void ComputeLookAtAngles(Player *player, int *target, CamStateEntry *cam)
{
    /* Store target position << 4 into cam struct */
    cam->posX = target[0] << 4;
    cam->posY = target[1] << 4;
    cam->posZ = target[2] << 4;

    /* Relative direction: -(player >> 12) - (cam >> 4) */
    int relX = -(player->posX >> 12) - (cam->posX >> 4);
    int relY = -(player->posY >> 12) - (cam->posY >> 4) + 0x28;
    int relZ = -(player->posZ >> 12) - (cam->posZ >> 4);

    /* Yaw: atan2(relX, relZ) * 4096/(2π) + 0x800 — binary fild order: relZ(A), relX(B) → atan2(B,A) */
    sr_double yawRad = sr_atan2((sr_double)relX, (sr_double)relZ);
    int yawIdx = (int)(yawRad * RADSCALE);
    yawIdx = ((yawIdx << 4) >> 4) + 0x800;
    cam->targetPitch = (short)yawIdx;

    /* Pitch: atan2(distXZ², yFactor) * 4096/(2π), negated */
    int distSq = relZ * relZ + relX * relX;
    int yFactor;
    if (relY < 0) {
        yFactor = relY * relY;       /* positive when below */
    }
    else {
        yFactor = -(relY) * relY;    /* negative when above: neg(relY)*relY */
    }

    sr_double pitchRad = sr_atan2((sr_double)yFactor, (sr_double)distSq);
    int pitchIdx = (int)(pitchRad * RADSCALE);
    pitchIdx = -((pitchIdx << 4) >> 4);
    cam->targetYaw = (short)pitchIdx;

    /* Copy computed angles to smooth fields */
    cam->smoothYaw = cam->targetYaw;
    cam->smoothPitch = cam->targetPitch;
}

/**
 * SetViewportClipRect( — 0x004CC298 — 215 bytes
 * Copies camera/viewport parameters into render state globals.
 */
void SetViewportClipRect(int *camBlock)
{
    if (camBlock == NULL) {
        return;
    }

    /* Copy camera orientation vectors (camBlock[0]-[2]) — 0x4CC2A5-0x4CC2C0 */
    g_camOrientX = camBlock[0];
    g_camOrientY = camBlock[1];
    g_camOrientZ = camBlock[2];

    /* Copy camera world position (camBlock[3]-[5]) — 0x4CC2C8-0x4CC2EF */
    g_camIntX = camBlock[3];
    g_camIntY = camBlock[4];
    g_camIntZ = camBlock[5];

    /* Copy extra int vectors (camBlock[6]-[8]) — 0x4CC2F5-0x4CC320 */
    g_camExtra6  = camBlock[6];    /* → 0x6E9C9C */
    g_camExtra7  = camBlock[7];    /* → 0x6E9CA0 */
    g_camExtra8  = camBlock[8];    /* → 0x6E9CA4 */

    /* Copy extra int vectors (camBlock[9]-[11]) — 0x4CC326-0x4CC341 */
    g_camExtra9  = camBlock[9];    /* → 0x6E9CC0 */
    g_camExtra10 = camBlock[10];   /* → 0x6E9CC4 */
    g_camExtra11 = camBlock[11];   /* → 0x6E9CC8 */

    /* Copy the float camera fields (RenderCamera +0x30..0x44 → 0x6E9CA8..0x6E9CBC).
     * Binary: fld dword [eax+0x30..0x44] → fstp dword [0x6E9CA8..0x6E9CBC]. */
    RenderCamera *rc = (RenderCamera *)camBlock;
    g_camFloatX      = rc->camFloatX;            /* +0x30 → 0x6E9CA8 */
    g_camFloatY      = rc->camFloatY;            /* +0x34 → 0x6E9CAC */
    g_camFloatZ      = rc->camFloatZ;            /* +0x38 → 0x6E9CB0 */
    g_camYawRad = rc->yawFloat;             /* +0x3C → 0x6E9CB4 */
    g_camPitchRad = rc->pitchFloat;           /* +0x40 → 0x6E9CB8 */
    g_camFloatExtra5 = *(float *)&camBlock[17];  /* +0x44 → 0x6E9CBC — _tail[0], no named field */

    /* Copy integer view matrix from camBlock (byte offset 0x48).
     * Binary does: rep movsd with ecx=0x10, copying 16 dwords (64 bytes).
     * Both source (camera+0x48) and dest (0x6E9C44) use stride-4 layout. */
    int *isrc = camBlock + 0x12;  /* byte offset 0x48 */
    for (int i = 0; i < 16; i++) {
        g_viewMtx[i] = isrc[i];
    }

    /* Copy 16 ints from camBlock+0x22 (byte offset 0x88, FLOAT view matrix)
     * to float matrix at 0x0068B280. */
    int *fsrc = camBlock + 0x22;
    for (int i = 0; i < 16; i++) {
        g_floatMtxDest[i] = fsrc[i];
    }

#ifdef SONICR_DC
    /* DC track path (RenderTrackD3D, RenderEnvMappedModel3D, etc.) reads
     * g_viewMtxF[] for per-vertex transforms. BuildD3DViewMatrix populates
     * it once per build, but build runs per-viewport and the LAST one wins;
     * without refreshing here, player 1's track render uses player 2's
     * stale matrix (manifests as world-orbits-around-camera in 2P split,
     * tile-grid which uses g_floatMtxDest[] is unaffected).
     *
     * Same source (cam+0x88) and same lane mapping as view_matrix.c. */
    float *src_f = (float *)(camBlock + 0x22);
    g_viewMtxF[0]  = src_f[0];
    g_viewMtxF[1]  = src_f[1];
    g_viewMtxF[2]  = src_f[2];
    g_viewMtxF[4]  = src_f[4];
    g_viewMtxF[5]  = src_f[5];
    g_viewMtxF[6]  = src_f[6];
    g_viewMtxF[8]  = src_f[8];
    g_viewMtxF[9]  = src_f[9];
    g_viewMtxF[10] = src_f[10];
    g_viewMtxF[3] = 0.0f;
    g_viewMtxF[7] = 0.0f;
    g_viewMtxF[11] = 0.0f;
    g_viewMtxF[12] = 0.0f;
    g_viewMtxF[13] = 0.0f;
    g_viewMtxF[14] = 0.0f;
    g_viewMtxF[15] = 0.0f;
#endif
}
/* =====================================================================
 * UpdateFlyoverCamera — FUN_004243ac — 1988 bytes — VALIDATED
 *
 * Flyover/intro camera state machine. Called each frame during the
 * pre-race camera flyover sequence. Sequences through waypoints from
 * a ROM table, with 4 camera behavior modes:
 *   0: Static look — find nearest waypoint by 3D distance
 *   1: Forward-facing — camera ahead of player along heading
 *   2: Steering-offset — rotated view with yaw offset
 *   3: Rear-facing — camera behind player
 *
 * Interpolates camera position (lerp at 1/3 rate), computes yaw/pitch
 * via atan2, outputs to camera struct with float conversion.
 *
 * Watcom fastcall: EAX=player, EDX=camStruct, EBX=outputStruct, ECX=waypointTable
 * Stack: [ebp+8]=param5, [ebp+0xC]=viewportIndex. ret 8.
 * ===================================================================== */
void UpdateFlyoverCamera(Player *player, CamStateEntry *cam, RenderCamera *outStruct,
                          int *waypointTable, int __attribute__((unused)) param5, int vpIdx)
{
    int changed = 0;

    /* Timer countdown and waypoint sequencing */

    int timer = --g_flyoverTimer[vpIdx];
    if (timer < 0) {
        g_flyoverTimer[vpIdx] = 0x78;  /* reset to 120 frames */

        /* Read next waypoint type from ROM sequence */
        int wpIdx = g_flyoverWpIdx[vpIdx];
        int mode = (int)s_flyoverWaypoints[wpIdx];
        g_flyoverMode[vpIdx] = mode;

        if (mode == -1) {
            /* End sentinel — wrap to start */
            g_flyoverWpIdx[vpIdx] = 0;
            mode = (int)s_flyoverWaypoints[0];
            g_flyoverMode[vpIdx] = mode;
        }

        /* Re-read wpIdx after potential sentinel reset (binary 0x424419) */
        wpIdx = g_flyoverWpIdx[vpIdx];

        /* Read steering param: second short of the pair (binary 0x42441F: dword sar 16) */
        int param = (int)s_flyoverWaypoints[wpIdx + 1];
        g_flyoverParam[vpIdx] = param;

        g_flyoverWpIdx[vpIdx] = wpIdx + 2;

        if (mode != 0) {
            g_flyoverNearest[vpIdx] = -1;
        }
        changed = 1;
    }

    /* Check if player has moved (force mode 0) */
    int moveState = player->loopMode;
    if (moveState != 0 && g_flyoverMode[vpIdx] != 0) {
        changed = 1;
        g_flyoverMode[vpIdx] = 0;
        g_flyoverNearest[vpIdx] = -1;
    }

    /* Camera target computation (switch on mode) */
    int targetX = 0, targetY = 0, targetZ = 0;  /* [ebp-0x4c], [ebp-0x48], [ebp-0x44] */

    int mode = g_flyoverMode[vpIdx];
    switch (mode) {
        case 0: {
            /* Static look — check timer ticks for SFX triggers */
            int t = g_flyoverTimer[vpIdx];
            if (t == 0x5A || t == 0x3C || t == 0x1E) {
                changed = 1;
            }

            if (changed) {
                /* Find nearest waypoint by 3D distance */
                int numWP = g_numWaypoints * 3;
                int bestDist = 0x7FFFFFFF;
                int bestIdx = 0;

                if (numWP > 0) {
                    int px = player->posX >> 12;
                    int py = -(player->posY >> 12);
                    int pz = player->posZ >> 12;

                    for (int idx = 0; idx < numWP; idx += 3) {
                        int dx = px - waypointTable[idx];
                        int dy = py - waypointTable[idx + 1];
                        int dz = pz - waypointTable[idx + 2];
                        int dist = dx*dx + dy*dy + dz*dz;
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestIdx = idx;
                        }
                    }
                }

                if (bestIdx == g_flyoverNearest[vpIdx]) {
                    changed = 0;
                } else {
                    g_flyoverNearest[vpIdx] = bestIdx;
                }
            }

            /* Read target from waypoint table */
            int wpBase = g_flyoverNearest[vpIdx];
            targetX = -waypointTable[wpBase];
            targetY = waypointTable[wpBase + 1];
            targetZ = -waypointTable[wpBase + 2];
            break;
        }

        case 1: {
            /* Forward-facing camera — compute position ahead of player */
            int angle = (0xFFF - player->angleYaw) & 0xFFF;
            int sinA = g_sinTable[angle];
            int cosA = g_cosTable[angle];

            targetX = -(player->posX >> 12) - (sinA * 0x44C) / 65536;
            targetZ = -(player->posZ >> 12) + (cosA * 0x44C) / 65536;

            /* Height from terrain */
            int terrainPos[3] = { targetX, 0, targetZ };
            int terrainH = SampleTerrainAtCamera(terrainPos, player->collisionLayer);
            targetY = -(terrainH >> 12);

            /* Clamp to player Y if needed */
            int playerY = -(player->posY >> 12);
            if (playerY > targetY) {
                targetY = playerY;
            }
            targetY += 0x70;

            /* 0x4235f8: cmp [g_renderMode], 1 — mode 1 is Direct3D, so this
            * near-ground y-boost is the *hardware* camera's. We run
            * RENDER_SOFT, which takes the else path and skips it. Define
            * ENABLE_D3D_CAMERA for the D3D framing (higher, pulled back). */
    #ifdef ENABLE_D3D_CAMERA
            if (player->posY > (int)0xFFFF0000) {
                targetY += 0x64;
            }
    #endif

            /* Steering drift */
            int steer = g_flyoverParam[vpIdx];
            if (steer > 0) {
                if (steer > 0x320) targetY += steer - 0x190;
                g_flyoverParam[vpIdx] += 0x10;
            }
            if (steer < 0) {
                if (steer > -0x320) targetY += steer + 0x320;
                g_flyoverParam[vpIdx] -= 0x20;
            }
            break;
        }

        case 2: {
            /* Steering-offset view */
            int heading = player->angleYaw;
            int steerOff = g_flyoverParam[vpIdx];
            int angle = (0xFFF - heading - steerOff) & 0xFFF;
            int sinA = g_sinTable[angle];
            int cosA = g_cosTable[angle];

            targetX = -(player->posX >> 12) - (sinA * 0x44C) / 65536;
            targetZ = -(player->posZ >> 12) + (cosA * 0x44C) / 65536;

            int terrainPos[3] = { targetX, 0, targetZ };
            int terrainH = SampleTerrainAtCamera(terrainPos, player->collisionLayer);
            targetY = -(terrainH >> 12);

            int playerY = -(player->posY >> 12);
            if (playerY > targetY) {
                targetY = playerY;
            }
            targetY += 0x70;

            /* D3D-mode y-boost — see case 1 comment. */
    #ifdef ENABLE_D3D_CAMERA
            if (player->posY > (int)0xFFFF0000) {
                targetY += 0x64;
            }
    #endif

            /* Advance steering offset (wrap to 12 bits) */
            steerOff = (steerOff + 0x20) & 0xFFF;
            g_flyoverParam[vpIdx] = steerOff;
            break;
        }

        case 3: {
            /* Rear-facing camera */
            int angle = (0xFFF - player->angleYaw) & 0xFFF;
            int sinA = g_sinTable[angle];
            int cosA = g_cosTable[angle];

            targetX = -(player->posX >> 12) + (sinA * 0x44C) / 65536;
            targetZ = -(player->posZ >> 12) - (cosA * 0x44C) / 65536;

            int terrainPos[3] = { targetX, 0, targetZ };
            int terrainH = SampleTerrainAtCamera(terrainPos, player->collisionLayer);
            targetY = -(terrainH >> 12);

            int playerY = -(player->posY >> 12);
            if (playerY > targetY) {
                targetY = playerY;
            }
            targetY += 0x70;

            /* D3D-mode y-boost — see case 1 comment. */
    #ifdef ENABLE_D3D_CAMERA
            if (player->posY > (int)0xFFFF0000) {
                targetY += 0x64;
            }
    #endif

            int steer = g_flyoverParam[vpIdx];
            if (steer > 0) {
                if (steer > 0x320) {
                    targetY += steer - 0x190;
                }
                g_flyoverParam[vpIdx] += 0x10;
            }
            if (steer < 0) {
                if (steer > -0x320) {
                    targetY += steer + 0x320;
                }
                g_flyoverParam[vpIdx] -= 0x20;
            }
            break;
        }
    }

    /* call ComputeLookAtAngles if waypoint changed */
    if (changed) {
        int targetPos[3] = { targetX, targetY, targetZ };
        ComputeLookAtAngles(player, targetPos, cam);
    }

    /* Interpolate camera position toward target */
    /* Lerp at 1/3 rate: cam -= (cam - target*16) / 3 */
    cam->posX -= (cam->posX - (targetX << 4)) / 3;
    cam->posY -= (cam->posY - (targetY << 4)) / 4;
    cam->posZ -= (cam->posZ - (targetZ << 4)) / 3;

    /* Compute camera angles via atan2 */
    int relX = -(player->posX >> 12) - (cam->posX >> 4);
    int relY = -(player->posY >> 12) - (cam->posY >> 4) + 0x28;
    int relZ = -(player->posZ >> 12) - (cam->posZ >> 4);

    /* Check for splitscreen vertical offset adjustment */
    if (g_numViewports == 2 && g_viewportIndex == 0 &&
        g_netSessionActive == 0) {
        relY -= 0x28;
    }

    /* Yaw angle: fild relZ(A), fild relX(B) → atan2(B,A) = atan2(relX, relZ) */
    sr_double yawRad = sr_atan2((sr_double)relX, (sr_double)relZ);
    int yawIdx = (int)(yawRad * 4096.0 * (1.0 / (2.0 * 3.14159265358979323846)));
    yawIdx = (yawIdx << 4) >> 4;  /* sign-extend 28→32 bits */
    yawIdx += 0x800;
    cam->targetPitch = (short)yawIdx;

    /* Pitch angle: atan2(dist², relY_factor) → 12-bit angle */
    int distSq = relX * relX + relZ * relZ;
    int yFactor;
    if (relY >= 0) {
        yFactor = -(relY * relY);
    }
    else {
        yFactor = relY * relY;
    }

    if (relY >= 0) {
        yFactor = -relY * relY;
    }
    else {
        yFactor = relY * relY;
    }

    sr_double pitchRad = sr_atan2((sr_double)yFactor, (sr_double)distSq);
    int pitchIdx = (int)(pitchRad * 4096.0 * (1.0 / (2.0 * 3.14159265358979323846)));
    pitchIdx = (pitchIdx << 4) >> 4;
    pitchIdx = -pitchIdx;
    cam->targetYaw = (short)pitchIdx;

    /* Smooth heading (yaw chase) */
    int curYaw = cam->targetYaw;
    int tgtYaw = cam->smoothYaw;
    int diff = (tgtYaw - curYaw) & 0xFFF;
    if (diff >= 0x800) {
        diff = -(0x1000 - diff);
    }
    int smoothYaw = cam->smoothYaw - (diff >> 2);
    cam->smoothYaw = (short)smoothYaw;
    cam->smoothYaw &= 0x0FFF;

    /* Smooth pitch similarly */
    int curPitch = cam->smoothPitch;
    int tgtPitch = cam->targetPitch;
    int diffP = (curPitch - tgtPitch) & 0xFFF;
    if (diffP >= 0x800) {
        diffP = -(0x1000 - diffP);
    }
    int smoothPitch = cam->smoothPitch - (diffP >> 2);
    cam->smoothPitch = (short)smoothPitch;
    cam->smoothPitch &= 0x0FFF;

    /* Part 8: Output to camera struct */
    /* Convert camera position to world coords */
    outStruct->worldX = -(cam->posX) << 8;
    outStruct->worldY = cam->posY << 8;
    outStruct->worldZ = -(cam->posZ) << 8;

    /* Copy angles */
    outStruct->yaw = cam->smoothYaw;
    int camPitchOut = cam->smoothPitch;
    outStruct->pitch = (-camPitchOut) & 0xFFF;

    /* Clamp Y */
    if (outStruct->worldY < 0x19000) {
        outStruct->worldY = 0x19000;
    }

    /* Convert to float for D3D */
    sr_double posScale = 1.0 / 4096.0;   /* 0x51FCD0 */
    sr_double angScale = 6.283185307 / 4096.0;  /* 0x51FCD8 ≈ 2π/4096 */

    outStruct->camFloatX      = (float)(outStruct->worldX * posScale);
    outStruct->camFloatY      = (float)(outStruct->worldY * posScale);
    outStruct->camFloatZ      = (float)(outStruct->worldZ * posScale);
    outStruct->yawFloat  = (float)(outStruct->yaw * angScale);
    outStruct->pitchFloat = (float)(outStruct->pitch * angScale);

    /* Integer >>12 versions */
    outStruct->intX = outStruct->worldX >> 12;
    outStruct->intY = outStruct->worldY >> 12;
    outStruct->intZ = outStruct->worldZ >> 12;

    /* Camera matrix updates — use the same functions as BuildCameraView */
    SetCameraStructPtr((int *)outStruct);
    BuildViewMatrix();
    ComputeCameraBasis();
    BuildD3DViewMatrix();
}
