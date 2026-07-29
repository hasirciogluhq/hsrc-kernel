-- Shared initrd / driver / disk layout
function kmod_order()
    -- Early ramfs / → disk FAT replaces / → virtual FS on top.
    return {
        "block", "vfs", "part_gpt", "part_mbr", "ramfs",
        "ramdisk", "loop", "virtio_blk", "fat",
        "tmpfs", "devtmpfs", "procfs", "sysfs", "initrdfs",
        "exfat", "ext", "iso9660", "udf", "ntfs", "ahci", "nvme",
        "display_bga", "display_virtio", "display", "dx", "virtio_net",
    }
end

-- Initrd: kmods + PID1 as bare name "init" (installed to VFS /init at boot).
function initrd_exec_names()
    return { "init" }
end

-- OS / GUI package → /system/bin
function system_exec_names()
    return {
        "window-manager", "os-shell", "os-settings",
        "terminal", "files", "activity-monitor",
    }
end

-- User applications → /applications
function user_exec_names()
    return {
        "minesweeper", "imgui-demo", "libfs-demo", "libfs-demo2",
    }
end

-- Dynamic userspace libraries → /system/lib
function system_dynlib_names()
    return { "libfs" }
end

function disk_exec_names()
    local t = {}
    for _, n in ipairs(system_exec_names()) do
        table.insert(t, n)
    end
    for _, n in ipairs(user_exec_names()) do
        table.insert(t, n)
    end
    return t
end

function app_exec_names()
    local t = {}
    for _, n in ipairs(initrd_exec_names()) do
        table.insert(t, n)
    end
    for _, n in ipairs(disk_exec_names()) do
        table.insert(t, n)
    end
    return t
end
