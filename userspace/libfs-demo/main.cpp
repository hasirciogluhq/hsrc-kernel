#include <user/exec.h>
#define LIBFS_API_DEFINE_SLOTS
#include "../../sdk/libfs/libfs_api.h"
#include <user/sdk/libfs.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * Example app #1 — uses dynamic /system/lib/libfs.dynlib
 * (kernel loads + relocates on spawn; shares with libfs-demo2).
 */
extern "C" void exec_main(void)
{
    hsrc::sdk::libfs::puts("libfs-demo: hello from dynamic libfs\n");
    (void)hsrc::sdk::libfs::print_listdir("/");
    (void)hsrc::sdk::libfs::print_listdir("/system");
    (void)hsrc::sdk::libfs::print_listdir("/system/lib");
    (void)hsrc::sdk::libfs::print_listdir("/applications");

    if (hsrc::sdk::libfs::exists("/system/etc/environment")) {
        char buf[256];
        long n = hsrc::sdk::libfs::read_file("/system/etc/environment", buf, sizeof(buf));
        hsrc::sdk::libfs::puts("libfs-demo: environment (" );
        /* tiny decimal-free message */
        if (n >= 0)
            hsrc::sdk::libfs::puts("ok):\n");
        else
            hsrc::sdk::libfs::puts("fail)\n");
        if (n > 0)
            hsrc::sdk::libfs::puts(buf);
        hsrc::sdk::libfs::puts("\n");
    }

    hsrc::sdk::exit(1);
}
