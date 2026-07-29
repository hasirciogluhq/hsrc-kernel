; spinlock.asm — CPU-level spin + ticket lock. cdecl i386.
; Playbook §5: XCHG (implicit LOCK), test-and-test-and-spin, PAUSE mandatory.
; Software/CAS-via-compiler is forbidden for these paths.

bits 32
section .text

global spinlock_acquire
global spinlock_release
global spinlock_try_acquire
global ticketlock_acquire
global ticketlock_release

; typedef struct { volatile uint32_t locked; } spinlock_t;  0=free 1=held

; void spinlock_acquire(spinlock_t *lock);  [esp+4]=lock*
spinlock_acquire:
    mov ecx, [esp + 4]
.retry:
    mov eax, 1
    xchg eax, [ecx]            ; implicit LOCK
    test eax, eax
    jz .acquired
.spin_wait:
    pause                      ; HT sibling + power; never omit
    cmp dword [ecx], 0
    jne .spin_wait
    jmp .retry
.acquired:
    ret

; int spinlock_try_acquire(spinlock_t *lock); → eax 1=got 0=busy
spinlock_try_acquire:
    mov ecx, [esp + 4]
    mov eax, 1
    xchg eax, [ecx]
    xor eax, 1
    and eax, 1
    ret

; void spinlock_release(spinlock_t *lock);
; Aligned 32-bit store is atomic + store-release under x86 TSO.
spinlock_release:
    mov ecx, [esp + 4]
    mov dword [ecx], 0
    ret

; ---- Ticket lock (fair) ----
; typedef struct {
;   volatile uint32_t next_ticket;   /* offset 0 */
;   volatile uint32_t now_serving;   /* offset 4 */
; } ticketlock_t;

; void ticketlock_acquire(ticketlock_t *lock);
ticketlock_acquire:
    mov ecx, [esp + 4]
    mov eax, 1
    lock xadd dword [ecx], eax     ; eax = my ticket (old next)
.wait:
    pause
    cmp eax, [ecx + 4]
    jne .wait
    ret

; void ticketlock_release(ticketlock_t *lock);
ticketlock_release:
    mov ecx, [esp + 4]
    lock inc dword [ecx + 4]
    ret
