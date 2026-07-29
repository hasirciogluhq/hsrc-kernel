#include <user/mke.h>
#include <user/sdk/process.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * Optional /applications/systemd.mke — same role as /init.
 * Prefer installing userspace/init as /applications/init.mke (kernel execs /init).
 */
void mke_main(void)
{
    using hsrc::sdk::process::spawn;
    using hsrc::sdk::process::waitpid;

    (void)spawn("window-manager", nullptr);
    (void)spawn("os-shell", nullptr);

    for (;;) {
        int status = 0;
        long rc = waitpid(-1, &status, 0);
        if (rc < 0)
            hsrc::sdk::syscall0(SYS_YIELD);
    }
}
