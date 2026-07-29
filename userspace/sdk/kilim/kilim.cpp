#include <user/sdk/kilim.hpp>
#include <kernel/string.h>

#include "kilim_font.inc"

/* Freestanding placement new (no <new> / atexit). */
inline void *operator new(size_t, void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}

namespace kilim {

namespace {

static Context *g_ctx;

static float cosf_approx(float x)
{
    while (x > 3.14159265f)
        x -= 6.2831853f;
    while (x < -3.14159265f)
        x += 6.2831853f;
    float x2 = x * x;
    return 1.0f - x2 / 2.0f + x2 * x2 / 24.0f;
}

static float sinf_approx(float x)
{
    while (x > 3.14159265f)
        x -= 6.2831853f;
    while (x < -3.14159265f)
        x += 6.2831853f;
    float x2 = x * x;
    return x - x * x2 / 6.0f + x * x2 * x2 / 120.0f;
}

static float inv_sqrt(float len)
{
    if (len <= 1e-8f)
        return 1.0f;
    float xhalf = 0.5f * len;
    union {
        float f;
        uint32_t i;
    } u = {len};
    u.i = 0x5f3759dfu - (u.i >> 1);
    return u.f * (1.5f - xhalf * u.f * u.f);
}

static float tanf_approx(float x)
{
    float s = sinf_approx(x);
    float c = cosf_approx(x);
    if (c > -1e-4f && c < 1e-4f)
        c = c < 0 ? -1e-4f : 1e-4f;
    return s / c;
}

static void mat_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

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

static void mat_perspective(float *m, float fovy, float aspect, float zn, float zf)
{
    float f = 1.0f / tanf_approx(fovy * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = (2.0f * zf * zn) / (zn - zf);
}

static void mat_lookat(float *m, Vec3 eye, Vec3 center, Vec3 up)
{
    Vec3 f = {center.x - eye.x, center.y - eye.y, center.z - eye.z};
    float fl = f.x * f.x + f.y * f.y + f.z * f.z;
    if (fl > 1e-8f) {
        float inv = inv_sqrt(fl);
        f.x *= inv;
        f.y *= inv;
        f.z *= inv;
    }
    Vec3 s = {f.y * up.z - f.z * up.y, f.z * up.x - f.x * up.z, f.x * up.y - f.y * up.x};
    float sl = s.x * s.x + s.y * s.y + s.z * s.z;
    if (sl > 1e-8f) {
        float inv = inv_sqrt(sl);
        s.x *= inv;
        s.y *= inv;
        s.z *= inv;
    }
    Vec3 u = {s.y * f.z - s.z * f.y, s.z * f.x - s.x * f.z, s.x * f.y - s.y * f.x};
    mat_identity(m);
    m[0] = s.x;
    m[4] = s.y;
    m[8] = s.z;
    m[1] = u.x;
    m[5] = u.y;
    m[9] = u.z;
    m[2] = -f.x;
    m[6] = -f.y;
    m[10] = -f.z;
    m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    m[14] = f.x * eye.x + f.y * eye.y + f.z * eye.z;
}

} /* namespace */

void Transform::to_matrix(float out[16]) const
{
    float cx = cosf_approx(rot.x), sx = sinf_approx(rot.x);
    float cy = cosf_approx(rot.y), sy = sinf_approx(rot.y);
    float cz = cosf_approx(rot.z), sz = sinf_approx(rot.z);
    mat_identity(out);
    /* Rz * Ry * Rx * Scale + translate */
    float r00 = cy * cz;
    float r01 = -cy * sz;
    float r02 = sy;
    float r10 = sx * sy * cz + cx * sz;
    float r11 = -sx * sy * sz + cx * cz;
    float r12 = -sx * cy;
    float r20 = -cx * sy * cz + sx * sz;
    float r21 = cx * sy * sz + sx * cz;
    float r22 = cx * cy;
    out[0] = r00 * scale.x;
    out[1] = r10 * scale.x;
    out[2] = r20 * scale.x;
    out[4] = r01 * scale.y;
    out[5] = r11 * scale.y;
    out[6] = r21 * scale.y;
    out[8] = r02 * scale.z;
    out[9] = r12 * scale.z;
    out[10] = r22 * scale.z;
    out[12] = pos.x;
    out[13] = pos.y;
    out[14] = pos.z;
}

void Camera::view_matrix(float out[16]) const
{
    mat_lookat(out, eye, target, up);
}

void Camera::proj_matrix(float out[16]) const
{
    if (orthographic)
        mat_ortho(out, -ortho_w * 0.5f, ortho_w * 0.5f, -ortho_h * 0.5f, ortho_h * 0.5f,
                  znear, zfar);
    else
        mat_perspective(out, fov_y, aspect, znear, zfar);
}

void Mesh::destroy()
{
    if (owns) {
        /* verts/indices are plain arrays from load — caller frees if heap; v1 stack/static */
    }
    verts = nullptr;
    indices = nullptr;
    vert_count = index_count = 0;
    owns = 0;
}

int mesh_load_kmesh(const void *data, uint32_t size, Mesh *out)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!out || !p || size < 12)
        return -1;
    if (p[0] != 'K' || p[1] != 'M' || p[2] != 'S' || p[3] != 'H')
        return -1;
    uint32_t vc = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) |
                  ((uint32_t)p[7] << 24);
    uint32_t ic = (uint32_t)p[8] | ((uint32_t)p[9] << 8) | ((uint32_t)p[10] << 16) |
                  ((uint32_t)p[11] << 24);
    uint32_t need = 12u + vc * (uint32_t)sizeof(reed::Vertex) + ic * 4u;
    if (size < need || vc == 0)
        return -1;
    out->vert_count = vc;
    out->index_count = ic;
    out->verts = (reed::Vertex *)(p + 12);
    out->indices = ic ? (uint32_t *)(p + 12 + vc * sizeof(reed::Vertex)) : nullptr;
    out->owns = 0;
    return 0;
}

