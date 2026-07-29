# Production-Grade x86 Context Switch + Locking Playbook (repo law)

Canonical AI / agent rule: `.cursor/rules/kernel-asm-locking.mdc`.

## Status in this tree

- **Locking**: CPU-level NASM — `src/arch/x86/spinlock.asm`, `atomic.asm`
  (`XCHG`/`LOCK`/`PAUSE`/ticket). `klock.h` is a thin C wrapper only.
- **Context switch**: `context_switch_locked` in `src/arch/x86/switch.asm` —
  callee-saved GP + `FXSAVE`/`FXRSTOR` + CR3-if-changed. Struct:
  `include/arch/x86/cpu_context.h`.
- **Widths**: playbook text is x86-64; current binary is still **i686**
  (`elf32`/cdecl). Semantics must match the playbook; do not keep a soft-atomic
  parallel path “for compatibility”.

## Spinlock (required shape)

1. `xchg` 1 into `locked` (implicit LOCK).
2. If old was 0 → acquired.
3. Else `pause` + load-test until 0, then retry `xchg`.
4. Release = aligned store 0 (x86 TSO store-release).

## Context switch (required shape)

1. Caller: IRQs off + runqueue lock. Switch takes **no** lock.
2. Save callee-saved into `cpu_context_t`; SP includes return EIP.
3. FXSAVE old / FXRSTOR new via 16B-aligned ≥512B area.
4. Write CR3 only if different (and non-zero).
5. Update TSS ESP0 before user return when applicable.

## Forbidden

- `__sync_*` / `__atomic_*` as kernel spinlock implementation
- Spin without `pause`
- Skipping FPU in switch
- Unconditional CR3 write on same address space
