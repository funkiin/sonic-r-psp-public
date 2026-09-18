#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <psptypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "pad_bits.h"
#include "gamepad_buttons.h"

PSP_MODULE_INFO("SonicRPSP", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define PSP_ANALOG_CENTER 128
#define PSP_ANALOG_DEADZONE 48
#define JOY_BUTTONS_PER_SLOT 80
#define JOY_CFG_MAX 32

unsigned char s_keystate[256];

extern unsigned char g_keyPressState[320];
extern short g_joystickConfigWords[];
extern char g_joystickSlots[4][282];
extern char g_joystickDeviceNames[4][260];
extern short g_joystickDeviceFlags[8];

static int s_quitRequested;
static uint32_t s_startTick;
static SceCtrlData s_lastPad;

static int analog_left(const SceCtrlData *pad)  { return pad->Lx < PSP_ANALOG_CENTER - PSP_ANALOG_DEADZONE; }
static int analog_right(const SceCtrlData *pad) { return pad->Lx > PSP_ANALOG_CENTER + PSP_ANALOG_DEADZONE; }
static int analog_up(const SceCtrlData *pad)    { return pad->Ly < PSP_ANALOG_CENTER - PSP_ANALOG_DEADZONE; }
static int analog_down(const SceCtrlData *pad)  { return pad->Ly > PSP_ANALOG_CENTER + PSP_ANALOG_DEADZONE; }

static void poll_pad(void)
{
    sceCtrlPeekBufferPositive(&s_lastPad, 1);
}

static int psp_button_held(const SceCtrlData *pad, int buttonIndex)
{
    switch (buttonIndex) {
        case GCBTN_A:             return (pad->Buttons & PSP_CTRL_CROSS) != 0;
        case GCBTN_B:             return (pad->Buttons & PSP_CTRL_CIRCLE) != 0;
        case GCBTN_X:             return (pad->Buttons & PSP_CTRL_SQUARE) != 0;
        case GCBTN_Y:             return (pad->Buttons & PSP_CTRL_TRIANGLE) != 0;
        case GCBTN_START:         return (pad->Buttons & PSP_CTRL_START) != 0;
        case GCBTN_BACK:          return (pad->Buttons & PSP_CTRL_SELECT) != 0;
        case GCBTN_LEFTSHOULDER:  return (pad->Buttons & PSP_CTRL_LTRIGGER) != 0;
        case GCBTN_RIGHTSHOULDER: return (pad->Buttons & PSP_CTRL_RTRIGGER) != 0;
        case GCBTN_DPAD_UP:       return (pad->Buttons & PSP_CTRL_UP) != 0 || analog_up(pad);
        case GCBTN_DPAD_DOWN:     return (pad->Buttons & PSP_CTRL_DOWN) != 0 || analog_down(pad);
        case GCBTN_DPAD_LEFT:     return (pad->Buttons & PSP_CTRL_LEFT) != 0 || analog_left(pad);
        case GCBTN_DPAD_RIGHT:    return (pad->Buttons & PSP_CTRL_RIGHT) != 0 || analog_right(pad);
        case GCBTN_TRIGGER_LEFT:  return (pad->Buttons & PSP_CTRL_LTRIGGER) != 0;
        case GCBTN_TRIGGER_RIGHT: return (pad->Buttons & PSP_CTRL_RTRIGGER) != 0;
        default:                  return 0;
    }
}

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    s_quitRequested = 1;
    exit(0);
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    SceUID thid = sceKernelCreateThread("update_thread", callback_thread,
                                        0x11, 0xFA0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }
}

