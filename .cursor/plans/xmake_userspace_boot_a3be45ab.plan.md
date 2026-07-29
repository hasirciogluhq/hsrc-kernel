---
name: Xmake Userspace Boot
overview: "Tek kaynak: Makefile/mk → __old_shits__; xmake.lua + xmake/ + ld/; userspace/ ağacı; boot init→systemd; embedded+custom drivers. Grafik/OS planları buraya bağlanır."
todos:
  - id: archive-make
    content: "Makefile + mk/ → __old_shits__; kök linker.ld/user.ld → ld/; xmake.lua + xmake/"
    status: pending
  - id: userspace-layout
    content: "userspace/<app|lib>/ kendi xmake.lua; src/user + apps/imgui-demo taşı; include/user ABI kalsın"
    status: pending
  - id: init-systemd
    content: "Kernel yalnız init; systemd + units/; mke_spawn_all ve service_start_critical kalkar"
    status: pending
  - id: drivers-xmake
    content: "Embedded → kernel link; custom .kmod targets → initrd"
    status: pending
  - id: tools-initrd-qemu
    content: "tools/pack_* xmake target; initrd; xmake run; compile_commands"
    status: pending
isProject: false
---

# Xmake + Userspace + Init/Systemd

**Tek kaynak** build / userspace layout / boot supervisor için.

- Grafik → [gpu_display_stack_bc5f6172.plan.md](gpu_display_stack_bc5f6172.plan.md) (paths = `userspace/…`, derleme **bu plan**)
- OS → [god-level_window_api_87fb7031.plan.md](god-level_window_api_87fb7031.plan.md) (Wave M / H11 Make → **bu plan**)

CMake yok. Aktif Make yok.

## 1. Arşiv + xmake

| Bugün | Sonra |
|-------|--------|
| kök `Makefile` | `__old_shits__/Makefile` |
| `mk/*` | `__old_shits__/mk/` |
| kök `linker.ld`, `user.ld` | `ld/linker.ld`, `ld/user.ld` |
| — | kök `xmake.lua` + `xmake/` |

```text
xmake/
  toolchain.lua    # i386 freestanding + userspace CXX
  kernel.lua
  drivers.lua      # custom .kmod
  userspace.lua    # .mke pack helpers
  qemu.lua
ld/
  linker.ld
  user.ld
tools/             # pack_mke, pack_initrd — yerinde kalır; xmake host target
```

Kök `xmake.lua` tek giriş: kernel + embedded drivers + custom kmods + userspace + initrd + `xmake run`.

```text
xmake
xmake kernel | drivers | userspace
xmake run
```

`xmake project -k compile_commands` (veya eşdeğeri) → clangd.

## 2. Userspace ağacı

Kernel / `src/kernel` içinde userspace program **yasak**.

Taşı:
- `src/user/apps/*`, `src/user/sdk/*`, `src/user/string.c` → `userspace/`
- `apps/imgui-demo` (third_party dahil) → `userspace/imgui-demo`

```text
userspace/
  sdk/                 # impl; xmake.lua; add_deps
  reed/                # gpu plan
  kilim/
  init/                # first process
  systemd/
    units/             # *.service → initrd /etc/systemd/
  window-manager/
  os-shell/
  os-settings/
  terminal/
  files/
  activity-monitor/
  minesweeper/
  imgui-demo/
```

Her program/lib: **kendi klasör + kendi `xmake.lua`**. App’ler `add_deps("sdk"|"reed"|"kilim")`.

**Headers (sabit karar):** public ABI `include/user/**` **kalır** (syscall/reed/kilim deklarasyonları). İmplementasyon `userspace/sdk|reed|kilim`. Çift include root yok.

Çıktı: `build/userspace/<name>/<name>.mke` → initrd (`/applications/…`, init → `/sbin/init` veya `/init.mke`).

## 3. Drivers

| Tür | Build | Çıktı |
|-----|--------|--------|
| Embedded (`DRIVER_KIND_INTERNAL`) | kernel target | kernel image |
| Custom | drivers target | `.kmod` → initrd |

## 4. Boot

1. Kernel: drivers, VFS, initrd  
2. Kernel **yalnızca init** exec (`mke_spawn_from_initrd` → tek dosya / init path)  
3. init → systemd  
4. systemd units: `window-manager`, `os-shell`, … + respawn  

Kalkar: tüm `.mke` auto-spawn; `service_start_critical(os-ui)`; kernel’in WM/os-ui özel-case’i.

**`service.c`:** boot critical spawn **silinir**. İsteğe bağlı ince `SYS_SERVICE_*` (liste/status) kalabilir (Activity Monitor); asıl supervisor systemd.

## 5. Sıra

1. `__old_shits__` + `xmake`/`ld` + tools wire + kernel smoke  
2. `userspace/init` + `systemd` + units; kernel tek-spawn  
3. SDK taşıma; app klasörleri  
4. Embedded/custom driver xmake  
5. gpu_display_stack app/lib’leri bu ağaca oturur  
6. `xmake run`  

## 6. Diğer planlar ne yapmaz

- gpu_display_stack: build/Makefile/initrd/xmake **yeniden tarif etmez**; path + bağımlılık yazar  
- god-level: H11 “Makefile” / Wave M kernel registry spawn **bu plana defer**; systemd unit semantiği burada  
