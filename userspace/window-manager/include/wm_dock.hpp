#pragma once

#include "wm_state.hpp"

#include <user/sdk/kilim.hpp>

namespace wms {

void dock_geom(int *out_x, int *out_y, int *out_w, int *out_h);
int dock_hit(int mx, int my);
void launch_or_focus(const DockItem &it);
void draw_dock(kilim::Context &k);

} /* namespace wms */
