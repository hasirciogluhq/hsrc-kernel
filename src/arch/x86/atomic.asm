; atomic.asm — CPU-level atomics (LOCK prefix). cdecl i386.
; No compiler soft-atomics. Playbook §4.

bits 32
section .text

global atomic_inc32
global atomic_fetch_add32
global atomic_cas32

; void atomic_inc32(volatile uint32_t *counter);  [esp+4]=ptr
atomic_inc32:
    mov eax, [esp + 4]
    lock inc dword [eax]
    ret

; uint32_t atomic_fetch_add32(volatile uint32_t *ptr, uint32_t val);
; [esp+4]=ptr [esp+8]=val → eax = old
atomic_fetch_add32:
    mov ecx, [esp + 4]
    mov eax, [esp + 8]
    lock xadd dword [ecx], eax
    ret

; int atomic_cas32(volatile uint32_t *ptr, uint32_t expected, uint32_t desired);
; [esp+4]=ptr [esp+8]=expected [esp+12]=desired → eax 0/1
atomic_cas32:
    mov ecx, [esp + 4]
    mov eax, [esp + 8]
    mov edx, [esp + 12]
    lock cmpxchg dword [ecx], edx
    sete al
    movzx eax, al
    ret
