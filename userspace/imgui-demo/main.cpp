#include <stddef.h>
#include <stdlib.h>

#include "imgui.h"
#include "imgui_impl_kilim.h"

#include <user/sdk/heap.hpp>
#include <user/sdk/wm.hpp>
#include <user/sdk/kilim.hpp>
#include <user/sdk/reed.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>

namespace {

constexpr int kWinW = 720;
constexpr int kWinH = 480;

} // namespace

extern "C" void exec_main(void)
{
    ImGui::SetAllocatorFunctions(
        [](size_t sz, void *) -> void * { return malloc(sz); },
        [](void *p, void *) { free(p); }, nullptr);

    static reed::Device dev;
    static kilim::Context k;

    while (dev.init() < 0 || k.init(&dev) < 0)
        hsrc::sdk::sleep(100);

    wm::WindowOptions opts;
    opts.w = kWinW;
    opts.h = kWinH;
    opts.x = 160;
    opts.y = 100;
    opts.capture_keys = true;
    opts.set_title("ImGui Demo");
    opts.set_class_name("imgui-demo");

    wm::Window win;
    if (!win.create(opts))
        hsrc::sdk::exit(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    if (!ImGui_ImplKilim_Init())
        hsrc::sdk::exit(1);

    bool mapped = false;
    uint8_t prev_buttons = 0;
    int clicks = 0;

    for (;;) {
        wm::Input in;
        (void)wm::input_snapshot(in);
        (void)win.get_options(opts);

        if (k.begin_frame() < 0) {
            continue;
        }

        ImGui_ImplKilim_NewFrame(win, in, opts, prev_buttons, wm::kChromeTitleH);
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("hsrcOS ImGui")) {
            ImGui::Text("kilim + reed backend");
            if (ImGui::Button("Click me"))
                clicks++;
            ImGui::SameLine();
            ImGui::Text("clicks = %d", clicks);
            ImGui::SliderFloat("demo", &ImGui::GetStyle().Alpha, 0.3f, 1.0f);
            ImGui::Text("heap used ~%u", (unsigned)hsrc::sdk::heap::used_bytes());
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplKilim_RenderDrawData(ImGui::GetDrawData(), k.target().color(),
                                       wm::kChromeTitleH);

        (void)k.commit_frame();
        if (!mapped) {
            mapped = win.map_surface(dev, k.target());
            (void)win.damage();
        } else {
            (void)win.damage();
        }
        prev_buttons = in.buttons;
    }
}
