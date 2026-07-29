; void context_switch(uint32_t **old_esp, uint32_t *new_esp, thread_regs_t *old_regs)
; cdecl: [esp+4]=old**, [esp+8]=new*, [esp+12]=regs* (nullable)
;
; Kstack frame at esp (low → high):
;   eflags, eax, ecx, edx, ebx, esi, edi, ebp, ret_eip

section .text
global context_switch

context_switch:
    push ebp
    push edi
    push esi
    push ebx
    push edx
    push ecx
    push eax
    pushf
    ; 8 dwords → args at [esp+36]=ret [esp+40]=old** [esp+44]=new* [esp+48]=regs*
    ; NO: 8*4=32, so ret at +32, old** at +36, new* at +40, regs* at +44.

    mov eax, [esp + 36]     ; old_esp **
    mov [eax], esp

    mov ecx, [esp + 44]     ; old_regs *
    mov esi, [esp + 40]     ; new_esp *

    test ecx, ecx
    jz .switch

    mov eax, [esp + 28]
    mov [ecx + 0], eax      ; ebp
    mov eax, [esp + 24]
    mov [ecx + 4], eax      ; edi
    mov eax, [esp + 20]
    mov [ecx + 8], eax      ; esi
    mov eax, [esp + 16]
    mov [ecx + 12], eax     ; ebx
    mov eax, [esp + 12]
    mov [ecx + 16], eax     ; edx
    mov eax, [esp + 8]
    mov [ecx + 20], eax     ; ecx
    mov eax, [esp + 4]
    mov [ecx + 24], eax     ; eax
    mov eax, [esp]
    mov [ecx + 28], eax     ; eflags
    mov eax, [esp + 32]
    mov [ecx + 32], eax     ; eip
    mov [ecx + 36], esp     ; esp
    mov ax, cs
    movzx eax, ax
    mov [ecx + 40], eax
    mov ax, ds
    movzx eax, ax
    mov [ecx + 44], eax
    mov ax, es
    movzx eax, ax
    mov [ecx + 48], eax
    mov ax, fs
    movzx eax, ax
    mov [ecx + 52], eax
    mov ax, gs
    movzx eax, ax
    mov [ecx + 56], eax
    mov ax, ss
    movzx eax, ax
    mov [ecx + 60], eax

.switch:
    mov esp, esi
    popf
    pop eax
    pop ecx
    pop edx
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret
