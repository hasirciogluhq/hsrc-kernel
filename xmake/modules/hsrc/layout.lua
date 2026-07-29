-- Shared initrd / driver / disk layout
function kmod_order()
    return {
        "block", "vfs", "part_gpt", "part_mbr", "ramfs", "devtmpfs",
        "ramdisk", "loop", "virtio_blk", "fat", "tmpfs", "procfs", "sysfs", "initrdfs",
        "exfat", "ext", "iso9660", "udf", "ntfs", "ahci", "nvme",
        "display_bga", "display_virtio", "mkdx", "virtio_net",
    }
end

-- Initrd: kmods + PID1 as bare name "init" (installed to VFS /init at boot).
function initrd_mke_names()
    return { "init" }
end

-- OS / GUI package → /system/bin
function system_mke_names()
    return {
        "window-manager", "os-shell", "os-settings",
        "terminal", "files", "activity-monitor",
    }
end

-- User applications → /applications
function user_mke_names()
    return {
        "minesweeper", "imgui-demo",
    }
end

function disk_mke_names()
    local t = {}
    for _, n in ipairs(system_mke_names()) do
        table.insert(t, n)
    end
    for _, n in ipairs(user_mke_names()) do
        table.insert(t, n)
    end
    return t
end

function app_mke_names()
    local t = {}
    for _, n in ipairs(initrd_mke_names()) do
        table.insert(t, n)
    end
    for _, n in ipairs(disk_mke_names()) do
        table.insert(t, n)
    end
    return t
end
