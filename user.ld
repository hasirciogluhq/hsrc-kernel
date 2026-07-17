/* Usermode .mke link script. LOAD_ADDR is set via --defsym=LOAD_ADDR=0x... */
ENTRY(mke_main)

SECTIONS
{
    . = LOAD_ADDR;

    _mke_start = .;

    .text : {
        *(.text.mke_main)
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    _mke_edata = .;

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    _mke_end = .;
}