Font::Font() : ready_(false), atlas_w_(0), atlas_h_(0)
{
    memset(glyph_x_, 0, sizeof(glyph_x_));
    memset(glyph_adv_, 0, sizeof(glyph_adv_));
}

int Font::load(const char * /*path*/)
{
    ready_ = true;
    return 0;
}

int Font::glyph_advance(char c) const
{
    if (c < 32 || c > 126)
        return 8;
    return (int)kilim_font[(int)c - 32].advance;
}

int Font::line_height(int size) const
{
    return size > 0 ? size : KILIM_FONT_H;
}

Context::Context()
    : dev_(nullptr), nbatches_(0), alive_(false), frame_open_(false), ptr_x_(0), ptr_y_(0),
      ptr_buttons_(0), wheel_(0), scissor_x_(0), scissor_y_(0), scissor_w_(0), scissor_h_(0),
      scroll_y_ptr_(0), dyn_aw_(0), dyn_ah_(0), shelf_x_(0), shelf_y_(0), shelf_h_(0)
{
    memset(glyphs_, 0, sizeof(glyphs_));
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
    pd.shade = reed::ShadeMode::UnlitTextured;
    pipe_tex_ = dev_->create_pipeline(pd);
    pd.shade = reed::ShadeMode::VertexLit;
    pd.cull = reed::CullMode::Back;
    pipe_lit_ = dev_->create_pipeline(pd);
    cmd_ = dev_->create_command_list();
    (void)font_.load(nullptr);
    alive_ = true;
    return 0;
}

void Context::shutdown()
{
    if (frame_open_)
        (void)end_frame();
    target_.destroy();
    blur_tmp_.destroy();
    font_.atlas_.destroy();
    dyn_atlas_.destroy();
    alive_ = false;
    dev_ = nullptr;
}

void Context::set_pointer(int x, int y, uint8_t buttons)
{
    ptr_x_ = x;
    ptr_y_ = y;
    ptr_buttons_ = buttons;
}

