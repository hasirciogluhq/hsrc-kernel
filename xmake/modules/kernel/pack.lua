-- Shared helper: ELF → .exec
-- needed: optional list of dynlib names (e.g. {"libfs.dynlib"})
function pack_exec(target, load_addr, exec_name, needed)
    local ROOT = os.projectdir()
    local BUILD = path.join(ROOT, "build")
    local outdir = target:targetdir()
    os.mkdir(outdir)
    local elf = target:targetfile()
    local bin = path.join(outdir, exec_name .. ".bin")
    local out = path.join(outdir, exec_name .. ".exec")
    local pack = path.join(BUILD, "tools", "pack_exec")
    local objcopy = "i686-elf-objcopy"
    local nm = "i686-elf-nm"

    os.execv(objcopy, {"-O", "binary", elf, bin})

    local nm_out = os.iorunv(nm, {"-n", elf}) or ""
    local entry_sym, edata_sym, end_sym, imports_sym
    for line in nm_out:gmatch("[^\r\n]+") do
        local addr, name = line:match("^(%x+)%s+%w%s+(.+)$")
        if name == "exec_main" then entry_sym = addr end
        if name == "_exec_edata" then edata_sym = addr end
        if name == "_exec_end" then end_sym = addr end
        if name == "__dynlib_imports" then imports_sym = addr end
    end
    assert(entry_sym and edata_sym and end_sym, "missing exec symbols in " .. elf)

    local load = tonumber(load_addr)
    local entry = tonumber(entry_sym, 16)
    local edata = tonumber(edata_sym, 16)
    local endv = tonumber(end_sym, 16)
    local entry_off = entry - load
    local img = edata - load
    local bss = endv - edata
    local imports_off = 0
    if imports_sym then
        imports_off = tonumber(imports_sym, 16) - load
    end

    local args = {
        out, bin,
        string.format("0x%x", load),
        string.format("0x%x", entry_off),
        tostring(img),
        tostring(bss),
        exec_name,
        "8192",
        string.format("0x%x", imports_off),
    }
    if needed then
        for _, lib in ipairs(needed) do
            table.insert(args, lib)
        end
    end
    os.execv(pack, args)
    print("packed " .. out)
end
