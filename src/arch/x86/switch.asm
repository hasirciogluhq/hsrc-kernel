; context_switch.asm — playbook context switch (i386 cdecl).
;
; void context_switch_locked(cpu_context_t *old_ctx, cpu_context_t *new_ctx);
; PRE: IRQs off + sched/runqueue lock held by CALLER. This fn takes NO locks.
;
; cpu_context_t offsets (byte) — hand-counted:
;   0  esp
;   4  ebx
;   8  esi
;  12  edi
;  16  ebp
;  20  cr3
;  24  fpu_area*   (16B aligned, ≥512B FXSAVE area)
;
; Entry stack (cdecl), no pushes before arg load:
;   [esp+0] = ret_eip (4)
;   [esp+4] = old_ctx*
;   [esp+8] = new_ctx*
; Args loaded into eax/edx so ebx/esi/edi/ebp stay as live callee-saved.

bits 32
section .text
global context_switch_locked

CTX_ESP      equ 0
CTX_EBX      equ 4
CTX_ESI      equ 8
CTX_EDI      equ 12
CTX_EBP      equ 16
CTX_CR3      equ 20
CTX_FPU_PTR  equ 24

context_switch_locked:
    mov eax, [esp + 4]          ; old_ctx*
    mov edx, [esp + 8]          ; new_ctx*

    ; ---- 1) callee-saved GP → old ----
    mov [eax + CTX_EBX], ebx
    mov [eax + CTX_ESI], esi
    mov [eax + CTX_EDI], edi
    mov [eax + CTX_EBP], ebp
    mov [eax + CTX_ESP], esp    ; includes ret_eip at [esp]

    ; ---- 2) FPU/SIMD (FXSAVE) — compilers use XMM even in “GP” code ----
    mov ecx, [eax + CTX_FPU_PTR]
    test ecx, ecx
    jz .skip_fpu_save
    fxsave [ecx]
.skip_fpu_save:

    ; ---- 3) CR3: save; write new only if different and non-zero ----
    mov ecx, cr3
    mov [eax + CTX_CR3], ecx
    mov ebx, [edx + CTX_CR3]
    cmp ecx, ebx
    je .same_as
    test ebx, ebx
    jz .same_as
    mov cr3, ebx
.same_as:

    ; ---- 4) restore FPU ----
    mov ecx, [edx + CTX_FPU_PTR]
    test ecx, ecx
    jz .skip_fpu_restore
    fxrstor [ecx]
.skip_fpu_restore:

    ; ---- 5) restore GP + switch stack ----
    mov ebx, [edx + CTX_EBX]
    mov esi, [edx + CTX_ESI]
    mov edi, [edx + CTX_EDI]
    mov ebp, [edx + CTX_EBP]
    mov esp, [edx + CTX_ESP]

    ret