void Context::set_wheel(int wheel)
{
    wheel_ = wheel;
}

reed::Uniforms Context::ortho_u(uint32_t color)
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

void Context::ensure_font_atlas()
{
    if (dyn_atlas_.valid())
        return;
    /* Dynamic AA atlas: glyphs rasterized at exact pixel size (no stretch). */
    dyn_aw_ = 1024;
    dyn_ah_ = 512;
    dyn_atlas_ = dev_->create_texture(reed::TexFormat::A8, dyn_aw_, dyn_ah_, 1);
    if (!dyn_atlas_.valid())
        return;
    uint8_t *px = (uint8_t *)dyn_atlas_.map();
    if (px) {
        memset(px, 0, dyn_aw_ * dyn_ah_);
        dyn_atlas_.unmap();
    }
    shelf_x_ = 1;
    shelf_y_ = 1;
    shelf_h_ = 0;
    memset(glyphs_, 0, sizeof(glyphs_));
    /* Keep master strip for sampling source (1:1 reference). */
    if (!font_.atlas_.valid()) {
        uint32_t aw = 95u * (uint32_t)KILIM_FONT_W;
        uint32_t ah = (uint32_t)KILIM_FONT_H;
        font_.atlas_ = dev_->create_texture(reed::TexFormat::A8, aw, ah, 1);
        if (font_.atlas_.valid()) {
            uint8_t *m = (uint8_t *)font_.atlas_.map();
            if (m) {
                memset(m, 0, aw * ah);
                for (int g = 0; g < 95; g++) {
                    font_.glyph_x_[g] = (uint16_t)(g * KILIM_FONT_W);
                    font_.glyph_adv_[g] = kilim_font[g].advance;
                    for (int y = 0; y < KILIM_FONT_H; y++)
                        for (int x = 0; x < KILIM_FONT_W; x++)
                            m[y * aw + (uint32_t)font_.glyph_x_[g] + (uint32_t)x] =
                                kilim_font[g].alpha[y][x];
                }
                font_.atlas_w_ = aw;
                font_.atlas_h_ = ah;
                font_.atlas_.unmap();
            }
        }
    }
}

static float sample_master(int gi, float fx, float fy)
{
    if (gi < 0 || gi >= 95)
        return 0.0f;
    if (fx < 0.0f || fy < 0.0f || fx >= (float)KILIM_FONT_W || fy >= (float)KILIM_FONT_H)
        return 0.0f;
    int x0 = (int)fx;
    int y0 = (int)fy;
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    float tx = fx - (float)x0;
    float ty = fy - (float)y0;
    auto samp = [](int gi2, int x, int y) -> float {
        if (x < 0 || y < 0 || x >= KILIM_FONT_W || y >= KILIM_FONT_H)
            return 0.0f;
        return (float)kilim_font[gi2].alpha[y][x];
    };
    float a = samp(gi, x0, y0);
    float b = samp(gi, x1, y0);
    float c = samp(gi, x0, y1);
    float d = samp(gi, x1, y1);
    float top = a + (b - a) * tx;
    float bot = c + (d - c) * tx;
    return top + (bot - top) * ty;
}

