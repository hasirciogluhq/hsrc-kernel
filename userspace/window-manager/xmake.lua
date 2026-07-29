define_app("window-manager", {
    "userspace/window-manager/src/main.cpp",
    "userspace/window-manager/src/wm_state.cpp",
    "userspace/window-manager/src/wm_ipc.cpp",
    "userspace/window-manager/src/wm_input.cpp",
    "userspace/window-manager/src/wm_compose.cpp",
    "userspace/window-manager/src/wm_dock.cpp",
}, {"userspace/window-manager/include"}, nil, nil, kGuiLibs)
