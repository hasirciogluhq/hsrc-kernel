#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>
#include <kernel/string.h>
#include <stdint.h>

namespace reed {

namespace {

static void mat4_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul_vec4(const float *m, float x, float y, float z, float w,
                          float *ox, float *oy, float *oz, float *ow)
{
    *ox = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
    *oy = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
    *oz = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
    *ow = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
}

static uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void unpack_rgba(uint32_t c, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    *a = (uint8_t)((c >> 24) & 0xffu);
    *r = (uint8_t)((c >> 16) & 0xffu);
    *g = (uint8_t)((c >> 8) & 0xffu);
    *b = (uint8_t)(c & 0xffu);
}

static uint32_t blend_alpha(uint32_t dst, uint32_t src)
{
    uint8_t sr, sg, sb, sa, dr, dg, db, da;
    unpack_rgba(src, &sr, &sg, &sb, &sa);
    unpack_rgba(dst, &dr, &dg, &db, &da);
    if (sa == 255)
        return src;
    if (sa == 0)
        return dst;
    uint32_t inv = 255u - (uint32_t)sa;
    uint8_t r = (uint8_t)(((uint32_t)sr * sa + (uint32_t)dr * inv) / 255u);
    uint8_t g = (uint8_t)(((uint32_t)sg * sa + (uint32_t)dg * inv) / 255u);
    uint8_t b = (uint8_t)(((uint32_t)sb * sa + (uint32_t)db * inv) / 255u);
    uint8_t a = (uint8_t)(sa + (uint32_t)da * inv / 255u);
    return pack_rgba(r, g, b, a);
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* Max vertices/indices per draw — DoS / hang guard (plan #4). */
static const uint32_t kMaxVertsPerDraw = 65536;
static const uint32_t kMaxTrisBudget = 200000;

} /* namespace */

long Device::call(uint32_t op, void *arg)
{
    return hsrc::sdk::syscall2(SYS_DISP_CALL, (long)op, (long)(uintptr_t)arg);
}

Device::Device() : alive_(false)
{
    memset(&caps_, 0, sizeof(caps_));
}

Device::~Device()
{
    destroy();
}

int Device::init()
{
    disp_info info;
    memset(&info, 0, sizeof(info));
    if (call(DISP_OP_INFO, &info) < 0)
        return -1;
    caps_.width = info.width;
    caps_.height = info.height;
    caps_.bpp = info.bpp;
    caps_.caps = info.caps;
    caps_.max_texture_size = info.max_texture_size;
    caps_.max_draw_calls_per_frame = info.max_draw_calls_per_frame;
    caps_.hw_accel_available = info.hw_accel_available;
    alive_ = true;
    return 0;
}

void Device::destroy()
{
    alive_ = false;
}

Stats Device::stats() const
{
    Stats s;
    disp_stats st;
    memset(&s, 0, sizeof(s));
    memset(&st, 0, sizeof(st));
    if (call(DISP_OP_STATS, &st) < 0)
        return s;
    s.buffers = st.buffers;
    s.textures = st.textures;
    s.render_targets = st.render_targets;
    s.fences = st.fences;
    s.bytes_live = st.bytes_live;
    return s;
}

Buffer Device::create_buffer(BufferKind kind, uint32_t size, const void *data)
{
    Buffer b;
    disp_buffer_create a;
    memset(&a, 0, sizeof(a));
    a.kind = (uint32_t)kind;
    a.size = size;
    if (call(DISP_OP_BUFFER_CREATE, &a) < 0)
        return b;
    b.handle_ = a.handle;
    b.kind_ = kind;
    b.size_ = size;
    if (data && size)
        (void)b.update(0, data, size);
    return b;
}

Texture2D Device::create_texture(TexFormat fmt, uint32_t w, uint32_t h, uint32_t levels)
{
    Texture2D t;
    disp_texture_create a;
    memset(&a, 0, sizeof(a));
    a.format = (uint32_t)fmt;
    a.width = w;
    a.height = h;
    a.levels = levels ? levels : 1;
    if (call(DISP_OP_TEXTURE_CREATE, &a) < 0)
        return t;
    t.handle_ = a.handle;
    t.format_ = fmt;
    t.width_ = w;
    t.height_ = h;
    uint32_t bpp = 4;
    if (fmt == TexFormat::R8 || fmt == TexFormat::A8)
        bpp = 1;
    else if (fmt == TexFormat::RG8)
        bpp = 2;
    t.stride_ = w * bpp;
    return t;
}

RenderTarget Device::create_render_target(uint32_t w, uint32_t h, bool depth)
{
    RenderTarget rt;
    Texture2D color = create_texture(TexFormat::RGBA8, w, h, 1);
    if (!color.valid())
        return rt;
    Texture2D depth_tex;
    if (depth) {
        depth_tex = create_texture(TexFormat::R8, w, h, 1);
        if (!depth_tex.valid()) {
            color.destroy();
            return rt;
        }
    }
    disp_rt_create a;
    memset(&a, 0, sizeof(a));
    a.color_tex = color.handle();
    a.depth_tex = depth_tex.valid() ? depth_tex.handle() : 0;
    if (call(DISP_OP_RT_CREATE, &a) < 0) {
        color.destroy();
        depth_tex.destroy();
        return rt;
    }
    rt.handle_ = a.handle;
    rt.color_ = color;
    rt.depth_ = depth_tex;
    return rt;
}

RenderTarget Device::create_swapchain_target()
{
    if (!alive_ || caps_.width == 0 || caps_.height == 0)
        return RenderTarget();
    return create_render_target(caps_.width, caps_.height, false);
}

Pipeline Device::create_pipeline(const PipelineDesc &desc)
{
    Pipeline p;
    p.desc_ = desc;
    p.alive_ = true;
    return p;
}

Fence Device::create_fence()
{
    Fence f;
    disp_fence_create a;
    memset(&a, 0, sizeof(a));
    if (call(DISP_OP_FENCE_CREATE, &a) < 0)
        return f;
    f.handle_ = a.handle;
    return f;
}

CommandList Device::create_command_list()
{
    CommandList cl;
    cl.dev_ = this;
    return cl;
}

int Device::export_handle(uint32_t handle, uint32_t *token_out)
{
    disp_export a;
    memset(&a, 0, sizeof(a));
    a.handle = handle;
    if (call(DISP_OP_EXPORT, &a) < 0)
        return -1;
    if (token_out)
        *token_out = a.token;
    return 0;
}

int Device::import_handle(uint32_t token, uint32_t *handle_out)
{
    disp_import a;
    memset(&a, 0, sizeof(a));
    a.token = token;
    if (call(DISP_OP_IMPORT, &a) < 0)
        return -1;
    if (handle_out)
        *handle_out = a.handle;
    return 0;
}

int Buffer::update(uint32_t offset, const void *data, uint32_t len)
{
    disp_buffer_update a;
    if (!handle_ || !data)
        return -1;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    a.offset = offset;
    a.len = len;
    a.data = data;
    return (int)Device::call(DISP_OP_BUFFER_UPDATE, &a);
}

void *Buffer::map()
{
    disp_buffer_map a;
    if (!handle_)
        return nullptr;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    if (Device::call(DISP_OP_BUFFER_MAP, &a) < 0)
        return nullptr;
    mapped_ = a.ptr;
    return mapped_;
}

void Buffer::unmap()
{
    disp_handle_arg a;
    if (!handle_)
        return;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    (void)Device::call(DISP_OP_BUFFER_UNMAP, &a);
    mapped_ = nullptr;
}

void Buffer::destroy()
{
    disp_handle_arg a;
    if (!handle_)
        return;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    (void)Device::call(DISP_OP_BUFFER_DESTROY, &a);
    handle_ = 0;
    mapped_ = nullptr;
    size_ = 0;
}

int Texture2D::upload(uint32_t level, const void *data, uint32_t len)
{
    disp_texture_upload a;
    if (!handle_ || !data)
        return -1;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    a.level = level;
    a.len = len;
    a.data = data;
    return (int)Device::call(DISP_OP_TEXTURE_UPLOAD, &a);
}

void *Texture2D::map()
{
    disp_texture_map a;
    if (!handle_)
        return nullptr;
    if (mapped_)
        return mapped_;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    if (Device::call(DISP_OP_TEXTURE_MAP, &a) < 0)
        return nullptr;
    mapped_ = a.ptr;
    width_ = a.width;
    height_ = a.height;
    stride_ = a.stride;
    return mapped_;
}

const void *Texture2D::map() const
{
    return mapped_;
}

void Texture2D::unmap()
{
    mapped_ = nullptr;
}

void Texture2D::destroy()
{
    disp_handle_arg a;
    if (!handle_)
        return;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    (void)Device::call(DISP_OP_TEXTURE_DESTROY, &a);
    handle_ = 0;
    mapped_ = nullptr;
}

void RenderTarget::destroy()
{
    disp_handle_arg a;
    if (handle_) {
        memset(&a, 0, sizeof(a));
        a.handle = handle_;
        (void)Device::call(DISP_OP_RT_DESTROY, &a);
        handle_ = 0;
    }
    color_.destroy();
    depth_.destroy();
}

int Fence::signal()
{
    disp_handle_arg a;
    if (!handle_)
        return -1;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    return (int)Device::call(DISP_OP_FENCE_SIGNAL, &a);
}

int Fence::wait(int32_t timeout_ticks)
{
    disp_fence_wait a;
    if (!handle_)
        return -1;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    a.timeout_ticks = timeout_ticks;
    return (int)Device::call(DISP_OP_FENCE_WAIT, &a);
}

void Fence::destroy()
{
    disp_handle_arg a;
    if (!handle_)
        return;
    memset(&a, 0, sizeof(a));
    a.handle = handle_;
    (void)Device::call(DISP_OP_FENCE_DESTROY, &a);
    handle_ = 0;
}

CommandList::CommandList()
    : dev_(nullptr), recording_(false), ended_(false), vb_(nullptr), vb_count_(0),
      ib_(nullptr), ib_count_(0), rt_(nullptr), draw_calls_(0)
{
    memset(&pipe_, 0, sizeof(pipe_));
    memset(&uniforms_, 0, sizeof(uniforms_));
    mat4_identity(uniforms_.model);
    mat4_identity(uniforms_.view);
    mat4_identity(uniforms_.proj);
    viewport_ = {0, 0, 0, 0, 0, 1};
    scissor_ = {0, 0, 0, 0};
}

void CommandList::begin()
{
    recording_ = true;
    ended_ = false;
    draw_calls_ = 0;
    vb_ = nullptr;
    vb_count_ = 0;
    ib_ = nullptr;
    ib_count_ = 0;
    rt_ = nullptr;
}

void CommandList::end()
{
    recording_ = false;
    ended_ = true;
}

void CommandList::bind_pipeline(const Pipeline &p)
{
    if (p.valid())
        pipe_ = p.desc();
}

void CommandList::bind_vertex_buffer(const Buffer &b)
{
    void *p = const_cast<Buffer &>(b).map();
    if (!p) {
        vb_ = nullptr;
        vb_count_ = 0;
        return;
    }
    vb_ = (const Vertex *)p;
    vb_count_ = b.size() / (uint32_t)sizeof(Vertex);
}

void CommandList::bind_index_buffer(const Buffer &b)
{
    void *p = const_cast<Buffer &>(b).map();
    if (!p) {
        ib_ = nullptr;
        ib_count_ = 0;
        return;
    }
    ib_ = (const uint32_t *)p;
    ib_count_ = b.size() / 4u;
}

void CommandList::bind_texture(uint32_t slot, const Texture2D &t, const Sampler &s)
{
    if (slot == 0) {
        tex0_ = t;
        samp0_ = s;
        (void)tex0_.map();
    }
}

void CommandList::bind_render_target(RenderTarget &rt)
{
    rt_ = &rt;
    (void)rt.color().map();
}

void CommandList::set_uniform(const Uniforms &u)
{
    uniforms_ = u;
}

void CommandList::set_viewport(const Viewport &vp)
{
    viewport_ = vp;
}

void CommandList::set_scissor(const Rect &r)
{
    scissor_ = r;
}

void CommandList::clear(uint32_t color_rgba, float /*depth*/)
{
    if (!rt_ || !rt_->color().map())
        return;
    uint32_t *px = (uint32_t *)rt_->color().map();
    uint32_t w = rt_->color().width();
    uint32_t h = rt_->color().height();
    uint32_t stride = rt_->color().stride() / 4u;
    int32_t x0 = 0, y0 = 0, x1 = (int32_t)w, y1 = (int32_t)h;
    if (scissor_.w > 0 && scissor_.h > 0) {
        x0 = clampi(scissor_.x, 0, (int32_t)w);
        y0 = clampi(scissor_.y, 0, (int32_t)h);
        x1 = clampi(scissor_.x + scissor_.w, 0, (int32_t)w);
        y1 = clampi(scissor_.y + scissor_.h, 0, (int32_t)h);
    }
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *row = px + (uint32_t)y * stride + (uint32_t)x0;
        int32_t n = x1 - x0;
        for (int32_t x = 0; x < n; x++)
            row[x] = color_rgba;
    }
}

static uint32_t sample_tex(const Texture2D &tex, const Sampler &samp, float u, float v)
{
    if (!tex.valid() || !tex.map())
        return 0xffffffffu;
    uint32_t tw = tex.width();
    uint32_t th = tex.height();
    if (tw == 0 || th == 0)
        return 0xffffffffu;

    if (samp.wrap() == WrapMode::Repeat) {
        u = u - (float)(int32_t)u;
        v = v - (float)(int32_t)v;
        if (u < 0)
            u += 1.0f;
        if (v < 0)
            v += 1.0f;
    } else {
        u = clampf(u, 0.0f, 1.0f);
        v = clampf(v, 0.0f, 1.0f);
    }

    float fx = u * (float)(tw - 1u);
    float fy = v * (float)(th - 1u);
    int32_t x0 = (int32_t)fx;
    int32_t y0 = (int32_t)fy;
    x0 = clampi(x0, 0, (int32_t)tw - 1);
    y0 = clampi(y0, 0, (int32_t)th - 1);

    const uint8_t *base = (const uint8_t *)tex.map();
    uint32_t stride = tex.stride();

    if (tex.format() == TexFormat::RGBA8) {
        const uint32_t *row = (const uint32_t *)(base + (uint32_t)y0 * stride);
        return row[x0];
    }
    if (tex.format() == TexFormat::A8 || tex.format() == TexFormat::R8) {
        uint8_t a = base[(uint32_t)y0 * stride + (uint32_t)x0];
        return pack_rgba(255, 255, 255, a);
    }
    return 0xffffffffu;
}

static uint32_t shade_pixel(const PipelineDesc &pipe, const Uniforms &u,
                            const Texture2D &tex, const Sampler &samp,
                            const Vertex &v)
{
    uint32_t base = v.color ? v.color : u.color;
    if (base == 0)
        base = 0xffffffffu;

    switch (pipe.shade) {
    case ShadeMode::UnlitColor:
        return base;
    case ShadeMode::UnlitTextured: {
        uint32_t tc = sample_tex(tex, samp, v.u, v.v);
        uint8_t tr, tg, tb, ta, br, bg, bb, ba;
        unpack_rgba(tc, &tr, &tg, &tb, &ta);
        unpack_rgba(base, &br, &bg, &bb, &ba);
        return pack_rgba((uint8_t)((tr * br) / 255u), (uint8_t)((tg * bg) / 255u),
                         (uint8_t)((tb * bb) / 255u), (uint8_t)((ta * ba) / 255u));
    }
    case ShadeMode::VertexLit: {
        float ndl = 0.25f; /* ambient */
        for (uint32_t i = 0; i < u.light_count && i < 4u; i++) {
            float lx = u.lights[i].x, ly = u.lights[i].y, lz = u.lights[i].z;
            float len = lx * lx + ly * ly + lz * lz;
            if (len > 0.0001f) {
                /* Quake-style invsqrt approximation (no libm). */
                float xhalf = 0.5f * len;
                union { float f; uint32_t i; } u = { len };
                u.i = 0x5f3759dfu - (u.i >> 1);
                float inv = u.f * (1.5f - xhalf * u.f * u.f);
                lx *= inv;
                ly *= inv;
                lz *= inv;
            }
            float d = v.nx * lx + v.ny * ly + v.nz * lz;
            if (d < 0)
                d = 0;
            ndl += d * u.lights[i].intensity;
        }
        ndl = clampf(ndl, 0.0f, 1.0f);
        uint8_t r, g, b, a;
        unpack_rgba(base, &r, &g, &b, &a);
        return pack_rgba((uint8_t)((float)r * ndl), (uint8_t)((float)g * ndl),
                         (uint8_t)((float)b * ndl), a);
    }
    case ShadeMode::BlurH:
    case ShadeMode::BlurV:
    case ShadeMode::AcrylicTint:
        return base;
    default:
        return base;
    }
}

void CommandList::raster_triangles(uint32_t count, uint32_t first, const uint32_t *idx,
                                   uint32_t idx_count, int32_t base_vertex)
{
    if (!rt_ || !vb_ || !dev_)
        return;
    uint32_t *fb = (uint32_t *)rt_->color().map();
    if (!fb)
        return;

    uint32_t fw = rt_->color().width();
    uint32_t fh = rt_->color().height();
    uint32_t fstride = rt_->color().stride() / 4u;
    if (fw == 0 || fh == 0)
        return;

    float vpx = viewport_.w > 0 ? viewport_.x : 0.0f;
    float vpy = viewport_.h > 0 ? viewport_.y : 0.0f;
    float vpw = viewport_.w > 0 ? viewport_.w : (float)fw;
    float vph = viewport_.h > 0 ? viewport_.h : (float)fh;

    int32_t sx0 = 0, sy0 = 0, sx1 = (int32_t)fw, sy1 = (int32_t)fh;
    if (scissor_.w > 0 && scissor_.h > 0) {
        sx0 = clampi(scissor_.x, 0, (int32_t)fw);
        sy0 = clampi(scissor_.y, 0, (int32_t)fh);
        sx1 = clampi(scissor_.x + scissor_.w, 0, (int32_t)fw);
        sy1 = clampi(scissor_.y + scissor_.h, 0, (int32_t)fh);
    }

    uint32_t tri_budget = kMaxTrisBudget;
    uint32_t ntri = count / 3u;
    if (ntri > tri_budget)
        ntri = tri_budget;

    for (uint32_t t = 0; t < ntri; t++) {
        uint32_t i0, i1, i2;
        if (idx) {
            uint32_t base = first + t * 3u;
            if (base + 2u >= idx_count)
                break;
            i0 = (uint32_t)((int32_t)idx[base + 0] + base_vertex);
            i1 = (uint32_t)((int32_t)idx[base + 1] + base_vertex);
            i2 = (uint32_t)((int32_t)idx[base + 2] + base_vertex);
        } else {
            i0 = first + t * 3u + 0u;
            i1 = first + t * 3u + 1u;
            i2 = first + t * 3u + 2u;
        }
        if (i0 >= vb_count_ || i1 >= vb_count_ || i2 >= vb_count_)
            continue;

        Vertex v0 = vb_[i0], v1 = vb_[i1], v2 = vb_[i2];

        /* MVP transform (column-major). */
        float x0, y0, z0, w0, x1, y1, z1, w1, x2, y2, z2, w2;
        float mx, my, mz, mw;
        mat4_mul_vec4(uniforms_.model, v0.x, v0.y, v0.z, 1.0f, &mx, &my, &mz, &mw);
        mat4_mul_vec4(uniforms_.view, mx, my, mz, mw, &mx, &my, &mz, &mw);
        mat4_mul_vec4(uniforms_.proj, mx, my, mz, mw, &x0, &y0, &z0, &w0);

        mat4_mul_vec4(uniforms_.model, v1.x, v1.y, v1.z, 1.0f, &mx, &my, &mz, &mw);
        mat4_mul_vec4(uniforms_.view, mx, my, mz, mw, &mx, &my, &mz, &mw);
        mat4_mul_vec4(uniforms_.proj, mx, my, mz, mw, &x1, &y1, &z1, &w1);

        mat4_mul_vec4(uniforms_.model, v2.x, v2.y, v2.z, 1.0f, &mx, &my, &mz, &mw);
        mat4_mul_vec4(uniforms_.view, mx, my, mz, mw, &mx, &my, &mz, &mw);
        mat4_mul_vec4(uniforms_.proj, mx, my, mz, mw, &x2, &y2, &z2, &w2);

        if (w0 == 0.0f || w1 == 0.0f || w2 == 0.0f)
            continue;
        x0 /= w0;
        y0 /= w0;
        x1 /= w1;
        y1 /= w1;
        x2 /= w2;
        y2 /= w2;

        /* NDC [-1,1] → viewport pixels. */
        float px0 = vpx + (x0 * 0.5f + 0.5f) * vpw;
        float py0 = vpy + (1.0f - (y0 * 0.5f + 0.5f)) * vph;
        float px1 = vpx + (x1 * 0.5f + 0.5f) * vpw;
        float py1 = vpy + (1.0f - (y1 * 0.5f + 0.5f)) * vph;
        float px2 = vpx + (x2 * 0.5f + 0.5f) * vpw;
        float py2 = vpy + (1.0f - (y2 * 0.5f + 0.5f)) * vph;

        float area = (px1 - px0) * (py2 - py0) - (px2 - px0) * (py1 - py0);
        if (pipe_.cull == CullMode::Back && area <= 0.0f)
            continue;
        if (pipe_.cull == CullMode::Front && area >= 0.0f)
            continue;
        if (area == 0.0f)
            continue;

        int32_t minx = (int32_t)(px0 < px1 ? (px0 < px2 ? px0 : px2) : (px1 < px2 ? px1 : px2));
        int32_t maxx = (int32_t)(px0 > px1 ? (px0 > px2 ? px0 : px2) : (px1 > px2 ? px1 : px2)) + 1;
        int32_t miny = (int32_t)(py0 < py1 ? (py0 < py2 ? py0 : py2) : (py1 < py2 ? py1 : py2));
        int32_t maxy = (int32_t)(py0 > py1 ? (py0 > py2 ? py0 : py2) : (py1 > py2 ? py1 : py2)) + 1;
        minx = clampi(minx, sx0, sx1 - 1);
        maxx = clampi(maxx, sx0, sx1 - 1);
        miny = clampi(miny, sy0, sy1 - 1);
        maxy = clampi(maxy, sy0, sy1 - 1);

        float inv_area = 1.0f / area;
        for (int32_t y = miny; y <= maxy; y++) {
            for (int32_t x = minx; x <= maxx; x++) {
                float px = (float)x + 0.5f;
                float py = (float)y + 0.5f;
                float w0b = ((px1 - px) * (py2 - py) - (px2 - px) * (py1 - py)) * inv_area;
                float w1b = ((px2 - px) * (py0 - py) - (px0 - px) * (py2 - py)) * inv_area;
                float w2b = 1.0f - w0b - w1b;
                if (w0b < 0.0f || w1b < 0.0f || w2b < 0.0f)
                    continue;

                Vertex pv;
                pv.x = v0.x * w0b + v1.x * w1b + v2.x * w2b;
                pv.y = v0.y * w0b + v1.y * w1b + v2.y * w2b;
                pv.z = v0.z * w0b + v1.z * w1b + v2.z * w2b;
                pv.nx = v0.nx * w0b + v1.nx * w1b + v2.nx * w2b;
                pv.ny = v0.ny * w0b + v1.ny * w1b + v2.ny * w2b;
                pv.nz = v0.nz * w0b + v1.nz * w1b + v2.nz * w2b;
                pv.u = v0.u * w0b + v1.u * w1b + v2.u * w2b;
                pv.v = v0.v * w0b + v1.v * w1b + v2.v * w2b;
                /* Flat color from v0 for unlit; barycentric lerp of channels skipped for speed. */
                pv.color = v0.color;

                uint32_t src = shade_pixel(pipe_, uniforms_, tex0_, samp0_, pv);
                uint32_t *dst = &fb[(uint32_t)y * fstride + (uint32_t)x];
                if (pipe_.blend == BlendMode::Opaque)
                    *dst = src;
                else
                    *dst = blend_alpha(*dst, src);
            }
        }
    }
}

void CommandList::draw(uint32_t count, uint32_t first)
{
    if (!recording_ || !rt_)
        return;
    if (draw_calls_ >= (dev_ ? dev_->caps().max_draw_calls_per_frame : 4096u))
        return;
    if (count > kMaxVertsPerDraw)
        count = kMaxVertsPerDraw;
    draw_calls_++;
    if (pipe_.topology == Topology::Triangles)
        raster_triangles(count, first, nullptr, 0, 0);
}

void CommandList::draw_indexed(uint32_t count, uint32_t first_index, int32_t base_vertex)
{
    if (!recording_ || !rt_ || !ib_)
        return;
    if (draw_calls_ >= (dev_ ? dev_->caps().max_draw_calls_per_frame : 4096u))
        return;
    if (count > kMaxVertsPerDraw)
        count = kMaxVertsPerDraw;
    draw_calls_++;
    if (pipe_.topology == Topology::Triangles)
        raster_triangles(count, first_index, ib_, ib_count_, base_vertex);
}

void CommandList::blit(const RenderTarget &src, RenderTarget &dst, const Rect &region)
{
    uint32_t *s = (uint32_t *)const_cast<Texture2D &>(src.color()).map();
    uint32_t *d = (uint32_t *)dst.color().map();
    if (!s || !d)
        return;
    uint32_t sw = src.color().width();
    uint32_t sh = src.color().height();
    uint32_t dw = dst.color().width();
    uint32_t dh = dst.color().height();
    uint32_t ss = src.color().stride() / 4u;
    uint32_t ds = dst.color().stride() / 4u;
    int32_t x0 = clampi(region.x, 0, (int32_t)sw);
    int32_t y0 = clampi(region.y, 0, (int32_t)sh);
    int32_t x1 = clampi(region.x + region.w, 0, (int32_t)sw);
    int32_t y1 = clampi(region.y + region.h, 0, (int32_t)sh);
    if (x1 > (int32_t)dw)
        x1 = (int32_t)dw;
    if (y1 > (int32_t)dh)
        y1 = (int32_t)dh;
    for (int32_t y = y0; y < y1; y++)
        for (int32_t x = x0; x < x1; x++)
            d[(uint32_t)y * ds + (uint32_t)x] = s[(uint32_t)y * ss + (uint32_t)x];
}

int CommandList::submit(Fence *fence)
{
    ended_ = true;
    recording_ = false;
    if (fence)
        return fence->signal();
    return 0;
}

int CommandList::present(const RenderTarget &rt, const Rect *damage)
{
    disp_scanout a;
    memset(&a, 0, sizeof(a));
    a.handle = rt.handle() ? rt.handle() : rt.color().handle();
    if (damage && damage->w > 0 && damage->h > 0) {
        a.x = (uint32_t)damage->x;
        a.y = (uint32_t)damage->y;
        a.w = (uint32_t)damage->w;
        a.h = (uint32_t)damage->h;
    }
    return (int)Device::call(DISP_OP_SCANOUT, &a);
}

int create_device(void)
{
    Device d;
    return d.init();
}

} /* namespace reed */
