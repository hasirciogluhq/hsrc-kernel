#include <user/mke.h>
#include <user/sdk/process.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * PID1-style first userspace process.
 * Spawns systemd then waits forever (reap orphans via waitpid loop).
 */
void mke_main(void)
{
    using hsrc::sdk::process::spawn;
    using hsrc::sdk::process::waitpid;

    (void)spawn("/applications/systemd.mke", nullptr);

    for (;;) {
        int status = 0;
        long rc = waitpid(-1, &status, 0);
        if (rc < 0)
            hsrc::sdk::syscall0(SYS_YIELD);
    }
}
