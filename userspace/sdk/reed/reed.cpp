#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>
#include <kernel/string.h>
#include <stdint.h>

/*
 * Reed — OpenGL-style userspace client.
 * Records DISP_CMD_* into a command buffer; submit → display.kmod → GpuProvider.
 * NEVER writes pixels. Raster/blit/compose run only in the GPU provider.
 */

namespace reed {

namespace {

static uint32_t align4(uint32_t n)
{
    return (n + 3u) & ~3u;
}

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
    cl.set_device(this);
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

Texture2D Device::adopt_texture(uint32_t handle)
{
    Texture2D t;
    if (!handle)
        return t;
    t.handle_ = handle;
    disp_texture_map a;
    memset(&a, 0, sizeof(a));
    a.handle = handle;
    if (call(DISP_OP_TEXTURE_MAP, &a) < 0 || !a.ptr) {
        t.handle_ = 0;
        return Texture2D();
    }
    t.width_ = a.width;
    t.height_ = a.height;
    t.stride_ = a.stride;
    t.mapped_ = nullptr;
    switch (a.format) {
    case DISP_FMT_R8:
        t.format_ = TexFormat::R8;
        break;
    case DISP_FMT_RG8:
        t.format_ = TexFormat::RG8;
        break;
    case DISP_FMT_A8:
        t.format_ = TexFormat::A8;
        break;
    default:
        t.format_ = TexFormat::RGBA8;
        break;
    }
    /* Probe only — unmap semantics: clear mapped so callers re-map if needed. */
    return t;
}

Texture2D Device::import_texture(uint32_t token)
{
    uint32_t handle = 0;
    if (import_handle(token, &handle) < 0 || handle == 0)
        return Texture2D();
    return adopt_texture(handle);
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
    : dev_(nullptr), recording_(false), ended_(false), draw_calls_(0), cmd_len_(0)
{
}

void CommandList::begin()
{
    recording_ = true;
    ended_ = false;
    draw_calls_ = 0;
    cmd_len_ = 0;
}

void CommandList::end()
{
    recording_ = false;
    ended_ = true;
}

int CommandList::emit(const void *pkt, uint32_t size)
{
    uint32_t aligned = align4(size);
    if (!recording_ || !pkt || size < sizeof(disp_cmd_hdr))
        return -1;
    if (cmd_len_ + aligned > kCmdCap)
        return -1;
    memcpy(cmd_ + cmd_len_, pkt, size);
    if (aligned > size)
        memset(cmd_ + cmd_len_ + size, 0, aligned - size);
    /* Fix hdr.size to aligned packet length. */
    {
        disp_cmd_hdr *h = (disp_cmd_hdr *)(cmd_ + cmd_len_);
        h->size = (uint16_t)aligned;
    }
    cmd_len_ += aligned;
    return 0;
}

void CommandList::bind_pipeline(const Pipeline &p)
{
    disp_cmd_bind_pipeline c;
    if (!p.valid())
        return;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_BIND_PIPELINE;
    c.hdr.size = (uint16_t)sizeof(c);
    c.topology = (uint32_t)p.desc().topology;
    c.cull = (uint32_t)p.desc().cull;
    c.blend = (uint32_t)p.desc().blend;
    c.shade = (uint32_t)p.desc().shade;
    c.depth_test = p.desc().depth_test;
    c.depth_write = p.desc().depth_write;
    (void)emit(&c, sizeof(c));
}

void CommandList::bind_vertex_buffer(const Buffer &b)
{
    disp_cmd_bind_handle c;
    if (!b.valid())
        return;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_BIND_VB;
    c.hdr.size = (uint16_t)sizeof(c);
    c.handle = b.handle();
    (void)emit(&c, sizeof(c));
}

void CommandList::bind_index_buffer(const Buffer &b)
{
    disp_cmd_bind_handle c;
    if (!b.valid())
        return;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_BIND_IB;
    c.hdr.size = (uint16_t)sizeof(c);
    c.handle = b.handle();
    (void)emit(&c, sizeof(c));
}

void CommandList::bind_texture(uint32_t slot, const Texture2D &t, const Sampler &s)
{
    disp_cmd_bind_tex c;
    if (!t.valid())
        return;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_BIND_TEX;
    c.hdr.size = (uint16_t)sizeof(c);
    c.slot = slot;
    c.handle = t.handle();
    c.wrap = (uint32_t)s.wrap();
    c.filter = (uint32_t)s.filter();
    (void)emit(&c, sizeof(c));
}

void CommandList::bind_render_target(RenderTarget &rt)
{
    disp_cmd_bind_handle c;
    if (!rt.valid())
        return;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_BIND_RT;
    c.hdr.size = (uint16_t)sizeof(c);
    c.handle = rt.handle();
    (void)emit(&c, sizeof(c));
}

void CommandList::set_uniform(const Uniforms &u)
{
    disp_cmd_set_uniform c;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_SET_UNIFORM;
    c.hdr.size = (uint16_t)sizeof(c);
    /* Layout matches disp_uniforms / reed::Uniforms. */
    memcpy(&c.u, &u, sizeof(disp_uniforms) < sizeof(Uniforms) ? sizeof(disp_uniforms)
                                                              : sizeof(Uniforms));
    (void)emit(&c, sizeof(c));
}

void CommandList::set_viewport(const Viewport &vp)
{
    disp_cmd_set_viewport c;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_SET_VIEWPORT;
    c.hdr.size = (uint16_t)sizeof(c);
    c.x = vp.x;
    c.y = vp.y;
    c.w = vp.w;
    c.h = vp.h;
    c.min_depth = vp.min_depth;
    c.max_depth = vp.max_depth;
    (void)emit(&c, sizeof(c));
}

void CommandList::set_scissor(const Rect &r)
{
    disp_cmd_set_scissor c;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_SET_SCISSOR;
    c.hdr.size = (uint16_t)sizeof(c);
    c.x = r.x;
    c.y = r.y;
    c.w = r.w;
    c.h = r.h;
    (void)emit(&c, sizeof(c));
}

void CommandList::clear(uint32_t color_rgba, float depth)
{
    disp_cmd_clear c;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_CLEAR;
    c.hdr.size = (uint16_t)sizeof(c);
    c.color_rgba = color_rgba;
    c.depth = depth;
    (void)emit(&c, sizeof(c));
}

void CommandList::draw(uint32_t count, uint32_t first)
{
    disp_cmd_draw c;
    if (!recording_)
        return;
    if (draw_calls_ >= (dev_ ? dev_->caps().max_draw_calls_per_frame : 4096u))
        return;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_DRAW;
    c.hdr.size = (uint16_t)sizeof(c);
    c.count = count;
    c.first = first;
    if (emit(&c, sizeof(c)) == 0)
        draw_calls_++;
}

void CommandList::draw_indexed(uint32_t count, uint32_t first_index, int32_t base_vertex)
{
    disp_cmd_draw_indexed c;
    if (!recording_)
        return;
    if (draw_calls_ >= (dev_ ? dev_->caps().max_draw_calls_per_frame : 4096u))
        return;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_DRAW_INDEXED;
    c.hdr.size = (uint16_t)sizeof(c);
    c.count = count;
    c.first_index = first_index;
    c.base_vertex = base_vertex;
    if (emit(&c, sizeof(c)) == 0)
        draw_calls_++;
}

void CommandList::blit(const RenderTarget &src, RenderTarget &dst, const Rect &region)
{
    blit(src.color(), dst, region.x, region.y, &region, BlendMode::Opaque);
}

void CommandList::blit(const Texture2D &src, RenderTarget &dst, int32_t dst_x, int32_t dst_y,
                       const Rect *src_rect, BlendMode blend)
{
    disp_cmd_blit c;
    if (!src.valid() || !dst.valid())
        return;
    memset(&c, 0, sizeof(c));
    c.hdr.op = DISP_CMD_BLIT;
    c.hdr.size = (uint16_t)sizeof(c);
    c.src_tex = src.handle();
    c.dst_rt = dst.handle();
    c.dst_x = dst_x;
    c.dst_y = dst_y;
    if (src_rect) {
        c.src_x = src_rect->x;
        c.src_y = src_rect->y;
        c.src_w = src_rect->w;
        c.src_h = src_rect->h;
    }
    c.blend = (uint32_t)blend;
    (void)emit(&c, sizeof(c));
}

int CommandList::submit(Fence *fence)
{
    disp_submit a;
    recording_ = false;
    ended_ = true;
    if (cmd_len_ == 0) {
        if (fence)
            return fence->signal();
        return 0;
    }
    memset(&a, 0, sizeof(a));
    a.cmds = cmd_;
    a.size = cmd_len_;
    a.fence = fence ? fence->handle() : 0;
    return (int)Device::call(DISP_OP_SUBMIT, &a);
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
