#ifndef MYKERNEL_KERNEL_MKE_COMPAT_H
#define MYKERNEL_KERNEL_MKE_COMPAT_H

/* Compat during mke → hxe rename */
#include <kernel/hxe.h>

#define MKE_MAGIC    HXE_MAGIC
#define MKE_VERSION  HXE_VERSION
#define MKE_NAME_MAX HXE_NAME_MAX
#define MKE_EXT      HXE_EXT
#define MKE_EXT_LEN  HXE_EXT_LEN
#define MKE_LOAD_MIN HXE_LOAD_MIN
#define MKE_LOAD_MAX HXE_LOAD_MAX

typedef hxe_header_t mke_header_t;

#define mke_spawn            hxe_spawn
#define mke_spawn_flags      hxe_spawn_flags
#define mke_spawn_path       hxe_spawn_path
#define mke_spawn_path_flags hxe_spawn_path_flags

#endif
