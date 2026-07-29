-- Cross freestanding flags for i686-elf
function mykernel_cflags()
    return {
        "-std=c11",
        "-ffreestanding",
        "-m32",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O2",
        "-fno-stack-protector",
        "-fno-pic",
        "-fno-builtin",
        "-nostdlib",
        "-fno-common",
    }
end

function mykernel_cxxflags()
    return {
        "-std=c++17",
        "-ffreestanding",
        "-m32",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-O2",
        "-fno-stack-protector",
        "-fno-pic",
        "-fno-builtin",
        "-nostdlib",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-use-cxa-atexit",
        "-fno-threadsafe-statics",
    }
end

function mykernel_imgui_cxxflags()
    return {
        "-std=c++23",
        "-ffreestanding",
        "-m32",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-Wno-unused-function",
        "-Wno-missing-field-initializers",
        "-Wno-invalid-offsetof",
        "-Wno-class-memaccess",
        "-O2",
        "-fno-stack-protector",
        "-fno-pic",
        "-fno-builtin",
        "-nostdlib",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-use-cxa-atexit",
        "-fno-threadsafe-statics",
        "-fcoroutines",
    }
end

rule("nasm")
    set_extensions(".asm")
    on_build_file(function (target, sourcefile, opt)
        import("core.project.depend")
        local objectfile = target:objectfile(sourcefile)
        os.mkdir(path.directory(objectfile))
        depend.on_changed(function ()
            os.execv("nasm", {"-f", "elf32", sourcefile, "-o", objectfile})
        end, {files = sourcefile, values = objectfile})
        local objs = target:objectfiles()
        for _, o in ipairs(objs) do
            if o == objectfile then return end
        end
        table.insert(objs, objectfile)
    end)

function mykernel_cross_target()
    set_plat("cross")
    set_arch("i386")
    set_toolset("cc", "i686-elf-gcc")
    set_toolset("cxx", "i686-elf-g++")
    set_toolset("ld", "i686-elf-ld")
    set_toolset("ar", "i686-elf-ar")
end

function mykernel_host_target()
    set_plat(os.host())
    set_arch(os.arch())
    set_toolset("cc", "cc")
    set_toolset("ld", "cc")
    set_toolset("cxx", "c++")
end
