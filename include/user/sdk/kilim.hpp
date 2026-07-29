#pragma once

#include <stdint.h>
#include <user/sdk/reed.hpp>

/*
 * Kilim — game-engine-style drawlist on Reed (ImGui DrawList analogue).
 * Emits quads/meshes into Reed; never writes framebuffer pixels itself.
 * Reed → display.kmod → GpuProvider does all raster/blit/compose.
 */

namespace kilim {

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };

struct Transform {
    Vec3 pos{0, 0, 0};
    Vec3 rot{0, 0, 0}; /* radians XYZ */
    Vec3 scale{1, 1, 1};
    void to_matrix(float out[16]) const;
};

struct Camera {
    Vec3 eye{0, 0, 3};
    Vec3 target{0, 0, 0};
    Vec3 up{0, 1, 0};
    float fov_y = 1.0f;
    float aspect = 1.0f;
    float znear = 0.1f;
    float zfar = 100.0f;
    int orthographic = 0;
    float ortho_w = 2.0f;
    float ortho_h = 2.0f;
    void view_matrix(float out[16]) const;
    void proj_matrix(float out[16]) const;
};

struct Material {
    reed::ShadeMode shade = reed::ShadeMode::UnlitColor;
    reed::Texture2D *texture = nullptr;
    uint32_t color = 0xffffffffu;
    reed::Light lights[4]{};
    uint32_t light_count = 0;
};

struct Mesh {
    reed::Vertex *verts = nullptr;
    uint32_t vert_count = 0;
    uint32_t *indices = nullptr;
    uint32_t index_count = 0;
    int owns = 0;
    void destroy();
};

/* Static .kmesh: magic 'KMSH', vert_count, index_count, verts[], indices[] */
int mesh_load_kmesh(const void *data, uint32_t size, Mesh *out);

struct SceneNode {
    Transform xform;
    Mesh *mesh = nullptr;
    Material *material = nullptr;
    SceneNode *child = nullptr;
    SceneNode *sibling = nullptr;
};

struct Scene {
    SceneNode *root = nullptr;
    Camera camera;
};

class Batch {
public:
    Batch() : pipe_key_(0), tex_handle_(0), vcount_(0), icount_(0) {}
    reed::Buffer &reed_vb() { return vb_; }
    reed::Buffer &reed_ib() { return ib_; }
    uint32_t vcount() const { return vcount_; }
    uint32_t icount() const { return icount_; }

private:
    friend class Context;
    uint32_t pipe_key_;
    uint32_t tex_handle_;
    reed::Texture2D tex_; /* pipe_key 3 — generic image texture */
    reed::Vertex verts_[2048];
    uint32_t indices_[4096];
    uint32_t vcount_, icount_;
    reed::Buffer vb_, ib_;
};

class Font {
public:
    Font();
    /* Built-in baked atlas (path ignored for v1; load() still succeeds). */
    int load(const char *path = nullptr);
    bool valid() const { return ready_; }
    reed::Texture2D &atlas() { return atlas_; }
    int glyph_advance(char c) const;
    int line_height(int size) const;

private:
    friend class Context;
    bool ready_;
    reed::Texture2D atlas_;
    uint16_t glyph_x_[95];
    uint16_t glyph_adv_[95];
    uint32_t atlas_w_, atlas_h_;
};

class Context {
public:
    Context();
    int init(reed::Device *dev);
    void shutdown();
    bool valid() const { return alive_; }

    reed::Device *device() { return dev_; }
    reed::RenderTarget &target() { return target_; }
    Font &font() { return font_; }

    int begin_frame();
    /*
     * Partial-redraw: scissors to [x,y,w,h], skips full-screen clear.
     * Pair with commit_frame() + present_damage() (WM).
     */
    int begin_frame_region(int x, int y, int w, int h);
    int end_frame(); /* flush batches + present to scanout */
    /* flush + submit only — for WM map_surface / composite (no present). */
    int commit_frame();
    /* Present current target with optional damage (no clear). */
    int present_damage(int x, int y, int w, int h);
    void flush();    /* emit accumulated batches into cmd (no submit) */

