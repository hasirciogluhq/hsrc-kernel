set_project("hsrc")
set_version("0.1.0")
set_xmakever("2.8.0")

add_moduledirs("xmake/modules")

-- Freestanding i686 cross; host tools override plat/arch per-target.
set_plat("cross")
set_arch("i386")
set_config("cross", "i686-elf-")

-- Auto-detect Homebrew / PATH prefix for i686-elf-gcc when --sdk is unset.
on_config(function ()
    import("core.project.config")
    import("lib.detect.find_tool")
    if config.get("sdk") and config.get("sdk") ~= "" then
        return
    end
    local gcc = find_tool("i686-elf-gcc")
    if not gcc or not gcc.program then
        return
    end
    local bindir = path.directory(gcc.program)
    local sdk = path.directory(bindir)
    if sdk and sdk ~= "" then
        config.set("sdk", sdk)
    end
end)

includes("xmake/toolchain.lua")
includes("xmake/kernel.lua")
includes("xmake/drivers.lua")
includes("userspace/sdk/reed/xmake.lua")
includes("userspace/sdk/kilim/xmake.lua")
includes("userspace/sdk/wm/xmake.lua")
includes("xmake/modules/kernel/appdef.lua")
includes("xmake/userspace.lua")
includes("userspace/*/xmake.lua")
includes("xmake/qemu.lua")
