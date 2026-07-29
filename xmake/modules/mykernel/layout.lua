-- Shared initrd / driver ordering
function kmod_order()
    return {
        "block", "vfs", "part_gpt", "part_mbr", "ramfs", "devtmpfs",
        "ramdisk", "loop", "virtio_blk", "fat", "tmpfs", "procfs", "sysfs", "initrdfs",
        "exfat", "ext", "iso9660", "udf", "ntfs", "ahci", "nvme",
        "display_bga", "display_virtio", "mkdx", "virtio_net",
    }
end

function app_mke_names()
    return {
        "init", "systemd", "window-manager", "os-shell", "os-settings",
        "terminal", "files", "activity-monitor", "minesweeper", "imgui-demo",
    }
end
