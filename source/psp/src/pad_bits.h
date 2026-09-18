/**
 * pad_bits.h — Bits of the 16-bit gameplay input word.
 *
 * Carried by g_perPlayerInput[] / g_combinedInputState, produced by the
 * platform pad pollers (platform_sdl.c, platform_dc.c), and stored per button
 * in g_joystickConfigWords[] and the per-slot remap configs. Values are
 * binary-verified against PollAllInputDevices @ 0x004769B0; see the
 * reference_input_keymap note for the full slot -> bit -> action table.
 *
 * Its own header rather than sitting in sonicr_globals.h because the platform
 * layers need it and deliberately keep a thin include surface.
 *
 * DO NOT CONFUSE WITH g_inputBits, the SDL menu-navigation abstraction, which
 * uses a DIFFERENT and NUMERICALLY COLLIDING set: g_inputBits 0x100 is "left"
 * where PAD_ACCEL is 0x0100, and g_inputBits 0x4 is "start" where PAD_START is
 * 0x0800. Nothing in a raw hex literal distinguishes the two — which is the
 * reason these names exist.
 */

#ifndef PAD_BITS_H
#define PAD_BITS_H

#define PAD_DRIFTL      0x0008
#define PAD_CAMERA      0x0040      /* look-back / camera cycle */
#define PAD_DRIFTR      0x0080
#define PAD_ACCEL       0x0100      /* doubles as "back"/cancel in menus */
#define PAD_JUMP        0x0400
#define PAD_START       0x0800
#define PAD_UP          0x1000
#define PAD_DOWN        0x2000
#define PAD_LEFT        0x4000
#define PAD_RIGHT       0x8000

/* Directions as a group. The remap UI binds ACTIONS only and never assigns a
 * direction, so its commit must leave these alone — see the commit loop in
 * screen_misc.c, where wiping them killed the D-pad on pads that report it as
 * buttons (6-9 on a Logitech Dual Action) rather than as a hat. */
#define PAD_DIRECTIONS  (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)

/* Ability mask the physics tests as `param_2 & 0x620`. Three separate sources
 * can trigger the jump/ability action: PAD_JUMP plus bits 0x0200 and 0x0020,
 * neither of which any keyboard slot writes — those arrive from the gamepad
 * path only. */
#define PAD_ABILITY     0x0620

#endif /* PAD_BITS_H */
