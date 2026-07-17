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

constexpr uint8_t color_a(Color c) { return (uint8_t)((c >> 24) & 0xFFu); }
constexpr uint8_t color_r(Color c) { return (uint8_t)((c >> 16) & 0xFFu); }
constexpr uint8_t color_g(Color c) { return (uint8_t)((c >> 8) & 0xFFu); }
constexpr uint8_t color_b(Color c) { return (uint8_t)(c & 0xFFu); }

inline Color color_mul_alpha(Color c, uint8_t a)
{
    uint32_t na = ((uint32_t)color_a(c) * (uint32_t)a) / 255u;
    return (c & 0x00FFFFFFu) | (na << 24);
}

inline Color color_blend(Color dst, Color src)
{
    uint32_t sa = color_a(src);
    if (sa == 0)
        return dst;
    if (sa == 255)
        return src;

    uint32_t inv = 255u - sa;
    uint32_t r = ((uint32_t)color_r(src) * sa + (uint32_t)color_r(dst) * inv) / 255u;
    uint32_t g = ((uint32_t)color_g(src) * sa + (uint32_t)color_g(dst) * inv) / 255u;
    uint32_t b = ((uint32_t)color_b(src) * sa + (uint32_t)color_b(dst) * inv) / 255u;
    uint32_t a = sa + ((uint32_t)color_a(dst) * inv) / 255u;
    return rgba((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
}

} // namespace hsrc::sdk
