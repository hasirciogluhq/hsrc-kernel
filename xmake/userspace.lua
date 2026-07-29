-- Userspace SDK + apps (.exec) + dynamic .dynlib libs
local ROOT = os.projectdir()
local INC = path.join(ROOT, "include")
local BUILD = path.join(ROOT, "build")

target("sdk-core")
    set_kind("static")
    set_default(false)
    kernel_cross_target()
    add_files(path.join(ROOT, "userspace/sdk/core/*.cpp"),
              path.join(ROOT, "userspace/sdk/core/string.c"))
    add_includedirs(INC, path.join(ROOT, "userspace/sdk/core"), {public = true})
    add_defines("USERMODE")
    add_cxxflags(kernel_cxxflags(), {force = true})
    add_cflags(kernel_cflags(), {force = true})
    add_cflags("-DUSERMODE", {force = true})
    set_targetdir(path.join(BUILD, "userspace/lib"))

-- Dynamic userspace FS library → build/userspace/lib/libfs.dynlib (ELF ET_REL)
target("dynlib-libfs")
    set_kind("object")
    set_default(false)
    kernel_cross_target()
    add_files(path.join(ROOT, "userspace/sdk/libfs/libfs.c"))
    add_includedirs(INC)
    add_defines("USERMODE")
    add_cflags(kernel_cflags(), {force = true})
    add_cflags("-DUSERMODE", {force = true})
    set_targetdir(path.join(BUILD, "userspace/lib/obj/libfs"))
    after_build(function (target)
        local out = path.join(BUILD, "userspace/lib/libfs.dynlib")
        os.mkdir(path.directory(out))
        local args = {"-m", "elf_i386", "-r", "-o", out}
        for _, o in ipairs(target:objectfiles()) do
            table.insert(args, o)
        end
        os.execv("i686-elf-ld", args)
        print("packed " .. out)
    end)

local function define_app(name, load_addr, files, incs, flags, needed)
    local abs_files = {}
    for _, f in ipairs(files) do
        table.insert(abs_files, path.join(ROOT, f))
    end
    target("app-" .. name)
        set_kind("binary")
        set_default(false)
        kernel_cross_target()
        add_deps("sdk-core", "pack_exec")
        if needed then
            add_deps("dynlib-libfs")
        end
        add_files(abs_files)
        add_includedirs(INC, path.join(ROOT, "userspace/sdk/core"))
        if incs then
            for _, d in ipairs(incs) do
                add_includedirs(path.join(ROOT, d))
            end
        end
        add_defines("USERMODE")
        add_cxxflags(flags or kernel_cxxflags(), {force = true})
        set_targetdir(path.join(BUILD, "userspace", name))
        set_filename(name .. ".elf")
        -- Pack .exec inside on_link (not after_build): with -jN, xmake can
        -- start disk/initrd after_build as soon as the .elf exists, racing
        -- pack_exec and leaving imgui-demo.exec missing on CI/release.
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
            import("kernel.pack")
            pack.pack_exec(target, load_addr, name, needed)
        end)
end

define_app("init", 0x02000000, {"userspace/init/main.cpp"})
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
}, kernel_imgui_cxxflags())

define_app("libfs-demo", 0x03400000, {
    "userspace/libfs-demo/main.cpp",
    "userspace/sdk/libfs/import.cpp",
}, nil, nil, {"libfs.dynlib"})
define_app("libfs-demo2", 0x03600000, {
    "userspace/libfs-demo2/main.cpp",
    "userspace/sdk/libfs/import.cpp",
}, nil, nil, {"libfs.dynlib"})

target("userspace")
    set_kind("phony")
    set_default(true)
    add_deps("sdk-core", "sdk-reed", "sdk-kilim", "dynlib-libfs",
        "app-init", "app-window-manager", "app-os-shell",
        "app-os-settings", "app-terminal", "app-files", "app-activity-monitor",
        "app-minesweeper", "app-imgui-demo",
        "app-libfs-demo", "app-libfs-demo2")
