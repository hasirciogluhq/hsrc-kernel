---
name: GPU Display Stack
overview: "End-to-end grafik: GpuProvider, display.kmod, Reed, Kilim, usermode WM. Backward compat YOK. Legacy dx/gfx silindi. Pipeline DONE; hybrid shell v1 chrome DONE; full app UX parity NEXT. Her oturumda bu plan güncellenir."
todos:
  - id: gpu-framework
    content: "DONE — gpu_provider_ops + DRIVER_CLASS_GPU + display→gpu bridge"
    status: completed
  - id: display-orchestrator
    content: "DONE — display.kmod + SYS_DISP_CALL(296) + disp_api"
    status: completed
  - id: reed-ll
    content: "DONE — Reed full API + SW rasterizer"
    status: completed
  - id: kilim-hl
    content: "DONE — batching, Font/Text atlas, 2D, Acrylic blur, Mesh/Material/Transform/Camera/Scene/.kmesh, widgets, commit_frame"
    status: completed
  - id: wm-usermode
    content: "DONE — usermode WM compositor + /tmp/wm file IPC + sdk-wm (focus≠hover, z-order, drag, damage, chrome, cursor)"
    status: completed
  - id: break-legacy
    content: "DONE — dx/*, mkdx/*, BGA eski yol, gfx.hpp, gx.h, SYS_WM_*/SYS_GX_*, dx_api silindi; gui_stack_ready=display+disp_api"
    status: completed
  - id: migrate-apps
    content: "DONE (stub) — os-shell/settings/terminal/files/activity-monitor/minesweeper/imgui-demo → wm+kilim smoke stubs"
    status: completed
  - id: input-feed
    content: "DONE — SYS_INPUT_STATE dx'siz; hit/focus usermode WM; present path'te ps2_poll yok"
    status: completed
  - id: hybrid-shell
    content: "DONE (v1) — WM menubar+dock chrome her frame; os-shell wallpaper background surface; tam macOS UX = restore-app-ux"
    status: completed
  - id: gpu-virtio
    content: "DONE — display_virtio GpuProvider; QEMU -vga virtio veya -vga none + virtio-gpu-pci; std+virtio yasak"
    status: completed
  - id: gpu-vga-fb
    content: "DONE — display_bga providers/bga; drivers tree; klock_disp; 1920x1080 default"
    status: completed
  - id: docs-reed-kilim
    content: "DONE — docs/graphics-reed-kilim.tr.md + .en.md"
    status: completed
  - id: restore-app-ux
    content: "DONE (v1) — interactive dock/menu, Fluent+macOS chrome; settings/terminal/files/monitor/mines UI; deeper TTY/FS/imgui NEXT"
    status: completed
  - id: harden-lessons
    content: "DONE — static kilim::Context; ustack default 1MiB + exec stack_size; import share; tek GPU QEMU; splash #1A1F2E; atexit stub"
    status: completed
  - id: restore-app-ux-deep
    content: "NEXT — real FS browser, real TTY, imgui kilim backend polish"
    status: pending
isProject: false
---

# GPU / Display / Reed + Kilim — End-to-End

## Politika

- **Backward compat yok.** Shim yok. Eski ABI silindi.
- Her oturumda bu dosyanın `todos` + “İlerleme” güncellenir.
- Mevcut çalışan stack’i bozmadan ilerlenir (static Context, import-share, tek primary QEMU, WM tek present).

## İlerleme (2026-07-29 — chrome UX + atexit)

| Adım | Durum |
|------|-------|
| Pipeline + docs + rules | **DONE** |
| Freestanding `atexit` / `__cxa_atexit` stub | **DONE** |
| WM Fluent chrome + rounded dock + traffic lights + resize | **DONE** |
| Interactive dock / menubar launch | **DONE** |
| Apps visual refresh | **DONE** (v1) |
| Real TTY / FS / imgui depth | **NEXT** |

## Drivers layout

```text
src/drivers/
  core/ bus/pci/ console/ input/
  display/{display.c,gpu.c,display_mod.c,providers/{bga,virtio_gpu}}
  block/ fs/ vfs/ part/ net/
include/drivers/{driver.h,bus/,console/,input/,display/,vfs/}
```

## Silinen legacy (kabul)

- `src/drivers/dx/*`, `src/drivers/mkdx/*`
- `dx_api.*`, `gfx.hpp`, `gx.h`, `gfx.cpp`, `ugx_font.inc`, `bake_ugx_font`
- Tüm `SYS_WM_*` / `SYS_GX_*`
- `gui_stack_ready` = `display_active() && disp_api_get()`
- `klock_gfx` → `klock_disp`

`rg SYS_WM_|SYS_GX_|gfx\.hpp|dx_api|klock_gfx` aktif path’te → **yok**.

## Aktif stack

```text
Apps (stub) → wm::Window + kilim::Context + reed::Device
                ↓ file IPC /tmp/wm/
         window-manager (compose + chrome + present)
                ↓ SYS_DISP_CALL
         display.kmod → GpuProvider ← display_bga / display_virtio
```

### Frame sözleşmesi

- İstemci: `begin_frame` → draw → `commit_frame` (present yok)
- WM: compose → `end_frame` (tek scanout)
- `kilim::Context` **static/BSS/heap** (~3MiB); ustack yasak (default 1 MiB; Context yine sığmaz)

### QEMU

- BGA: `-vga std`
- Virtio: `-vga virtio` **veya** `-vga none` + `-device virtio-gpu-pci`
- Yasak: std/default VGA + `virtio-gpu-pci` → siyah ekran

## Cursor rules (güncel)

Tüm `graphics-*.mdc` + `kernel-gui-disk.mdc` (eski `graphics-eng-*` / `gfx-eng-*` rename).

## Docs

- [docs/graphics-reed-kilim.tr.md](../../docs/graphics-reed-kilim.tr.md)
- [docs/graphics-reed-kilim.en.md](../../docs/graphics-reed-kilim.en.md)

## Sonraki iş (restore-app-ux)

Eski masaüstü UX parity: interactive dock/menubar, settings hub, terminal emulator, files, activity-monitor, minesweeper, imgui Kilim backend. Pipeline’ı bozmadan, stub’ların üzerine yazılacak.
