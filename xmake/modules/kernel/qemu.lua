-- Launch QEMU with kernel + initrd + disk
--
-- Display (pick ONE primary — dual GPU = black window):
--   std:    "-vga std"                              → BGA + visible VGA text (kshell)
--   virtio: "-vga none" + virtio-gpu-pci            → 2D scanout only (no VirGL → no GUI)
--   gl:     "-vga none" + virtio-gpu-gl-pci         → VirGL → GUI (B24)
--
-- Default: gl if QEMU offers *-gl-pci, else std (usable console on stock macOS QEMU).
-- Override: MYKERNEL_DISPLAY=std|virtio|gl

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
    local has_gl = gpu:find("%-gl%-") ~= nil
    local mode = os.getenv("MYKERNEL_DISPLAY") or ""

    local args = {
        "-kernel", path.join(BUILD, "kernel.bin"),
        "-initrd", path.join(BUILD, "drivers", "initrd.img"),
        "-m", "1G",
        "-smp", "4,sockets=1,cores=4,threads=1",
        "-serial", "stdio",
        "-vga", "none",
        "-device", gpu,
        "-drive", "if=none,id=vd0,file=" .. path.join(ROOT, "disk.img") .. ",format=raw,cache=writethrough",
        "-device", "virtio-blk-pci,drive=vd0,disable-legacy=on",
        "-netdev", "user,id=n0",
        "-device", "virtio-net-pci,netdev=n0,disable-legacy=on",
    }
    return args
end

function run_qemu()
    os.execv("qemu-system-i386", qemu_args())
end
