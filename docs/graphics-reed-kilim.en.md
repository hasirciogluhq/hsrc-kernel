# Reed / Kilim / Display — Graphics Stack

Active graphics architecture for mykernel. Legacy `dx` / `mkdx` / `gfx.hpp` / `SYS_WM_*` / `SYS_GX_*` are **gone**.

## Stack

```text
Application
  wm::Window          — window + /tmp/wm file IPC
  kilim::Context      — 2D/text/3D/widgets (batched)
  reed::Device        — buffers/textures/RTs/commands/present
        │
        ▼  SYS_DISP_CALL (296)
display.kmod          — orchestrator (disp_api)
        │
        ▼  GpuProvider
display_bga | display_virtio
```

- **WM** (`userspace/window-manager`) is the sole present owner (`kilim::end_frame` / Reed present).
- **Client apps** only `kilim::commit_frame` (export + attach); they **must not** scanout.
- **os-shell** owns the wallpaper (background surface); menubar/dock v1 is drawn as WM chrome.

`gui_stack_ready()` = `display_active() && disp_api_get()`. Without GUI the kernel falls back to **kshell**; it does not halt.

## Kernel / display

| Piece | Location | Role |
|-------|----------|------|
| Bridge | `src/drivers/display/display.c`, `gpu.c` | provider pick, mode, LFB/scanout |
| Orchestrator | `display_mod.c` | `SYS_DISP_CALL` opcodes |
| BGA | `providers/bga/` | Bochs/QEMU std VGA LFB |
| Virtio | `providers/virtio_gpu/` | virtio-gpu 2D scanout |
| ABI | `include/user/disp.h` | `DISP_OP_*` |
| Lock | `klock_disp` | `disp_api` + `drivers_poll` (asm spinlock) |

### QEMU (one primary)

| Goal | Args | Provider |
|------|------|----------|
| BGA | `-vga std` | `display_bga` |
| Virtio | `-vga virtio` **or** `-vga none` + `-device virtio-gpu-pci` | `display_virtio` |

**Forbidden:** default/std VGA together with `virtio-gpu-pci` — present goes to virtio while the window shows std → black screen.

Default resolution: **1920×1080**.

## Reed (`reed::`)

- Header: `include/user/sdk/reed.hpp`
- Source: `userspace/sdk/reed/`
- Low level: Device, Buffer, Texture2D, RenderTarget, CommandBuffer, Fence, Pipeline.
- v1 backend: **software rasterizer**; `hw_accel_available` is capability only.
- Talks to the kernel via `SYS_DISP_CALL` for buffer/texture/RT/export/import/scanout.

Key opcodes: `DISP_OP_BUFFER_*`, `TEXTURE_*`, `RT_*`, `EXPORT` / `IMPORT`, `SCANOUT`, `STATS`.

**Import rule:** imported textures **share** exporter backing (no steal-and-free); destroy frees on last ref.

## Kilim (`kilim::`)

- Header: `include/user/sdk/kilim.hpp`
- Source: `userspace/sdk/kilim/`
- High level on Reed only: batching, Font/Text atlas, 2D, Acrylic blur, Mesh/Material/Transform/Camera/Scene, `.kmesh`, widgets.
- Does not call `SYS_DISP_*` directly.

### Frame API

| Call | Who | Effect |
|------|-----|--------|
| `begin_frame` | app / WM | clear + open batches |
| `commit_frame` | **client** | flush + submit; **no present** |
| `end_frame` | **WM only** | submit + **scanout/present** |

### Context size (critical)

`kilim::Context` holds large batch arrays (~**3 MiB**).

- **MUST:** `static` / BSS / heap
- **MUST NOT:** process ustack (`PROC_USTACK_SIZE` = 64 KiB)

Otherwise stack smash / `#GP` (frozen window-manager).

## WM (`wm::`)

- Client: `include/user/sdk/wm.hpp`
- Server: `userspace/window-manager/main.cpp`
- IPC: `/tmp/wm` file request/response (`WMRq` / `WMRs`)
- No kernel `SYS_WM_*`

Owns: create/show/focus/move/resize/damage, z-order, hit-test (focus ≠ hover), surface import + compose, window chrome, system menubar/dock (v1), arrow cursor, single present.

## Input

- `SYS_INPUT_STATE` — from mouse/keyboard drivers; no DX dependency.
- Focus routing lives in usermode WM.
- No `ps2_poll` / heavy polling inside the present hot path.

## Application status

| Component | Status |
|-----------|--------|
| Pipeline (display→Reed→Kilim→WM) | Working |
| Apps | wm+kilim **smoke stubs** |
| Menubar/dock | WM v1 chrome (not full macOS UX) |
| Settings/terminal/files/minesweeper/imgui | Feature parity **NEXT** (`restore-app-ux`) |

## Related rules

`.cursor/rules/gfx-eng-*.mdc`, `kernel-gui-disk.mdc`, `kernel-asm-locking.mdc`.