int Context::cache_glyph(int code, int size, GlyphCache **out)
{
    if (!out || size < 6)
        size = 6;
    if (size > 72)
        size = 72;
    ensure_font_atlas();
    if (!dyn_atlas_.valid())
        return -1;

    for (int i = 0; i < 192; i++) {
        if (glyphs_[i].used && glyphs_[i].code == (uint16_t)code &&
            glyphs_[i].size == (uint16_t)size) {
            *out = &glyphs_[i];
            return 0;
        }
    }

    int gi = code - 32;
    if (gi < 0 || gi >= 95)
        gi = '?' - 32;

    /* Pixel-perfect integer height; width from master aspect. */
    int dst_h = size;
    int dst_w = (int)(((float)KILIM_FONT_W * (float)size) / (float)KILIM_FONT_H + 0.5f);
    if (dst_w < 1)
        dst_w = 1;
    int adv = (int)(((float)kilim_font[gi].advance * (float)size) / (float)KILIM_FONT_H + 0.5f);
    if (adv < 1)
        adv = 1;

    /* Shelf pack */
    if (shelf_x_ + dst_w + 1 > (int)dyn_aw_) {
        shelf_x_ = 1;
        shelf_y_ += shelf_h_ + 1;
        shelf_h_ = 0;
    }
    if (shelf_y_ + dst_h + 1 > (int)dyn_ah_) {
        /* Atlas full — reset shelf (overwrite old glyphs). */
        shelf_x_ = 1;
        shelf_y_ = 1;
        shelf_h_ = 0;
        memset(glyphs_, 0, sizeof(glyphs_));
        uint8_t *clr = (uint8_t *)dyn_atlas_.map();
        if (clr) {
            memset(clr, 0, dyn_aw_ * dyn_ah_);
            dyn_atlas_.unmap();
        }
    }

    int slot = -1;
    for (int i = 0; i < 192; i++) {
        if (!glyphs_[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        slot = 0;

    GlyphCache *g = &glyphs_[slot];
    g->used = 1;
    g->code = (uint16_t)code;
    g->size = (uint16_t)size;
    g->x = (uint16_t)shelf_x_;
    g->y = (uint16_t)shelf_y_;
    g->w = (uint16_t)dst_w;
    g->h = (uint16_t)dst_h;
    g->adv = (uint16_t)adv;

    uint8_t *px = (uint8_t *)dyn_atlas_.map();
    if (px) {
        /* 4×4 supersampled area filter from master → crisp AA at any size. */
        for (int y = 0; y < dst_h; y++) {
            for (int x = 0; x < dst_w; x++) {
                float sum = 0.0f;
                for (int sy = 0; sy < 4; sy++) {
                    for (int sx = 0; sx < 4; sx++) {
                        float u = ((float)x + ((float)sx + 0.5f) / 4.0f) *
                                  ((float)KILIM_FONT_W / (float)dst_w);
                        float v = ((float)y + ((float)sy + 0.5f) / 4.0f) *
                                  ((float)KILIM_FONT_H / (float)dst_h);
                        sum += sample_master(gi, u, v);
                    }
                }
                float a = sum / 16.0f;
                if (a < 0.0f)
                    a = 0.0f;
                if (a > 255.0f)
                    a = 255.0f;
                /* Slight contrast curve for sharper stems */
                a = a * a / 255.0f;
                px[(g->y + (uint32_t)y) * dyn_aw_ + g->x + (uint32_t)x] =
                    (uint8_t)(a + 0.5f);
            }
        }
        dyn_atlas_.unmap();
    }

    shelf_x_ += dst_w + 1;
    if (dst_h > shelf_h_)
        shelf_h_ = dst_h;
    *out = g;
    return 0;
}

Batch &Context::text(const char *str, int x, int y, int size, uint32_t color)
{
    ensure_font_atlas();
    text_batch_.pipe_key_ = 1;
    text_batch_.tex_handle_ = dyn_atlas_.handle();
    if (!str || !dyn_atlas_.valid())
        return text_batch_;
    if (size < 6)
        size = 6;

    float cx = (float)x;
    float cy = (float)y;
    float aw = (float)dyn_aw_;
    float ah = (float)dyn_ah_;
    for (const char *p = str; *p; p++) {
        char c = *p;
        if (c == '\n') {
            cx = (float)x;
            cy += (float)size + 2.0f;
            continue;
        }
        if (c < 32 || c > 126)
            c = '?';
        GlyphCache *g = nullptr;
        if (cache_glyph((int)c, size, &g) < 0 || !g)
            continue;
        float u0 = (float)g->x / aw;
        float u1 = (float)(g->x + g->w) / aw;
        float v0 = (float)g->y / ah;
        float v1 = (float)(g->y + g->h) / ah;
        /* 1:1 pixel quads — no GPU stretch (pixel-perfect). */
        emit_quad(&text_batch_, cx, cy, cx + (float)g->w, cy + (float)g->h, u0, v0, u1,
                  v1, color);
        cx += (float)g->adv;
    }
    return text_batch_;
}

int Context::present_damage(int x, int y, int w, int h)
{
    if (!alive_ || !dev_)
        return -1;
    reed::Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return cmd_.present(target_, &r);
}

Batch *Context::get_batch(uint32_t pipe_key, uint32_t tex_handle)
{
    for (int i = 0; i < nbatches_; i++) {
        if (batches_[i].pipe_key_ == pipe_key && batches_[i].tex_handle_ == tex_handle &&
            batches_[i].vcount_ + 6u < 2048u)
            return &batches_[i];
    }
    if (nbatches_ >= kMaxBatches) {
        flush();
    }
    if (nbatches_ >= kMaxBatches)
        return &batches_[0];
    Batch *b = &batches_[nbatches_++];
    b->pipe_key_ = pipe_key;
    b->tex_handle_ = tex_handle;
    b->vcount_ = 0;
    b->icount_ = 0;
    return b;
}

void Context::emit_quad(Batch *b, float x0, float y0, float x1, float y1, float u0, float v0,
                        float u1, float v1, uint32_t color)
{
    if (!b || b->vcount_ + 6u > 2048u)
        return;
    uint32_t base = b->vcount_;
    reed::Vertex *v = b->verts_ + base;
    v[0] = {x0, y0, 0, 0, 0, 1, u0, v0, color};
    v[1] = {x1, y0, 0, 0, 0, 1, u1, v0, color};
    v[2] = {x1, y1, 0, 0, 0, 1, u1, v1, color};
    v[3] = {x0, y0, 0, 0, 0, 1, u0, v0, color};
    v[4] = {x1, y1, 0, 0, 0, 1, u1, v1, color};
    v[5] = {x0, y1, 0, 0, 0, 1, u0, v1, color};
    b->vcount_ += 6;
}

void Context::flush_batch(Batch *b)
{
    if (!b || b->vcount_ == 0 || !dev_)
        return;
    reed::Buffer vb =
        dev_->create_buffer(reed::BufferKind::Vertex, b->vcount_ * sizeof(reed::Vertex),
                            b->verts_);
    if (!vb.valid()) {
        b->vcount_ = 0;
        return;
    }
    if (b->pipe_key_ == 1) {
        cmd_.bind_pipeline(pipe_tex_);
        reed::Sampler s;
        s.set(reed::WrapMode::Clamp, reed::FilterMode::Nearest);
        reed::Texture2D tex = dyn_atlas_.valid() ? dyn_atlas_ : font_.atlas_;
        cmd_.bind_texture(0, tex, s);
    } else if (b->pipe_key_ == 2) {
        cmd_.bind_pipeline(pipe_lit_);
    } else {
        cmd_.bind_pipeline(pipe_color_);
    }
    cmd_.set_uniform(ortho_u(0xffffffffu));
    cmd_.bind_vertex_buffer(vb);
    cmd_.draw(b->vcount_, 0);
    vb.destroy();
    b->vcount_ = 0;
    b->icount_ = 0;
}

void Context::flush()
{
    if (!frame_open_)
        return;
    for (int i = 0; i < nbatches_; i++)
        flush_batch(&batches_[i]);
    nbatches_ = 0;
    if (text_batch_.vcount_)
        flush_batch(&text_batch_);
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
    ensure_font_atlas();
    cmd_ = dev_->create_command_list();
    cmd_.begin();
    cmd_.bind_pipeline(pipe_color_);
    cmd_.bind_render_target(target_);
    reed::Viewport vp = {0, 0, (float)dev_->caps().width, (float)dev_->caps().height, 0, 1};
    cmd_.set_viewport(vp);
    reed::Rect sc = {0, 0, (int32_t)dev_->caps().width, (int32_t)dev_->caps().height};
    cmd_.set_scissor(sc);
    cmd_.clear(rgba(26, 31, 46, 255)); /* desktop #1A1F2E — not pure black */
    nbatches_ = 0;
    text_batch_.vcount_ = 0;
    text_batch_.pipe_key_ = 1;
    text_batch_.tex_handle_ = dyn_atlas_.valid() ? dyn_atlas_.handle() : font_.atlas_.handle();
    frame_open_ = true;
    return 0;
}

int Context::commit_frame()
{
    if (!frame_open_)
        return -1;
    flush();
    cmd_.end();
    int rc = cmd_.submit(nullptr);
    frame_open_ = false;
    return rc;
}

int Context::end_frame()
{
    if (!frame_open_)
        return -1;
    flush();
    cmd_.end();
    (void)cmd_.submit(nullptr);
    int rc = cmd_.present(target_, nullptr);
    frame_open_ = false;
    return rc;
}

void Context::fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!frame_open_ || w <= 0 || h <= 0)
        return;
    Batch *b = get_batch(0, 0);
    emit_quad(b, (float)x, (float)y, (float)(x + w), (float)(y + h), 0, 0, 1, 1, color);
}

void Context::fill_round_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    if (radius <= 0) {
        fill_rect(x, y, w, h, color);
        return;
    }
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
    int dx = x1 - x0, dy = y1 - y0;
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
        int e2 = err * 2;
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

void Context::polygon(const int *xy, int npoints, uint32_t color, int filled)
{
    if (!xy || npoints < 3)
        return;
    if (!filled) {
        for (int i = 0; i < npoints; i++) {
            int j = (i + 1) % npoints;
            line(xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1], color);
        }
        return;
    }
    /* Fan triangulation from first vertex */
    for (int i = 1; i + 1 < npoints; i++) {
        Batch *b = get_batch(0, 0);
        if (!b || b->vcount_ + 3u > 2048u)
            continue;
        float x0 = (float)xy[0], y0 = (float)xy[1];
        float x1 = (float)xy[i * 2], y1 = (float)xy[i * 2 + 1];
        float x2 = (float)xy[(i + 1) * 2], y2 = (float)xy[(i + 1) * 2 + 1];
        reed::Vertex *v = b->verts_ + b->vcount_;
        v[0] = {x0, y0, 0, 0, 0, 1, 0, 0, color};
        v[1] = {x1, y1, 0, 0, 0, 1, 0, 0, color};
        v[2] = {x2, y2, 0, 0, 0, 1, 0, 0, color};
        b->vcount_ += 3;
    }
}

