-- Kernel binary (embedded drivers only)
local ROOT = os.projectdir()
local INC = path.join(ROOT, "include")
local BUILD = path.join(ROOT, "build")

target("kernel")
    set_kind("binary")
    set_default(true)
    mykernel_cross_target()
    add_deps("initrd", "disk")
    add_rules("nasm")
    add_files(path.join(ROOT, "src/arch/x86/*.asm|smp_trampoline.asm|smp_tramp_blob.asm"))
    add_files(path.join(ROOT, "src/arch/x86/*.c"))
    add_files(path.join(ROOT, "src/kernel/*.c"))
    add_files(path.join(ROOT, "src/lib/*.c"))
    add_files(path.join(ROOT, "src/drivers/*.c"))
    add_files(path.join(ROOT, "src/drivers/pci/*.c"))
    add_files(path.join(ROOT, "src/drivers/display/display.c"))
    add_includedirs(INC,
        path.join(ROOT, "src/drivers/mkdx"),
        path.join(ROOT, "src/drivers/display/bga"),
        path.join(ROOT, "src/drivers/display/virtio_gpu"))
    add_cflags(mykernel_cflags(), {force = true})
    set_targetdir(BUILD)
    set_filename("kernel.bin")

    before_build(function (target)
        local tramp_bin = path.join(BUILD, "smp_trampoline.bin")
        local tramp_obj = path.join(BUILD, "arch/x86/smp_tramp_blob.o")
        os.mkdir(path.join(BUILD, "arch/x86"))
        os.execv("nasm", {"-f", "bin", path.join(ROOT, "src/arch/x86/smp_trampoline.asm"), "-o", tramp_bin})
        os.execv("nasm", {"-f", "elf32", "-I" .. BUILD .. "/", path.join(ROOT, "src/arch/x86/smp_tramp_blob.asm"), "-o", tramp_obj})
        local objs = target:objectfiles()
        local found = false
        for _, o in ipairs(objs) do
            if o == tramp_obj then found = true break end
        end
        if not found then
            table.insert(objs, tramp_obj)
        end
    end)

    on_link(function (target)
        local out = target:targetfile()
        os.mkdir(path.directory(out))
        local args = {"-m", "elf_i386", "-n", "-T", path.join(ROOT, "ld/linker.ld"), "-nostdlib", "-o", out}
        for _, o in ipairs(target:objectfiles()) do
            table.insert(args, o)
        end
        os.execv("i686-elf-ld", args)
    end)

    -- `xmake run` must not exec kernel.bin on the host — launch QEMU instead.
    on_run(function (target)
        import("mykernel.qemu")
        qemu.run_qemu()
    end)
