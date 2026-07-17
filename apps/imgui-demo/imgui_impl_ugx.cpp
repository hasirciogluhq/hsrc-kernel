#include "imgui.h"

#include "imgui_impl_ugx.h"

#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>
#include <user/gx.h>
#include <drivers/keyboard.h>

using hsrc::sdk::WindowOptions;

namespace {

struct BackendData {
    int      win_id = -1;
    uint8_t *font_pixels = nullptr; /* Alpha8 atlas */
    int      font_w = 0;
    int      font_h = 0;
    uint32_t frame = 0;
    uint32_t tri_budget = 0;
};

BackendData *bd()
{
    return reinterpret_cast<BackendData *>(ImGui::GetIO().BackendRendererUserData);
}

/* Cooperative scheduler: never burn the CPU for a full frame without yielding. */
void maybe_yield(BackendData *b)
{
    if (!b)
        return;
    b->tri_budget++;
    if ((b->tri_budget & 63u) == 0u)
        hsrc::sdk::yield();
}

uint32_t blend_argb(uint32_t dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (a == 0)
        return dst;
    if (a == 255)
        return (255u << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;

    const uint32_t dr = (dst >> 16) & 0xFFu;
    const uint32_t dg = (dst >> 8) & 0xFFu;
    const uint32_t db = dst & 0xFFu;
    const uint32_t ia = 255u - a;
    const uint32_t or_ = (r * a + dr * ia) / 255u;
    const uint32_t og = (g * a + dg * ia) / 255u;
    const uint32_t ob = (b * a + db * ia) / 255u;
    return (255u << 24) | (or_ << 16) | (og << 8) | ob;
}

uint8_t sample_font_a(BackendData *b, float u, float v)
{
    if (!b || !b->font_pixels || b->font_w <= 0 || b->font_h <= 0)
        return 255;
    int x = (int)(u * (float)b->font_w);
    int y = (int)(v * (float)b->font_h);
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= b->font_w)
        x = b->font_w - 1;
    if (y >= b->font_h)
        y = b->font_h - 1;
    return b->font_pixels[(size_t)y * (size_t)b->font_w + (size_t)x];
}

void unpack_col(ImU32 c, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a)
{
    r = (uint8_t)((c >> IM_COL32_R_SHIFT) & 0xFFu);
    g = (uint8_t)((c >> IM_COL32_G_SHIFT) & 0xFFu);
    b = (uint8_t)((c >> IM_COL32_B_SHIFT) & 0xFFu);
    a = (uint8_t)((c >> IM_COL32_A_SHIFT) & 0xFFu);
}

bool nearly_eq(float a, float b, float eps)
{
    float d = a - b;
    if (d < 0.0f)
        d = -d;
    return d < eps;
}

/* Pixel-space corners (ImGui AA quads). */
bool nearly_eq_pos(float a, float b)
{
    return nearly_eq(a, b, 0.5f);
}

/* UV-space: glyph quads often span << 0.5; 0.5 wrongly treated text as solid fill. */
bool nearly_eq_uv(float a, float b)
{
    return nearly_eq(a, b, 1.0e-4f);
}

/* Most ImGui geometry is axis-aligned textured quads (2 triangles). */
bool try_draw_aa_quad(hsrc::sdk::Surface &surf, BackendData *b,
                      const ImDrawVert &a, const ImDrawVert &c,
                      int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    /* a = min corner, c = max corner of an AA rect (from two tris). */
    float min_x = a.pos.x < c.pos.x ? a.pos.x : c.pos.x;
    float max_x = a.pos.x > c.pos.x ? a.pos.x : c.pos.x;
    float min_y = a.pos.y < c.pos.y ? a.pos.y : c.pos.y;
    float max_y = a.pos.y > c.pos.y ? a.pos.y : c.pos.y;

    int x0 = (int)min_x;
    int y0 = (int)min_y;
    int x1 = (int)max_x;
    int y1 = (int)max_y;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x0 >= x1 || y0 >= y1)
        return true;

