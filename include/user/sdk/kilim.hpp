#pragma once

#include <stdint.h>
#include <user/sdk/reed.hpp>

/*
 * Kilim — high-level 2D/UI/3D renderer on Reed only (no SYS_DISP_*).
 * v1: 2D primitives + acrylic stub + batching hooks.
 */

namespace kilim {

struct Color {
    uint32_t rgba; /* R in low byte matching reed pack: AARRGGBB via helpers */
};

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

class Context {
public:
    Context();

    int init(reed::Device *dev);
    void shutdown();
    bool valid() const { return alive_; }

    reed::Device *device() { return dev_; }
    reed::RenderTarget &target() { return target_; }

    /* Begin frame into swapchain-sized RT (creates on first call). */
    int begin_frame();
    /* Submit + present full RT. */
    int end_frame();

    void fill_rect(int x, int y, int w, int h, uint32_t color);
    void fill_round_rect(int x, int y, int w, int h, int radius, uint32_t color);
    void stroke_rect(int x, int y, int w, int h, int thickness, uint32_t color);
    void line(int x0, int y0, int x1, int y1, uint32_t color);
    void circle(int cx, int cy, int radius, uint32_t color, int filled = 1);

    /* Acrylic: separable blur + tint over region (v1 box blur). */
    int acrylic(int x, int y, int w, int h, int radius, uint32_t tint, uint8_t alpha);

private:
    reed::Device *dev_;
    reed::RenderTarget target_;
    reed::Pipeline pipe_color_;
    reed::CommandList cmd_;
    bool alive_;
    bool frame_open_;

    void ensure_ortho();
    void draw_quad(float x0, float y0, float x1, float y1, uint32_t color);
    reed::Uniforms ortho_uniforms(uint32_t color);
};

/* Global convenience (single context) — for early migration. */
int fill_rect(int x, int y, int w, int h, unsigned rgba);

} /* namespace kilim */