int Context::acrylic(int x, int y, int w, int h, int radius, uint32_t tint, uint8_t alpha)
{
    if (!frame_open_ || w <= 0 || h <= 0)
        return -1;
    flush();
    /* v1: separable box blur via CPU on mapped RT region + tint (Reed blur modes
     * available for full-screen passes; region blur here for latency). */
    uint32_t *fb = (uint32_t *)target_.color().map();
    if (!fb)
        return -1;
    uint32_t fw = target_.color().width();
    uint32_t fh = target_.color().height();
    uint32_t stride = target_.color().stride() / 4u;
    int r = radius > 0 ? radius : 8;
    if (r > 32)
        r = 32;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > (int)fw)
        x1 = (int)fw;
    if (y1 > (int)fh)
        y1 = (int)fh;
    /* Horizontal then vertical box blur into place (two passes, scratch on stack rows). */
    static uint32_t row[4096];
    for (int pass = 0; pass < 2; pass++) {
        for (int yy = y0; yy < y1; yy++) {
            for (int xx = x0; xx < x1; xx++) {
                uint32_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0, n = 0;
                for (int k = -r; k <= r; k++) {
                    int sx = pass == 0 ? xx + k : xx;
                    int sy = pass == 0 ? yy : yy + k;
                    if (sx < x0 || sx >= x1 || sy < y0 || sy >= y1)
                        continue;
                    uint32_t c = fb[(uint32_t)sy * stride + (uint32_t)sx];
                    sum_a += (c >> 24) & 0xffu;
                    sum_r += (c >> 16) & 0xffu;
                    sum_g += (c >> 8) & 0xffu;
                    sum_b += c & 0xffu;
                    n++;
                }
                if (n == 0)
                    n = 1;
                row[xx - x0] = ((sum_a / n) << 24) | ((sum_r / n) << 16) |
                               ((sum_g / n) << 8) | (sum_b / n);
            }
            for (int xx = x0; xx < x1; xx++)
                fb[(uint32_t)yy * stride + (uint32_t)xx] = row[xx - x0];
        }
    }
    uint8_t tr = (uint8_t)((tint >> 16) & 0xff), tg = (uint8_t)((tint >> 8) & 0xff),
            tb = (uint8_t)(tint & 0xff);
    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            uint32_t c = fb[(uint32_t)yy * stride + (uint32_t)xx];
            uint8_t cr = (uint8_t)((c >> 16) & 0xff), cg = (uint8_t)((c >> 8) & 0xff),
                    cb = (uint8_t)(c & 0xff);
            uint8_t nr = (uint8_t)(((uint32_t)cr * (255 - alpha) + (uint32_t)tr * alpha) / 255);
            uint8_t ng = (uint8_t)(((uint32_t)cg * (255 - alpha) + (uint32_t)tg * alpha) / 255);
            uint8_t nb = (uint8_t)(((uint32_t)cb * (255 - alpha) + (uint32_t)tb * alpha) / 255);
            fb[(uint32_t)yy * stride + (uint32_t)xx] = rgba(nr, ng, nb, 255);
        }
    }
    return 0;
}

