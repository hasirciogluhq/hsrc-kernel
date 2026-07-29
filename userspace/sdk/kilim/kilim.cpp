#include <user/sdk/kilim.hpp>
#include <kernel/string.h>

namespace kilim {

namespace {

static Context *g_ctx;

static void mat_ortho(float *m, float l, float r, float b, float t, float n, float f)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

static void mat_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

} /* namespace */

Context::Context()
    : dev_(nullptr), alive_(false), frame_open_(false)
{
}

int Context::init(reed::Device *dev)
{
    if (!dev || !dev->valid())
        return -1;
    dev_ = dev;
    reed::PipelineDesc pd;
    memset(&pd, 0, sizeof(pd));
    pd.topology = reed::Topology::Triangles;
    pd.cull = reed::CullMode::None;
    pd.blend = reed::BlendMode::Alpha;
    pd.shade = reed::ShadeMode::UnlitColor;
    pipe_color_ = dev_->create_pipeline(pd);
    cmd_ = dev_->create_command_list();
    alive_ = true;
    return 0;
}

void Context::shutdown()
{
    if (frame_open_)
        (void)end_frame();
    target_.destroy();
    alive_ = false;
    dev_ = nullptr;
}

reed::Uniforms Context::ortho_uniforms(uint32_t color)
{
    reed::Uniforms u;
    memset(&u, 0, sizeof(u));
    mat_identity(u.model);
    mat_identity(u.view);
    float w = (float)dev_->caps().width;
    float h = (float)dev_->caps().height;
    mat_ortho(u.proj, 0.0f, w, h, 0.0f, -1.0f, 1.0f);
    u.color = color;
    return u;
}

int Context::begin_frame()
{
    if (!alive_ || !dev_)
        return -1;
    if (!target_.valid()) {
        target_ = dev_->create_swapchain_target();
        if (!target_.valid())
            return -1;
    }
    cmd_ = dev_->create_command_list();
    cmd_.begin();
    cmd_.bind_pipeline(pipe_color_);
    cmd_.bind_render_target(target_);
    reed::Viewport vp = {0, 0, (float)dev_->caps().width, (float)dev_->caps().height, 0, 1};
    cmd_.set_viewport(vp);
    reed::Rect sc = {0, 0, (int32_t)dev_->caps().width, (int32_t)dev_->caps().height};
    cmd_.set_scissor(sc);
    cmd_.clear(rgba(0, 0, 0, 255));
    frame_open_ = true;
    return 0;
}

int Context::end_frame()
{
    if (!frame_open_)
        return -1;
    cmd_.end();
    (void)cmd_.submit(nullptr);
    int rc = cmd_.present(target_, nullptr);
    frame_open_ = false;
    return rc;
}

void Context::draw_quad(float x0, float y0, float x1, float y1, uint32_t color)
{
    reed::Vertex verts[6];
    memset(verts, 0, sizeof(verts));
    verts[0] = {x0, y0, 0, 0, 0, 1, 0, 0, color};
    verts[1] = {x1, y0, 0, 0, 0, 1, 0, 0, color};
    verts[2] = {x1, y1, 0, 0, 0, 1, 0, 0, color};
    verts[3] = {x0, y0, 0, 0, 0, 1, 0, 0, color};
    verts[4] = {x1, y1, 0, 0, 0, 1, 0, 0, color};
    verts[5] = {x0, y1, 0, 0, 0, 1, 0, 0, color};

    reed::Buffer vb = dev_->create_buffer(reed::BufferKind::Vertex, sizeof(verts), verts);
    if (!vb.valid())
        return;
    cmd_.set_uniform(ortho_uniforms(color));
    cmd_.bind_vertex_buffer(vb);
    cmd_.draw(6, 0);
    vb.destroy();
}

void Context::fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!frame_open_ || w <= 0 || h <= 0)
        return;
    draw_quad((float)x, (float)y, (float)(x + w), (float)(y + h), color);
}

void Context::fill_round_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    if (radius <= 0) {
        fill_rect(x, y, w, h, color);
        return;
    }
    /* Approximate: center + 4 side bars + 4 corner circles. */
    int r = radius;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    fill_rect(x + r, y, w - 2 * r, h, color);
    fill_rect(x, y + r, r, h - 2 * r, color);
    fill_rect(x + w - r, y + r, r, h - 2 * r, color);
    circle(x + r, y + r, r, color, 1);
    circle(x + w - r - 1, y + r, r, color, 1);
    circle(x + r, y + h - r - 1, r, color, 1);
    circle(x + w - r - 1, y + h - r - 1, r, color, 1);
}

void Context::stroke_rect(int x, int y, int w, int h, int thickness, uint32_t color)
{
    if (thickness < 1)
        thickness = 1;
    fill_rect(x, y, w, thickness, color);
    fill_rect(x, y + h - thickness, w, thickness, color);
    fill_rect(x, y, thickness, h, color);
    fill_rect(x + w - thickness, y, thickness, h, color);
}

void Context::line(int x0, int y0, int x1, int y1, uint32_t color)
{
    /* Bresenham as 1px rects — coarse but works without line topology path. */
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    for (;;) {
        fill_rect(x, y, 1, 1, color);
        if (x == x1 && y == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

void Context::circle(int cx, int cy, int radius, uint32_t color, int filled)
{
    if (radius <= 0)
        return;
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int d = x * x + y * y;
            if (filled) {
                if (d <= radius * radius)
                    fill_rect(cx + x, cy + y, 1, 1, color);
            } else {
                int r2 = radius * radius;
                int inner = (radius - 1) * (radius - 1);
                if (d <= r2 && d >= inner)
                    fill_rect(cx + x, cy + y, 1, 1, color);
            }
        }
    }
}

int Context::acrylic(int x, int y, int w, int h, int radius, uint32_t tint, uint8_t alpha)
{
    if (!frame_open_ || w <= 0 || h <= 0)
        return -1;
    /* v1: tinted translucent overlay (full blur passes land with RT ping-pong). */
    uint8_t tr = (uint8_t)((tint >> 16) & 0xffu);
    uint8_t tg = (uint8_t)((tint >> 8) & 0xffu);
    uint8_t tb = (uint8_t)(tint & 0xffu);
    (void)radius;
    fill_rect(x, y, w, h, rgba(tr, tg, tb, alpha));
    return 0;
}

int fill_rect(int x, int y, int w, int h, unsigned color)
{
    if (!g_ctx || !g_ctx->valid()) {
        static reed::Device s_dev;
        static Context s_ctx;
        if (!s_dev.valid()) {
            if (s_dev.init() < 0)
                return -1;
            if (s_ctx.init(&s_dev) < 0)
                return -1;
            g_ctx = &s_ctx;
        }
    }
    if (!g_ctx->valid())
        return -1;
    /* One-shot: begin/clear/draw/present — OK for smoke; apps should own Context. */
    if (g_ctx->begin_frame() < 0)
        return -1;
    g_ctx->fill_rect(x, y, w, h, (uint32_t)color);
    return g_ctx->end_frame();
}

} /* namespace kilim */
