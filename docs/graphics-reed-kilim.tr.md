# Reed / Kilim / Display — Grafik Yığını

mykernel aktif grafik mimarisi. Legacy `dx` / `mkdx` / `gfx.hpp` / `SYS_WM_*` / `SYS_GX_*` **yoktur**.

## Zihinsel model

| Katman | Analoji | Rol |
|--------|---------|-----|
| **Reed** | OpenGL / D3D device | FBO/backbuffer, command buffer, submit, present |
| **Kilim** | ImGui DrawList | Yüksek seviye quad/text/widget → Reed komutları |
| **WM** | Compositor | App FBO import, Reed blit ile compose, tek scanout |
| **GpuProvider** | GPU driver | **Tüm** raster / blit / compose piksel işi |

**Kural:** userspace (Reed / Kilim / app) framebuffer’a **asla** piksel yazmaz. Softpipe / CPU raster / `gpu_soft` **yasaktır**.

## Stack

```text
Uygulama
  kilim::Context      — drawlist (fill_rect / text / …)
  reed::Device        — FBO yarat, cmd kaydet, submit
        │
        ▼  SYS_DISP_CALL (296)  DISP_OP_SUBMIT / SCANOUT
display.kmod          — handle + resolve → GpuProvider
        │
        ▼  gpu_submit / present
display_virtio (VirGL SUBMIT_3D)   tercih — GPU_CAP_HW_SUBMIT
display_bga                        yalnızca scanout/present (3D yok)
```

- **WM** tek present sahibi (`kilim::end_frame` / Reed present).
- **İstemci** yalnızca `kilim::commit_frame`; scanout **yapmaz**.
- **os-shell** wallpaper; menubar/dock v1 WM chrome.

`gui_stack_ready()` = `display_active() && disp_api_get() && gpu_provider_active() + GPU_CAP_HW_SUBMIT`. HW 3D yoksa → **kshell** (B24).

## Frame akışı (DX11-vari)

1. Reed FBO bağlar (`create_render_target` / swapchain).
2. Kilim `begin_frame` → clear + drawlist.
3. App/Kilim çizim (`fill_rect` → Reed `draw`); komutlar Reed buffer’ında birikir.
4. `commit_frame` / `end_frame` → `DISP_OP_SUBMIT` → **GpuProvider::gpu_submit** (host VirGL).
5. WM app surface token’larını import eder, kendi FBO’suna `blit` (yine submit), sonra `present`.

## Kernel / display

| Parça | Konum | Rol |
|-------|--------|-----|
| VirGL 3D | `providers/virtio/virtio_virgl.c` | CLEAR / DRAW / BLIT → `SUBMIT_3D` |
| Virtio 2D | `providers/virtio/virtio_cmd.c` | PRESENT + scanout ring |
| Bridge | `display.c`, `gpu.c` | provider seçimi; `display_ops.gpu_caps` |
| Orchestrator | `display_mod.c` | `SYS_DISP_CALL` — **piksel loop yok** |
| BGA | `providers/bga/*` | LFB present; 3D komutlar -1 |

### VirGL yolu

1. `VIRTIO_GPU_F_VIRGL` negotiate (`virtio_ring_has_virgl()`).
2. `virtio_virgl_init` → `CTX_CREATE` + pipeline (blend/RS/DSA/VE + TGSI VS/FS) + sub-ctx.
3. `gpu_cmd_*` → `virtio_virgl_exec`. Vertex MVP staging buffer’a CPU prep + `TRANSFER_TO_HOST` — RT’ye CPU piksel yazılmaz.
4. `GPU_CMD_PRESENT` → `virtio_cmd` (2D transfer/flush).

### QEMU (tek primary)

| Amaç | Argümanlar | Provider |
|------|------------|----------|
| BGA | `-vga std` | `display_bga` (HW_SUBMIT yok → GUI kshell) |
| Virtio + VirGL | `-vga none` + `virtio-gpu-gl-pci` (varsa) yoksa `virtio-gpu-pci` | VirGL → HW_SUBMIT; yalnız 2D → GUI yok (B24) |

**Yasak:** std VGA + `virtio-gpu-*` birlikte → siyah ekran.

Varsayılan: **1920×1080**.

## Reed (`reed::`)

- Sadece `DISP_CMD_*` kaydı; `submit()` → `DISP_OP_SUBMIT`.
- Opcode’lar: `BUFFER_*`, `TEXTURE_*`, `RT_*`, `SUBMIT`, `SCANOUT`, `EXPORT`/`IMPORT`.
- **Import:** exporter backing paylaşılır (steal yasak).

## Kilim (`kilim::`)

- Sadece Reed drawlist (`SYS_DISP_*` yok).
- Vertex buffer’lar submit bitene kadar canlı tutulur.

### Frame API

| Çağrı | Kim | Etki |
|-------|-----|------|
| `begin_frame` | app / WM | clear + batch aç |
| `commit_frame` | **istemci** | flush + submit; **present yok** |
| `end_frame` | **yalnızca WM** | submit + **scanout/present** |

### Context boyutu

`kilim::Context` (~3 MiB+) **MUST** `static` / heap — default 1 MiB ustack **yasak**.

## İlgili kurallar

`.cursor/rules/graphics-*.mdc`, `kernel-gui-disk.mdc`, `kernel-asm-locking.mdc`.
