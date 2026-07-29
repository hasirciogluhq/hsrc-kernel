#ifndef USER_INPUT_H
#define USER_INPUT_H

#include <kernel/types.h>

/*
 * Userspace input snapshot (SYS_INPUT_STATE).
 * focus_id / hit_id / drag_id are WM-owned when a userspace WM is present;
 * kernel path leaves them at -1.
 */

#define INPUT_BTN_LEFT    0x01
#define INPUT_BTN_RIGHT   0x02
#define INPUT_BTN_MIDDLE  0x04
#define INPUT_BTN_BACK    0x08
#define INPUT_BTN_FORWARD 0x10

typedef struct input_state {
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t buttons;   /* INPUT_BTN_* */
    uint8_t mods;      /* KBD_MOD_* from keyboard.h */
    int32_t focus_id;  /* -1 = none (WM-owned) */
    int32_t hit_id;    /* topmost under cursor, or -1 */
    int32_t wheel;     /* notches since previous read */
    uint32_t seq;      /* monotonic generation */
    uint8_t keys[32];  /* PS/2 set-1 scancode bitmap */
    int32_t drag_id;   /* window being dragged, or -1 */
} input_state_t;

#endif
