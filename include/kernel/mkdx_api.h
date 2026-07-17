#ifndef MYKERNEL_KERNEL_MKDX_API_H
#define MYKERNEL_KERNEL_MKDX_API_H

#include <kernel/types.h>

/*
 * Thin ABI between kernel core (syscalls) and mkdx.kmod.
 * mkdx registers this table from kmod_init; core never links gfx objects.
 */
typedef struct mkdx_api {
    int  (*info)(uint32_t *w, uint32_t *h, uint32_t *bpp);
    int  (*present)(void);
    void (*mark_dirty)(void);

    long (*wm_create)(const void *args, uint32_t owner_pid);
    int  (*wm_destroy)(int id);
    int  (*wm_map)(int id, void *out);
    int  (*wm_move)(int id, int32_t x, int32_t y);
    int  (*wm_resize)(int id, int32_t w, int32_t h);
    int  (*wm_focus)(int id);
    int  (*wm_show)(int id, int vis);
    int  (*wm_get_frame)(int id, void *out);
    int  (*wm_pop_key)(int id);
    int  (*wm_focused_id)(void);

    int  (*fill)(const void *args, int rounded);
    int  (*set_wallpaper)(const void *args);
    int  (*input_state)(void *out);
} mkdx_api_t;

void              mkdx_api_register(const mkdx_api_t *api);
const mkdx_api_t *mkdx_api_get(void);

#endif
