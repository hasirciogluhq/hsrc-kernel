# Reed / Kilim / Display — Grafik Yığını

Bu belge mykernel’in aktif grafik mimarisini açıklar. Legacy `dx` / `mkdx` / `gfx.hpp` / `SYS_WM_*` / `SYS_GX_*` **yoktur**.

## Stack

```text
Uygulama
  wm::Window          — pencere + /tmp/wm file IPC
  kilim::Context      — 2D/text/3D/widgets (batch)
  reed::Device        — buffer/texture/RT/command/present
        │
        ▼  SYS_DISP_CALL (296)
display.kmod          — orchestrator (disp_api)
        │
        ▼  GpuProvider
display_bga | display_virtio
```

- **WM** (`userspace/window-manager`) tek present sahibidir (`kilim::end_frame` / Reed present).
- **İstemci uygulamalar** yalnızca `kilim::commit_frame` (export + attach); scanout **yapmaz**.
- **os-shell** wallpaper (background surface); menubar/dock v1 WM chrome çizimi.

`gui_stack_ready()` = `display_active() && disp_api_get()`. GUI yoksa kernel **kshell**’e düşer; halt etmez.

## Kernel / display

| Parça | Konum | Rol |
|-------|--------|-----|
| Bridge | `src/drivers/display/display.c`, `gpu.c` | provider seçimi, mode, LFB/scanout |
| Orchestrator | `display_mod.c` | `SYS_DISP_CALL` opcode’ları |
| BGA | `providers/bga/` | Bochs/QEMU std VGA LFB |
| Virtio | `providers/virtio_gpu/` | virtio-gpu 2D scanout |
| ABI | `include/user/disp.h` | `DISP_OP_*` |
| Lock | `klock_disp` | `disp_api` + `drivers_poll` (asm spinlock) |

### QEMU (tek primary)

| Amaç | Argümanlar | Provider |
|------|------------|----------|
| BGA | `-vga std` | `display_bga` |
| Virtio | `-vga virtio` **veya** `-vga none` + `-device virtio-gpu-pci` | `display_virtio` |

**Yasak:** default/std VGA ile birlikte `virtio-gpu-pci` — present virtio’ya gider, pencere std’yi gösterir → siyah ekran.

Varsayılan çözünürlük: **1920×1080**.

## Reed (`reed::`)

- Header: `include/user/sdk/reed.hpp`
- Kaynak: `userspace/sdk/reed/`
- Düşük seviye: Device, Buffer, Texture2D, RenderTarget, CommandBuffer, Fence, Pipeline.
- v1 backend: **yazılım rasterizer**; `hw_accel_available` yalnızca capability.
- Kernel’e `SYS_DISP_CALL` ile buffer/texture/RT/export/import/scanout.

Önemli opcode’lar: `DISP_OP_BUFFER_*`, `TEXTURE_*`, `RT_*`, `EXPORT` / `IMPORT`, `SCANOUT`, `STATS`.

**Import kuralı:** import edilen texture exporter backing’i **paylaşır** (steal + free yasak); destroy son ref’te `kfree`.

## Kilim (`kilim::`)

- Header: `include/user/sdk/kilim.hpp`
- Kaynak: `userspace/sdk/kilim/`
- Reed üzerinde yüksek seviye: batching, Font/Text atlas, 2D, Acrylic blur, Mesh/Material/Transform/Camera/Scene, `.kmesh`, widgets.
- `SYS_DISP_*` doğrudan çağırmaz; sadece Reed.

### Frame API

| Çağrı | Kim | Etki |
|-------|-----|------|
| `begin_frame` | app / WM | clear + batch aç |
| `commit_frame` | **istemci** | flush + submit; **present yok** |
| `end_frame` | **yalnızca WM** | submit + **scanout/present** |

### Context boyutu (kritik)

`kilim::Context` içinde büyük batch dizileri vardır (~**3 MiB**).

- **MUST:** `static` / BSS / heap
- **MUST NOT:** process ustack (`PROC_USTACK_SIZE` = 64 KiB)

Aksi halde stack smash / `#GP` (window-manager donması).

## WM (`wm::`)

- Client: `include/user/sdk/wm.hpp`
- Server: `userspace/window-manager/main.cpp`
- IPC: `/tmp/wm` file request/response (`WMRq` / `WMRs`)
- Kernel `SYS_WM_*` **yok**

Sorumluluklar: create/show/focus/move/resize/damage, z-order, hit-test (focus ≠ hover), surface import + compose, pencere chrome, sistem menubar/dock (v1), ok imleci, tek present.

## Input

- `SYS_INPUT_STATE` — mouse/keyboard driver’larından; DX bağımlılığı yok.
- Focus routing usermode WM’de.
- Present hot path içinde `ps2_poll` / ağır poll yok.

## Uygulama durumu

| Bileşen | Durum |
|---------|--------|
| Pipeline (display→Reed→Kilim→WM) | Çalışıyor |
| Apps | wm+kilim **smoke stub** |
| Menubar/dock | WM v1 chrome (tam macOS UX değil) |
| Settings/terminal/files/minesweeper/imgui | Feature parity **NEXT** (`restore-app-ux`) |

## İlgili kurallar

`.cursor/rules/gfx-eng-*.mdc`, `kernel-gui-disk.mdc`, `kernel-asm-locking.mdc`.
