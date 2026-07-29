-- Custom .kmod targets → packed into initrd
local ROOT = os.projectdir()
local INC = path.join(ROOT, "include")
local BUILD = path.join(ROOT, "build")

local KMOD_INCLUDES = {
    INC,
    path.join(ROOT, "src/drivers/mkdx"),
    path.join(ROOT, "src/drivers/display/bga"),
    path.join(ROOT, "src/drivers/display/virtio_gpu"),
}

function mykernel_define_kmod(name, patterns)
    target("kmod-" .. name)
        set_kind("object")
        set_default(false)
        mykernel_cross_target()
        for _, p in ipairs(patterns) do
            add_files(path.join(ROOT, p))
        end
        add_includedirs(table.unpack(KMOD_INCLUDES))
        add_cflags(mykernel_cflags(), {force = true})
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

mykernel_define_kmod("block", {"src/drivers/block/block.c"})
mykernel_define_kmod("vfs", {"src/drivers/vfs/*.c"})
mykernel_define_kmod("part_gpt", {"src/drivers/part/gpt/*.c"})
mykernel_define_kmod("part_mbr", {"src/drivers/part/mbr/*.c"})
mykernel_define_kmod("ramfs", {"src/drivers/fs/ramfs/*.c"})
mykernel_define_kmod("devtmpfs", {"src/drivers/fs/devtmpfs/*.c"})
mykernel_define_kmod("ramdisk", {"src/drivers/block/ramdisk/*.c"})
mykernel_define_kmod("loop", {"src/drivers/block/loop/*.c"})
mykernel_define_kmod("virtio_blk", {"src/drivers/block/virtio_blk/*.c"})
mykernel_define_kmod("fat", {"src/drivers/fs/fat/*.c"})
mykernel_define_kmod("tmpfs", {"src/drivers/fs/tmpfs/*.c"})
mykernel_define_kmod("procfs", {"src/drivers/fs/procfs/*.c"})
mykernel_define_kmod("sysfs", {"src/drivers/fs/sysfs/*.c"})
mykernel_define_kmod("initrdfs", {"src/drivers/fs/initrdfs/*.c"})
mykernel_define_kmod("exfat", {"src/drivers/fs/exfat/*.c"})
mykernel_define_kmod("ext", {"src/drivers/fs/ext/*.c"})
mykernel_define_kmod("iso9660", {"src/drivers/fs/iso9660/*.c"})
mykernel_define_kmod("udf", {"src/drivers/fs/udf/*.c"})
mykernel_define_kmod("ntfs", {"src/drivers/fs/ntfs/*.c"})
mykernel_define_kmod("ahci", {"src/drivers/block/ahci/*.c"})
mykernel_define_kmod("nvme", {"src/drivers/block/nvme/*.c"})
mykernel_define_kmod("virtio_net", {"src/drivers/net/virtio_net/*.c"})
mykernel_define_kmod("display_bga", {"src/drivers/display/bga/*.c"})
mykernel_define_kmod("display_virtio", {"src/drivers/display/virtio_gpu/*.c"})
mykernel_define_kmod("mkdx", {"src/drivers/mkdx/*.c"})

local KMOD_ORDER = {
    "block", "vfs", "part_gpt", "part_mbr", "ramfs", "devtmpfs",
    "ramdisk", "loop", "virtio_blk", "fat", "tmpfs", "procfs", "sysfs", "initrdfs",
    "exfat", "ext", "iso9660", "udf", "ntfs", "ahci", "nvme",
    "display_bga", "display_virtio", "mkdx", "virtio_net",
}

target("drivers")
    set_kind("phony")
    set_default(true)
    for _, n in ipairs(KMOD_ORDER) do
        add_deps("kmod-" .. n)
    end