    const float du = c.uv.x - a.uv.x;
    const float dv = c.uv.y - a.uv.y;
    const float dw = max_x - min_x;
    const float dh = max_y - min_y;
    if (dw <= 0.0f || dh <= 0.0f)
        return true;

    uint8_t cr, cg, cb, ca;
    unpack_col(a.col, cr, cg, cb, ca);

    uint32_t *pixels = reinterpret_cast<uint32_t *>(surf.pixels());
    const uint32_t stride = surf.stride();
    const bool solid_uv = nearly_eq_uv(a.uv.x, c.uv.x) && nearly_eq_uv(a.uv.y, c.uv.y);

    for (int y = y0; y < y1; y++) {
        const float fy = ((float)y + 0.5f - min_y) / dh;
        const float v = a.uv.y + dv * fy;
        uint32_t *row = pixels + (uint32_t)y * stride;
        for (int x = x0; x < x1; x++) {
            uint8_t ta = 255;
            if (!solid_uv) {
                const float fx = ((float)x + 0.5f - min_x) / dw;
                const float u = a.uv.x + du * fx;
                ta = sample_font_a(b, u, v);
            }
            const uint8_t out_a = (uint8_t)((ca * ta) / 255u);
            if (out_a == 0)
                continue;
            row[x] = blend_argb(row[x], cr, cg, cb, out_a);
        }
    }
    return true;
}

bool verts_form_aa_quad(const ImDrawVert *vs, int count,
                        ImDrawVert &out_min, ImDrawVert &out_max)
{
    if (!vs || count < 3)
        return false;

    float min_x = vs[0].pos.x, max_x = vs[0].pos.x;
    float min_y = vs[0].pos.y, max_y = vs[0].pos.y;
    ImU32 col = vs[0].col;
    for (int i = 1; i < count; i++) {
        if (vs[i].col != col)
            return false;
        if (vs[i].pos.x < min_x) min_x = vs[i].pos.x;
        if (vs[i].pos.x > max_x) max_x = vs[i].pos.x;
        if (vs[i].pos.y < min_y) min_y = vs[i].pos.y;
        if (vs[i].pos.y > max_y) max_y = vs[i].pos.y;
    }

    const ImDrawVert *vmin = nullptr;
    const ImDrawVert *vmax = nullptr;
    for (int i = 0; i < count; i++) {
        const bool on_x = nearly_eq_pos(vs[i].pos.x, min_x) || nearly_eq_pos(vs[i].pos.x, max_x);
        const bool on_y = nearly_eq_pos(vs[i].pos.y, min_y) || nearly_eq_pos(vs[i].pos.y, max_y);
        if (!on_x || !on_y)
            return false;
        if (nearly_eq_pos(vs[i].pos.x, min_x) && nearly_eq_pos(vs[i].pos.y, min_y))
            vmin = &vs[i];
        if (nearly_eq_pos(vs[i].pos.x, max_x) && nearly_eq_pos(vs[i].pos.y, max_y))
            vmax = &vs[i];
    }
    if (!vmin || !vmax)
        return false;

    out_min = *vmin;
    out_max = *vmax;
    out_min.pos.x = min_x;
    out_min.pos.y = min_y;
    out_max.pos.x = max_x;
    out_max.pos.y = max_y;
    return true;
}