    /* 2D — batched */
    void fill_rect(int x, int y, int w, int h, uint32_t color);
    void fill_round_rect(int x, int y, int w, int h, int radius, uint32_t color);
    void stroke_rect(int x, int y, int w, int h, int thickness, uint32_t color);
    void line(int x0, int y0, int x1, int y1, uint32_t color);
    void circle(int cx, int cy, int radius, uint32_t color, int filled = 1);
    void polygon(const int *xy, int npoints, uint32_t color, int filled = 1);

    /*
     * Textured quad (batched, UnlitTextured). w/h <= 0 → texture size.
     * Good for icons/cursor; full window surfaces prefer blit() (row-copy).
     */
    void image(reed::Texture2D &tex, int x, int y, int w = -1, int h = -1,
               uint32_t tint = 0xffffffffu);

    /* Textured axis-aligned quad with explicit UVs (ImGui glyphs / atlas). */
    void image_uv(reed::Texture2D &tex, float x0, float y0, float x1, float y1,
                  float u0, float v0, float u1, float v1,
                  uint32_t tint = 0xffffffffu);

    /* GPU scissor for nested clip (flush + Reed set_scissor). */
    void set_clip(int x, int y, int w, int h);
    void clear_clip();

    /*
     * Compositor surface copy via Reed blit cmd (GPU path).
     * Flushes pending batches first so draw order stays correct.
     */
    void blit(reed::Texture2D &src, int x, int y, int w = -1, int h = -1,
              int alpha_blend = 1);

    /* Text — glyph quads into textured batch; returns Batch& for .reed() access */
    Batch &text(const char *str, int x, int y, int size, uint32_t color);

    /* Acrylic: box-blur H+V then tint composite over region */
    int acrylic(int x, int y, int w, int h, int radius, uint32_t tint, uint8_t alpha);

    /* 3D */
    void draw_mesh(const Mesh &mesh, const Material &mat, const Transform &xf,
                   const Camera &cam);
    void draw_scene(const Scene &scene);

    /* Widgets (immediate-mode, return 1 if clicked/changed) */
    int button(int x, int y, int w, int h, const char *label, int *hover = nullptr);
    void panel(int x, int y, int w, int h, uint32_t bg);
    int toggle(int x, int y, int *on);
    int slider(int x, int y, int w, float *value /*0..1*/);
    void scroll_view_begin(int x, int y, int w, int h, int *scroll_y);
    void scroll_view_end();

    /* Input snapshot for widgets (set by WM/app each frame). */
    void set_pointer(int x, int y, uint8_t buttons);
    void set_wheel(int wheel);

private:
    static constexpr int kMaxBatches = 32;

    reed::Device *dev_;
    reed::RenderTarget target_;
    reed::RenderTarget blur_tmp_;
    reed::Pipeline pipe_color_;
    reed::Pipeline pipe_tex_;
    reed::Pipeline pipe_lit_;
    reed::CommandList cmd_;
    Font font_;
    Batch batches_[kMaxBatches];
    int nbatches_;
    Batch text_batch_;
    bool alive_;
    bool frame_open_;
    int ptr_x_, ptr_y_;
    uint8_t ptr_buttons_;
    int wheel_;
    int scissor_x_, scissor_y_, scissor_w_, scissor_h_;
    int scroll_y_ptr_;

    /* Dynamic AA glyph atlas — rasterize master glyphs at exact pixel size. */
    reed::Texture2D dyn_atlas_;
    uint32_t dyn_aw_, dyn_ah_;
    int shelf_x_, shelf_y_, shelf_h_;
    struct GlyphCache {
        uint16_t code, size, x, y, w, h, adv;
        uint8_t used;
    } glyphs_[192];

    Batch *get_batch(uint32_t pipe_key, uint32_t tex_handle);
    void emit_quad(Batch *b, float x0, float y0, float x1, float y1,
                   float u0, float v0, float u1, float v1, uint32_t color);
    void flush_batch(Batch *b);
    void retain_buf(reed::Buffer &b);
    void release_frame_bufs();
    reed::Uniforms ortho_u(uint32_t color);
    void ensure_font_atlas();
    int cache_glyph(int code, int size, GlyphCache **out);

    static constexpr int kMaxFrameBufs = 64;
    reed::Buffer frame_bufs_[kMaxFrameBufs];
    int nframe_bufs_;
};

/* One-shot smoke helper */
int fill_rect(int x, int y, int w, int h, unsigned color);

} /* namespace kilim */
