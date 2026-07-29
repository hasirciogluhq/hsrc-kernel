#include <arch/x86/cpu.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <arch/x86/irq.h>
#include <drivers/bus/pci.h>
#include <drivers/console/serial.h>
#include <drivers/console/vga.h>
#include <drivers/display/display.h>
#include <drivers/display/gpu.h>
#include <drivers/driver.h>
#include <kernel/boot_splash.h>
#include <kernel/bootmem.h>
#include <kernel/disp_api.h>
#include <kernel/env.h>
#include <kernel/epoll.h>
#include <kernel/errno.h>
#include <kernel/heap.h>
#include <kernel/klock.h>
#include <kernel/ksym.h>
#include <kernel/mm.h>
#include <kernel/module.h>
#include <kernel/netif.h>
#include <kernel/proc_mem.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/service.h>
#include <kernel/smp.h>
#include <kernel/socket.h>
#include <kernel/sync.h>
#include <kernel/syscall.h>
#include <kernel/time.h>
#include <kernel/userspace_boot.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>
#include <multiboot.h>

static void klog_heap(const char *tag) {
  klog(tag);
  klog(" heap_used=");
  serial_print_uint((uint32_t)heap_used());
  klog(" heap_free=");
  serial_print_uint((uint32_t)heap_free());
  klog("\n");
}

void kernel_main(uint32_t magic, multiboot_info_t *mbi) {
  bootmem_layout_t mem;

  serial_init();
  vga_init();
  klog("[boot] kernel_main\n");

  if (magic != MULTIBOOT_MAGIC) {
    klog("[boot] bad multiboot magic\n");
    vga_print("bad multiboot magic\n");
    for (;;)
      __asm__ volatile("hlt");
  }

  /*
   * Heap lives in a Multiboot-discovered available region above the fixed
   * .exec load window (see bootmem). Avoid a huge .bss heap - loaders may
   * place the initrd inside the kernel BSS span and then zero it.
   */
  if (bootmem_init(mbi, &mem) < 0 || mem.heap_size == 0) {
    klog("[boot] bootmem_init FAILED\n");
    vga_print("bootmem_init failed\n");
    for (;;)
      __asm__ volatile("hlt");
  }
  heap_init((void *)(uintptr_t)mem.heap_phys, mem.heap_size);
  klog_hex("[boot] heap_phys=", mem.heap_phys);
  klog_uint("[boot] heap_size=", mem.heap_size);
  klog_uint("[boot] total_ram=", mem.total_ram_bytes);
  mm_init();
  {
    uint32_t pend = mem.phys_end;
    if (pend < mem.heap_phys + mem.heap_size)
      pend = mem.heap_phys + mem.heap_size;
    if (pend < mem.total_ram_bytes)
      pend = mem.total_ram_bytes;
    vmm_bootstrap(pend);
  }
  gdt_init();
  idt_init();
  cpu_init_bsp();
  klock_subsystem_init();
  irq_init(); /* STI+PIT: timer must not schedule until scheduler_start */
  klog("[boot] irq ready\n");
  syscall_init();
  vfs_init();
  netif_init();
  socket_init();
  epoll_init();
  ksym_init();
  process_init();
  proc_mem_init();
  sync_init();
  env_init();
  service_init();
  scheduler_init();
  klog("[boot] scheduler_init done\n");
  smp_init();
  cpu_debug_dump();
  time_init();
  klog("[boot] core init done\n");

  driver_framework_init();
  gpu_framework_init();
  display_framework_init();
  pci_init();
  drivers_register_internal();
  driver_attach("vga");

  if (drivers_load_all(NULL) < 0) {
    klog("[boot] drivers_load_all FAILED\n");
    vga_print("drivers_load_all failed\n");
    for (;;)
      __asm__ volatile("hlt");
  }
  /* ps2_init used to mask the PIC; re-assert timer after driver load. */
  irq_ensure_timer_unmasked();
  klog("[boot] builtin drivers loaded\n");
  klog_heap("[boot]");

  klog("[boot] loading initrd modules...\n");
  if (modules_load_from_mbi(mbi) < 0) {
    klog("[boot] modules_load_from_mbi FAILED\n");
    vga_print("modules_load_from_mbi failed\n");
    for (;;)
      __asm__ volatile("hlt");
  }
  klog("[boot] modules loaded\n");
  klog_heap("[boot]");
  display_boot_log();

  if (env_load_initrd() < 0)
    klog("[boot] env_load_initrd failed (using defaults)\n");
  /* Disk is / (FAT); env files live under the on-disk FHS. */
  if (env_load_file("/system/etc/environment") < 0 &&
      env_load_file("/applications/environment") < 0 &&
      env_load_file("/etc/environment") < 0)
    klog("[boot] no environment file on disk\n");

  {
    int fd = vfs_open("/system", O_RDONLY);
    if (fd < 0)
      klog("[boot] /system missing - OS package not on disk\n");
    else
      (void)vfs_close(fd);
    fd = vfs_open("/applications", O_RDONLY);
    if (fd < 0)
      klog("[boot] /applications missing - user apps not on disk\n");
    else
      (void)vfs_close(fd);
  }

  /* Kernel is standalone: no display/disp → continue without GUI, not halt. */
  if (gui_stack_ready()) {
    klog("[boot] GUI stack ready (display+disp)\n");
    boot_splash_show();
  } else {
    klog("[boot] GUI stack unavailable\n");
    if (display_active() && !disp_api_get())
      klog("[boot] hint: display ok but disp_api missing (display.kmod?)\n");
    display_console_hold(0xFF1B2432u);
  }

  service_register_builtin_defaults();

  userspace_boot();
  service_bind_existing_processes();
  klog_heap("[boot]");

  {
    process_t **table = process_table();
    int i, n = 0;
    for (i = 0; i < PROC_MAX; i++) {
      if (table[i] && table[i]->state != PROC_UNUSED) {
        n++;
        klog("[boot] proc name=");
        klog(table[i]->name);
        klog(" pid=");
        serial_print_uint((uint32_t)table[i]->pid);
        klog(" user=");
        serial_print_uint((uint32_t)table[i]->is_user);
        klog("\n");
      }
    }
    klog_uint("[boot] ready processes=", (uint32_t)n);
  }

  klog("[boot] scheduler_start\n");
  scheduler_start();
}
