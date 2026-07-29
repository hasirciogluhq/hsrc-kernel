-- Userspace SDK + apps (.mke)
local ROOT = os.projectdir()
local INC = path.join(ROOT, "include")
local BUILD = path.join(ROOT, "build")

target("sdk-core")
    set_kind("static")
    set_default(false)
    mykernel_cross_target()
    add_files(path.join(ROOT, "userspace/sdk/core/*.cpp"),
              path.join(ROOT, "userspace/sdk/core/string.c"))
    add_includedirs(INC, path.join(ROOT, "userspace/sdk/core"), {public = true})
    add_defines("USERMODE")
    add_cxxflags(mykernel_cxxflags(), {force = true})
    add_cflags(mykernel_cflags(), {force = true})
    add_cflags("-DUSERMODE", {force = true})
    set_targetdir(path.join(BUILD, "userspace/lib"))

local function define_app(name, load_addr, files, incs, flags)
    local abs_files = {}
    for _, f in ipairs(files) do
        table.insert(abs_files, path.join(ROOT, f))
    end
    target("app-" .. name)
        set_kind("binary")
        set_default(false)
        mykernel_cross_target()
        add_deps("sdk-core", "pack_mke")
        add_files(abs_files)
        add_includedirs(INC, path.join(ROOT, "userspace/sdk/core"))
        if incs then
            for _, d in ipairs(incs) do
                add_includedirs(path.join(ROOT, d))
            end
        end
        add_defines("USERMODE")
        add_cxxflags(flags or mykernel_cxxflags(), {force = true})
        set_targetdir(path.join(BUILD, "userspace", name))
        set_filename(name .. ".elf")
        on_link(function (target)
            local out = target:targetfile()
            os.mkdir(path.directory(out))
            local libgcc = (os.iorun("i686-elf-g++ -print-libgcc-file-name") or ""):gsub("%s+$", "")
            local sdk = path.join(BUILD, "userspace/lib/libsdk-core.a")
            local args = {
                "-m", "elf_i386", "-nostdlib",
                "-T", path.join(ROOT, "ld/user.ld"),
                "--defsym=LOAD_ADDR=" .. string.format("0x%x", load_addr),
                "-o", out,
            }
            for _, o in ipairs(target:objectfiles()) do table.insert(args, o) end
            table.insert(args, sdk)
            if libgcc ~= "" then table.insert(args, libgcc) end
            os.execv("i686-elf-ld", args)
        end)
        after_build(function (target)
            import("mykernel.pack")
            pack.pack_mke(target, load_addr, name)
        end)
end

define_app("init", 0x02000000, {"userspace/init/main.cpp"})
define_app("systemd", 0x02200000, {"userspace/systemd/main.cpp"})
define_app("window-manager", 0x02400000, {"userspace/window-manager/main.cpp"})
define_app("os-shell", 0x02600000, {"userspace/os-shell/main.cpp"})
define_app("os-settings", 0x02800000, {"userspace/os-settings/main.cpp"})
define_app("terminal", 0x02A00000, {"userspace/terminal/main.cpp"})
define_app("files", 0x02C00000, {"userspace/files/main.cpp"})
define_app("activity-monitor", 0x02E00000, {"userspace/activity-monitor/main.cpp"})
define_app("minesweeper", 0x03000000, {"userspace/minesweeper/main.cpp"})
define_app("imgui-demo", 0x03200000, {
    "userspace/imgui-demo/main.cpp",
    "userspace/imgui-demo/imgui_impl_ugx.cpp",
    "userspace/imgui-demo/support.cpp",
    "userspace/imgui-demo/third_party/imgui/imgui.cpp",
    "userspace/imgui-demo/third_party/imgui/imgui_draw.cpp",
    "userspace/imgui-demo/third_party/imgui/imgui_tables.cpp",
    "userspace/imgui-demo/third_party/imgui/imgui_widgets.cpp",
}, {
    "userspace/imgui-demo",
    "userspace/imgui-demo/freestanding",
    "userspace/imgui-demo/third_party/imgui",
}, mykernel_imgui_cxxflags())

target("userspace")
    set_kind("phony")
    set_default(true)
    add_deps("sdk-core", "sdk-reed", "sdk-kilim",
        "app-init", "app-systemd", "app-window-manager", "app-os-shell",
        "app-os-settings", "app-terminal", "app-files", "app-activity-monitor",
        "app-minesweeper", "app-imgui-demo")
