-- Launch QEMU with kernel + initrd + disk
--
-- Display (pick ONE primary — dual GPU = black window):
--   BGA path:    "-vga", "std"              → display_bga, LFB 0xfd00_0000
--   Virtio path: "-vga", "virtio"           → virtio-vga = visible window + virtio-gpu
--
-- NEVER do: comment out -vga std and only add "-device", "virtio-gpu-pci"
--   → QEMU still keeps default std VGA; BGA blacks that window; present goes to
--     the invisible virtio head (prio 20) → splash/WM “work” but you see black.
-- NEVER combine "-vga", "std" with "-device", "virtio-gpu-pci" for the same reason.
function qemu_args()
    local ROOT = os.projectdir()
    local BUILD = path.join(ROOT, "build")
    return {
        "-kernel", path.join(BUILD, "kernel.bin"),
        "-initrd", path.join(BUILD, "drivers", "initrd.img"),
        "-m", "1G",
        "-smp", "4,sockets=1,cores=4,threads=1",
        "-vga", "none",
        "-device", "virtio-gpu-pci",
        "-serial", "stdio",
        "-drive", "if=none,id=vd0,file=" .. path.join(ROOT, "disk.img") .. ",format=raw,cache=writethrough",
        "-device", "virtio-blk-pci,drive=vd0,disable-legacy=on",
        "-netdev", "user,id=n0",
        "-device", "virtio-net-pci,netdev=n0,disable-legacy=on",
    }
end

function run_qemu()
    os.execv("qemu-system-i386", qemu_args())
end
