#include "imgui.h"
#include "imgui_impl_kilim.h"

#include <user/input.h>
#include <drivers/input/keyboard.h>
#include <kernel/string.h>

namespace {

struct BackendData {
    int win_id = -1;
    uint8_t *font_pixels = nullptr;
    int font_w = 0;
    int font_h = 0;
    uint8_t prev_keys[32]{};
};

BackendData *bd()
{
    return reinterpret_cast<BackendData *>(ImGui::GetIO().BackendRendererUserData);
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
    return (255u << 24) | (((r * a + dr * ia) / 255u) << 16) |
           (((g * a + dg * ia) / 255u) << 8) | ((b * a + db * ia) / 255u);
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

bool nearly_eq_pos(float a, float b) { return nearly_eq(a, b, 0.5f); }
bool nearly_eq_uv(float a, float b) { return nearly_eq(a, b, 1.0e-4f); }

bool try_draw_aa_quad(uint32_t *pixels, uint32_t stride_px, int fb_w, int fb_h,
                      BackendData *b, const ImDrawVert &a, const ImDrawVert &c,
                      int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    float min_x = a.pos.x < c.pos.x ? a.pos.x : c.pos.x;
    float max_x = a.pos.x > c.pos.x ? a.pos.x : c.pos.x;
    float min_y = a.pos.y < c.pos.y ? a.pos.y : c.pos.y;
    float max_y = a.pos.y > c.pos.y ? a.pos.y : c.pos.y;

    int x0 = (int)min_x;
    int y0 = (int)min_y;
    int x1 = (int)max_x;
    int y1 = (int)max_y;
    if (x0 < clip_x0)
        x0 = clip_x0;
    if (y0 < clip_y0)
        y0 = clip_y0;
    if (x1 > clip_x1)
        x1 = clip_x1;
    if (y1 > clip_y1)
        y1 = clip_y1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > fb_w)
        x1 = fb_w;
    if (y1 > fb_h)
        y1 = fb_h;
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
    const bool solid_uv = nearly_eq_uv(a.uv.x, c.uv.x) && nearly_eq_uv(a.uv.y, c.uv.y);

    for (int y = y0; y < y1; y++) {
        const float fy = ((float)y + 0.5f - min_y) / dh;
        const float v = a.uv.y + dv * fy;
        uint32_t *row = pixels + (uint32_t)y * stride_px;
        for (int x = x0; x < x1; x++) {
            uint8_t ta = 255;
            if (!solid_uv) {
                const float fx = ((float)x + 0.5f - min_x) / dw;
                ta = sample_font_a(b, a.uv.x + du * fx, v);
            }
            const uint8_t out_a = (uint8_t)((ca * ta) / 255u);
            if (out_a == 0)
                continue;
            row[x] = blend_argb(row[x], cr, cg, cb, out_a);
        }
    }
    return true;
}

bool verts_form_aa_quad(const ImDrawVert *vs, int count, ImDrawVert &out_min,
                        ImDrawVert &out_max)
{
    if (!vs || count < 3)
        return false;
    float min_x = vs[0].pos.x, max_x = vs[0].pos.x;
    float min_y = vs[0].pos.y, max_y = vs[0].pos.y;
    ImU32 col = vs[0].col;
    for (int i = 1; i < count; i++) {
        if (vs[i].col != col)
            return false;
        if (vs[i].pos.x < min_x)
            min_x = vs[i].pos.x;
        if (vs[i].pos.x > max_x)
            max_x = vs[i].pos.x;
        if (vs[i].pos.y < min_y)
            min_y = vs[i].pos.y;
        if (vs[i].pos.y > max_y)
            max_y = vs[i].pos.y;
    }
    const ImDrawVert *vmin = nullptr;
    const ImDrawVert *vmax = nullptr;
    for (int i = 0; i < count; i++) {
        const bool on_x =
            nearly_eq_pos(vs[i].pos.x, min_x) || nearly_eq_pos(vs[i].pos.x, max_x);
        const bool on_y =
            nearly_eq_pos(vs[i].pos.y, min_y) || nearly_eq_pos(vs[i].pos.y, max_y);
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

void draw_triangle_slow(uint32_t *pixels, uint32_t stride_px, int fb_w, int fb_h,
                        BackendData *b, const ImDrawVert &v0, const ImDrawVert &v1,
                        const ImDrawVert &v2, int clip_x0, int clip_y0, int clip_x1,
                        int clip_y1)
{
    float min_x = v0.pos.x, max_x = v0.pos.x;
    float min_y = v0.pos.y, max_y = v0.pos.y;
    if (v1.pos.x < min_x)
        min_x = v1.pos.x;
    if (v2.pos.x < min_x)
        min_x = v2.pos.x;
    if (v1.pos.x > max_x)
        max_x = v1.pos.x;
    if (v2.pos.x > max_x)
        max_x = v2.pos.x;
    if (v1.pos.y < min_y)
        min_y = v1.pos.y;
    if (v2.pos.y < min_y)
        min_y = v2.pos.y;
    if (v1.pos.y > max_y)
        max_y = v1.pos.y;
    if (v2.pos.y > max_y)
        max_y = v2.pos.y;

    int x0 = (int)min_x;
    int y0 = (int)min_y;
    int x1 = (int)max_x + 1;
    int y1 = (int)max_y + 1;
    if (x0 < clip_x0)
        x0 = clip_x0;
    if (y0 < clip_y0)
        y0 = clip_y0;
    if (x1 > clip_x1)
        x1 = clip_x1;
    if (y1 > clip_y1)
        y1 = clip_y1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > fb_w)
        x1 = fb_w;
    if (y1 > fb_h)
        y1 = fb_h;
    if (x0 >= x1 || y0 >= y1)
        return;
    if ((x1 - x0) * (y1 - y0) > 4096)
        return;

    const float area = (v1.pos.x - v0.pos.x) * (v2.pos.y - v0.pos.y) -
                       (v1.pos.y - v0.pos.y) * (v2.pos.x - v0.pos.x);
    if (area == 0.0f)
        return;
    const float inv = 1.0f / area;

    uint8_t r0, g0, b0, a0, r1, g1, b1, a1, r2, g2, b2, a2;
    unpack_col(v0.col, r0, g0, b0, a0);
    unpack_col(v1.col, r1, g1, b1, a1);
    unpack_col(v2.col, r2, g2, b2, a2);

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = ((v1.pos.x - px) * (v2.pos.y - py) -
                        (v1.pos.y - py) * (v2.pos.x - px)) *
                       inv;
            float w1 = ((v2.pos.x - px) * (v0.pos.y - py) -
                        (v2.pos.y - py) * (v0.pos.x - px)) *
                       inv;
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
            uint32_t &dst = pixels[(uint32_t)y * stride_px + (uint32_t)x];
            dst = blend_argb(dst, cr, cg, cb, out_a);
        }
    }
}

