# Reed / Kilim / Display — Graphics Stack

Active graphics architecture for mykernel. Legacy `dx` / `mkdx` / `gfx.hpp` / `SYS_WM_*` / `SYS_GX_*` are **gone**.

## Mental model

| Layer | Analogy | Role |
|-------|---------|------|
| **Reed** | OpenGL / D3D device | FBO/backbuffer, command buffer, submit, present |
| **Kilim** | ImGui DrawList | High-level quads/text/widgets → Reed cmds |
| **WM** | Compositor | Import app FBOs, compose via Reed blit, sole scanout |
| **GpuProvider** | GPU driver | **All** raster / blit / compose pixel work |

**Hard rule:** userspace (Reed / Kilim / apps) **never** writes framebuffer pixels. CPU may upload textures/buffers only.

## Stack

```text
Application
  kilim::Context      — drawlist (fill_rect / text / …)
  reed::Device        — create FBO, record cmds, submit
        │
        ▼  SYS_DISP_CALL (296)  DISP_OP_SUBMIT / SCANOUT
display.kmod          — handles + resolve → GpuProvider
        │
        ▼  gpu_submit / present
display_bga | display_virtio   (softpipe today; VirGL later)
```

- **WM** is the sole present owner (`kilim::end_frame` / Reed present).
- **Client apps** only `kilim::commit_frame` (export + attach); they **must not** scanout.
- **os-shell** owns the wallpaper (background surface); menubar/dock v1 is WM chrome.

`gui_stack_ready()` = `display_active() && disp_api_get()`. Without GUI → **kshell**.

## Frame flow (DX11-ish)

1. Reed opens / binds an FBO (`create_render_target` / swapchain target).
2. Kilim `begin_frame` → clear + drawlist open.
3. App/Kilim emit draws (`fill_rect` → Reed `draw`); cmds go into Reed command buffer.
4. `commit_frame` / `end_frame` → `DISP_OP_SUBMIT` → **GpuProvider::gpu_submit** writes the FBO.
5. WM imports app surface tokens, `blit` into its FBO (also via submit), then `present` / scanout.

## Kernel / display

| Piece | Location | Role |
|-------|----------|------|
| Softpipe | `gpu_soft.c` (kernel) | Provider `gpu_submit` backend for BGA/virtio-2D |
| Bridge | `display.c`, `gpu.c` | provider pick, mode, LFB/scanout |
| Orchestrator | `display_mod.c` | `SYS_DISP_CALL` — **no** pixel loops |
| BGA / Virtio | `providers/*` | `gpu_submit` + `present*` |

### QEMU (one primary)

| Goal | Args | Provider |
|------|------|----------|
| BGA | `-vga std` | `display_bga` |
| Virtio | `-vga virtio` **or** `-vga none` + `-device virtio-gpu-pci` | `display_virtio` |

**Forbidden:** std VGA + `virtio-gpu-pci` together → black screen.

Default resolution: **1920×1080**.

## Reed (`reed::`)

- Records `DISP_CMD_*` only; `submit()` → `DISP_OP_SUBMIT`.
- Key opcodes: `BUFFER_*`, `TEXTURE_*`, `RT_*`, `SUBMIT`, `SCANOUT`, `EXPORT`/`IMPORT`.
- **Import rule:** share exporter backing (no steal-and-free).

## Kilim (`kilim::`)

- Drawlist on Reed only (no `SYS_DISP_*`).
- Vertex buffers retained until after submit (GPU needs them live).

### Frame API

| Call | Who | Effect |
|------|-----|--------|
| `begin_frame` | app / WM | clear + open batches |
| `commit_frame` | **client** | flush + submit; **no present** |
| `end_frame` | **WM only** | submit + **scanout/present** |

### Context size

`kilim::Context` (~3 MiB+) **MUST** be `static` / heap — **NOT** on the 1 MiB default ustack.

## Related rules

`.cursor/rules/graphics-*.mdc`, `kernel-gui-disk.mdc`, `kernel-asm-locking.mdc`.
