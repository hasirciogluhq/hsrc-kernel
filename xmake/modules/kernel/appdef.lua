-- Shared userspace app target factory (canonical USER_IMAGE_BASE link).

--[[
All usermode apps link at the same canonical VA (USER_IMAGE_BASE / 0x00400000).
The kernel ELF loader maps PT_LOAD into a private address space at that VA.
Output is a plain ELF32 ET_EXEC (.elf); legacy .exec packing is optional/unused.
--]]

kGuiLibs = {"sdk-wm", "sdk-kilim", "sdk-reed"}

function define_app(name, files, incs, flags, needed, extra_libs)
    local ROOT = os.projectdir()
    local INC = path.join(ROOT, "include")
    local BUILD = path.join(ROOT, "build")
    local abs_files = {}
    for _, f in ipairs(files) do
        table.insert(abs_files, path.join(ROOT, f))
    end
    target("app-" .. name)
        set_kind("binary")
        set_default(false)
        kernel_cross_target()
        add_deps("sdk-core")
        if needed then
            -- Build-order only: object-kind dep must not pull libfs.c.o into the app link
            -- (import.cpp resolves libfs_* via dynlib; linking libfs.c.o doubles symbols).
            add_deps("dynlib-libfs", {inherit = false})
        end
        if extra_libs then
            for _, lib in ipairs(extra_libs) do
                add_deps(lib)
            end
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
        on_link(function (target)
            local out = target:targetfile()
            os.mkdir(path.directory(out))
            local libgcc = (os.iorun("i686-elf-g++ -print-libgcc-file-name") or ""):gsub("%s+$", "")
            local sdk = path.join(BUILD, "userspace/lib/libsdk-core.a")
            local args = {
                "-m", "elf_i386", "-nostdlib",
                "-T", path.join(ROOT, "ld/user.ld"),
                "-o", out,
            }
            local app_tag = "/app-" .. name .. "/"
            for _, o in ipairs(target:objectfiles()) do
                local p = tostring(o)
                if p:find(app_tag, 1, true) then
                    table.insert(args, o)
                end
            end
            -- Extra SDKs before sdk-core (wm → kilim → reed → core).
            if extra_libs then
                for _, lib in ipairs(extra_libs) do
                    table.insert(args, path.join(BUILD, "userspace/lib/lib" .. lib .. ".a"))
                end
            end
            table.insert(args, sdk)
            if libgcc ~= "" then table.insert(args, libgcc) end
            os.execv("i686-elf-ld", args)
        end)
end
