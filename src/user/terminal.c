#include <user/gx.h>
#include <user/string.h>
#include <user/mke.h>

/*
 * macOS Terminal.app look — ring-3 .mke process.
 */

#define TERM_COLS    80
#define TERM_ROWS    24
#define TERM_CW      8
#define TERM_CH      14
#define TERM_PAD_X   6
#define TERM_PAD_Y   4
#define TERM_TITLE_H 28
#define TERM_SCROLL  12
#define TERM_W       (TERM_COLS * TERM_CW + TERM_PAD_X * 2 + TERM_SCROLL)
#define TERM_H       (TERM_TITLE_H + TERM_ROWS * TERM_CH + TERM_PAD_Y * 2)

#define COL_TITLE_ACTIVE   UGX_RGB(53, 53, 55)
#define COL_TITLE_INACTIVE UGX_RGB(72, 72, 74)
#define COL_BG             UGX_RGB(0, 0, 0)
#define COL_FG             UGX_RGB(0, 255, 0)
#define COL_CURSOR         UGX_RGB(0, 220, 0)
#define COL_SCROLL         UGX_RGB(40, 40, 40)
#define COL_SCROLL_THUMB   UGX_RGB(90, 90, 90)
#define COL_BTN_CLOSE      UGX_RGB(255, 95, 87)
#define COL_BTN_MIN        UGX_RGB(255, 189, 46)
#define COL_BTN_ZOOM       UGX_RGB(39, 201, 63)

static int     g_win = -1;
static ugx_map g_map;
static char    g_cells[TERM_ROWS][TERM_COLS + 1];
static int     g_cx, g_cy;
static char    g_line[TERM_COLS];
static int     g_llen;
static int     g_blink;
static uint8_t prev_buttons;

static void term_clear(void)
{
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++)
            g_cells[r][c] = ' ';
        g_cells[r][TERM_COLS] = 0;
    }
    g_cx = 0;
    g_cy = 0;
    g_llen = 0;
    g_line[0] = 0;
}

static void term_scroll(void)
{
    for (int r = 1; r < TERM_ROWS; r++)
        memcpy(g_cells[r - 1], g_cells[r], TERM_COLS + 1);
    for (int c = 0; c < TERM_COLS; c++)
        g_cells[TERM_ROWS - 1][c] = ' ';
    if (g_cy > 0)
        g_cy--;
}

static void term_putc_cell(char ch)
{
    if (ch == '\n') {
        g_cx = 0;
        g_cy++;
        if (g_cy >= TERM_ROWS) {
            term_scroll();
            g_cy = TERM_ROWS - 1;
        }
        return;
    }
    if (g_cx >= TERM_COLS) {
        g_cx = 0;
        g_cy++;
        if (g_cy >= TERM_ROWS) {
            term_scroll();
            g_cy = TERM_ROWS - 1;
        }
    }
    g_cells[g_cy][g_cx++] = ch;
}

static void term_puts(const char *s)
{
    while (*s)
        term_putc_cell(*s++);
}

static void term_prompt(void)
{
    term_puts("user@mykernel ~ % ");
}

static void term_newline_cmd(void)
{
    term_putc_cell('\n');
    if (g_llen == 0) {
        term_prompt();
        return;
    }

    if (strcmp(g_line, "clear") == 0) {
        term_clear();
        term_puts("Last login: Sat Jul 11 15:39:00 on ttys001\n");
        term_prompt();
    } else if (strcmp(g_line, "help") == 0) {
        term_puts("Built-in: clear help uname whoami ls pwd echo\n");
        term_prompt();
    } else if (strcmp(g_line, "uname") == 0 || strcmp(g_line, "uname -a") == 0) {
        term_puts("Darwin mykernel 24.0.0 Darwin Kernel Version 24.0.0: i386\n");
        term_prompt();
    } else if (strcmp(g_line, "whoami") == 0) {
        term_puts("user\n");
        term_prompt();
    } else if (strcmp(g_line, "pwd") == 0) {
        term_puts("/Users/user\n");
        term_prompt();
    } else if (strcmp(g_line, "ls") == 0) {
        term_puts("Applications\tDesktop\tDocuments\tDownloads\n");
        term_prompt();
    } else if (strncmp(g_line, "echo ", 5) == 0) {
        term_puts(g_line + 5);
        term_putc_cell('\n');
        term_prompt();
    } else {
        term_puts("zsh: command not found: ");
        term_puts(g_line);
        term_putc_cell('\n');
        term_prompt();
    }
    g_llen = 0;
    g_line[0] = 0;
}

static void draw_traffic_lights(void)
{
    ugx_fill_round(g_win, 8, 8, 12, 12, 6, COL_BTN_CLOSE);
    ugx_fill_round(g_win, 28, 8, 12, 12, 6, COL_BTN_MIN);
    ugx_fill_round(g_win, 48, 8, 12, 12, 6, COL_BTN_ZOOM);
}

