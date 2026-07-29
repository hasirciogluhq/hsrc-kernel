#pragma once

/* Reed — low-level graphics API (Vulkan-portable surface).
 * Implementation: userspace/sdk/reed. Kernel counterpart: display.kmod.
 */

namespace reed {

int create_device(void);

} /* namespace reed */
