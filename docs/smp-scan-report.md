# SMP-unsafe global scan (playbook §3)

Date: 2026-07-29. Format: DOSYA / DEĞİŞKEN / RİSK / NEDEN / ÖNERİLEN DÜZELTME

## Fixed this pass

| DOSYA | DEĞİŞKEN | RİSK | DÜZELTME |
|---|---|---|---|
| `src/arch/x86/cpu.c` | `g_cpus` / `g_cpu_count` | Yüksek→OK | `g_cpu_table_lock`; compound reserve+fill; ownership comments; ready release |
| `src/kernel/process.c` | `g_procs` / freelist / graveyard / `next_pid` | Yüksek→OK | `g_proc_lock`; publish-after-init; lock order documented |
| `src/kernel/sync.c` | `process_wake` / `process_suspend` | Yüksek→OK | state under `g_proc_lock` |
| `src/kernel/sync.c` | `g_input_seq` | Yüksek→OK | inc under `g_sync_lock` |
| `src/kernel/sync.c` | `g_input_need_sched` | Orta→OK | `xchg` clear (no soft TOCTOU) |

## Still open (next passes)

| DOSYA | DEĞİŞKEN | RİSK | NEDEN | ÖNERİLEN DÜZELTME |
|---|---|---|---|---|
| `vfs_core.c` | `g_files` / `g_next_ino` | Yüksek | syscall parallel | `g_vfs_lock` irqsave |
| `socket.c` | `g_socks` / `g_ephemeral` | Yüksek | poll × syscall | `g_sock_lock` / `lock xadd` |
| `block.c` | `g_pending` | Yüksek | IRQ poll × submit | `g_bio_lock` irqsave |
| `netstack.c` | `g_arp` / `g_ip_id` | Yüksek | RX×TX | net lock / atomic id |
| `scheduler.c` | `g_tick_us` / life | Orta | SYS_SCHED_SET × timer | under `g_sched_lock` |
| `env.c` | `g_global` | Orta | setenv race | `g_env_lock` |
| `service.c` | `g_services` | Orta | reap × register | service lock |

## Lock order (this tree)

`g_sched_lock` → `g_proc_lock` → `g_heap_lock`  
`g_sync_lock` → `g_proc_lock`  
Never reverse. No Big Kernel Lock.
