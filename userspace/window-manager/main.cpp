#include <user/mke.h>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/*
 * Placeholder window-manager process (compositor still kernel mkdx for now).
 * systemd keeps this alive; gpu_display_stack will replace the body.
 */
void mke_main(void)
{
    for (;;)
        hsrc::sdk::syscall0(SYS_YIELD);
}
