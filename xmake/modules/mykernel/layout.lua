-- Shared initrd / driver / disk layout
function kmod_order()
    return {
        "block", "vfs", "part_gpt", "part_mbr", "ramfs", "devtmpfs",
        "ramdisk", "loop", "virtio_blk", "fat", "tmpfs", "procfs", "sysfs", "initrdfs",
        "exfat", "ext", "iso9660", "udf", "ntfs", "ahci", "nvme",
        "display_bga", "display_virtio", "mkdx", "virtio_net",
    }
end

-- Only init stays in initrd (RAM). Everything else is on the FAT disk.
function initrd_mke_names()
    return {"init"}
end

function disk_mke_names()
    return {
        "systemd", "window-manager", "os-shell", "os-settings",
        "terminal", "files", "activity-monitor", "minesweeper", "imgui-demo",
    }
end

function app_mke_names()
    local t = {}
    for _, n in ipairs(initrd_mke_names()) do table.insert(t, n) end
    for _, n in ipairs(disk_mke_names()) do table.insert(t, n) end
    return t
end
