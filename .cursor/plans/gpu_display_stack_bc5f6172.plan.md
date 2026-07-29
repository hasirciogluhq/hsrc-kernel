---
name: GPU Display Stack
overview: "End-to-end grafik: GpuProvider, display.kmod, Reed, Kilim, usermode WM, hibrit shell, docs. Build/userspace/boot → xmake_userspace_boot planı. Yön değişmez."
todos:
  - id: break-legacy
    content: Eski ugx, kernel compositor/WM, BGA, SYS_WM_*/SYS_GX_*/mkdx_api kalıcı sil
    status: pending
  - id: gpu-framework
    content: gpu_provider_ops + DRIVER_CLASS_GPU + PCI display-class detect/bind
    status: pending
  - id: gpu-virtio
    content: gpu_virtio.kmod (custom driver; xmake_userspace_boot ile derlenir)
    status: pending
  - id: gpu-vga-fb
    content: gpu_vga.kmod LFB provider
    status: pending
  - id: display-orchestrator
    content: display.kmod — resource/queue/fence/export/import/scanout + SYS_DISP_*
    status: pending
  - id: reed-ll
    content: "userspace/reed — Reed API (device, VBO/IBO, DrawIndexed, present)"
    status: pending
  - id: kilim-hl
    content: "userspace/kilim — fill/text/blit/widgets + frosted chrome"
    status: pending
  - id: wm-usermode
    content: "userspace/window-manager — boolean opts, events, damage, focus≠hover"
    status: pending
  - id: hybrid-shell
    content: "userspace/os-shell — hibrit chrome + app rewrite"
    status: pending
  - id: input-feed
    content: PS/2 + virtio-input → usermode WM
    status: pending
  - id: docs-reed-kilim
    content: docs/graphics-reed-kilim.tr.md + .en.md
    status: pending
isProject: false
---

# GPU / Display / Reed + Kilim — End-to-End

**İlişki**

| Konu | Plan |
|------|------|
| Build, `userspace/`, init→systemd, xmake | [xmake_userspace_boot_a3be45ab.plan.md](xmake_userspace_boot_a3be45ab.plan.md) |
| OS net/fs/Settings/Explorer… | [god-level_window_api_87fb7031.plan.md](god-level_window_api_87fb7031.plan.md) |
| Grafik / GPU / WM çizim | **bu dosya** |

Bu plan build omurgasını **tekrar etmez**. Kod `userspace/reed|kilim|window-manager|os-shell` altında yaşar; derleme xmake planına göre.

Teslimat: çalışan masaüstü grafik yığını (xmake planı ile birlikte). Ertelenmiş stub yok.

## İsimlendirme

| Katman | İsim |
|--------|------|
| Low-level | **Reed** (`userspace/reed`) |
| High-level | **Kilim** (`userspace/kilim`) |
| Orchestrator | `display.kmod` |
| GPU | `gpu_virtio` / `gpu_vga` |

Prefiks `reed*` / `kilim*`. Yasak: `mk*`, `ugx`.

## Politika

- Backward compat yok; shim yok.
- Focus ≠ hover; hibrit shell; boolean window opts.
- App doğrudan `display_*`/`gpu_*` çağırmaz.

```mermaid
flowchart TB
  subgraph usermode [Usermode]
    Kilim[Kilim]
    Reed[Reed]
    App[Applications]
    WM[window_manager]
    Shell[os_shell]
  end
  subgraph kernel [Kernel]
    DD[display_driver]
    GP[GpuProvider]
    PCI[PCI]
    INP[input]
  end
  Kilim --> Reed
  App --> Reed
  App --> Kilim
  App --> WM
  Reed --> DD
  WM --> DD
  Shell --> WM
  INP --> WM
  DD --> GP
  PCI --> GP
```

## Kodlarken dikkat

1. `SYS_WM_*` / `mkdx_api` / kernel compositor → yok  
2. `ugx` / bitflag style → yok  
3. BGA / “BGA fallback derlensin” → yok  
4. Raw kernel pixel `wm_map` → handle export/import  
5. Kernel fill/font/blur/clipboard/console paint → yok  
6. Process exit cleanup → usermode WM  
7. Makefile aktif path / `src/user/apps` → yok (xmake plan)  
8. Kernel çoklu `.mke` spawn → yok (init→systemd)  
9. ISR compose / per-draw syscall → yok  

## WM / shell checklist (teslimatta)

Boolean opts (uint8_t), min/max/restore, owner/parent, z-order, hit-test, cursor, events+wait, damage rect, clipboard, AllocConsole usermode, ~64 windows, errno; dock pin + running zorunlu; deep-link menubar; frosted ~70% Kilim; process death cleanup.

## 1–5. Stack

1. **Reed** — device, buffers, DrawIndexed, present, fence, WSI  
2. **Kilim** — 2D/UI on Reed  
3. **GpuProvider** — `gpu.h`, PCI class 0x03, register/select  
4. **gpu_virtio** + **gpu_vga**; BGA sil  
5. **display.kmod** — submit çevirisi, handles, export/import, scanout, `SYS_DISP_*`  

## 6. WM + input

`userspace/window-manager` + focus≠hover + input feed. Kernel’de WM syscall yok.

## 7. Shell + apps

`userspace/os-shell` hibrit; diğer GUI app’ler aynı ağaçta (xmake plan layout).

## 8. Sıra

1. xmake_userspace_boot iskeleti (init/systemd + userspace dirs)  
2. Legacy gfx sil + GpuProvider + virtio/vga + display.kmod  
3. Reed → Kilim → WM → os-shell  
4. App rewrite + docs  

## 9. Docs

`docs/graphics-reed-kilim.{tr,en}.md` — esinlenme, terminoloji, katmanlar, focus≠hover, portability; build için xmake planına link.
