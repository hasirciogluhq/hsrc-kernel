-- Launch QEMU with kernel + initrd + disk
function qemu_args()
    local ROOT = os.projectdir()
    local BUILD = path.join(ROOT, "build")
    return {
        "-kernel", path.join(BUILD, "kernel.bin"),
        "-initrd", path.join(BUILD, "drivers", "initrd.img"),
        "-m", "1G",
        "-smp", "4,sockets=1,cores=4,threads=1",
        "-vga", "std",
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
