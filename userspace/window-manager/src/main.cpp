#include "wm_compose.hpp"
#include "wm_ipc.hpp"
#include "wm_input.hpp"
#include "wm_state.hpp"

#include <kernel/string.h>
#include <kernel/syscall.h>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>

/*
 * Usermode compositor — file IPC under /tmp/wm/, Reed+Kilim present.
 * Kernel SYS_WM_* is not used.
 */

extern "C" void exec_main(void) {
  static reed::Device device;
  static kilim::Context kctx;

  while (wms::setup_dirs() < 0)
    (void)hsrc::sdk::syscall3(SYS_INPUT_WAIT, -1, 0, 1);

  while (device.init() < 0)
    (void)hsrc::sdk::syscall3(SYS_INPUT_WAIT, -1, 0, 1);
  wms::g_dev = &device;
  wms::g_screen_w = (int)device.caps().width;
  wms::g_screen_h = (int)device.caps().height;

  while (kctx.init(&device) < 0)
    (void)hsrc::sdk::syscall3(SYS_INPUT_WAIT, -1, 0, 1);

  memset(wms::g_slots, 0, sizeof(wms::g_slots));

  while (true) {
    wms::poll_requests();
    wms::handle_input();
    wms::compose_frame(kctx);
  }
}