static ImDrawVert offset_vert(ImDrawVert v, int dy)
{
    v.pos.y += (float)dy;
    return v;
}

/* US QWERTY set-1 → printable / special for ImGui. */
static void feed_scancode_edge(ImGuiIO &io, int sc, int shift, int down)
{
    ImGuiKey key = ImGuiKey_None;
    switch (sc) {
    case 0x0E:
        key = ImGuiKey_Backspace;
        break;
    case 0x0F:
        key = ImGuiKey_Tab;
        break;
    case 0x1C:
        key = ImGuiKey_Enter;
        break;
    case 0x01:
        key = ImGuiKey_Escape;
        break;
    case 0x39:
        key = ImGuiKey_Space;
        break;
    default:
        break;
    }
    if (key != ImGuiKey_None) {
        io.AddKeyEvent(key, down != 0);
        return;
    }
    if (!down || sc <= 0 || sc >= 58)
        return;
    static const char *un =
        "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char *sh =
        "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
    char ch = shift ? sh[sc] : un[sc];
    if (ch >= 32)
        io.AddInputCharacter((unsigned)ch);
}

} // namespace

bool ImGui_ImplKilim_Init()
{
    ImGuiIO &io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr);

    BackendData *b = IM_NEW(BackendData)();
    io.BackendRendererUserData = b;
    io.BackendRendererName = "imgui_impl_kilim";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    unsigned char *pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &w, &h);
    b->font_pixels = pixels;
    b->font_w = w;
    b->font_h = h;
    io.Fonts->SetTexID((ImTextureID)(uintptr_t)1);
    return pixels != nullptr && w > 0 && h > 0;
}

void ImGui_ImplKilim_Shutdown()
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

