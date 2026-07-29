-- Launch QEMU with kernel + initrd + disk
-- Display: -vga std → Bochs LFB (display_bga). Do NOT also add virtio-gpu-pci:
-- that steals display_active() (higher prio) while the visible window stays std → black screen.
-- Virtio GPU boot: use -vga virtio (no -vga std) when testing display_virtio.
function qemu_args()
    local ROOT = os.projectdir()
    local BUILD = path.join(ROOT, "build")
    return {
        "-kernel", path.join(BUILD, "kernel.bin"),
        "-initrd", path.join(BUILD, "drivers", "initrd.img"),
        "-m", "1G",
        "-smp", "4,sockets=1,cores=4,threads=1",
        -- "-vga", "std",
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
