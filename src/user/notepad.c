#include <user/gx.h>
#include <user/string.h>
#include <user/mke.h>

#define NOTE_W       480
#define NOTE_H       320
#define NOTE_TITLE_H 28
#define NOTE_COLS    56
#define NOTE_ROWS    20
#define NOTE_MAX     (NOTE_COLS * NOTE_ROWS)

static int     g_win = -1;
static ugx_map g_map;
static char    g_text[NOTE_MAX + 1];
static int     g_len;
static int     g_blink;
static uint8_t prev_buttons;

static void notepad_paint(int focused)
{
    if (g_win < 0)
        return;

    uint32_t title = focused ? UGX_RGB(236, 236, 236) : UGX_RGB(210, 210, 210);
    ugx_buf_clear(&g_map, UGX_RGB(255, 255, 255));
    ugx_fill(g_win, 0, 0, NOTE_W, NOTE_TITLE_H, title);
    ugx_fill_round(g_win, 10, 8, 12, 12, 6, UGX_RGB(255, 95, 87));
    ugx_fill_round(g_win, 28, 8, 12, 12, 6, UGX_RGB(255, 189, 46));
    ugx_fill_round(g_win, 46, 8, 12, 12, 6, UGX_RGB(39, 201, 63));
    ugx_buf_text(&g_map, NOTE_W / 2 - 36, 10, "Untitled", UGX_RGB(40, 40, 40));

    ugx_fill(g_win, 0, NOTE_TITLE_H, NOTE_W, NOTE_H - NOTE_TITLE_H, UGX_RGB(255, 255, 255));

    int x = 16;
    int y = NOTE_TITLE_H + 12;
    for (int i = 0; i < g_len; i++) {
        char ch = g_text[i];
        if (ch == '\n') {
            x = 16;
            y += 12;
            continue;
        }
        char s[2] = { ch, 0 };
        ugx_buf_text(&g_map, x, y, s, UGX_RGB(20, 20, 20));
        x += 8;
        if (x > NOTE_W - 24) {
            x = 16;
            y += 12;
        }
    }

    if (focused && ((g_blink / 20) & 1) == 0)
        ugx_fill(g_win, x, y, 2, 12, UGX_RGB(0, 122, 255));

    ugx_damage();
}

static int notepad_open(int screen_w, int screen_h)
{
    ugx_win_create a;

    if (g_win >= 0) {
        ugx_wm_show(g_win, 1);
        ugx_wm_focus(g_win);
        return 0;
    }

    memset(&a, 0, sizeof(a));
    a.w = NOTE_W;
    a.h = NOTE_H;
    a.x = screen_w / 2 - NOTE_W / 2 + 40;
    a.y = screen_h / 2 - NOTE_H / 2 + 20;
    if (a.y < 40)
        a.y = 40;
    a.style = UGX_STYLE_ROUNDED;
    a.radius = 10;
    strncpy(a.title, "Notepad", sizeof(a.title) - 1);

    g_win = ugx_wm_create(&a);
    if (g_win < 0)
        return -1;
    if (ugx_wm_map(g_win, &g_map) < 0)
        return -1;

    memset(g_text, 0, sizeof(g_text));
    g_len = 0;
    strncpy(g_text, "Notepad\nType here...\n", sizeof(g_text) - 1);
    g_len = (int)strlen(g_text);
    ugx_wm_focus(g_win);
    notepad_paint(1);
    return 0;
}

static void notepad_close(void)
{
    if (g_win < 0)
        return;
    ugx_wm_destroy(g_win);
    g_win = -1;
}

static void notepad_tick(const ugx_input_state *in, uint8_t pressed)
{
    ugx_frame fr;

    if (g_win < 0)
        return;

    g_blink++;

    if (ugx_wm_get_frame(g_win, &fr) == 0 && (pressed & UGX_BTN_LEFT)) {
        if (in->mouse_x >= fr.x + 6 && in->mouse_x < fr.x + 22 &&
            in->mouse_y >= fr.y + 6 && in->mouse_y < fr.y + 22) {
            notepad_close();
            return;
        }
    }

    int ch;
    int dirty = 0;
    while ((ch = ugx_wm_pop_key(g_win)) >= 0) {
        dirty = 1;
        if (ch == '\b') {
            if (g_len > 0)
                g_text[--g_len] = 0;
        } else if (ch == '\n' || ch == '\r') {
            if (g_len < NOTE_MAX) {
                g_text[g_len++] = '\n';
                g_text[g_len] = 0;
            }
        } else if (ch >= 32 && g_len < NOTE_MAX) {
            g_text[g_len++] = (char)ch;
            g_text[g_len] = 0;
        }
    }

    if (dirty || (g_blink % 20) == 0)
        notepad_paint(in->focus_id == g_win);
}

void mke_main(void)
{
    ugx_info info;
    if (ugx_info_get(&info) < 0) {
        for (;;)
            ugx_yield();
    }

    if (notepad_open((int)info.width, (int)info.height) < 0) {
        for (;;)
            ugx_yield();
    }

    prev_buttons = 0;
    for (;;) {
        ugx_present();

        ugx_input_state in;
        if (ugx_input_get(&in) == 0) {
            uint8_t pressed = (uint8_t)(in.buttons & ~prev_buttons);
            notepad_tick(&in, pressed);
            prev_buttons = in.buttons;
        } else {
            ugx_input_state z;
            memset(&z, 0, sizeof(z));
            notepad_tick(&z, 0);
        }

        if (g_win < 0)
            notepad_open((int)info.width, (int)info.height);

        ugx_yield();
    }
}
