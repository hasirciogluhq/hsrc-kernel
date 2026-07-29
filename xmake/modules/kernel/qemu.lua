-- Launch QEMU with kernel + initrd + disk
--
-- Display (pick ONE primary — dual GPU = black window):
--   BGA path:    "-vga", "std"                         → display_bga (scanout only; no HW_SUBMIT)
--   Virtio 2D:   "-vga", "none" + virtio-gpu-pci       → display_virtio (present; VirGL iff host offers)
--   Virtio+GL:   "-vga", "none" + virtio-gpu-gl-pci    → display_virtio + VirGL (Linux QEMU + virglrenderer)
--
-- NEVER combine "-vga", "std" with virtio-gpu-* (dual head → black window).
-- Homebrew macOS QEMU often lacks virtio-gpu-gl-pci; we auto-pick.

function qemu_virtio_gpu_device()
    local help = try { function () return os.iorunv("qemu-system-i386", {"-device", "help"}) end } or ""
    if help:find("virtio%-gpu%-gl%-pci", 1) then
        return "virtio-gpu-gl-pci"
    end
    return "virtio-gpu-pci"
end

function qemu_args()
    local ROOT = os.projectdir()
    local BUILD = path.join(ROOT, "build")
    local gpu = qemu_virtio_gpu_device()
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