void Context::draw_mesh(const Mesh &mesh, const Material &mat, const Transform &xf,
                        const Camera &cam)
{
    if (!frame_open_ || !mesh.verts || mesh.vert_count == 0)
        return;
    flush();
    reed::Uniforms u;
    memset(&u, 0, sizeof(u));
    xf.to_matrix(u.model);
    cam.view_matrix(u.view);
    cam.proj_matrix(u.proj);
    u.color = mat.color;
    u.light_count = mat.light_count;
    for (uint32_t i = 0; i < mat.light_count && i < 4u; i++)
        u.lights[i] = mat.lights[i];

    reed::Buffer vb = dev_->create_buffer(reed::BufferKind::Vertex,
                                          mesh.vert_count * sizeof(reed::Vertex), mesh.verts);
    if (!vb.valid())
        return;
    if (mat.shade == reed::ShadeMode::VertexLit)
        cmd_.bind_pipeline(pipe_lit_);
    else if (mat.texture && mat.texture->valid()) {
        cmd_.bind_pipeline(pipe_tex_);
        reed::Sampler s;
        cmd_.bind_texture(0, *mat.texture, s);
    } else
        cmd_.bind_pipeline(pipe_color_);
    cmd_.set_uniform(u);
    cmd_.bind_vertex_buffer(vb);
    if (mesh.indices && mesh.index_count) {
        reed::Buffer ib =
            dev_->create_buffer(reed::BufferKind::Index, mesh.index_count * 4u, mesh.indices);
        if (ib.valid()) {
            cmd_.bind_index_buffer(ib);
            cmd_.draw_indexed(mesh.index_count, 0, 0);
            ib.destroy();
        }
    } else {
        cmd_.draw(mesh.vert_count, 0);
    }
    vb.destroy();
}

