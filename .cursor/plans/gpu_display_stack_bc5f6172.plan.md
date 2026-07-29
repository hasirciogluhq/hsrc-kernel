---
name: GPU Display Stack
overview: "End-to-end grafik: GpuProvider, display.kmod, Reed, Kilim (2D+text+3D+batching+widgets), usermode WM. Politika: backward compat YOK. Legacy dx/gfx/ugx/SYS_WM_*/SYS_GX_*/BGA SİLİNDİ. Apps wm+kilim stub. Her oturumda bu plan güncellenir."
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
    content: "DONE — usermode WM compositor + /tmp/wm file IPC + sdk-wm client (focus≠hover, z-order, drag, damage)"
    status: completed
  - id: break-legacy
    content: "DONE — dx/*, mkdx/*, BGA, gfx.hpp, gx.h, gfx.cpp, ugx_font, SYS_WM_*/SYS_GX_*, dx_api silindi; gui_stack_ready=display+disp_api"
    status: completed
  - id: migrate-apps
    content: "DONE (stub) — os-shell/settings/terminal/files/activity-monitor/minesweeper/imgui-demo → wm+kilim smoke stubs; full UX rebuild ayrı iş"
    status: completed
  - id: input-feed
    content: "DONE — SYS_INPUT_STATE dx'siz (mouse/keyboard drivers); hit/focus usermode WM"
    status: completed
  - id: hybrid-shell
    content: "PARTIAL — os-shell wm+kilim stub; macOS dock/menubar UX henüz geri yazılmadı (eski gfx UI silindi)"
    status: in_progress
  - id: gpu-virtio
    content: "PARTIAL — display_virtio + GpuProvider bridge; rename gpu_virtio optional"
    status: pending
  - id: gpu-vga-fb
    content: "DONE — display_vga (vga_lfb Bochs/QEMU -vga std) + initrd order; QEMU -device virtio-gpu-pci; kshell çift serial fix"
    status: completed
  - id: docs-reed-kilim
    content: docs/graphics-reed-kilim.tr.md + .en.md
    status: pending
  - id: restore-app-ux
    content: "NEXT — shell dock/menubar, settings hub, terminal, files, activity-monitor, minesweeper, imgui kilim backend — feature parity with deleted gfx UI"
    status: pending
isProject: false
---

# GPU / Display / Reed + Kilim — End-to-End

## Politika

- **Backward compat yok.** Shim yok. Eski ABI silindi.
- Her oturumda bu dosyanın `todos` + “İlerleme” güncellenir.

## İlerleme (2026-07-29 — boot fix)

| Adım | Durum |
|------|-------|
| GpuProvider + DRIVER_CLASS_GPU | **DONE** |
| display.kmod + SYS_DISP_CALL | **DONE** |
| Reed SW API | **DONE** |
| Kilim full (batch/text/3D/widgets/acrylic) | **DONE** |
| Usermode WM + sdk-wm | **DONE** |
| Input dx'siz | **DONE** |
| break-legacy (dx/gfx/BGA/SYS_WM_GX/mkdx) | **DONE** |
| Apps → wm+kilim stubs | **DONE** (UX stub; parity NEXT) |
| display_vga LFB + QEMU virtio-gpu | **DONE** (BGA yok; vga_lfb provider) |
| kshell çift `kernel>` (console+klog) | **DONE** |
| Full shell/app UX restore | **NEXT** |
| Docs | pending |

## Silinen legacy (kabul)

- `src/drivers/dx/*`, `src/drivers/mkdx/*`, `src/drivers/display/bga/*`
- `dx_api.*`, `gfx.hpp`, `gx.h`, `gfx.cpp`, `ugx_font.inc`, `bake_ugx_font`
- Tüm `SYS_WM_*` / `SYS_GX_*`
- `gui_stack_ready` = `display_active() && disp_api_get()`

`rg SYS_WM_|SYS_GX_|gfx\.hpp|dx_api` aktif path’te → **yok** (ölü `src/user` mirror’lar da temizlendi).

## Aktif stack

```text
Apps (stub) → wm::Window + kilim::Context + reed::Device
                ↓ file IPC /tmp/wm/
         window-manager (compositor, present)
                ↓ SYS_DISP_CALL
         display.kmod → GpuProvider ← display_virtio
```

## Sonraki iş (restore-app-ux)

Eski masaüstü UX (dock, menubar, settings hub, terminal emulator, files, …) Kilim+WM üzerinde yeniden yazılacak — bilinçli stub’lar şu an sadece yeşil build + yeni pipeline smoke.

## Reed / Kilim / WM checklist

(Plan API yüzeyleri geçerli; Kilim+WM v1 implement.)

## Docs

`docs/graphics-reed-kilim.{tr,en}.md` — henüz yok.
