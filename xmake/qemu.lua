-- Host tools, initrd pack, disk image, QEMU run
local ROOT = os.projectdir()
local BUILD = path.join(ROOT, "build")

for _, t in ipairs({
    {"pack_initrd", "tools/pack_initrd.c"},
    {"pack_mke", "tools/pack_mke.c"},
    {"pack_fat", "tools/pack_fat.c"},
    {"mkfatimg", "tools/mkfatimg.c"},
}) do
    target(t[1])
        set_kind("binary")
        set_default(false)
        mykernel_host_target()
        add_files(path.join(ROOT, t[2]))
        set_targetdir(path.join(BUILD, "tools"))
        set_filename(t[1])
end

-- Initrd: kmods only.
target("initrd")
    set_kind("phony")
    set_default(true)
    add_deps("drivers", "pack_initrd")
    after_build(function (target)
        import("mykernel.layout")
        local packer = path.join(BUILD, "tools", "pack_initrd")
        local out = path.join(BUILD, "drivers", "initrd.img")
        os.mkdir(path.directory(out))
        local args = {out}
        for _, n in ipairs(layout.kmod_order()) do
            table.insert(args, path.join(BUILD, "drivers", n .. ".kmod"))
        end
        os.execv(packer, args)
    end)

-- Disk: FAT with /applications/init.mke (+ apps). Kernel execs /init.
target("disk")
    set_kind("phony")
    set_default(true)
    add_deps("mkfatimg", "pack_fat", "userspace")
    after_build(function (target)
        import("mykernel.layout")
        local img = path.join(ROOT, "disk.img")
        local mkfat = path.join(BUILD, "tools", "mkfatimg")
        local packer = path.join(BUILD, "tools", "pack_fat")
        os.execv(mkfat, {img, "64"})
        local args = {img}
        for _, n in ipairs(layout.disk_mke_names()) do
            local src = path.join(BUILD, "userspace", n, n .. ".mke")
            table.insert(args, src .. ":" .. n .. ".mke")
        end
        table.insert(args, path.join(ROOT, "assets/os/wallpaper-default.bmp") .. ":wallpaper-default.bmp")
        for _, ic in ipairs({
            "theme-sun.svg", "theme-moon.svg", "status-wifi.svg",
            "status-wifi-off.svg", "status-battery.svg", "status-bolt.svg",
        }) do
            table.insert(args, path.join(ROOT, "assets/os/icons", ic) .. ":" .. ic)
        end
        table.insert(args, path.join(ROOT, "assets/etc/environment") .. ":environment")
        table.insert(args, path.join(ROOT, "userspace/systemd/units/window-manager.service") .. ":window-manager.service")
        table.insert(args, path.join(ROOT, "userspace/systemd/units/os-shell.service") .. ":os-shell.service")
        os.execv(packer, args)
    end)

target("kernel")
    add_deps("initrd", "disk")
    on_run(function (target)
        import("mykernel.qemu")
        qemu.run_qemu()
    end)

target("qemu")
    set_kind("phony")
    set_default(false)
    add_deps("kernel", "initrd", "disk")
    on_build(function (target)
        import("mykernel.qemu")
        qemu.run_qemu()
    end)