void ImGui_ImplKilim_NewFrame(wm::Window &win, const wm::Input &in,
                              const wm::WindowOptions &opts, uint8_t prev_buttons,
                              int client_top)
{
    BackendData *b = bd();
    if (!b)
        return;

    b->win_id = win.id();
    ImGuiIO &io = ImGui::GetIO();
    if (client_top < 0)
        client_top = 0;

    const int win_w = opts.w > 0 ? opts.w : 640;
    const int win_h = opts.h > 0 ? opts.h : 480;
    int client_h = win_h - client_top;
    if (client_h < 1)
        client_h = 1;

    io.DisplaySize = ImVec2((float)win_w, (float)client_h);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;

    const int lx = in.mouse_x - opts.x;
    const int ly = in.mouse_y - opts.y;
    const int client_y = ly - client_top;

    const bool hit = in.hits(win.id());
    const bool focused = in.focused(win.id());
    const bool in_client =
        hit && lx >= 0 && client_y >= 0 && lx < win_w && client_y < client_h;
    const bool dragging =
        (prev_buttons & (INPUT_BTN_LEFT | INPUT_BTN_RIGHT | INPUT_BTN_MIDDLE)) != 0;

    if (in_client || (focused && dragging))
        io.AddMousePosEvent((float)lx, (float)client_y);
    else
        io.AddMousePosEvent(-100000.0f, -100000.0f);

    const bool mouse_ok = focused && (in_client || dragging);
    io.AddMouseButtonEvent(0, mouse_ok && (in.buttons & INPUT_BTN_LEFT) != 0);
    io.AddMouseButtonEvent(1, mouse_ok && (in.buttons & INPUT_BTN_RIGHT) != 0);
    io.AddMouseButtonEvent(2, mouse_ok && (in.buttons & INPUT_BTN_MIDDLE) != 0);

    if (in_client && in.wheel != 0)
        io.AddMouseWheelEvent(0.0f, (float)in.wheel);

    io.AddKeyEvent(ImGuiMod_Shift, (in.mods & KBD_MOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Ctrl, (in.mods & KBD_MOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (in.mods & KBD_MOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (in.mods & KBD_MOD_SUPER) != 0);

    if (focused) {
        int shift = (in.mods & KBD_MOD_SHIFT) != 0;
        for (int sc = 1; sc < 58; sc++) {
            int now = in.key_down(sc) ? 1 : 0;
            int was = (b->prev_keys[sc >> 3] & (uint8_t)(1u << (sc & 7))) ? 1 : 0;
            if (now != was)
                feed_scancode_edge(io, sc, shift, now);
        }
    }
    for (int i = 0; i < 32; i++)
        b->prev_keys[i] = in.keys[i];
}

void ImGui_ImplKilim_RenderDrawData(ImDrawData *draw_data, reed::Texture2D &fb,
                                    int client_top)
{
    BackendData *b = bd();
    if (!draw_data || !fb.valid() || !b)
        return;
    if (client_top < 0)
        client_top = 0;

    uint32_t *pixels = (uint32_t *)fb.map();
    if (!pixels)
        return;
    const int fb_w = (int)fb.width();
    const int fb_h = (int)fb.height();
    const uint32_t stride_px = fb.stride() / 4u;

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
            int clip_y0 = (int)pcmd->ClipRect.y + client_top;
            int clip_x1 = (int)pcmd->ClipRect.z;
            int clip_y1 = (int)pcmd->ClipRect.w + client_top;

            unsigned int i = 0;
            while (i + 6 <= pcmd->ElemCount) {
                ImDrawVert vs[6];
                for (int k = 0; k < 6; k++) {
                    const ImDrawIdx id = idx[pcmd->IdxOffset + i + (unsigned)k];
                    vs[k] = offset_vert(vtx[pcmd->VtxOffset + id], client_top);
                }
                ImDrawVert qmin{}, qmax{};
                if (verts_form_aa_quad(vs, 6, qmin, qmax)) {
                    (void)try_draw_aa_quad(pixels, stride_px, fb_w, fb_h, b, qmin,
                                           qmax, clip_x0, clip_y0, clip_x1, clip_y1);
                    i += 6;
                    continue;
                }
                draw_triangle_slow(pixels, stride_px, fb_w, fb_h, b, vs[0], vs[1],
                                   vs[2], clip_x0, clip_y0, clip_x1, clip_y1);
                draw_triangle_slow(pixels, stride_px, fb_w, fb_h, b, vs[3], vs[4],
                                   vs[5], clip_x0, clip_y0, clip_x1, clip_y1);
                i += 6;
            }
            for (; i + 3 <= pcmd->ElemCount; i += 3) {
                draw_triangle_slow(
                    pixels, stride_px, fb_w, fb_h, b,
                    offset_vert(vtx[pcmd->VtxOffset + idx[pcmd->IdxOffset + i + 0]],
                                client_top),
                    offset_vert(vtx[pcmd->VtxOffset + idx[pcmd->IdxOffset + i + 1]],
                                client_top),
                    offset_vert(vtx[pcmd->VtxOffset + idx[pcmd->IdxOffset + i + 2]],
                                client_top),
                    clip_x0, clip_y0, clip_x1, clip_y1);
            }
        }
    }
    fb.unmap();
}