static void draw_node(Context *ctx, SceneNode *n, const Camera &cam)
{
    if (!n)
        return;
    if (n->mesh && n->material)
        ctx->draw_mesh(*n->mesh, *n->material, n->xform, cam);
    draw_node(ctx, n->child, cam);
    draw_node(ctx, n->sibling, cam);
}

void Context::draw_scene(const Scene &scene)
{
    draw_node(this, scene.root, scene.camera);
}

void Context::panel(int x, int y, int w, int h, uint32_t bg)
{
    fill_round_rect(x, y, w, h, 8, bg);
}

int Context::button(int x, int y, int w, int h, const char *label, int *hover)
{
    int over = ptr_x_ >= x && ptr_x_ < x + w && ptr_y_ >= y && ptr_y_ < y + h;
    if (hover)
        *hover = over;
    uint32_t bg = over ? rgba(60, 120, 220, 255) : rgba(45, 45, 50, 255);
    fill_round_rect(x, y, w, h, 6, bg);
    if (label)
        text(label, x + 10, y + h / 2 - 8, 16, rgba(240, 240, 240, 255));
    return over && (ptr_buttons_ & 1) ? 1 : 0;
}

int Context::toggle(int x, int y, int *on)
{
    if (!on)
        return 0;
    int w = 40, h = 22;
    int over = ptr_x_ >= x && ptr_x_ < x + w && ptr_y_ >= y && ptr_y_ < y + h;
    int clicked = over && (ptr_buttons_ & 1);
    if (clicked)
        *on = !*on;
    fill_round_rect(x, y, w, h, 11, *on ? rgba(50, 160, 80, 255) : rgba(80, 80, 85, 255));
    int kx = *on ? x + w - 18 : x + 4;
    fill_round_rect(kx, y + 3, 16, 16, 8, rgba(240, 240, 240, 255));
    return clicked;
}