/* Slow path: only for rare non-AA triangles (kept cheap — skip tiny / clipped). */
void draw_triangle_slow(hsrc::sdk::Surface &surf, BackendData *b,
                        const ImDrawVert &v0, const ImDrawVert &v1, const ImDrawVert &v2,
                        int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    float min_x = v0.pos.x, max_x = v0.pos.x;
    float min_y = v0.pos.y, max_y = v0.pos.y;
    if (v1.pos.x < min_x) min_x = v1.pos.x;
    if (v2.pos.x < min_x) min_x = v2.pos.x;
    if (v1.pos.x > max_x) max_x = v1.pos.x;
    if (v2.pos.x > max_x) max_x = v2.pos.x;
    if (v1.pos.y < min_y) min_y = v1.pos.y;
    if (v2.pos.y < min_y) min_y = v2.pos.y;
    if (v1.pos.y > max_y) max_y = v1.pos.y;
    if (v2.pos.y > max_y) max_y = v2.pos.y;

    int x0 = (int)min_x;
    int y0 = (int)min_y;
    int x1 = (int)max_x + 1;
    int y1 = (int)max_y + 1;
    if (x0 < clip_x0) x0 = clip_x0;
    if (y0 < clip_y0) y0 = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;
    if (x0 >= x1 || y0 >= y1)
        return;
    /* Skip huge slow fills — AA path should have handled real UI quads. */
    if ((x1 - x0) * (y1 - y0) > 4096)
        return;

    const float area =
        (v1.pos.x - v0.pos.x) * (v2.pos.y - v0.pos.y) -
        (v1.pos.y - v0.pos.y) * (v2.pos.x - v0.pos.x);
    if (area == 0.0f)
        return;
    const float inv = 1.0f / area;

    uint32_t *pixels = reinterpret_cast<uint32_t *>(surf.pixels());
    const uint32_t stride = surf.stride();

    uint8_t r0, g0, b0, a0, r1, g1, b1, a1, r2, g2, b2, a2;
    unpack_col(v0.col, r0, g0, b0, a0);
    unpack_col(v1.col, r1, g1, b1, a1);
    unpack_col(v2.col, r2, g2, b2, a2);

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = ((v1.pos.x - px) * (v2.pos.y - py) - (v1.pos.y - py) * (v2.pos.x - px)) * inv;
            float w1 = ((v2.pos.x - px) * (v0.pos.y - py) - (v2.pos.y - py) * (v0.pos.x - px)) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;

            float u = w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x;
            float v = w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y;
            uint8_t ta = sample_font_a(b, u, v);

            uint8_t cr = (uint8_t)(w0 * r0 + w1 * r1 + w2 * r2);
            uint8_t cg = (uint8_t)(w0 * g0 + w1 * g1 + w2 * g2);
            uint8_t cb = (uint8_t)(w0 * b0 + w1 * b1 + w2 * b2);
            uint8_t ca = (uint8_t)(w0 * a0 + w1 * a1 + w2 * a2);
            uint8_t out_a = (uint8_t)((ca * ta) / 255u);
            if (out_a == 0)
                continue;
            uint32_t &dst = pixels[(uint32_t)y * stride + (uint32_t)x];
            dst = blend_argb(dst, cr, cg, cb, out_a);
        }
    }
}

} // namespace

bool ImGui_ImplUgx_Init()
{
    ImGuiIO &io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr);

    BackendData *b = IM_NEW(BackendData)();
    io.BackendRendererUserData = b;
    io.BackendRendererName = "imgui_impl_ugx";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    /* Alpha8 avoids a second 512x512x4 atlas alloc on the bump/freelist heap. */
    unsigned char *pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &w, &h);
    b->font_pixels = pixels;
    b->font_w = w;
    b->font_h = h;
    io.Fonts->SetTexID((ImTextureID)(uintptr_t)1);

    hsrc::sdk::yield();
    return pixels != nullptr && w > 0 && h > 0;
}

void ImGui_ImplUgx_Shutdown()
{
    ImGuiIO &io = ImGui::GetIO();
    BackendData *b = bd();
    if (!b)
        return;
    io.Fonts->SetTexID(0);
    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    IM_DELETE(b);
}

