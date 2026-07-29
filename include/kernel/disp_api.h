#ifndef DISP_API_H
#define DISP_API_H

#include <kernel/types.h>

/*
 * Thin ABI between kernel core (SYS_DISP_*) and display.kmod.
 * display.kmod registers this table from kmod_init.
 */

typedef struct disp_api {
    long (*call)(uint32_t op, void *arg, uint32_t owner_pid);
    void (*cleanup_pid)(uint32_t pid);
} disp_api_t;

void             disp_api_register(const disp_api_t *api);
const disp_api_t *disp_api_get(void);

#endif
