---
name: GPU Display Stack
overview: "End-to-end grafik: GpuProvider, display.kmod, Reed, Kilim, usermode WM. Backward compat YOK. Legacy dx/gfx silindi. Pipeline DONE; hybrid shell v1 chrome DONE; restore-app-ux-deep + process memory API landed."
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
    content: "DONE — apps on wm+kilim; files/terminal real; imgui kilim backend"
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
    content: "DONE (v1) — interactive dock/menu, Fluent+macOS chrome; settings/terminal/files/monitor/mines UI"
    status: completed
  - id: harden-lessons
    content: "DONE — static kilim::Context; ustack default 1MiB + exec stack_size; import share; tek GPU QEMU; splash #1A1F2E; atexit stub"
    status: completed
  - id: restore-app-ux-deep
    content: "DONE — real FS browser, real TTY, imgui_impl_kilim + mmap heap"
    status: completed
  - id: process-memory-api
    content: "DONE — OpenProcess/RPM/WPM/VirtualAlloc(Ex), driver hooks (proc_audit), userspace+drv mirrors"
    status: completed
isProject: false
---

# GPU / Display / Reed + Kilim — End-to-End

## Politika

- **Backward compat yok.** Shim yok. Eski ABI silindi.
- Her oturumda bu dosyanın `todos` + “İlerleme” güncellenir.
- Mevcut çalışan stack’i bozmadan ilerlenir (static Context, import-share, tek primary QEMU, WM tek present).

## İlerleme (2026-07-29 — FS/TTY/imgui + process mem)

| Adım | Durum |
|------|-------|
| Pipeline + docs + rules | **DONE** |
| Real Files (getdents/chdir) | **DONE** |
| Real Terminal (line buffer + cmds) | **DONE** |
| imgui_impl_kilim + mmap malloc | **DONE** |
| OpenProcess / RPM / WPM / VirtualAlloc | **DONE** |
| Driver open-hook (proc_audit.kmod) | **DONE** |

## Process memory (Windows-like)

```text
Userspace:  <user/sdk/process_mem.hpp> + <user/sdk/heap.hpp> + mmap.hpp
Syscalls:   SYS_OPEN_PROCESS(297) … SYS_QUERY_PROCESS_VM(306)
Kernel:     src/kernel/proc_mem.c  — handles + permissions + open hooks
Drivers:    drv_* mirror + proc_register_open_hook (deny → OpenProcess -EACCES)
Pseudo:     PROCESS_HANDLE_CURRENT (-1) for self without OpenProcess
```

## Aktif stack

```text
Apps → wm::Window + kilim::Context + reed::Device
         ↓ /tmp/wm
window-manager → SYS_DISP_CALL → display.kmod → GpuProvider
```

## Sonraki iş

- ImGui GPU path (kilim textured batches) instead of SW blit
- Per-process page tables (mmap MAP_FIXED)
- Richer proc_audit policy (sysfs toggle)
