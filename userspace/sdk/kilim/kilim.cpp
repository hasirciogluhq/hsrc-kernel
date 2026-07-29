/* Kilim high-level 2D/UI SDK — built on Reed only (no kernel counterpart). */
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>

namespace kilim {

int fill_rect(int /*x*/, int /*y*/, int /*w*/, int /*h*/, unsigned /*rgba*/)
{
    (void)reed::create_device();
    return -1;
}

} /* namespace kilim */
