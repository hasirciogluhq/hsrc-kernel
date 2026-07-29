/* Usermode .exec link script. LOAD_ADDR is set via --defsym=LOAD_ADDR=0x... */
ENTRY(exec_main)

SECTIONS
{
    . = LOAD_ADDR;

    _exec_start = .;

    .text : {
        *(.text.exec_main)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    /* Auto-collected dynlib import table (DYNLIB_IMPORT macro → .dynimports).
     * Must live before _exec_edata (copied image), not in .bss (zeroed). */
    .dynimports : {
        . = ALIGN(4);
        __dynlib_imports = .;
        KEEP(*(.dynimports))
        . = ALIGN(4);
        /* terminator: sizeof(dynlib_import_t)=68, all-zero → lib[0]==0 */
        LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0)
        LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0) LONG(0)
        LONG(0)
    }

    _exec_edata = .;

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    _exec_end = .;
}
