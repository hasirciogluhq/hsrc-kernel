; context_switch — x86 cdecl kernel context switch
;
; void context_switch(uint32_t **old_esp, uint32_t *new_esp, thread_regs_t *old_regs);
;
; PRE: interrupts OFF (caller holds irqsave spinlock).
; new_esp must point at a frame built by this function or setup_kstack:
;   [esp]=eflags, eax, ecx, edx, ebx, esi, edi, ebp, ret_eip
;
; thread_regs_t offsets (must match process.h):
;   0 ebp, 4 edi, 8 esi, 12 ebx, 16 edx, 20 ecx, 24 eax
;   28 eflags, 32 eip, 36 esp, 40 cs, 44 ds, 48 es, 52 fs, 56 gs, 60 ss

bits 32
section .text
global context_switch

REG_EBP    equ 0
REG_EDI    equ 4
REG_ESI    equ 8
REG_EBX    equ 12
REG_EDX    equ 16
REG_ECX    equ 20
REG_EAX    equ 24
REG_EFLAGS equ 28
REG_EIP    equ 32
REG_ESP    equ 36
REG_CS     equ 40
REG_DS     equ 44
REG_ES     equ 48
REG_FS     equ 52
REG_GS     equ 56
REG_SS     equ 60

; Clear TF/IOPL/NT/RF/VM/AC; force bit1=1 so a corrupt frame cannot #DB.
EFLAGS_SAFE_MASK equ 0xFFF88EFF
EFLAGS_BIT1      equ 0x00000002

context_switch:
    push ebp
    push edi
    push esi
    push ebx
    push edx
    push ecx
    push eax
    pushf
    ; 8 dwords pushed → ret at [esp+32], args at +36/+40/+44

    mov eax, [esp + 36]     ; old_esp **
    mov [eax], esp

    mov esi, [esp + 40]     ; new_esp *
    mov ecx, [esp + 44]     ; old_regs *
    test ecx, ecx
    jz .switch

    mov eax, [esp + 28]
    mov [ecx + REG_EBP], eax
    mov eax, [esp + 24]
    mov [ecx + REG_EDI], eax
    mov eax, [esp + 20]
    mov [ecx + REG_ESI], eax
    mov eax, [esp + 16]
    mov [ecx + REG_EBX], eax
    mov eax, [esp + 12]
    mov [ecx + REG_EDX], eax
    mov eax, [esp + 8]
    mov [ecx + REG_ECX], eax
    mov eax, [esp + 4]
    mov [ecx + REG_EAX], eax
    mov eax, [esp]
    mov [ecx + REG_EFLAGS], eax
    mov eax, [esp + 32]
    mov [ecx + REG_EIP], eax
    mov [ecx + REG_ESP], esp

    mov ax, cs
    movzx eax, ax
    mov [ecx + REG_CS], eax
    mov ax, ds
    movzx eax, ax
    mov [ecx + REG_DS], eax
    mov ax, es
    movzx eax, ax
    mov [ecx + REG_ES], eax
    mov ax, fs
    movzx eax, ax
    mov [ecx + REG_FS], eax
    mov ax, gs
    movzx eax, ax
    mov [ecx + REG_GS], eax
    mov ax, ss
    movzx eax, ax
    mov [ecx + REG_SS], eax

.switch:
    mov esp, esi

    ; Sanitize eflags before popf — corrupt TF caused vector=1 (#DB).
    mov eax, [esp]
    and eax, EFLAGS_SAFE_MASK
    or  eax, EFLAGS_BIT1
    mov [esp], eax

    popf
    pop eax
    pop ecx
    pop edx
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret
