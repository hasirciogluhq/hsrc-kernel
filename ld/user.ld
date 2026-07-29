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

    _exec_edata = .;

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    _exec_end = .;
}
