; Hardware IRQ stubs (PIC remapped to vectors 32-47)

section .text
global irq_common
extern irq_dispatch

%macro IRQ_STUB 1
global irq_stub_%1
irq_stub_%1:
    push dword 0
    push dword %1
    jmp irq_common
%endmacro

IRQ_STUB 0
IRQ_STUB 1
IRQ_STUB 2
IRQ_STUB 3
IRQ_STUB 4
IRQ_STUB 5
IRQ_STUB 6
IRQ_STUB 7
IRQ_STUB 8
IRQ_STUB 9
IRQ_STUB 10
IRQ_STUB 11
IRQ_STUB 12
IRQ_STUB 13
IRQ_STUB 14
IRQ_STUB 15

irq_common:
    pusha
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, [esp + 32]     ; irq number pushed before pusha
    push eax
    call irq_dispatch
    add esp, 4

    popa
    add esp, 8              ; drop irq number + dummy err
    iret
