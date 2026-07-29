#include "imgui.h"
#include "imgui_impl_kilim.h"

#include <user/input.h>
#include <drivers/input/keyboard.h>
#include <kernel/string.h>

namespace {

struct BackendData {
    int win_id = -1;
    reed::Device *dev = nullptr;
    reed::Texture2D font_atlas;
    uint8_t prev_keys[32]{};
};

BackendData *bd()
{
    return reinterpret_cast<BackendData *>(ImGui::GetIO().BackendRendererUserData);
}

void unpack_col(ImU32 c, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a)
{
    r = (uint8_t)((c >> IM_COL32_R_SHIFT) & 0xFFu);
    g = (uint8_t)((c >> IM_COL32_G_SHIFT) & 0xFFu);
    b = (uint8_t)((c >> IM_COL32_B_SHIFT) & 0xFFu);
    a = (uint8_t)((c >> IM_COL32_A_SHIFT) & 0xFFu);
}

uint32_t pack_rgba(ImU32 c)
{
    uint8_t r, g, b, a;
    unpack_col(c, r, g, b, a);
    return kilim::rgba(r, g, b, a);
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

static ImDrawVert offset_vert(ImDrawVert v, int dy)
{
    v.pos.y += (float)dy;
    return v;
}

static void emit_quad_gpu(kilim::Context &k, BackendData *b, const ImDrawVert &a,
                          const ImDrawVert &c)
{
    const bool solid_uv =
        nearly_eq_uv(a.uv.x, c.uv.x) && nearly_eq_uv(a.uv.y, c.uv.y);
    const uint32_t tint = pack_rgba(a.col);
    if (solid_uv) {
        int x = (int)a.pos.x;
        int y = (int)a.pos.y;
        int w = (int)(c.pos.x - a.pos.x);
        int h = (int)(c.pos.y - a.pos.y);
        if (w < 0) {
            x = (int)c.pos.x;
            w = -w;
        }
        if (h < 0) {
            y = (int)c.pos.y;
            h = -h;
        }
        k.fill_rect(x, y, w, h, tint);
        return;
    }
    if (!b->font_atlas.valid())
        return;
    k.image_uv(b->font_atlas, a.pos.x, a.pos.y, c.pos.x, c.pos.y, a.uv.x, a.uv.y,
               c.uv.x, c.uv.y, tint);
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

bool ImGui_ImplKilim_Init(reed::Device *dev)
{
    ImGuiIO &io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr);
    if (!dev || !dev->valid())
        return false;

    BackendData *b = IM_NEW(BackendData)();
    b->dev = dev;
    io.BackendRendererUserData = b;
    io.BackendRendererName = "imgui_impl_kilim";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    unsigned char *pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &w, &h);
    if (!pixels || w <= 0 || h <= 0) {
        IM_DELETE(b);
        io.BackendRendererUserData = nullptr;
        return false;
    }

    /* Atlas bake is CPU; upload once — runtime FB never written by CPU. */
    b->font_atlas = dev->create_texture(reed::TexFormat::A8, (uint32_t)w, (uint32_t)h, 1);
    if (!b->font_atlas.valid()) {
        IM_DELETE(b);
        io.BackendRendererUserData = nullptr;
        return false;
    }
    uint8_t *dst = (uint8_t *)b->font_atlas.map();
    if (!dst) {
        b->font_atlas.destroy();
        IM_DELETE(b);
        io.BackendRendererUserData = nullptr;
        return false;
    }
    memcpy(dst, pixels, (size_t)w * (size_t)h);
    b->font_atlas.unmap();
    io.Fonts->SetTexID((ImTextureID)(uintptr_t)b->font_atlas.handle());
    return true;
}

void ImGui_ImplKilim_Shutdown()
{
    ImGuiIO &io = ImGui::GetIO();
    BackendData *b = bd();
    if (!b)
        return;
    b->font_atlas.destroy();
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

void ImGui_ImplKilim_RenderDrawData(ImDrawData *draw_data, kilim::Context &k,
                                    int client_top)
{
    BackendData *b = bd();
    if (!draw_data || !k.valid() || !b)
        return;
    if (client_top < 0)
        client_top = 0;

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
            int cw = clip_x1 - clip_x0;
            int ch = clip_y1 - clip_y0;
            if (cw > 0 && ch > 0)
                k.set_clip(clip_x0, clip_y0, cw, ch);

            unsigned int i = 0;
            while (i + 6 <= pcmd->ElemCount) {
                ImDrawVert vs[6];
                for (int kk = 0; kk < 6; kk++) {
                    const ImDrawIdx id = idx[pcmd->IdxOffset + i + (unsigned)kk];
                    vs[kk] = offset_vert(vtx[pcmd->VtxOffset + id], client_top);
                }
                ImDrawVert qmin{}, qmax{};
                if (verts_form_aa_quad(vs, 6, qmin, qmax)) {
                    emit_quad_gpu(k, b, qmin, qmax);
                    i += 6;
                    continue;
                }
                /* Non-AA path: still emit as two tris via AABB UV approx of first tri pair. */
                emit_quad_gpu(k, b, vs[0], vs[2]);
                emit_quad_gpu(k, b, vs[3], vs[5]);
                i += 6;
            }
            /* Leftover triangles → skip (no CPU raster). */
        }
    }
    k.clear_clip();
}
