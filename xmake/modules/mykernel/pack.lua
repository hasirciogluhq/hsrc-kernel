-- Shared helper: ELF → .mke
function pack_mke(target, load_addr, mke_name)
    local ROOT = os.projectdir()
    local BUILD = path.join(ROOT, "build")
    local outdir = target:targetdir()
    os.mkdir(outdir)
    local elf = target:targetfile()
    local bin = path.join(outdir, mke_name .. ".bin")
    local mke = path.join(outdir, mke_name .. ".mke")
    local pack = path.join(BUILD, "tools", "pack_mke")
    local objcopy = "i686-elf-objcopy"
    local nm = "i686-elf-nm"

    os.execv(objcopy, {"-O", "binary", elf, bin})

    local nm_out = os.iorunv(nm, {"-n", elf}) or ""
    local entry_sym, edata_sym, end_sym
    for line in nm_out:gmatch("[^\r\n]+") do
        local addr, name = line:match("^(%x+)%s+%w%s+(.+)$")
        if name == "mke_main" then entry_sym = addr end
        if name == "_mke_edata" then edata_sym = addr end
        if name == "_mke_end" then end_sym = addr end
    end
    assert(entry_sym and edata_sym and end_sym, "missing mke symbols in " .. elf)

    local load = tonumber(load_addr)
    local entry = tonumber(entry_sym, 16)
    local edata = tonumber(edata_sym, 16)
    local endv = tonumber(end_sym, 16)
    local entry_off = entry - load
    local img = edata - load
    local bss = endv - edata

    os.execv(pack, {
        mke, bin,
        string.format("0x%x", load),
        string.format("0x%x", entry_off),
        tostring(img),
        tostring(bss),
        mke_name,
    })
    print("packed " .. mke)
end
