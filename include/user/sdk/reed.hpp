#pragma once

#include <stdint.h>
#include <user/disp.h>

/*
 * Reed — low-level graphics (Vulkan-portable surface, fixed-function shading).
 * Kernel counterpart: display.kmod via SYS_DISP_CALL.
 * Software rasterizer is the v1 backend; hw_accel_available is capability only.
 */

namespace reed {

enum class BufferKind : uint32_t {
    Vertex  = DISP_BUF_VERTEX,
    Index   = DISP_BUF_INDEX,
    Uniform = DISP_BUF_UNIFORM,
    Staging = DISP_BUF_STAGING,
    Color   = DISP_BUF_COLOR,
};

enum class TexFormat : uint32_t {
    R8    = DISP_FMT_R8,
    RG8   = DISP_FMT_RG8,
    RGBA8 = DISP_FMT_RGBA8,
    A8    = DISP_FMT_A8,
};

enum class ShadeMode : uint32_t {
    UnlitColor    = 0,
    UnlitTextured = 1,
    VertexLit     = 2,
    BlurH         = 3,
    BlurV         = 4,
    AcrylicTint   = 5,
};

enum class Topology : uint32_t {
    Triangles = 0,
    Lines     = 1,
    Points    = 2,
};

enum class CullMode : uint32_t {
    None  = 0,
    Back  = 1,
    Front = 2,
};

enum class BlendMode : uint32_t {
    Opaque      = 0,
    Alpha       = 1,
    Premultiplied = 2,
};

enum class WrapMode : uint32_t {
    Clamp  = 0,
    Repeat = 1,
};

enum class FilterMode : uint32_t {
    Nearest  = 0,
    Bilinear = 1,
};

struct Caps {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t caps;
    uint32_t max_texture_size;
    uint32_t max_draw_calls_per_frame;
    uint32_t hw_accel_available;
};

struct Stats {
    uint32_t buffers;
    uint32_t textures;
    uint32_t render_targets;
    uint32_t fences;
    uint32_t bytes_live;
};

struct Rect {
    int32_t x, y, w, h;
};

struct Viewport {
    float x, y, w, h;
    float min_depth;
    float max_depth;
};

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    uint32_t color; /* RGBA packed */
};

struct Light {
    float x, y, z;     /* position or direction */
    float intensity;
    uint32_t color;
    uint32_t directional; /* 0=point 1=dir */
};

struct Uniforms {
    float model[16];
    float view[16];
    float proj[16];
    Light lights[4];
    uint32_t light_count;
    uint32_t color;
    float tint[4];
    float blur_radius;
};

struct PipelineDesc {
    Topology  topology;
    CullMode  cull;
    BlendMode blend;
    ShadeMode shade;
    uint8_t   depth_test;
    uint8_t   depth_write;
};

class Device;
class Buffer;
class Texture2D;
class Sampler;
class RenderTarget;
class Pipeline;
class Fence;
class CommandList;

class Buffer {
public:
    Buffer() : handle_(0), kind_(BufferKind::Vertex), size_(0), mapped_(nullptr) {}
    bool valid() const { return handle_ != 0; }
    uint32_t handle() const { return handle_; }
    BufferKind kind() const { return kind_; }
    uint32_t size() const { return size_; }

    int update(uint32_t offset, const void *data, uint32_t len);
    void *map();
    void unmap();
    void destroy();

private:
    friend class Device;
    uint32_t handle_;
    BufferKind kind_;
    uint32_t size_;
    void *mapped_;
};

class Texture2D {
public:
    Texture2D()
        : handle_(0), format_(TexFormat::RGBA8), width_(0), height_(0),
          stride_(0), mapped_(nullptr) {}
    bool valid() const { return handle_ != 0; }
    uint32_t handle() const { return handle_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t stride() const { return stride_; }
    TexFormat format() const { return format_; }

    int upload(uint32_t level, const void *data, uint32_t len);
    void *map();
    const void *map() const;
    void unmap();
    void destroy();

private:
    friend class Device;
    friend class CommandList;
    uint32_t handle_;
    TexFormat format_;
    uint32_t width_, height_, stride_;
    void *mapped_;
};

class Sampler {
public:
    Sampler() : wrap_(WrapMode::Clamp), filter_(FilterMode::Nearest) {}
    WrapMode wrap() const { return wrap_; }
    FilterMode filter() const { return filter_; }
    void set(WrapMode w, FilterMode f) { wrap_ = w; filter_ = f; }

private:
    WrapMode wrap_;
    FilterMode filter_;
};

class RenderTarget {
public:
    RenderTarget() : handle_(0), color_(), depth_() {}
    bool valid() const { return handle_ != 0; }
    uint32_t handle() const { return handle_; }
    Texture2D &color() { return color_; }
    const Texture2D &color() const { return color_; }