void ImGui_ImplUgx_NewFrame(hsrc::sdk::Window &win, const hsrc::sdk::Input &in,
                            uint8_t prev_buttons)
{
    BackendData *b = bd();
    if (!b)
        return;

    b->win_id = win.id();
    ImGuiIO &io = ImGui::GetIO();

    WindowOptions opts;
    int win_x = 0, win_y = 0;
    if (win.get_options(opts)) {
        io.DisplaySize = ImVec2((float)opts.w, (float)opts.h);
        win_x = opts.x;
        win_y = opts.y;
    } else if (win.surface().valid()) {
        io.DisplaySize = ImVec2((float)win.surface().width(), (float)win.surface().height());
    }

    b->frame++;
    io.DeltaTime = 1.0f / 60.0f;

    const int lx = in.mouse_x - win_x;
    const int ly = in.mouse_y - win_y;
    io.AddMousePosEvent((float)lx, (float)ly);

    const bool focused = (in.focus_id == win.id());
    (void)prev_buttons;
    io.AddMouseButtonEvent(0, focused && (in.buttons & UGX_BTN_LEFT) != 0);
    io.AddMouseButtonEvent(1, focused && (in.buttons & UGX_BTN_RIGHT) != 0);
    io.AddMouseButtonEvent(2, focused && (in.buttons & UGX_BTN_MIDDLE) != 0);

    io.AddKeyEvent(ImGuiMod_Shift, (in.mods & KBD_MOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Ctrl, (in.mods & KBD_MOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (in.mods & KBD_MOD_ALT) != 0);

    if (focused) {
        for (;;) {
            long k = hsrc::sdk::syscall1(SYS_WM_POP_KEY, win.id());
            if (k < 0)
                break;
            io.AddInputCharacter((unsigned int)k);
        }
    }
}

void ImGui_ImplUgx_RenderDrawData(ImDrawData *draw_data, hsrc::sdk::Surface &surf)
{
    BackendData *b = bd();
    if (!draw_data || !surf.valid() || !b)
        return;

    surf.clear(hsrc::sdk::rgb(30, 30, 34));
    b->tri_budget = 0;

    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList *cmd_list = draw_data->CmdLists[n];
        const ImDrawVert *vtx = cmd_list->VtxBuffer.Data;
        const ImDrawIdx *idx = cmd_list->IdxBuffer.Data;

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
            const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback) {
                pcmd->UserCallback(cmd_list, pcmd);
                continue;
            }

            int clip_x0 = (int)pcmd->ClipRect.x;
            int clip_y0 = (int)pcmd->ClipRect.y;
            int clip_x1 = (int)pcmd->ClipRect.z;
            int clip_y1 = (int)pcmd->ClipRect.w;
            if (clip_x0 < 0) clip_x0 = 0;
            if (clip_y0 < 0) clip_y0 = 0;
            if (clip_x1 > (int)surf.width()) clip_x1 = (int)surf.width();
            if (clip_y1 > (int)surf.height()) clip_y1 = (int)surf.height();

            unsigned int i = 0;
            while (i + 6 <= pcmd->ElemCount) {
                ImDrawVert vs[6];
                for (int k = 0; k < 6; k++) {
                    const ImDrawIdx id = idx[pcmd->IdxOffset + i + (unsigned)k];
                    vs[k] = vtx[pcmd->VtxOffset + id];
                }

                /* ImGui PrimRect*: two tris → one axis-aligned textured quad. */
                ImDrawVert qmin{}, qmax{};
                if (verts_form_aa_quad(vs, 6, qmin, qmax)) {
                    (void)try_draw_aa_quad(surf, b, qmin, qmax,
                                           clip_x0, clip_y0, clip_x1, clip_y1);
                    maybe_yield(b);
                    i += 6;
                    continue;
                }

                draw_triangle_slow(surf, b, vs[0], vs[1], vs[2],
                                   clip_x0, clip_y0, clip_x1, clip_y1);
                draw_triangle_slow(surf, b, vs[3], vs[4], vs[5],
                                   clip_x0, clip_y0, clip_x1, clip_y1);
                maybe_yield(b);
                i += 6;
            }

            for (; i + 3 <= pcmd->ElemCount; i += 3) {
                const ImDrawIdx i0 = idx[pcmd->IdxOffset + i + 0];
                const ImDrawIdx i1 = idx[pcmd->IdxOffset + i + 1];
                const ImDrawIdx i2 = idx[pcmd->IdxOffset + i + 2];
                draw_triangle_slow(surf, b,
                                   vtx[pcmd->VtxOffset + i0],
                                   vtx[pcmd->VtxOffset + i1],
                                   vtx[pcmd->VtxOffset + i2],
                                   clip_x0, clip_y0, clip_x1, clip_y1);
                maybe_yield(b);
            }
        }
    }
}
