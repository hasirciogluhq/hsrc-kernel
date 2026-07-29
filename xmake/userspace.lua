-- Userspace SDK + shared dynlib + aggregate phony target
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

target("userspace")
    set_kind("phony")
    set_default(true)
    add_deps("sdk-core", "sdk-reed", "sdk-kilim", "sdk-wm", "dynlib-libfs",
        "app-init", "app-window-manager", "app-os-shell",
        "app-os-settings", "app-terminal", "app-files", "app-activity-monitor",
        "app-minesweeper", "app-imgui-demo")
