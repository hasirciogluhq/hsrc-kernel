-- Shared initrd / driver / disk layout
function kmod_order()
    return {
        "block", "vfs", "part_gpt", "part_mbr", "ramfs", "devtmpfs",
        "ramdisk", "loop", "virtio_blk", "fat", "tmpfs", "procfs", "sysfs", "initrdfs",
        "exfat", "ext", "iso9660", "udf", "ntfs", "ahci", "nvme",
        "display_bga", "display_virtio", "mkdx", "virtio_net",
    }
end

-- Initrd: kmods only (no userspace binaries in RAM).
function initrd_mke_names()
    return {}
end

-- Disk FAT: /init (+ apps). Dev installs defaults here; prod ISO does the same.
function disk_mke_names()
    return {
        "init", "window-manager", "os-shell", "os-settings",
        "terminal", "files", "activity-monitor", "minesweeper", "imgui-demo",
    }
end

function app_mke_names()
    return disk_mke_names()
end
