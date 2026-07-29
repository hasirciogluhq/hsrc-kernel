#ifndef MYKERNEL_USER_MKE_COMPAT_H
#define MYKERNEL_USER_MKE_COMPAT_H

#include <user/hxe.h>

#define MKE_MAGIC    HXE_MAGIC
#define MKE_VERSION  HXE_VERSION
#define MKE_NAME_MAX HXE_NAME_MAX
typedef hxe_header_t mke_header_t;

#ifdef __cplusplus
extern "C" {
#endif
/* Alias: apps may still define mke_main; linker ENTRY is mke_main. */
void mke_main(void);
#ifdef __cplusplus
}
#endif

#endif
