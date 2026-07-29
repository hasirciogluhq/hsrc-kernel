-- Legacy helper: ELF → .exec (optional; kernel loads ELF directly now).
-- needed: optional list of dynlib names (e.g. {"libfs.dynlib"})
-- Link base comes from ELF _exec_start (ld/user.ld).
function pack_exec(target, exec_name, needed)
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
    local start_sym, entry_sym, edata_sym, end_sym, imports_sym
    for line in nm_out:gmatch("[^\r\n]+") do
        local addr, name = line:match("^(%x+)%s+%w%s+(.+)$")
        if name == "_exec_start" then start_sym = addr end
        if name == "exec_main" then entry_sym = addr end
        if name == "_exec_edata" then edata_sym = addr end
        if name == "_exec_end" then end_sym = addr end
        if name == "__dynlib_imports" then imports_sym = addr end
    end
    assert(start_sym and entry_sym and edata_sym and end_sym,
           "missing exec symbols in " .. elf)

    local base = tonumber(start_sym, 16)
    local entry = tonumber(entry_sym, 16)
    local edata = tonumber(edata_sym, 16)
    local endv = tonumber(end_sym, 16)
    assert(base and entry and edata and endv, "bad symbol addresses in " .. elf)

    local entry_off = entry - base
    local img = edata - base
    local bss = endv - edata
    local imports_off = 0
    if imports_sym then
        local iaddr = tonumber(imports_sym, 16)
        assert(iaddr, "bad __dynlib_imports address in " .. elf)
        imports_off = iaddr - base
    end

    local args = {
        out, bin,
        string.format("0x%x", entry_off),
        tostring(img),
        tostring(bss),
        exec_name,
        "1048576", -- default user stack reserve (1 MiB)
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
