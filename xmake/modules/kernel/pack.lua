-- Shared helper: ELF → .exe
function pack_exe(target, load_addr, exe_name)
    local ROOT = os.projectdir()
    local BUILD = path.join(ROOT, "build")
    local outdir = target:targetdir()
    os.mkdir(outdir)
    local elf = target:targetfile()
    local bin = path.join(outdir, exe_name .. ".bin")
    local exe = path.join(outdir, exe_name .. ".exe")
    local pack = path.join(BUILD, "tools", "pack_exe")
    local objcopy = "i686-elf-objcopy"
    local nm = "i686-elf-nm"

    os.execv(objcopy, {"-O", "binary", elf, bin})

    local nm_out = os.iorunv(nm, {"-n", elf}) or ""
    local entry_sym, edata_sym, end_sym
    for line in nm_out:gmatch("[^\r\n]+") do
        local addr, name = line:match("^(%x+)%s+%w%s+(.+)$")
        if name == "exe_main" then entry_sym = addr end
        if name == "_exe_edata" then edata_sym = addr end
        if name == "_exe_end" then end_sym = addr end
    end
    assert(entry_sym and edata_sym and end_sym, "missing exe symbols in " .. elf)

    local load = tonumber(load_addr)
    local entry = tonumber(entry_sym, 16)
    local edata = tonumber(edata_sym, 16)
    local endv = tonumber(end_sym, 16)
    local entry_off = entry - load
    local img = edata - load
    local bss = endv - edata

    os.execv(pack, {
        exe, bin,
        string.format("0x%x", load),
        string.format("0x%x", entry_off),
        tostring(img),
        tostring(bss),
        exe_name,
    })
    print("packed " .. exe)
end
