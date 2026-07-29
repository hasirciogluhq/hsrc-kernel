/* Usermode .exe link script. LOAD_ADDR is set via --defsym=LOAD_ADDR=0x... */
ENTRY(exe_main)

SECTIONS
{
    . = LOAD_ADDR;

    _exe_start = .;

    .text : {
        *(.text.exe_main)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    _exe_edata = .;

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    _exe_end = .;
}
