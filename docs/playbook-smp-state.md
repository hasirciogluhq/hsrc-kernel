# Kernel Development Playbook: SMP-Safe State Yönetimi

Repo law: `.cursor/rules/kernel-smp-state.mdc` (+ locking: `kernel-asm-locking.mdc`).

## Fingerprint — Unsynchronized Shared Mutable Global

1. static/global mutable (not const)
2. Written from ≥2 paths
3. Paths can run on different CPUs or IRQ vs thread
4. No spinlock / CPU atomic / LOCK insn  
→ race candidate.

## Forbidden fixes

- Big Kernel Lock
- `volatile` as atomic
- Hand-rolled busy flag without XCHG/CMPXCHG
- Atomic counter alone for compound slot reserve+fill

## Fix tree

- BSP write-once → read-only after + release ready flag
- Simple value → atomic / CAS
- Compound struct/array → data-specific spinlock or CAS-reserve-then-fill
- Per-CPU → per-CPU slot only

## Report format

`DOSYA / DEĞİŞKEN / RİSK / NEDEN / ÖNERİLEN DÜZELTME`