int Context::slider(int x, int y, int w, float *value)
{
    if (!value || w <= 0)
        return 0;
    if (*value < 0)
        *value = 0;
    if (*value > 1)
        *value = 1;
    fill_round_rect(x, y + 6, w, 6, 3, rgba(60, 60, 65, 255));
    int filled = (int)(*value * (float)w);
    fill_round_rect(x, y + 6, filled, 6, 3, rgba(70, 140, 230, 255));
    int kx = x + filled - 6;
    fill_round_rect(kx, y, 12, 18, 4, rgba(230, 230, 235, 255));
    int over = ptr_x_ >= x && ptr_x_ < x + w && ptr_y_ >= y && ptr_y_ < y + 18;
    if (over && (ptr_buttons_ & 1)) {
        *value = (float)(ptr_x_ - x) / (float)w;
        if (*value < 0)
            *value = 0;
        if (*value > 1)
            *value = 1;
        return 1;
    }
    return 0;
}

void Context::scroll_view_begin(int x, int y, int w, int h, int *scroll_y)
{
    scissor_x_ = x;
    scissor_y_ = y;
    scissor_w_ = w;
    scissor_h_ = h;
    scroll_y_ptr_ = scroll_y ? *scroll_y : 0;
    if (scroll_y && wheel_) {
        *scroll_y -= wheel_ * 16;
        if (*scroll_y < 0)
            *scroll_y = 0;
        scroll_y_ptr_ = *scroll_y;
        wheel_ = 0;
    }
    stroke_rect(x, y, w, h, 1, rgba(80, 80, 90, 255));
    reed::Rect sc = {x, y, w, h};
    flush();
    cmd_.set_scissor(sc);
}

void Context::scroll_view_end()
{
    flush();
    reed::Rect sc = {0, 0, (int32_t)dev_->caps().width, (int32_t)dev_->caps().height};
    cmd_.set_scissor(sc);
    scissor_w_ = 0;
}

int fill_rect(int x, int y, int w, int h, unsigned color)
{
    /* No static dtors (freestanding — no atexit). */
    static uint8_t dev_mem[sizeof(reed::Device)];
    static uint8_t ctx_mem[sizeof(Context)];
    static int ready = 0;
    reed::Device *dev = (reed::Device *)dev_mem;
    Context *ctx = (Context *)ctx_mem;
    if (!ready) {
        new (dev) reed::Device();
        new (ctx) Context();
        if (dev->init() < 0)
            return -1;
        if (ctx->init(dev) < 0)
            return -1;
        g_ctx = ctx;
        ready = 1;
    }
    if (g_ctx->begin_frame() < 0)
        return -1;
    g_ctx->fill_rect(x, y, w, h, (uint32_t)color);
    return g_ctx->end_frame();
}

} /* namespace kilim */
