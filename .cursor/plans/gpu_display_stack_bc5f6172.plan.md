---
name: GPU Display Stack
overview: "End-to-end grafik: GpuProvider, display.kmod, Reed (OpenGL/Vulkan-seviyesi, MVP değil), Kilim (Reed üzerine game-engine renderer: 2D+text+3D model), usermode WM, hibrit shell, docs. Kademeli migrasyon: Stage 0-5 (bkz. Gerçeklik durumu). Yön değişmez."
todos:
  - id: break-legacy
    content: "Eski ugx (ugx_font.inc), kernel compositor/WM (src/drivers/dx/*), BGA, SYS_WM_*/SYS_GX_*/mkdx_api sil — SON adim: os-shell/terminal/files/os-settings/activity-monitor/imgui-demo/minesweeper hepsi bunlara bagimli oldugu icin Stage 5'ten once silinemez (bkz. Gerceklik durumu)"
    status: pending
  - id: gpu-framework
    content: gpu_provider_ops + DRIVER_CLASS_GPU + PCI display-class detect/bind
    status: pending
  - id: gpu-virtio
    content: gpu_virtio.kmod — src/drivers/display/virtio_gpu zaten var ama kernel dx compositor'a bagli (yeni gpu_provider_ops'a gore ayristirilmadi)
    status: in_progress
  - id: gpu-vga-fb
    content: gpu_vga.kmod LFB provider — src/drivers/display/bga hâlâ var, plan bunun silinmesini istiyor ama once virtio/vga provider ayrimi bitmeli
    status: pending
  - id: display-orchestrator
    content: display.kmod — resource/queue/fence/export/import/scanout + SYS_DISP_* (syscall.h'de SYS_DISP_* HENÜZ YOK)
    status: pending
  - id: reed-ll
    content: "Reed (userspace/sdk/reed) — MVP DEGIL, OpenGL/Vulkan-seviyesi tam API: handle-based buffer/texture/sampler/render-target/pipeline/fence, command-list + submit/present, fixed-function shading-mode enum (unlit/textured/vertex-lit — shader compiler YOK), software rasterizer (bound-checked, per-draw quota) birincil backend + virtio-gpu 3D v1.1 stretch. Su an 12 satir stub. Bkz. 'Reed API yuzeyi' bolumu."
    status: pending
  - id: kilim-hl
    content: "Kilim (userspace/sdk/kilim) — MVP DEGIL, Reed uzerine game-engine-tarzi renderer: batching/state-sort, Text (font atlas + glyph batch), 2D primitives, acrylic/blur (2-pass Reed shading-mode), 3D Mesh/Material/Transform/Camera/Scene + basit model loader (.kmesh, static mesh, iskelet animasyonu v1.1'e ertelendi), widget layer. Kilim asla kernel/SYS_DISP_*'a dogrudan gitmez, hep Reed uzerinden. Su an 13 satir stub. Bkz. 'Kilim API yuzeyi' bolumu."
    status: pending
  - id: wm-usermode
    content: "userspace/window-manager/main.cpp VAR ama sadece SYS_YIELD donen bir placeholder (13 satir); kendi yorumu: 'compositor still kernel mkdx for now'. Gercek WM mantigi hâlâ kernel dx/server.c + compositor.c icinde."
    status: pending
  - id: hybrid-shell
    content: userspace/os-shell TAMAM VE COK GELISMIS (macOS-vari dock magnification, menubar deep-link, tema, wifi/battery/clock) — ANCAK Reed/Kilim degil, kernel dx SYS_WM_*/SYS_GX_* uzerinden calisiyor. 'hybrid-shell' hedefi görsel/UX olarak bitti, backend migrasyonu bekliyor.
    status: in_progress
  - id: input-feed
    content: PS/2 → SYS_INPUT_STATE ile kernel dx'e feed ediyor (calisiyor); virtio-input yok; usermode WM'e feed henuz yok (WM placeholder oldugu icin)
    status: pending
  - id: docs-reed-kilim
    content: docs/graphics-reed-kilim.tr.md + .en.md
    status: pending
isProject: false
---

# GPU / Display / Reed + Kilim — End-to-End

## ⚠ Gerçeklik durumu (2026-07-29 — kod taramasi sonrasi)

Bu plan "backward compat yok, shim yok, kernel compositor/WM YASAK" diyor ve
`break-legacy` maddesini ilk adım gibi sıralıyor. Kod taramasi şunu gösterdi:
**tam tersi oldu** — proje kernel-side `dx` compositor'ı (`src/drivers/dx/`:
`compositor.c`, `window.c`, `server.c`, `console.c`, `draw.c`, `blur.c`, `font.c`,
`accel.c`, `render3d.c`, `context.c`, `device.c`, `dx_mod.c`) üzerine **çok daha
fazla** yatırım yaparak `SYS_WM_*`/`SYS_GX_*` (syscall.h 200-219, 275, 278) ABI'sini
production-grade hale getirdi: acrylic/blur, damage-rect, focus, topmost, class-based
window lookup, wallpaper. Bunun üstünde şu an **çalışan** bir masaüstü var:
`os-shell` (menubar + dinamik dock + tema + wifi/battery/clock), `os-settings`
(~1250 satır tam sayfa hub), `terminal` (~1545 satır), `files` (~471 satır Explorer),
`activity-monitor` (~754 satır), `imgui-demo`, `minesweeper` — hepsi `hsrc::sdk::gfx`
(kernel dx'e syscall ile bağlı) kullanıyor.

Buna karşılık bu planın gerçek teslimatı (Reed/Kilim/usermode WM) **hiç başlamadı**:

| Plan hedefi                             | Plandaki konum             | Gerçek konum                    | Durum                                                                                             |
| --------------------------------------- | -------------------------- | ------------------------------- | ------------------------------------------------------------------------------------------------- |
| Reed (low-level)                        | `userspace/reed`           | `userspace/sdk/reed/reed.cpp`   | 12 satır stub, `create_device()` → `-1`                                                           |
| Kilim (high-level)                      | `userspace/kilim`          | `userspace/sdk/kilim/kilim.cpp` | 13 satır stub, `fill_rect()` → `-1`                                                               |
| usermode window-manager                 | `userspace/window-manager` | aynı yerde, gerçek              | 13 satır — sadece `SYS_YIELD` döner, kendi yorumunda "compositor still kernel mkdx for now" diyor |
| `display.kmod` + `SYS_DISP_*`           | yeni                       | yok                             | `syscall.h`'de `SYS_DISP_*` tanımı yok                                                            |
| `gpu_provider_ops` + `DRIVER_CLASS_GPU` | yeni                       | yok                             | `src/drivers/display/{bga,virtio_gpu}` hâlâ eski model, provider soyutlaması yok                  |

**Sonuç:** `break-legacy` maddesini plandaki gibi "ilk iş" olarak yapmak bugün
`os-shell`/`terminal`/`files`/`os-settings`/`activity-monitor`/`imgui-demo`/`minesweeper`'ın
hepsini aynı anda kırar (hepsi `SYS_WM_*`/`SYS_GX_*`'a bağımlı). Bu plan aşağıdaki
**kademeli migrasyon sırasına** göre revize edilmiştir — eski "1–5. Stack" ve "8. Sıra"
bölümleri artık bu sırayla okunmalı, çelişirse bu bölüm geçerlidir:

1. **Stage 0 (mevcut, dokunma):** Kernel `dx` compositor + `SYS_WM_*`/`SYS_GX_*` = tek
   çalışan grafik ABI'si. Silinmez, kırılmaz; yeni OS syscall numaraları (god-level plan)
   200-286 aralığıyla çakışmayacak şekilde eklenir.
2. **Stage 1 — gerçek Reed (OpenGL/Vulkan-seviyesi, MVP değil):** `gpu_provider_ops`,
   `display.kmod`, `SYS_DISP_*` yazılır; `userspace/sdk/reed/reed.cpp` **tam** handle-based
   kaynak modeli (buffer/texture/sampler/render-target/pipeline/fence) + command-list +
   submit/present + fixed-function shading-mode ile implement edilir (detay: "Reed API
   yüzeyi"). Bu aşamada `SYS_WM_*`'a dokunulmaz — iki ABI paralel yaşar.
3. **Stage 2 — Kilim (game-engine-tarzı renderer, MVP değil):** Kilim, bugün
   `hsrc::sdk::gfx`'in sağladığı özellik kümesine (rounded-fill, text/font, SVG icon blit,
   acrylic/blur, damage-rect, wallpaper cover-scale) **ek olarak** 3D Mesh/Material/Camera/Scene
   - model loader + batching/state-sort ile Reed üzerinde yazılır (detay: "Kilim API yüzeyi").
     Parity olmadan hiçbir app migrate edilmez.
4. **Stage 3 — gerçek usermode window-manager:** `window-manager/main.cpp` placeholder'ı
   kaldırılır; focus≠hover, z-order, damage, `WindowOptions` boolean opts, process-death
   cleanup dahil `SYS_WM_*` semantiğinin tamamı Reed üzerinde yeniden inşa edilir.
5. **Stage 4 — app-by-app migrasyon:** `os-shell` → `os-settings` → `terminal` → `files`
   → `activity-monitor` → `imgui-demo`/`minesweeper` sırasıyla `hsrc::sdk::gfx`'ten
   Kilim/window-manager'a taşınır (god-level planın H9/H10/H13 phase'leriyle birebir
   örtüşür). Her app taşındıktan sonra kendi kabul kriteri regresyonsuz geçmeli.
6. **Stage 5 — break-legacy (asıl silme):** Tüm tüketiciler taşındıktan sonra
   `src/drivers/dx/*`, BGA, `ugx_font.inc`, `SYS_WM_*`/`SYS_GX_*` silinir. `rg
SYS_WM_|SYS_GX_|ugx_|mkdx` → sıfır eşleşme kabul kriteri.

**Kırık referans:** `xmake_userspace_boot_a3be45ab.plan.md` artık `.cursor/plans/`
altında yok; bu dosyanın işaret ettiği init→systemd ayrımı zaten `userspace/systemd`

- `userspace/init` + mevcut xmake kurulumuyla tamamlanmış durumda. Bu plandaki tüm
  "xmake_userspace_boot" linkleri artık geçersiz sayılmalı; build/init konuları için
  doğrudan `userspace/systemd/` ve `userspace/init/`'e bakılmalı.

**SMP/locking:** `src/drivers/dx/compositor.c` ve `window.c` kernel-side paylaşılan
state (window list, focus, z-order) tutuyor. `.cursor/rules/kernel-smp-state.mdc` /
`kernel-asm-locking.mdc` bu dosyalar için henüz taranmadı (`docs/smp-scan-report.md`
listesinde yok) — Stage 1-4 sırasında bu dosyalara dokunulmayacağı için acil değil,
ama Stage 5 öncesi (ve varsa yeni `display.kmod`/`SYS_DISP_*` kodu) için ayrı bir
SMP taraması yapılmadan "production-grade" denemez.

**İlişki**

| Konu                                     | Plan                                                                           |
| ---------------------------------------- | ------------------------------------------------------------------------------ |
| Build, `userspace/`, init→systemd, xmake | [xmake_userspace_boot_a3be45ab.plan.md](xmake_userspace_boot_a3be45ab.plan.md) |
| OS net/fs/Settings/Explorer…             | [god-level_window_api_87fb7031.plan.md](god-level_window_api_87fb7031.plan.md) |
| Grafik / GPU / WM çizim                  | **bu dosya**                                                                   |

Bu plan build omurgasını **tekrar etmez**. Kod `userspace/reed|kilim|window-manager|os-shell` altında yaşar; derleme xmake planına göre.

Teslimat: çalışan masaüstü grafik yığını (xmake planı ile birlikte). Ertelenmiş stub yok.

## İsimlendirme

| Katman       | İsim                          |
| ------------ | ----------------------------- |
| Low-level    | **Reed** (`userspace/reed`)   |
| High-level   | **Kilim** (`userspace/kilim`) |
| Orchestrator | `display.kmod`                |
| GPU          | `gpu_virtio` / `gpu_vga`      |

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
10. Reed kaynak API'si validasyonsuz/kotasız/handle-generation'sız → yok (bkz. "Güvenlik ve bellek güvenliği")

## WM / shell checklist (teslimatta)

Boolean opts (uint8_t), min/max/restore, owner/parent, z-order, hit-test, cursor, events+wait, damage rect, clipboard, AllocConsole usermode, ~64 windows, errno; dock pin + running zorunlu; deep-link menubar; frosted ~70% Kilim; process death cleanup.

## 1–5. Stack (özet — detay için aşağıdaki iki bölüm)

1. **Reed** — device, buffers, DrawIndexed, present, fence, WSI
2. **Kilim** — 2D/UI/3D on Reed (game-engine renderer, MVP değil)
3. **GpuProvider** — `gpu.h`, PCI class 0x03, register/select
4. **gpu_virtio** + **gpu_vga**; BGA sil
5. **display.kmod** — submit çevirisi, handles, export/import, scanout, `SYS_DISP_*`

---

## Reed API yüzeyi (OpenGL/Vulkan seviyesi — MVP değil)

**Zihniyet:** Reed = "bu kernele özel OpenGL". App `kilim` kullanmadan doğrudan Reed'i
çağırabilir (`kilim.renderText("hello")->reed.renderPolygonsEtc()` — Kilim, ürettiği
geometriyi Reed tipi/handle'ları üzerinden döner; app aynı frame'e ek Reed çağrısı
yapabilir). **Bilinçli kapsam sınırı:** gerçek bir shader derleyicisi/JIT **yok**
(god-level planın "bilerek dışarıda" listesiyle tutarlı) — bunun yerine genişletilebilir
**fixed-function shading-mode** enum'u kullanılır; bu, tam 3D model + texture + basit
aydınlatma rendering'ini shader compiler yazmadan mümkün kılar.

### Kaynak modeli (handle-based, Vulkan-vari)

| Kaynak               | Açıklama                                                                                                                                                                                                                                                                                                               |
| -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `reed::Device`       | `SYS_DISP_*` bağlantısı; `create(surface_handle)`; capability query: `max_texture_size`, `hw_accel_available`, `max_draw_calls_per_frame`                                                                                                                                                                              |
| `reed::Buffer`       | `VERTEX` / `INDEX` / `UNIFORM` / `STAGING`; `create(kind, size, data)`, `update(offset, data, len)`, `destroy()`                                                                                                                                                                                                       |
| `reed::Texture2D`    | format `R8`/`RG8`/`RGBA8`/`A8` (font atlas), mip levels, `upload(level, data, len)`                                                                                                                                                                                                                                    |
| `reed::Sampler`      | wrap (clamp/repeat) + filter (nearest/bilinear)                                                                                                                                                                                                                                                                        |
| `reed::RenderTarget` | color + opsiyonel depth attachment; offscreen render-to-texture (blur/acrylic pass'leri için zorunlu)                                                                                                                                                                                                                  |
| `reed::Pipeline`     | primitive topology (triangle/line/point), cull mode, depth test/write, blend mode, vertex layout, **shading-mode**                                                                                                                                                                                                     |
| `reed::Fence`        | tek-kullanımlık; state machine `UNSIGNALED → SIGNALED → CONSUMED` (çift-signal/çift-wait hata döner, UB yok)                                                                                                                                                                                                           |
| `reed::CommandList`  | `begin/end`, `bind_pipeline`, `bind_vertex_buffer`, `bind_index_buffer`, `bind_texture(slot)`, `set_uniform(model, view, proj, light[], color)`, `set_viewport/scissor`, `clear(color, depth)`, `draw(count, first)`, `draw_indexed(count, first_index, base_vertex)`, `blit(src_rt, dst_rt, region)`, `submit(fence)` |

**Handle güvenliği:** her handle `(16-bit index, 16-bit generation)`; her çağrıda
generation doğrulanır → **use-after-free/ABA imkansız**. Ham kernel pointer'ı asla
usermode'a sızmaz (mevcut "raw kernel pixel `wm_map` → handle export/import" kuralıyla
birebir).

### Shading-mode tablosu (fixed-function, genişletilebilir)

| Mode                                      | İş                                                      |
| ----------------------------------------- | ------------------------------------------------------- |
| `REED_SHADE_UNLIT_COLOR`                  | Düz renk (UI dikdörtgenleri)                            |
| `REED_SHADE_UNLIT_TEXTURED`               | Texture sample (ikon/text atlas/blit)                   |
| `REED_SHADE_VERTEX_LIT`                   | Ambient + Lambert diffuse, ≤4 nokta/yön ışık (3D model) |
| `REED_SHADE_BLUR_H` / `REED_SHADE_BLUR_V` | Separable box/gauss blur pass (acrylic)                 |
| `REED_SHADE_ACRYLIC_TINT`                 | Blur sonucu + tint + alpha composite                    |

Yeni mod eklemek enum + C fonksiyon; shader dili/derleyici yok.

### Backend

- **Birincil (v1, zorunlu):** software rasterizer — scanline/half-space, tüm yazımlar
  render-target sınırına **clip'li** (OOB write imkansız).
- **İkincil (v1.1, stretch, zorunlu değil):** `virtio-gpu` 3D/virgl passthrough —
  `src/drivers/display/virtio_gpu` şu an sadece 2D scanout yapıyor, 3D command queue
  implement değil; bu v1 kapsamı dışında, Reed API'si backend'i şeffaf soyutlar
  (`hw_accel_available` capability flag ile app/Kilim fark etmeden çalışır).

## Kilim API yüzeyi (Reed üzerine game-engine renderer — MVP değil)

Kilim **hiçbir zaman** `SYS_DISP_*`/kernele doğrudan gitmez — her zaman Reed üzerinden
(mermaid'deki `Kilim --> Reed` kuralı sabit).

| Katman          | API                                                                                                                                                           | Not                                                                                                                                |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| **2D primitif** | `fill_rect`, `fill_round_rect`, `stroke_rect`, `line`, `circle`, `polygon`                                                                                    | Tessellate → Reed vertex/index buffer; batch by (pipeline, texture)                                                                |
| **Text**        | `Font::load(path)`, `Text::render(str, x, y, size, color)`                                                                                                    | Glyph atlas `Texture2D`; tek atlas = tek draw call/run; döndürdüğü `Batch&` üzerinden `.reed()` ile ham Reed handle'a erişilebilir |
| **Efekt**       | `Acrylic::apply(rt, radius, tint, alpha)`                                                                                                                     | Offscreen `RenderTarget` + `BLUR_H`/`BLUR_V`/`ACRYLIC_TINT` 2-pass; kernel `blur.c`'nin yerini alır                                |
| **3D**          | `Mesh{pos,normal,uv,color}`, `Material{shading_mode,texture,color,lights}`, `Transform` (TRS), `Camera` (persp/ortho + view matrix), `Scene`/node hiyerarşisi | Model loader: basit `.kmesh` (statik mesh + texture referansı); **iskelet animasyon v1.1'e ertelendi** (kapsam şişmesin)           |
| **Widget**      | `Button`, `Panel`, `Toggle`, `Slider`, `ScrollView`                                                                                                           | os-shell/os-settings Stage 4 migrasyonunda kernel chrome painting'in yerini alır                                                   |
| **Batching**    | otomatik state-sort (pipeline+texture)                                                                                                                        | "game engine renderer" davranışı — naif immediate-mode değil, frame başı draw-call sayısı minimize edilir                          |

## Güvenlik ve bellek güvenliği (Reed/Kilim — zorunlu, atlanamaz)

Kullanıcı talebi: "security/memory leak ihtimali varsa optimize et." Aşağıdaki riskler
`display.kmod` implement edilirken **tasarımdan itibaren** kapatılmalı, sonradan yama
değil:

| #   | Risk                                           | Zorunlu önlem                                                                                                                                                                                                                                                                                              |
| --- | ---------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Handle UAF / double-free                       | `(index, generation)` handle; `destroy()` slotu anında temizler + generation'ı arttırır                                                                                                                                                                                                                    |
| 2   | OOB buffer/texture upload                      | `display.kmod` her `update()`/`upload()`'da `len`'i hedef kapasiteye karşı doğrular; `copy_from_user` öncesi kesin sınır kontrolü, kısmi yazım yok                                                                                                                                                         |
| 3   | Kaynak tükenmesi (DoS)                         | Process başına sert kota: örn. 256 buffer / 64 texture / 32 pipeline / 8 render-target / 16 fence (mevcut "~64 pencere" kotasıyla aynı disiplin)                                                                                                                                                           |
| 4   | Kaçak/dejenere draw call rasterizer'ı kilitler | Draw-call başına vertex/index sayısı tavanı + render-target'a zorunlu scissor-clip + rasterizer iç döngüsünde zaman/iterasyon bütçesi (kötü üçgen kernel/compositor thread'ini asla sonsuza kadar bloklamaz)                                                                                               |
| 5   | Process crash/kill'de kaynak sızıntısı         | "Kodlarken dikkat #6" (process exit → WM cleanup) **Reed kaynak tablosuna da genişletilir**: ölen pid'in tüm buffer/texture/pipeline/fence'i otomatik serbest bırakılır                                                                                                                                    |
| 6   | Cross-process kaynak erişimi                   | Reed kaynakları sahibi connection/window'a scoped; başka process'in handle'ıyla erişim mümkün değil (global paylaşımlı tablo yok)                                                                                                                                                                          |
| 7   | SMP — kernel-side paylaşılan kaynak tabloları  | `display.kmod`'un buffer/texture/pipeline/fence tabloları `kernel-smp-state.mdc` kurallarına **gün 1'den** uyar: per-resource-table spinlock veya per-connection ownership; bugünkü taranmamış `dx/compositor.c` gibi "unscanned global" olarak bırakılmaz — yazılınca `docs/smp-scan-report.md`'e eklenir |
| 8   | Fence/sync kötüye kullanımı                    | Tek-kullanımlık fence state machine (madde: kaynak tablosu) — çift-signal/çift-wait hata döner                                                                                                                                                                                                             |
| 9   | Sessiz sızıntı tespiti                         | `SYS_DISP_DEBUG_STATS` / `reed::Device::stats()` — process başına canlı kaynak sayısı; kabul kriteri: her app kapandıktan sonra sayaç **0**'a döner (otomatik test edilebilir)                                                                                                                             |

## 6. WM + input

`userspace/window-manager` + focus≠hover + input feed. Kernel’de WM syscall yok.

## 7. Shell + apps

`userspace/os-shell` hibrit; diğer GUI app’ler aynı ağaçta (xmake plan layout).

## 8. Sıra

> **Güncel:** Bu sıra artık geçersiz — "Gerçeklik durumu" bölümündeki Stage 0-5
> kademeli sırası geçerlidir (legacy silme en sona alındı, çünkü tüm app'ler ona
> bağımlı hale geldi). Aşağıki liste sadece tarihsel referans.

1. ~~xmake_userspace_boot iskeleti~~ — zaten tamam (`userspace/systemd` + `userspace/init`)
2. ~~Legacy gfx sil~~ + GpuProvider + virtio/vga + display.kmod
3. Reed → Kilim → WM → os-shell
4. App rewrite + docs

## 9. Docs

`docs/graphics-reed-kilim.{tr,en}.md` — esinlenme, terminoloji, katmanlar, focus≠hover, portability; build için xmake planına link.
