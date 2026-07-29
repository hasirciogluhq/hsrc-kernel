-- Custom .kmod targets → packed into initrd
local ROOT = os.projectdir()
local INC = path.join(ROOT, "include")
local BUILD = path.join(ROOT, "build")

local KMOD_INCLUDES = {
    INC,
    path.join(ROOT, "src/drivers/display/providers/virtio_gpu"),
}

function kernel_define_kmod(name, patterns)
    target("kmod-" .. name)
        set_kind("object")
        set_default(false)
        kernel_cross_target()
        for _, p in ipairs(patterns) do
            add_files(path.join(ROOT, p))
        end
        add_includedirs(table.unpack(KMOD_INCLUDES))
        add_cflags(kernel_cflags(), {force = true})
        set_targetdir(path.join(BUILD, "drivers/obj", name))
        after_build(function (target)
            local out = path.join(BUILD, "drivers", name .. ".kmod")
            os.mkdir(path.directory(out))
            local args = {"-m", "elf_i386", "-r", "-o", out}
            for _, o in ipairs(target:objectfiles()) do
                table.insert(args, o)
            end
            os.execv("i686-elf-ld", args)
        end)
end

kernel_define_kmod("block", {"src/drivers/block/block.c"})
kernel_define_kmod("vfs", {"src/drivers/vfs/*.c"})
kernel_define_kmod("part_gpt", {"src/drivers/part/gpt/*.c"})
kernel_define_kmod("part_mbr", {"src/drivers/part/mbr/*.c"})
kernel_define_kmod("ramfs", {"src/drivers/fs/ramfs/*.c"})
kernel_define_kmod("devtmpfs", {"src/drivers/fs/devtmpfs/*.c"})
kernel_define_kmod("ramdisk", {"src/drivers/block/ramdisk/*.c"})
kernel_define_kmod("loop", {"src/drivers/block/loop/*.c"})
kernel_define_kmod("virtio_blk", {"src/drivers/block/virtio_blk/*.c"})
kernel_define_kmod("fat", {"src/drivers/fs/fat/*.c"})
kernel_define_kmod("tmpfs", {"src/drivers/fs/tmpfs/*.c"})
kernel_define_kmod("procfs", {"src/drivers/fs/procfs/*.c"})
kernel_define_kmod("sysfs", {"src/drivers/fs/sysfs/*.c"})
kernel_define_kmod("initrdfs", {"src/drivers/fs/initrdfs/*.c"})
kernel_define_kmod("exfat", {"src/drivers/fs/exfat/*.c"})
kernel_define_kmod("ext", {"src/drivers/fs/ext/*.c"})
kernel_define_kmod("iso9660", {"src/drivers/fs/iso9660/*.c"})
kernel_define_kmod("udf", {"src/drivers/fs/udf/*.c"})
kernel_define_kmod("ntfs", {"src/drivers/fs/ntfs/*.c"})
kernel_define_kmod("ahci", {"src/drivers/block/ahci/*.c"})
kernel_define_kmod("nvme", {"src/drivers/block/nvme/*.c"})
kernel_define_kmod("virtio_net", {"src/drivers/net/virtio_net/*.c"})
kernel_define_kmod("display_bga", {"src/drivers/display/providers/bga/*.c"})
kernel_define_kmod("display_virtio", {"src/drivers/display/providers/virtio_gpu/*.c"})
kernel_define_kmod("display", {"src/drivers/display/display_mod.c"})

local KMOD_ORDER = {
    "block", "vfs", "part_gpt", "part_mbr", "ramfs",
    "ramdisk", "loop", "virtio_blk", "fat",
    "tmpfs", "devtmpfs", "procfs", "sysfs", "initrdfs",
    "exfat", "ext", "iso9660", "udf", "ntfs", "ahci", "nvme",
    "display_bga", "display_virtio", "display", "virtio_net",
}

target("drivers")
    set_kind("phony")
    set_default(true)
    for _, n in ipairs(KMOD_ORDER) do
        add_deps("kmod-" .. n)
    end
