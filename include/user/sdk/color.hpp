#pragma once

#include <kernel/types.h>

namespace hsrc::sdk {

using Color = uint32_t;

constexpr Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return (Color(a) << 24) | (Color(r) << 16) | (Color(g) << 8) | Color(b);
}

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return rgba(r, g, b, 255);
}

constexpr Color kTransparent = 0;

} // namespace hsrc::sdk
