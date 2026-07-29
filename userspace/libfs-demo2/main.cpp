#include <user/exec.h>
#define LIBFS_API_DEFINE_SLOTS
#include "../../sdk/libfs/libfs_api.h"
#include <user/sdk/libfs.hpp>
#include <user/sdk/syscall.hpp>

/*
 * Example app #2 — same libfs.dynlib; kernel should reuse the already-mapped lib.
 */
extern "C" void exec_main(void)
{
    hsrc::sdk::libfs::puts("libfs-demo2: sharing libfs.dynlib\n");
    (void)hsrc::sdk::libfs::print_listdir("/system/bin");
    (void)hsrc::sdk::libfs::print_listdir("/system/lib");

    for (;;)
        hsrc::sdk::sleep_ticks(10);
}
