#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/process.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

/* Activity Monitor — process table (Fluent list). */

namespace {

[[noreturn]] void hang(void)
{
    for (;;)
        hsrc::sdk::syscall0(SYS_YIELD);
}

} // namespace

extern "C" void exec_main(void)
{
    static reed::Device dev;
    static kilim::Context k;

    if (dev.init() < 0 || k.init(&dev) < 0)
        hang();

    wm::WindowOptions opts;
    opts.x = 180;
    opts.y = 100;
    opts.w = 820;
    opts.h = 520;
    opts.min_w = 560;
    opts.min_h = 360;
    opts.set_title("Activity Monitor");
    opts.set_class_name("activity-monitor");

    wm::Window win;
    if (!win.create(opts))
        hang();

    (void)hsrc::sdk::process::map_proc_page();
    bool mapped = false;
    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        k.set_pointer(in.mouse_x, in.mouse_y, in.buttons);
        (void)hsrc::sdk::process::poll_snapshot_publish();

        if (k.begin_frame() < 0) {
            hsrc::sdk::yield(1);
            continue;
        }

        int w = opts.w;
        int h = opts.h;
        int top = wm::kChromeTitleH;
        k.fill_rect(0, 0, w, h, kilim::rgba(32, 34, 40, 255));
        k.fill_rect(0, top, w, 40, kilim::rgba(28, 30, 36, 255));
        k.text("CPU / Memory / Processes", 20, top + 12, 14,
               kilim::rgba(200, 206, 218, 255));

        k.fill_rect(0, top + 40, w, 28, kilim::rgba(40, 44, 54, 255));
        k.text("PID", 20, top + 48, 12, kilim::rgba(160, 170, 185, 255));
        k.text("Name", 90, top + 48, 12, kilim::rgba(160, 170, 185, 255));
        k.text("State", 320, top + 48, 12, kilim::rgba(160, 170, 185, 255));
        k.text("Mem", 420, top + 48, 12, kilim::rgba(160, 170, 185, 255));

        int count = hsrc::sdk::process::snapshot_count();
        int y = top + 76;
        int shown = 0;
        for (int i = 0; i < count && shown < 12; i++) {
            hsrc::sdk::process::ProcListEntry e{};
            if (!hsrc::sdk::process::snapshot_entry(i, &e))
                continue;
            if (!e.is_user && e.pid > 2)
                continue;
            if (shown % 2)
                k.fill_rect(0, y - 4, w, 28, kilim::rgba(36, 38, 46, 255));
            char pidbuf[16];
            pidbuf[0] = '\0';
            /* tiny itoa */
            {
                unsigned v = (unsigned)e.pid;
                char tmp[12];
                int n = 0;
                if (v == 0)
                    tmp[n++] = '0';
                while (v) {
                    tmp[n++] = (char)('0' + (v % 10u));
                    v /= 10u;
                }
                int p = 0;
                while (n--)
                    pidbuf[p++] = tmp[n];
                pidbuf[p] = '\0';
            }
            k.text(pidbuf, 20, y, 13, kilim::rgba(230, 235, 245, 255));
            k.text(e.name[0] ? e.name : "?", 90, y, 13,
                   kilim::rgba(230, 235, 245, 255));
            const char *st = "?";
            if (e.state == 1)
                st = "Ready";
            else if (e.state == 2)
                st = "Running";
            else if (e.state == 3)
                st = "Sleep";
            else if (e.state == 4)
                st = "Zombie";
            k.text(st, 320, y, 13, kilim::rgba(180, 190, 205, 255));
            y += 28;
            shown++;
        }

        (void)k.commit_frame();
        static int tick;
        if (!mapped) {
            mapped = win.map_surface(dev, k.target());
            (void)win.damage();
        } else if ((++tick & 15) == 0) {
            (void)win.damage(); /* ~throttle process list refresh */
        }
        hsrc::sdk::yield(2);
    }
}
