#ifndef MYKERNEL_KERNEL_MKDX_API_H
#define MYKERNEL_KERNEL_MKDX_API_H

/* Compat during mkdx → dx rename: prefer <kernel/dx_api.h>. */
#include <kernel/dx_api.h>

#ifndef HSRC_DX_API_MKDX_ALIASES
#define HSRC_DX_API_MKDX_ALIASES
typedef dx_api_t mkdx_api_t;
#define mkdx_api_register dx_api_register
#define mkdx_api_get      dx_api_get
#endif

#endif
