---
name: Xmake Userspace Boot
overview: Makefile/mk → __old_shits__; xmake + ld/; userspace/ taşıma; init→systemd; sonra include/import map + clangd/compile_commands zorunlu güncelleme.
todos:
  - id: archive-make
    content: Makefile + mk/ → __old_shits__; kök linker.ld/user.ld → ld/; xmake.lua + xmake/
    status: completed
  - id: userspace-layout
    content: userspace/<app|sdk|…>/ taşı; çoklu sdk (sdk/reed, sdk/kilim); tekil+nested app
    status: completed
  - id: fix-imports-clangd
    content: "Taşıma sonrası #include/path, xmake includes, .clangd, compile_commands.json yenile"
    status: completed
  - id: init-systemd
    content: Kernel yalnız init; systemd + units/; mke_spawn_all ve service_start_critical kalkar
    status: completed
  - id: drivers-xmake
    content: Embedded → kernel link; custom .kmod targets → initrd
    status: completed
  - id: tools-initrd-qemu
    content: tools/pack_* xmake; initrd; xmake run
    status: completed
isProject: false
---

# Xmake + Userspace + Init/Systemd

**Tek kaynak** build / userspace layout / boot supervisor.

- Grafik → [gpu_display_stack_bc5f6172.plan.md](gpu_display_stack_bc5f6172.plan.md)
- OS → [god-level_window_api_87fb7031.plan.md](god-level_window_api_87fb7031.plan.md)

CMake yok. Aktif Make yok.

**Zorunlu:** Taşıma bitince yarım bırakılmaz — **tüm import/include map’leri + clangd** aynı teslimatta güncellenir.

## 1. Arşiv + xmake

| Bugün | Sonra |
|-------|--------|
| kök `Makefile` | `__old_shits__/Makefile` |
| `mk/*` | `__old_shits__/mk/` |
| kök `linker.ld`, `user.ld` | `ld/linker.ld`, `ld/user.ld` |
| — | kök `xmake.lua` + `xmake/` |

```text
xmake/
  toolchain.lua
  kernel.lua
  drivers.lua
  userspace.lua
  qemu.lua
ld/
  linker.ld
  user.ld
tools/             # pack_* yerinde; xmake host target
```

```text
xmake
xmake kernel | drivers | userspace
xmake run
xmake project -k compile_commands   # clangd için
```

## 2. Userspace ağacı

Kernel içinde userspace program yok.

Taşı:
- `src/user/apps/*`, `src/user/sdk/*`, `src/user/string.c` → `userspace/`
- `apps/imgui-demo` → `userspace/imgui-demo` (veya nested `userspace/apps/...`)

```text
userspace/
  sdk/                 # çoklu SDK (tek monolit değil)
    reed/              # LL; display ile konuşur (kernel karşılığı: display)
    kilim/             # HL; sadece Reed — kernel karşılığı yok
    process/           # örnek OS glue SDK (gerekirse)
  init/
  systemd/
    units/
  terminal/            # tekil app
  apps/                # nested örnek alanı
    ...
  window-manager/
  os-shell/
  os-settings/
  files/
  activity-monitor/
  minesweeper/
  imgui-demo/
```

Her app/lib: kendi `xmake.lua`. App’ler `add_deps` ile sdk’lara bağlanır.

**Headers:** public ABI `include/user/**` kalır. İmpl `userspace/sdk/...`.

## 3. Taşıma sonrası — import map + clangd (zorunlu)

Sadece dosya taşımak yetmez. Aynı adımda:

1. **Kaynak `#include` / relative path** — eski `src/user/...` referanslarını tara (`rg`); kırık include’ları düzelt.
2. **xmake include roots** — her target’ta `-I include`, userspace sdk public path’leri, driver local headers; `add_includedirs` tutarlı.
3. **[`.clangd`](.clangd)** — Make-era absolute `-I.../mkdx`, `-I.../bga` vb. **yeniden yaz**:
   - ortak: `-Iinclude`
   - stale mkdx/bga path’leri kaldır (gpu planı sonrası zaten ölecek; xmake aşamasında en azından gerçek path’lere çek)
   - userspace C++ için ayrı flag seti gerekirse `.clangd` `If:` path match veya `compile_commands` öncelikli
4. **`compile_commands.json`** — `xmake project -k compile_commands` (veya eşdeğeri) kökte üret; clangd bunu kullansın. Elle bayat `.clangd` Add listesine güvenme.
5. **IDE / Cursor** — eski build dir path’leri; gerekirse `.vscode`/`c_cpp_properties` yoksa sadece compile_commands yeterli.
6. **Smoke:** kernel + bir userspace dosyası clangd’de kırmızı include kalmasın; `xmake` clean build yeşil.

Bu madde `userspace-layout` ile aynı PR/dalga; “sonra bakarız” yok.

## 4. Drivers

Embedded → kernel link. Custom → `.kmod` → initrd.

## 5. Boot

Kernel yalnız **init** → **systemd** → units (`window-manager`, `os-shell`, …).  
`mke_spawn` hepsi + `service_start_critical` kalkar.

## 6. Sıra

1. `__old_shits__` + xmake/ld + kernel smoke  
2. userspace taşı + **import/clangd/compile_commands**  
3. init + systemd + tek spawn  
4. driver targets  
5. gpu plan lib/app path’leri bu ağaca  
6. `xmake run`  

## 7. Diğer planlar

gpu_display_stack / god-level build tarif etmez; path + defer buraya.