    void destroy();

private:
    friend class Device;
    friend class CommandList;
    uint32_t handle_;
    Texture2D color_;
    Texture2D depth_;
};

class Pipeline {
public:
    Pipeline() : desc_(), alive_(false) {}
    bool valid() const { return alive_; }
    const PipelineDesc &desc() const { return desc_; }

private:
    friend class Device;
    PipelineDesc desc_;
    bool alive_;
};

class Fence {
public:
    Fence() : handle_(0) {}
    bool valid() const { return handle_ != 0; }
    uint32_t handle() const { return handle_; }

    int signal();
    int wait(int32_t timeout_ticks = -1);
    void destroy();

private:
    friend class Device;
    friend class CommandList;
    uint32_t handle_;
};

class CommandList {
public:
    CommandList();

    void begin();
    void end();

    void bind_pipeline(const Pipeline &p);
    void bind_vertex_buffer(const Buffer &b);
    void bind_index_buffer(const Buffer &b);
    void bind_texture(uint32_t slot, const Texture2D &t, const Sampler &s);
    void bind_render_target(RenderTarget &rt);
    void set_uniform(const Uniforms &u);
    void set_viewport(const Viewport &vp);
    void set_scissor(const Rect &r);
    void clear(uint32_t color_rgba, float depth = 1.0f);
    void draw(uint32_t count, uint32_t first = 0);
    void draw_indexed(uint32_t count, uint32_t first_index = 0, int32_t base_vertex = 0);
    void blit(const RenderTarget &src, RenderTarget &dst, const Rect &region);

    /* Execute SW rasterizer into bound RT; optional fence signal after. */
    int submit(Fence *fence = nullptr);

    /* Present bound RT (or explicit) to scanout. */
    int present(const RenderTarget &rt, const Rect *damage = nullptr);

private:
    friend class Device;
    Device *dev_;
    bool recording_;
    bool ended_;
    PipelineDesc pipe_;
    const Vertex *vb_;
    uint32_t vb_count_;
    const uint32_t *ib_;
    uint32_t ib_count_;
    Texture2D tex0_;
    Sampler samp0_;
    RenderTarget *rt_;
    Uniforms uniforms_;
    Viewport viewport_;
    Rect scissor_;
    uint32_t draw_calls_;

    void raster_triangles(uint32_t count, uint32_t first, const uint32_t *idx,
                          uint32_t idx_count, int32_t base_vertex);
};

class Device {
public:
    Device();
    ~Device();

    /* Initialize against display.kmod. No heap. */
    int init();
    void destroy();

    bool valid() const { return alive_; }
    const Caps &caps() const { return caps_; }
    Stats stats() const;

    Buffer create_buffer(BufferKind kind, uint32_t size, const void *data = nullptr);
    Texture2D create_texture(TexFormat fmt, uint32_t w, uint32_t h, uint32_t levels = 1);
    RenderTarget create_render_target(uint32_t w, uint32_t h, bool depth = false);
    Pipeline create_pipeline(const PipelineDesc &desc);
    Fence create_fence();
    CommandList create_command_list();

    int export_handle(uint32_t handle, uint32_t *token_out);
    int import_handle(uint32_t token, uint32_t *handle_out);

    /* Screen-sized RGBA8 RT helper for fullscreen present. */
    RenderTarget create_swapchain_target();

private:
    friend class CommandList;
    friend class Buffer;
    friend class Texture2D;
    friend class RenderTarget;
    friend class Fence;

    bool alive_;
    Caps caps_;

    static long call(uint32_t op, void *arg);
};

/* Backward-compat stub name used by early Kilim. */
int create_device(void);

} /* namespace reed */