static void terminal_paint(int focused)
{
    if (g_win < 0)
        return;

    uint32_t title = focused ? COL_TITLE_ACTIVE : COL_TITLE_INACTIVE;
    ugx_fill(g_win, 0, 0, TERM_W, TERM_TITLE_H, title);
    draw_traffic_lights();

    const char *ttl = "user@mykernel: ~  -bash-  80x24";
    int tw = (int)strlen(ttl) * 8;
    ugx_buf_text(&g_map, (TERM_W - tw) / 2, 10, ttl, UGX_RGB(210, 210, 210));

    ugx_fill(g_win, 0, TERM_TITLE_H, TERM_W, TERM_H - TERM_TITLE_H, COL_BG);

    int sx = TERM_W - TERM_SCROLL;
    ugx_fill(g_win, sx, TERM_TITLE_H, TERM_SCROLL, TERM_H - TERM_TITLE_H, COL_SCROLL);
    ugx_fill_round(g_win, sx + 3, TERM_TITLE_H + 8, 6, 48, 3, COL_SCROLL_THUMB);

    char tmp[2];
    tmp[1] = 0;
    for (int r = 0; r < TERM_ROWS; r++) {
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = g_cells[r][c];
            if (ch == ' ')
                continue;
            tmp[0] = ch;
            ugx_buf_text(&g_map,
                         TERM_PAD_X + c * TERM_CW,
                         TERM_TITLE_H + TERM_PAD_Y + r * TERM_CH,
                         tmp, COL_FG);
        }
    }

    if (focused && ((g_blink / 25) & 1) == 0) {
        int px = TERM_PAD_X + g_cx * TERM_CW;
        int py = TERM_TITLE_H + TERM_PAD_Y + g_cy * TERM_CH;
        ugx_fill(g_win, px, py, TERM_CW, TERM_CH - 2, COL_CURSOR);
        if (g_cx < TERM_COLS && g_cells[g_cy][g_cx] != ' ') {
            tmp[0] = g_cells[g_cy][g_cx];
            ugx_buf_text(&g_map, px, py, tmp, COL_BG);
        }
    }

    ugx_damage();
}

static int terminal_open(int screen_w, int screen_h)
{
    ugx_win_create a;

    if (g_win >= 0) {
        ugx_wm_show(g_win, 1);
        ugx_wm_focus(g_win);
        return 0;
    }

    memset(&a, 0, sizeof(a));
    a.w = TERM_W;
    a.h = TERM_H;
    a.x = (screen_w - TERM_W) / 2;
    a.y = (screen_h - TERM_H) / 2;
    if (a.y < 40)
        a.y = 40;
    a.style = UGX_STYLE_ROUNDED;
    a.radius = 10;
    strncpy(a.title, "Terminal", sizeof(a.title) - 1);

    g_win = ugx_wm_create(&a);
    if (g_win < 0)
        return -1;
    if (ugx_wm_map(g_win, &g_map) < 0)
        return -1;

    term_clear();
    term_puts("Last login: Sat Jul 11 15:39:00 on ttys001\n");
    term_prompt();
    ugx_wm_focus(g_win);
    terminal_paint(1);
    return 0;
}

static void terminal_close(void)
{
    if (g_win < 0)
        return;
    ugx_wm_destroy(g_win);
    g_win = -1;
}

static void terminal_tick(const ugx_input_state *in, uint8_t pressed)
{
    ugx_frame fr;

    if (g_win < 0)
        return;

    g_blink++;

    if (ugx_wm_get_frame(g_win, &fr) == 0 && (pressed & UGX_BTN_LEFT)) {
        if (in->mouse_x >= fr.x + 6 && in->mouse_x < fr.x + 22 &&
            in->mouse_y >= fr.y + 6 && in->mouse_y < fr.y + 22) {
            terminal_close();
            return;
        }
    }

    int ch;
    int dirty = 0;
    while ((ch = ugx_wm_pop_key(g_win)) >= 0) {
        dirty = 1;
        if (ch == '\b') {
            if (g_llen > 0) {
                g_llen--;
                g_line[g_llen] = 0;
                if (g_cx > 0) {
                    g_cx--;
                    g_cells[g_cy][g_cx] = ' ';
                }
            }
        } else if (ch == '\n' || ch == '\r') {
            term_newline_cmd();
        } else if (ch >= 32 && g_llen < TERM_COLS - 1) {
            g_line[g_llen++] = (char)ch;
            g_line[g_llen] = 0;
            term_putc_cell((char)ch);
        }
    }

    if (dirty || (g_blink % 25) == 0)
        terminal_paint(in->focus_id == g_win);
}

void mke_main(void)
{
    ugx_info info;
    if (ugx_info_get(&info) < 0) {
        for (;;)
            ugx_yield();
    }

    if (terminal_open((int)info.width, (int)info.height) < 0) {
        for (;;)
            ugx_yield();
    }

    prev_buttons = 0;
    for (;;) {
        ugx_present();

        ugx_input_state in;
        if (ugx_input_get(&in) == 0) {
            uint8_t pressed = (uint8_t)(in.buttons & ~prev_buttons);
            terminal_tick(&in, pressed);
            prev_buttons = in.buttons;
        } else {
            ugx_input_state z;
            memset(&z, 0, sizeof(z));
            terminal_tick(&z, 0);
        }

        if (g_win < 0) {
            /* Recreate if closed so dock focus still works after relaunch delay */
            terminal_open((int)info.width, (int)info.height);
        }

        ugx_yield();
    }
}