static void psp_update_input(unsigned char *keys, int keyCount)
{
    if (keyCount >= 256) {
        memset(keys, 0, (size_t)keyCount);
    }

    const SceCtrlData *pad = &s_lastPad;
    if (pad->Buttons & PSP_CTRL_START) {
        keys[DIK_RETURN] = 0x80;
    }
    if (pad->Buttons & PSP_CTRL_SELECT) {
        keys[DIK_ESCAPE] = 0x80;
    }
    if (pad->Buttons & PSP_CTRL_CROSS) {
        keys[DIK_SPACE] = 0x80;
    }
    if ((pad->Buttons & PSP_CTRL_CIRCLE) || (pad->Buttons & PSP_CTRL_SQUARE)) {
        keys[DIK_A] = 0x80;
    }
    if (pad->Buttons & PSP_CTRL_LTRIGGER) {
        keys[DIK_Z] = 0x80;
    }
    if (pad->Buttons & PSP_CTRL_RTRIGGER) {
        keys[DIK_X] = 0x80;
    }
    if (pad->Buttons & PSP_CTRL_TRIANGLE) {
        keys[DIK_1] = 0x80;
    }
    if ((pad->Buttons & PSP_CTRL_UP) || analog_up(pad)) {
        keys[DIK_UP] = 0x80;
    }
    if ((pad->Buttons & PSP_CTRL_DOWN) || analog_down(pad)) {
        keys[DIK_DOWN] = 0x80;
    }
    if ((pad->Buttons & PSP_CTRL_LEFT) || analog_left(pad)) {
        keys[DIK_LEFT] = 0x80;
    }
    if ((pad->Buttons & PSP_CTRL_RIGHT) || analog_right(pad)) {
        keys[DIK_RIGHT] = 0x80;
    }
}

int platform_init(int width, int height, int fullscreen, const char *title)
{
    (void)width;
    (void)height;
    (void)fullscreen;
    (void)title;

    pspDebugScreenInit();
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    s_startTick = sceKernelGetSystemTimeLow() / 1000u;
    return 0;
}

void platform_shutdown(void)
{
}

const char *platform_base_path(void)
{
    pspDebugScreenInit();
    pspDebugScreenPrintf("Sonic R PSP boot\n");
    return ".";
}

int platform_poll_events(unsigned char *keystateOut, int keystateSize)
{
    poll_pad();
    psp_update_input(keystateOut, keystateSize);
    return s_quitRequested;
}

int platform_init_gamepads(void)
{
    memset(g_joystickDeviceNames[0], 0, 260);
    strncpy(g_joystickDeviceNames[0], "PSP Controller", 259);
    g_joystickDeviceFlags[0] = JOY_SLOT_CFG_WORDS;
    return 1;
}

int platform_poll_gamepads(unsigned short *joySlotState, int maxSlots)
{
    if (maxSlots <= 0) {
        return 0;
    }

    poll_pad();
    const SceCtrlData *pad = &s_lastPad;

    unsigned short state = 0;
    unsigned char *pressBase = &g_keyPressState[0];
    const short *slotCfg = (const short *)&g_joystickSlots[0][0x104];

    for (int b = 0; b < GC_BUTTON_COUNT; b++) {
        int held = psp_button_held(pad, b);
        pressBase[b] = held ? 0x80 : 0x00;
        if (!held) {
            continue;
        }

        short cfg;
        if (b < JOY_SLOT_CFG_WORDS) {
            cfg = slotCfg[b];
        }
        else if (b < JOY_CFG_MAX) {
            cfg = g_joystickConfigWords[b];
        }
        else {
            cfg = 0;
        }
        state |= (unsigned short)cfg;
    }
    for (int b = GC_BUTTON_COUNT; b < JOY_BUTTONS_PER_SLOT; b++) {
        pressBase[b] = 0;
    }

    joySlotState[0] = state;
    for (int i = 1; i < maxSlots; i++) {
        joySlotState[i] = 0;
    }
    for (int i = 1; i < 4; i++) {
        memset(&g_keyPressState[i * JOY_BUTTONS_PER_SLOT], 0, JOY_BUTTONS_PER_SLOT);
    }
    return 1;
}

uint32_t platform_get_time_ms(void)
{
    return (sceKernelGetSystemTimeLow() / 1000u) - s_startTick;
}

void platform_sleep_ms(int ms)
{
    if (ms > 0) {
        sceKernelDelayThread((SceUInt)ms * 1000u);
    }
}

int platform_audio_init(void)
{
    return 0;
}

void platform_audio_shutdown(void)
{
}

void platform_gl_swap(void)
{
    sceDisplayWaitVblankStart();
}

void platform_get_drawable_size(int *w, int *h)
{
    if (w) {
        *w = 480;
    }
    if (h) {
        *h = 272;
    }
}

void platform_pump_events(void)
{
    platform_poll_events(s_keystate, (int)sizeof(s_keystate));
}

int platform_net_init(void)
{
    return -1;
}

void platform_net_shutdown(void)
{
}

int platform_net_is_modem(void)
{
    return 0;
}

int platform_get_region(void)
{
    return 0;
}

unsigned int platform_menu_buttons(void)
{
    return 0;
}
