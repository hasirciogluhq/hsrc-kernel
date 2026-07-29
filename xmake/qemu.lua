-- Host tools, initrd pack, disk image, QEMU run
local ROOT = os.projectdir()
local BUILD = path.join(ROOT, "build")

for _, t in ipairs({
    {"pack_initrd", "tools/pack_initrd.c"},
    {"pack_exec", "tools/pack_exec.c"},
    {"pack_fat", "tools/pack_fat.c"},
    {"mkfatimg", "tools/mkfatimg.c"},
}) do
    target(t[1])
        set_kind("binary")
        set_default(false)
        kernel_host_target()
        add_files(path.join(ROOT, t[2]))
        set_targetdir(path.join(BUILD, "tools"))
        set_filename(t[1])
end

-- Initrd: kmods + PID1 ("init" → VFS /init).
target("initrd")
    set_kind("phony")
    set_default(true)
    add_deps("drivers", "pack_initrd", "userspace")
    after_build(function (target)
        import("kernel.layout")
        local packer = path.join(BUILD, "tools", "pack_initrd")
        local out = path.join(BUILD, "drivers", "initrd.img")
        local stagedir = path.join(BUILD, "initrd-root")
        os.mkdir(path.directory(out))
        os.mkdir(stagedir)
        local args = {out}
        for _, n in ipairs(layout.kmod_order()) do
            table.insert(args, path.join(BUILD, "drivers", n .. ".kmod"))
        end
        for _, n in ipairs(layout.initrd_exec_names()) do
            local src = path.join(BUILD, "userspace", n, n .. ".exec")
            local staged = path.join(stagedir, n)
            os.cp(src, staged)
            table.insert(args, staged)
        end
        os.execv(packer, args)
    end)

--[[
  Dev disk image layout (FAT on vda = /):
    /init                     PID1 (ELF bytes, bare name)
    /system/bin/*.exec        OS / GUI package
    /system/lib/*.dynlib      dynamic libraries
    /system/share/...         assets
    /system/etc/environment
    /applications/*.exec      user apps
  Virtual mounts on top after boot: /dev /proc /sys /tmp
  Resolve tries bare → .exec → .elf (legacy).
]]
target("disk")
    set_kind("phony")
    set_default(true)
    add_deps("mkfatimg", "pack_fat", "userspace")
    after_build(function (target)
        import("kernel.layout")
        local img = path.join(ROOT, "disk.img")
        local mkfat = path.join(BUILD, "tools", "mkfatimg")
        local packer = path.join(BUILD, "tools", "pack_fat")
        -- DISK_SIZE_MB=32 for release; default 64 for local dev.
        local size_mb = os.getenv("DISK_SIZE_MB") or "64"
        local args = {img}
        -- PID1 lives on the root disk like Linux /init (bare name, ELF bytes)
        table.insert(args, path.join(BUILD, "userspace/init/init.exec") .. ":init")
        for _, n in ipairs(layout.system_exec_names()) do
            local src = path.join(BUILD, "userspace", n, n .. ".exec")
            table.insert(args, src .. ":system/bin/" .. n .. ".exec")
        end
        for _, n in ipairs(layout.user_exec_names()) do
            local src = path.join(BUILD, "userspace", n, n .. ".exec")
            table.insert(args, src .. ":applications/" .. n .. ".exec")
        end
        for _, n in ipairs(layout.system_dynlib_names()) do
            local src = path.join(BUILD, "userspace/lib", n .. ".dynlib")
            table.insert(args, src .. ":system/lib/" .. n .. ".dynlib")
        end
        table.insert(args, path.join(ROOT, "assets/os/wallpaper-default.bmp") .. ":system/share/wallpaper-default.bmp")
        for _, ic in ipairs({
            "theme-sun.svg", "theme-moon.svg", "status-wifi.svg",
            "status-wifi-off.svg", "status-battery.svg", "status-bolt.svg",
        }) do
            table.insert(args, path.join(ROOT, "assets/os/icons", ic) .. ":system/share/" .. ic)
        end
        table.insert(args, path.join(ROOT, "assets/etc/environment") .. ":system/etc/environment")
        -- Fail before mkfat if any payload is missing (clears -jN races / stale deps).
        for i = 2, #args do
            local src = args[i]:match("^([^:]+)")
            assert(src and os.isfile(src), "disk payload missing: " .. tostring(src))
        end
        os.execv(mkfat, {img, size_mb})
        os.execv(packer, args)
    end)

-- Alias: `xmake build disk-install` == refresh disk.img from build artifacts
target("disk-install")
    set_kind("phony")
    set_default(false)
    add_deps("disk")

-- Production release tree under dist/ (see scripts/pack-release.sh)
target("release")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "initrd", "disk")
    on_build(function (target)
        local ver = os.getenv("RELEASE_VERSION") or ""
        local args = {path.join(ROOT, "scripts/pack-release.sh"), "--skip-build"}
        if ver ~= "" then
            table.insert(args, ver)
        end
        os.setenv("DISK_SIZE_MB", os.getenv("DISK_SIZE_MB") or "32")
        os.execv("bash", args)
    end)

target("kernel")
    add_deps("initrd", "disk")
    on_run(function (target)
        import("kernel.qemu")
        qemu.run_qemu()
    end)

target("qemu")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "initrd", "disk")
    on_build(function (target)
        import("kernel.qemu")
        qemu.run_qemu()
    end)
