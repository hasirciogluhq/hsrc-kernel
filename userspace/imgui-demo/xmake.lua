define_app("imgui-demo", {
    "userspace/imgui-demo/main.cpp",
    "userspace/imgui-demo/imgui_impl_kilim.cpp",
    "userspace/imgui-demo/support.cpp",
    "userspace/imgui-demo/third_party/imgui/imgui.cpp",
    "userspace/imgui-demo/third_party/imgui/imgui_draw.cpp",
    "userspace/imgui-demo/third_party/imgui/imgui_tables.cpp",
    "userspace/imgui-demo/third_party/imgui/imgui_widgets.cpp",
}, {
    "userspace/imgui-demo",
    "userspace/imgui-demo/freestanding",
    "userspace/imgui-demo/third_party/imgui",
}, kernel_imgui_cxxflags(), nil, kGuiLibs)
