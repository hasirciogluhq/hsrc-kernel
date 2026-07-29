# Reed / Kilim / Display — Graphics Stack

Active graphics architecture for mykernel. Legacy `dx` / `mkdx` / `gfx.hpp` / `SYS_WM_*` / `SYS_GX_*` are **gone**.

## Mental model

| Layer | Analogy | Role |
|-------|---------|------|
| **Reed** | OpenGL / D3D device | FBO/backbuffer, command buffer, submit, present |
| **Kilim** | ImGui DrawList | High-level quads/text/widgets → Reed cmds |
| **WM** | Compositor | Import app FBOs, compose via Reed blit, sole scanout |
| **GpuProvider** | GPU driver | **All** raster / blit / compose pixel work |

**Hard rule:** userspace (Reed / Kilim / apps) **never** writes framebuffer pixels. CPU may upload textures/buffers only. Softpipe / CPU raster / `gpu_soft` are **forbidden**.

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
display_virtio (VirGL SUBMIT_3D)   preferred — GPU_CAP_HW_SUBMIT
display_bga                        scanout/present only (no 3D)
```

- **WM** is the sole present owner (`kilim::end_frame` / Reed present).
- **Client apps** only `kilim::commit_frame` (export + attach); they **must not** scanout.
- **os-shell** owns the wallpaper (background surface); menubar/dock v1 is WM chrome.

`gui_stack_ready()` = `display_active() && disp_api_get() && gpu_provider_active() with GPU_CAP_HW_SUBMIT`. Without HW 3D → **kshell** (B24).

## Frame flow (DX11-ish)

1. Reed opens / binds an FBO (`create_render_target` / swapchain target).
2. Kilim `begin_frame` → clear + drawlist open.
3. App/Kilim emit draws (`fill_rect` → Reed `draw`); cmds go into Reed command buffer.
4. `commit_frame` / `end_frame` → `DISP_OP_SUBMIT` → **GpuProvider::gpu_submit** (VirGL on host).
5. WM imports app surface tokens, `blit` into its FBO (also via submit), then `present` / scanout.

## Kernel / display

| Piece | Location | Role |
|-------|----------|------|
| VirGL 3D | `providers/virtio/virtio_virgl.c` | CLEAR / DRAW / BLIT via `SUBMIT_3D` |
| Virtio 2D | `providers/virtio/virtio_cmd.c` | PRESENT + scanout ring cmds |
| Bridge | `display.c`, `gpu.c` | provider pick; `display_ops.gpu_caps` → `GPU_CAP_*` |
| Orchestrator | `display_mod.c` | `SYS_DISP_CALL` — **no** pixel loops |
| BGA | `providers/bga/*` | LFB present only; 3D cmds return -1 |

### VirGL path

1. Feature negotiate `VIRTIO_GPU_F_VIRGL` (`virtio_ring_has_virgl()`).
2. `virtio_virgl_init` → `CTX_CREATE` + minimal pipeline (blend/RS/DSA/VE + TGSI VS/FS) + sub-ctx.
3. `gpu_cmd_*` → `virtio_virgl_exec` (bind state, CLEAR, DRAW, BLIT). Vertex MVP is CPU-prepped into a staging buffer then `TRANSFER_TO_HOST` — **no** RT pixel writes on CPU.
4. `GPU_CMD_PRESENT` stays in `virtio_cmd` (2D transfer/flush).

### QEMU (one primary)

| Goal | Args | Provider |
|------|------|----------|
| BGA (console-friendly) | `-vga std` | `display_bga` (no HW_SUBMIT → kshell GUI) |
| Virtio + VirGL | `-vga none` + `virtio-gpu-gl-pci` (if QEMU has it) else `virtio-gpu-pci` | VirGL → HW_SUBMIT; 2D-only → no GUI (B24) |

**Forbidden:** std VGA + `virtio-gpu-*` together → black screen.

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
