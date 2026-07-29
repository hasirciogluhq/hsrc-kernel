#include <drivers/console/serial.h>
#include <drivers/display/display.h>
#include <drivers/display/gpu.h>
#include <drivers/display/gpu_cmd.h>
#include <drivers/driver.h>
#include <kernel/disp_api.h>
#include <kernel/heap.h>
#include <kernel/klock.h>
#include <kernel/mm.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/types.h>
#include <kernel/vmm.h>
#include <user/disp.h>

/*
 * display.kmod — Reed resource orchestrator + command translator.
 *
 * Reed DISP_CMD_* → gpu_cmd_* (resolved views) → GpuProvider gpu_submit.
 * Renderer backend lives in the provider (virtio: VirGL or CPU rast).
 * display.kmod does not rasterize.
 */

#define DISP_MAX_BUF 256
#define DISP_MAX_TEX 64
#define DISP_MAX_RT 8
#define DISP_MAX_FENCE 16
#define DISP_MAX_EXPORT 64

#define DISP_MAX_TEX_DIM 4096u
#define DISP_MAX_BUF_BYTES (16u * 1024u * 1024u)

typedef struct disp_buf {
  int used;
  uint16_t gen;
  uint32_t owner_pid;
  uint32_t kind;
  uint32_t size;
  uint8_t *data;
  int mapped;
} disp_buf_t;

typedef struct disp_tex {
  int used;
  uint16_t gen;
  uint32_t owner_pid;
  uint32_t format;
  uint32_t width;
  uint32_t height;
  uint32_t levels;
  uint32_t stride; /* bytes */
  uint32_t nbytes;
  uint8_t *data;
  int mapped;
} disp_tex_t;

typedef struct disp_rt {
  int used;
  uint16_t gen;
  uint32_t owner_pid;
  uint32_t color_tex; /* packed handle */
  uint32_t depth_tex;
} disp_rt_t;

typedef struct disp_fence {
  int used;
  uint16_t gen;
  uint32_t owner_pid;
  uint32_t state; /* DISP_FENCE_* */
} disp_fence_t;

typedef struct disp_export_slot {
  int used;
  uint32_t token;
  uint32_t owner_pid;
  uint32_t kind;   /* 1=buf 2=tex 3=rt */
  uint32_t handle; /* packed local handle of exporter */
} disp_export_slot_t;

static disp_buf_t g_bufs[DISP_MAX_BUF];
static disp_tex_t g_texs[DISP_MAX_TEX];
static disp_rt_t g_rts[DISP_MAX_RT];
static disp_fence_t g_fences[DISP_MAX_FENCE];
static disp_export_slot_t g_exports[DISP_MAX_EXPORT];
static uint32_t g_export_seq = 1;

static uint32_t pack_handle(uint16_t idx, uint16_t gen) {
  return ((uint32_t)gen << 16) | (uint32_t)idx;
}

static uint16_t handle_idx(uint32_t h) { return (uint16_t)(h & 0xffffu); }

static uint16_t handle_gen(uint32_t h) { return (uint16_t)(h >> 16); }

static uint32_t fmt_bpp(uint32_t format) {
  switch (format) {
  case DISP_FMT_R8:
  case DISP_FMT_A8:
    return 1;
  case DISP_FMT_RG8:
    return 2;
  case DISP_FMT_RGBA8:
    return 4;
  default:
    return 0;
  }
}

static uint32_t pid_count_bufs(uint32_t pid) {
  uint32_t n = 0;
  for (int i = 0; i < DISP_MAX_BUF; i++)
    if (g_bufs[i].used && g_bufs[i].owner_pid == pid)
      n++;
  return n;
}

static uint32_t pid_count_texs(uint32_t pid) {
  uint32_t n = 0;
  for (int i = 0; i < DISP_MAX_TEX; i++)
    if (g_texs[i].used && g_texs[i].owner_pid == pid)
      n++;
  return n;
}

static uint32_t pid_count_rts(uint32_t pid) {
  uint32_t n = 0;
  for (int i = 0; i < DISP_MAX_RT; i++)
    if (g_rts[i].used && g_rts[i].owner_pid == pid)
      n++;
  return n;
}

static uint32_t pid_count_fences(uint32_t pid) {
  uint32_t n = 0;
  for (int i = 0; i < DISP_MAX_FENCE; i++)
    if (g_fences[i].used && g_fences[i].owner_pid == pid)
      n++;
  return n;
}

static disp_buf_t *buf_lookup(uint32_t handle, uint32_t pid) {
  uint16_t idx = handle_idx(handle);
  uint16_t gen = handle_gen(handle);
  disp_buf_t *b;

  if (idx >= DISP_MAX_BUF || gen == 0)
    return NULL;
  b = &g_bufs[idx];
  if (!b->used || b->gen != gen)
    return NULL;
  if (b->owner_pid != pid)
    return NULL;
  return b;
}

static disp_tex_t *tex_lookup(uint32_t handle, uint32_t pid) {
  uint16_t idx = handle_idx(handle);
  uint16_t gen = handle_gen(handle);
  disp_tex_t *t;

  if (idx >= DISP_MAX_TEX || gen == 0)
    return NULL;
  t = &g_texs[idx];
  if (!t->used || t->gen != gen)
    return NULL;
  if (t->owner_pid != pid)
    return NULL;
  return t;
}

static disp_rt_t *rt_lookup(uint32_t handle, uint32_t pid) {
  uint16_t idx = handle_idx(handle);
  uint16_t gen = handle_gen(handle);
  disp_rt_t *r;

  if (idx >= DISP_MAX_RT || gen == 0)
    return NULL;
  r = &g_rts[idx];
  if (!r->used || r->gen != gen)
    return NULL;
  if (r->owner_pid != pid)
    return NULL;
  return r;
}

static disp_fence_t *fence_lookup(uint32_t handle, uint32_t pid) {
  uint16_t idx = handle_idx(handle);
  uint16_t gen = handle_gen(handle);
  disp_fence_t *f;

  if (idx >= DISP_MAX_FENCE || gen == 0)
    return NULL;
  f = &g_fences[idx];
  if (!f->used || f->gen != gen)
    return NULL;
  if (f->owner_pid != pid)
    return NULL;
  return f;
}

static void buf_free_slot(disp_buf_t *b) {
  if (!b || !b->used)
    return;
  if (b->data)
    kfree(b->data);
  b->data = NULL;
  b->used = 0;
  b->mapped = 0;
  b->gen++;
  if (b->gen == 0)
    b->gen = 1;
}

static int tex_data_refs(const void *data) {
  int n = 0;
  if (!data)
    return 0;
  for (int i = 0; i < DISP_MAX_TEX; i++) {
    if (g_texs[i].used && g_texs[i].data == data)
      n++;
  }
  return n;
}

static void tex_free_slot(disp_tex_t *t) {
  void *data;
  if (!t || !t->used)
    return;
  data = t->data;
  t->data = NULL;
  t->used = 0;
  t->mapped = 0;
  t->gen++;
  if (t->gen == 0)
    t->gen = 1;
  /* Shared import aliases: free backing only when last reference drops. */
  if (data && tex_data_refs(data) == 0)
    kfree(data);
}

static void rt_free_slot(disp_rt_t *r) {
  if (!r || !r->used)
    return;
  r->used = 0;
  r->color_tex = 0;
  r->depth_tex = 0;
  r->gen++;
  if (r->gen == 0)
    r->gen = 1;
}

static void fence_free_slot(disp_fence_t *f) {
  if (!f || !f->used)
    return;
  f->used = 0;
  f->state = DISP_FENCE_CONSUMED;
  f->gen++;
  if (f->gen == 0)
    f->gen = 1;
}

static long op_info(disp_info *out) {
  gpu_provider_ops_t *gpu = gpu_provider_active();
  display_mode_t mode;

  if (!out)
    return -1;
  memset(out, 0, sizeof(*out));
  if (!gpu || gpu->get_mode(gpu, &mode) < 0) {
    /* Fall back to legacy display_ops if GpuProvider not yet up. */
    display_ops_t *d = display_active();
    if (!d || d->get_mode(&mode) < 0)
      return -1;
    out->caps = GPU_CAP_SCANOUT;
    out->hw_accel_available = 0;
  } else {
    out->caps = gpu->caps;
    out->hw_accel_available = gpu->gpu_submit ? 1u : 0u;
  }
  out->width = mode.width;
  out->height = mode.height;
  out->bpp = mode.bpp;
  out->max_texture_size = DISP_MAX_TEX_DIM;
  out->max_draw_calls_per_frame = DISP_MAX_DRAW_PER_FRAME;
  return 0;
}

static long op_buffer_create(disp_buffer_create *a, uint32_t pid) {
  int free_i = -1;
  disp_buf_t *b;
  uint8_t *mem;

  if (!a || a->size == 0 || a->size > DISP_MAX_BUF_BYTES)
    return -1;
  if (a->kind < DISP_BUF_VERTEX || a->kind > DISP_BUF_COLOR)
    return -1;
  if (pid_count_bufs(pid) >= DISP_MAX_BUF)
    return -1;

  for (int i = 0; i < DISP_MAX_BUF; i++) {
    if (!g_bufs[i].used) {
      free_i = i;
      break;
    }
  }
  if (free_i < 0)
    return -1;

  mem = (uint8_t *)kmalloc_aligned(a->size, 16);
  if (!mem)
    return -1;
  memset(mem, 0, a->size);

  b = &g_bufs[free_i];
  if (b->gen == 0)
    b->gen = 1;
  b->used = 1;
  b->owner_pid = pid;
  b->kind = a->kind;
  b->size = a->size;
  b->data = mem;
  b->mapped = 0;
  a->handle = pack_handle((uint16_t)free_i, b->gen);
  return 0;
}

static long op_buffer_destroy(disp_handle_arg *a, uint32_t pid) {
  disp_buf_t *b;
  if (!a)
    return -1;
  b = buf_lookup(a->handle, pid);
  if (!b)
    return -1;
  buf_free_slot(b);
  return 0;
}

static int map_disp_buf_for_caller(void *ptr, size_t len) {
  process_t *p = process_leader(process_current());
  if (!p || !p->as || !ptr || len == 0)
    return -1;
  return vmm_map_buf_user(p->as, ptr, len);
}

static long op_buffer_map(disp_buffer_map *a, uint32_t pid) {
  disp_buf_t *b;
  if (!a)
    return -1;
  b = buf_lookup(a->handle, pid);
  if (!b || !b->data)
    return -1;
  if (map_disp_buf_for_caller(b->data, b->size) < 0)
    return -1;
  b->mapped = 1;
  a->ptr = b->data;
  a->size = b->size;
  return 0;
}

static long op_buffer_unmap(disp_handle_arg *a, uint32_t pid) {
  disp_buf_t *b;
  if (!a)
    return -1;
  b = buf_lookup(a->handle, pid);
  if (!b)
    return -1;
  b->mapped = 0;
  return 0;
}

static long op_buffer_update(disp_buffer_update *a, uint32_t pid) {
  disp_buf_t *b;
  if (!a || !a->data)
    return -1;
  b = buf_lookup(a->handle, pid);
  if (!b || !b->data)
    return -1;
  if ((uint64_t)a->offset + (uint64_t)a->len > (uint64_t)b->size)
    return -1;
  /* Caller (syscall layer) already copied into kernel arg; data is kptr. */
  memcpy(b->data + a->offset, a->data, a->len);
  return 0;
}

static long op_texture_create(disp_texture_create *a, uint32_t pid) {
  int free_i = -1;
  disp_tex_t *t;
  uint32_t bpp, stride, nbytes;
  uint8_t *mem;

  if (!a || a->width == 0 || a->height == 0)
    return -1;
  if (a->width > DISP_MAX_TEX_DIM || a->height > DISP_MAX_TEX_DIM)
    return -1;
  if (a->levels == 0)
    a->levels = 1;
  bpp = fmt_bpp(a->format);
  if (bpp == 0)
    return -1;
  if (pid_count_texs(pid) >= DISP_MAX_TEX)
    return -1;

  for (int i = 0; i < DISP_MAX_TEX; i++) {
    if (!g_texs[i].used) {
      free_i = i;
      break;
    }
  }
  if (free_i < 0)
    return -1;

  stride = a->width * bpp;
  nbytes = stride * a->height; /* level 0 only for v1 */
  if (nbytes == 0 || nbytes > DISP_MAX_BUF_BYTES)
    return -1;

  /* Page-align so virtio-gpu ATTACH_BACKING / PRESENT works (splash path). */
  mem = (uint8_t *)kmalloc_aligned(nbytes, 4096);
  if (!mem)
    return -1;
  memset(mem, 0, nbytes);

  t = &g_texs[free_i];
  if (t->gen == 0)
    t->gen = 1;
  t->used = 1;
  t->owner_pid = pid;
  t->format = a->format;
  t->width = a->width;
  t->height = a->height;
  t->levels = a->levels;
  t->stride = stride;
  t->nbytes = nbytes;
  t->data = mem;
  t->mapped = 0;
  a->handle = pack_handle((uint16_t)free_i, t->gen);
  return 0;
}

static long op_texture_destroy(disp_handle_arg *a, uint32_t pid) {
  disp_tex_t *t;
  if (!a)
    return -1;
  t = tex_lookup(a->handle, pid);
  if (!t)
    return -1;
  tex_free_slot(t);
  return 0;
}

static long op_texture_upload(disp_texture_upload *a, uint32_t pid) {
  disp_tex_t *t;
  if (!a || !a->data)
    return -1;
  t = tex_lookup(a->handle, pid);
  if (!t || !t->data)
    return -1;
  if (a->level != 0)
    return -1; /* mip upload v1.1 */
  if (a->len != t->nbytes)
    return -1; /* exact level-0 size; no partial */
  memcpy(t->data, a->data, a->len);
  return 0;
}

static long op_texture_map(disp_texture_map *a, uint32_t pid) {
  disp_tex_t *t;
  if (!a)
    return -1;
  t = tex_lookup(a->handle, pid);
  if (!t || !t->data)
    return -1;
  if (map_disp_buf_for_caller(t->data, t->nbytes) < 0)
    return -1;
  t->mapped = 1;
  a->ptr = t->data;
  a->width = t->width;
  a->height = t->height;
  a->stride = t->stride;
  a->format = t->format;
  return 0;
}

static long op_rt_create(disp_rt_create *a, uint32_t pid) {
  int free_i = -1;
  disp_tex_t *color;
  disp_rt_t *r;

  if (!a)
    return -1;
  color = tex_lookup(a->color_tex, pid);
  if (!color || color->format != DISP_FMT_RGBA8)
    return -1;
  if (a->depth_tex) {
    if (!tex_lookup(a->depth_tex, pid))
      return -1;
  }
  if (pid_count_rts(pid) >= DISP_MAX_RT)
    return -1;

  for (int i = 0; i < DISP_MAX_RT; i++) {
    if (!g_rts[i].used) {
      free_i = i;
      break;
    }
  }
  if (free_i < 0)
    return -1;

  r = &g_rts[free_i];
  if (r->gen == 0)
    r->gen = 1;
  r->used = 1;
  r->owner_pid = pid;
  r->color_tex = a->color_tex;
  r->depth_tex = a->depth_tex;
  a->handle = pack_handle((uint16_t)free_i, r->gen);
  return 0;
}

static long op_rt_destroy(disp_handle_arg *a, uint32_t pid) {
  disp_rt_t *r;
  if (!a)
    return -1;
  r = rt_lookup(a->handle, pid);
  if (!r)
    return -1;
  rt_free_slot(r);
  return 0;
}

static long op_fence_create(disp_fence_create *a, uint32_t pid) {
  int free_i = -1;
  disp_fence_t *f;

  if (!a)
    return -1;
  if (pid_count_fences(pid) >= DISP_MAX_FENCE)
    return -1;
  for (int i = 0; i < DISP_MAX_FENCE; i++) {
    if (!g_fences[i].used) {
      free_i = i;
      break;
    }
  }
  if (free_i < 0)
    return -1;

  f = &g_fences[free_i];
  if (f->gen == 0)
    f->gen = 1;
  f->used = 1;
  f->owner_pid = pid;
  f->state = DISP_FENCE_UNSIGNALED;
  a->handle = pack_handle((uint16_t)free_i, f->gen);
  return 0;
}

static long op_fence_destroy(disp_handle_arg *a, uint32_t pid) {
  disp_fence_t *f;
  if (!a)
    return -1;
  f = fence_lookup(a->handle, pid);
  if (!f)
    return -1;
  fence_free_slot(f);
  return 0;
}

static long op_fence_signal(disp_handle_arg *a, uint32_t pid) {
  disp_fence_t *f;
  if (!a)
    return -1;
  f = fence_lookup(a->handle, pid);
  if (!f)
    return -1;
  if (f->state != DISP_FENCE_UNSIGNALED)
    return -1; /* double-signal */
  f->state = DISP_FENCE_SIGNALED;
  return 0;
}

static long op_fence_wait(disp_fence_wait *a, uint32_t pid) {
  disp_fence_t *f;
  if (!a)
    return -1;
  f = fence_lookup(a->handle, pid);
  if (!f)
    return -1;
  if (f->state == DISP_FENCE_CONSUMED)
    return -1; /* double-wait */
  if (f->state == DISP_FENCE_UNSIGNALED) {
    if (a->timeout_ticks == 0)
      return -1; /* would block */
    /* Software path: signal is CPU-side; unsignaled forever = fail. */
    return -1;
  }
  f->state = DISP_FENCE_CONSUMED;
  return 0;
}

static const uint32_t *scanout_pixels(uint32_t handle, uint32_t pid,
                                      uint32_t *out_w, uint32_t *out_h,
                                      uint32_t *out_stride_px) {
  disp_buf_t *b;
  disp_tex_t *t;
  disp_rt_t *r;

  b = buf_lookup(handle, pid);
  if (b && b->kind == DISP_BUF_COLOR && b->data) {
    /* COLOR buffers for scanout require implicit screen-sized layout;
     * Reed passes texture/RT handles preferred. */
    return NULL;
  }

  t = tex_lookup(handle, pid);
  if (t && t->format == DISP_FMT_RGBA8 && t->data) {
    *out_w = t->width;
    *out_h = t->height;
    *out_stride_px = t->stride / 4u;
    return (const uint32_t *)t->data;
  }

  r = rt_lookup(handle, pid);
  if (r) {
    t = tex_lookup(r->color_tex, pid);
    if (t && t->format == DISP_FMT_RGBA8 && t->data) {
      *out_w = t->width;
      *out_h = t->height;
      *out_stride_px = t->stride / 4u;
      return (const uint32_t *)t->data;
    }
  }

  /* COLOR buffer: size must be width*height*4 matching screen or explicit. */
  if (b && b->kind == DISP_BUF_COLOR && b->data) {
    display_mode_t mode;
    gpu_provider_ops_t *gpu = gpu_provider_active();
    if (gpu && gpu->get_mode(gpu, &mode) == 0) {
      uint32_t need = mode.width * mode.height * 4u;
      if (b->size >= need) {
        *out_w = mode.width;
        *out_h = mode.height;
        *out_stride_px = mode.width;
        return (const uint32_t *)b->data;
      }
    }
  }
  return NULL;
}

static long op_scanout(disp_scanout *a, uint32_t pid) {
  gpu_provider_ops_t *gpu;
  display_ops_t *disp;
  const uint32_t *src;
  uint32_t sw, sh, stride;

  if (!a)
    return -1;
  src = scanout_pixels(a->handle, pid, &sw, &sh, &stride);
  if (!src)
    return -1;

  gpu = gpu_provider_active();
  if (gpu && gpu->gpu_submit) {
    /* Prefer ring path: ready buffer → GPU_CMD_PRESENT → device. */
    gpu_cmd_present_t p;
    memset(&p, 0, sizeof(p));
    p.hdr.op = GPU_CMD_PRESENT;
    p.hdr.size = (uint16_t)sizeof(p);
    p.color.data = (void *)src;
    p.color.width = sw;
    p.color.height = sh;
    p.color.stride = stride * 4u;
    p.color.format = DISP_FMT_RGBA8;
    p.x = a->x;
    p.y = a->y;
    p.w = a->w;
    p.h = a->h;
    return gpu->gpu_submit(gpu, &p, sizeof(p));
  }
  if (gpu) {
    if (a->w == 0 || a->h == 0)
      return gpu->present(gpu, src, stride);
    if (gpu->present_rect)
      return gpu->present_rect(gpu, src, stride, a->x, a->y, a->w, a->h);
    return gpu->present(gpu, src, stride);
  }

  disp = display_active();
  if (!disp)
    return -1;
  if (a->w == 0 || a->h == 0)
    return disp->present(src, stride);
  if (disp->present_rect)
    return disp->present_rect(src, stride, a->x, a->y, a->w, a->h);
  return disp->present(src, stride);
}

static long op_export(disp_export *a, uint32_t pid) {
  int free_i = -1;
  uint32_t kind = 0;

  if (!a)
    return -1;
  if (buf_lookup(a->handle, pid))
    kind = 1;
  else if (tex_lookup(a->handle, pid))
    kind = 2;
  else if (rt_lookup(a->handle, pid))
    kind = 3;
  else
    return -1;

  for (int i = 0; i < DISP_MAX_EXPORT; i++) {
    if (!g_exports[i].used) {
      free_i = i;
      break;
    }
  }
  if (free_i < 0)
    return -1;

  g_exports[free_i].used = 1;
  g_exports[free_i].token = g_export_seq++;
  if (g_export_seq == 0)
    g_export_seq = 1;
  g_exports[free_i].owner_pid = pid;
  g_exports[free_i].kind = kind;
  g_exports[free_i].handle = a->handle;
  a->token = g_exports[free_i].token;
  return 0;
}

static long op_import(disp_import *a, uint32_t pid) {
  disp_export_slot_t *ex = NULL;
  /* Import grants the importer a local alias handle to the same object
   * by transferring ownership metadata — for v1 we duplicate tex/buf slots
   * by sharing the underlying pointer (refcount deferred; single-consumer). */
  if (!a || a->token == 0)
    return -1;

  for (int i = 0; i < DISP_MAX_EXPORT; i++) {
    if (g_exports[i].used && g_exports[i].token == a->token) {
      ex = &g_exports[i];
      break;
    }
  }
  if (!ex)
    return -1;

  if (ex->kind == 2) {
    disp_tex_t *src = tex_lookup(ex->handle, ex->owner_pid);
    int free_i = -1;
    disp_tex_t *t;
    if (!src)
      return -1;
    if (pid_count_texs(pid) >= DISP_MAX_TEX)
      return -1;
    for (int i = 0; i < DISP_MAX_TEX; i++) {
      if (!g_texs[i].used) {
        free_i = i;
        break;
      }
    }
    if (free_i < 0)
      return -1;
    t = &g_texs[free_i];
    if (t->gen == 0)
      t->gen = 1;
    *t = *src;
    t->owner_pid = pid;
    t->mapped = 0;
    /* Share backing with exporter — do NOT steal/free the source slot. */
    ex->used = 0;
    a->handle = pack_handle((uint16_t)free_i, t->gen);
    return 0;
  }

  if (ex->kind == 1) {
    disp_buf_t *src = buf_lookup(ex->handle, ex->owner_pid);
    int free_i = -1;
    disp_buf_t *b;
    if (!src)
      return -1;
    for (int i = 0; i < DISP_MAX_BUF; i++) {
      if (!g_bufs[i].used) {
        free_i = i;
        break;
      }
    }
    if (free_i < 0)
      return -1;
    b = &g_bufs[free_i];
    if (b->gen == 0)
      b->gen = 1;
    *b = *src;
    b->owner_pid = pid;
    b->mapped = 0;
    src->data = NULL;
    buf_free_slot(src);
    ex->used = 0;
    a->handle = pack_handle((uint16_t)free_i, b->gen);
    return 0;
  }

  return -1;
}

static long op_stats(disp_stats *a, uint32_t pid) {
  uint32_t bytes = 0;
  if (!a)
    return -1;
  memset(a, 0, sizeof(*a));
  a->buffers = pid_count_bufs(pid);
  a->textures = pid_count_texs(pid);
  a->render_targets = pid_count_rts(pid);
  a->fences = pid_count_fences(pid);
  for (int i = 0; i < DISP_MAX_BUF; i++)
    if (g_bufs[i].used && g_bufs[i].owner_pid == pid)
      bytes += g_bufs[i].size;
  for (int i = 0; i < DISP_MAX_TEX; i++)
    if (g_texs[i].used && g_texs[i].owner_pid == pid)
      bytes += g_texs[i].nbytes;
  a->bytes_live = bytes;
  return 0;
}

/* Scratch for DISP_CMD_* → gpu_cmd_* under klock_disp (single-threaded). */
static uint8_t g_gpu_cmd_scratch[GPU_CMD_MAX_BYTES];

static int tex_view_from_handle(uint32_t handle, uint32_t pid,
                                gpu_tex_view_t *out) {
  disp_tex_t *t = tex_lookup(handle, pid);
  if (!t || !t->data || !out)
    return -1;
  out->data = t->data;
  out->width = t->width;
  out->height = t->height;
  out->stride = t->stride;
  out->format = t->format;
  return 0;
}

static int rt_color_view(uint32_t rt_handle, uint32_t pid,
                         gpu_tex_view_t *out) {
  disp_rt_t *r = rt_lookup(rt_handle, pid);
  if (!r || !r->color_tex)
    return -1;
  return tex_view_from_handle(r->color_tex, pid, out);
}

static int buf_view_from_handle(uint32_t handle, uint32_t pid,
                                gpu_buf_view_t *out) {
  disp_buf_t *b = buf_lookup(handle, pid);
  if (!b || !b->data || !out)
    return -1;
  out->data = b->data;
  out->size = b->size;
  return 0;
}

static int emit_gpu(uint8_t *dst, uint32_t *off, uint32_t cap, const void *pkt,
                    uint32_t sz) {
  uint32_t n = (sz + 3u) & ~3u;
  if (!pkt || sz < sizeof(gpu_cmd_hdr_t) || *off + n > cap)
    return -1;
  memcpy(dst + *off, pkt, sz);
  if (n > sz)
    memset(dst + *off + sz, 0, n - sz);
  *off += n;
  return 0;
}

/*
 * Translate Reed DISP_CMD_* (handles) → gpu_cmd_* (ready buffers).
 * No pixel work — only resolve + repack for the provider.
 */
static long reed_to_gpu_cmds(const void *cmds, uint32_t size, uint32_t pid,
                             void *out, uint32_t out_cap, uint32_t *out_size) {
  const uint8_t *p = (const uint8_t *)cmds;
  const uint8_t *end;
  uint32_t off = 0;

  if (!cmds || !out || !out_size || size < sizeof(disp_cmd_hdr))
    return -1;
  end = p + size;
  *out_size = 0;

  while (p + sizeof(disp_cmd_hdr) <= end) {
    const disp_cmd_hdr *h = (const disp_cmd_hdr *)p;
    uint32_t psz;

    if (h->size < sizeof(disp_cmd_hdr) || (h->size & 3u))
      return -1;
    psz = h->size;
    if (p + psz > end)
      return -1;

    switch (h->op) {
    case DISP_CMD_BIND_PIPELINE: {
      const disp_cmd_bind_pipeline *c = (const disp_cmd_bind_pipeline *)p;
      gpu_cmd_bind_pipeline_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_BIND_PIPELINE;
      g.hdr.size = (uint16_t)sizeof(g);
      g.topology = c->topology;
      g.cull = c->cull;
      g.blend = c->blend;
      g.shade = c->shade;
      g.depth_test = c->depth_test;
      g.depth_write = c->depth_write;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_BIND_VB: {
      const disp_cmd_bind_handle *c = (const disp_cmd_bind_handle *)p;
      gpu_cmd_bind_vb_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_BIND_VB;
      g.hdr.size = (uint16_t)sizeof(g);
      if (buf_view_from_handle(c->handle, pid, &g.vb) < 0)
        return -1;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_BIND_IB: {
      const disp_cmd_bind_handle *c = (const disp_cmd_bind_handle *)p;
      gpu_cmd_bind_ib_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_BIND_IB;
      g.hdr.size = (uint16_t)sizeof(g);
      if (buf_view_from_handle(c->handle, pid, &g.ib) < 0)
        return -1;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_BIND_TEX: {
      const disp_cmd_bind_tex *c = (const disp_cmd_bind_tex *)p;
      gpu_cmd_bind_tex_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_BIND_TEX;
      g.hdr.size = (uint16_t)sizeof(g);
      g.slot = c->slot;
      g.wrap = c->wrap;
      g.filter = c->filter;
      if (tex_view_from_handle(c->handle, pid, &g.tex) < 0)
        return -1;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_BIND_RT: {
      const disp_cmd_bind_handle *c = (const disp_cmd_bind_handle *)p;
      gpu_cmd_bind_rt_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_BIND_RT;
      g.hdr.size = (uint16_t)sizeof(g);
      if (rt_color_view(c->handle, pid, &g.color) < 0)
        return -1;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_SET_UNIFORM: {
      const disp_cmd_set_uniform *c = (const disp_cmd_set_uniform *)p;
      gpu_cmd_set_uniform_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_SET_UNIFORM;
      g.hdr.size = (uint16_t)sizeof(g);
      memcpy(&g.u, &c->u,
             sizeof(g.u) < sizeof(c->u) ? sizeof(g.u) : sizeof(c->u));
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_SET_VIEWPORT: {
      const disp_cmd_set_viewport *c = (const disp_cmd_set_viewport *)p;
      gpu_cmd_set_viewport_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_SET_VIEWPORT;
      g.hdr.size = (uint16_t)sizeof(g);
      g.x = c->x;
      g.y = c->y;
      g.w = c->w;
      g.h = c->h;
      g.min_depth = c->min_depth;
      g.max_depth = c->max_depth;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_SET_SCISSOR: {
      const disp_cmd_set_scissor *c = (const disp_cmd_set_scissor *)p;
      gpu_cmd_set_scissor_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_SET_SCISSOR;
      g.hdr.size = (uint16_t)sizeof(g);
      g.x = c->x;
      g.y = c->y;
      g.w = c->w;
      g.h = c->h;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_CLEAR: {
      const disp_cmd_clear *c = (const disp_cmd_clear *)p;
      gpu_cmd_clear_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_CLEAR;
      g.hdr.size = (uint16_t)sizeof(g);
      g.color_rgba = c->color_rgba;
      g.depth = c->depth;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_DRAW: {
      const disp_cmd_draw *c = (const disp_cmd_draw *)p;
      gpu_cmd_draw_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_DRAW;
      g.hdr.size = (uint16_t)sizeof(g);
      g.count = c->count;
      g.first = c->first;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_DRAW_INDEXED: {
      const disp_cmd_draw_indexed *c = (const disp_cmd_draw_indexed *)p;
      gpu_cmd_draw_indexed_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_DRAW_INDEXED;
      g.hdr.size = (uint16_t)sizeof(g);
      g.count = c->count;
      g.first_index = c->first_index;
      g.base_vertex = c->base_vertex;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    case DISP_CMD_BLIT: {
      const disp_cmd_blit *c = (const disp_cmd_blit *)p;
      gpu_cmd_blit_t g;
      if (psz < sizeof(*c))
        return -1;
      memset(&g, 0, sizeof(g));
      g.hdr.op = GPU_CMD_BLIT;
      g.hdr.size = (uint16_t)sizeof(g);
      if (tex_view_from_handle(c->src_tex, pid, &g.src) < 0)
        return -1;
      if (rt_color_view(c->dst_rt, pid, &g.dst) < 0)
        return -1;
      g.dst_x = c->dst_x;
      g.dst_y = c->dst_y;
      g.src_x = c->src_x;
      g.src_y = c->src_y;
      g.src_w = c->src_w;
      g.src_h = c->src_h;
      g.blend = c->blend;
      if (emit_gpu(out, &off, out_cap, &g, sizeof(g)) < 0)
        return -1;
      break;
    }
    default:
      return -1;
    }
    p += psz;
  }

  *out_size = off;
  return 0;
}

static long op_submit(disp_submit *a, uint32_t pid) {
  gpu_provider_ops_t *gpu;
  uint32_t gpu_size = 0;
  long rc;

  if (!a || !a->cmds || a->size == 0 || a->size > DISP_MAX_SUBMIT_BYTES)
    return -1;

  gpu = gpu_provider_active();
  if (!gpu || !gpu->gpu_submit)
    return -1;

  if (reed_to_gpu_cmds(a->cmds, a->size, pid, g_gpu_cmd_scratch,
                       sizeof(g_gpu_cmd_scratch), &gpu_size) < 0)
    return -1;
  if (gpu_size == 0)
    return 0;

  /* Always forward to provider — renderer backend is inside virtio/BGA. */
  rc = gpu->gpu_submit(gpu, g_gpu_cmd_scratch, gpu_size);
  if (rc < 0)
    return rc;
  if (a->fence) {
    disp_handle_arg f;
    f.handle = a->fence;
    (void)op_fence_signal(&f, pid);
  }
  return 0;
}

static long disp_call(uint32_t op, void *arg, uint32_t owner_pid) {
  switch (op) {
  case DISP_OP_INFO:
    return op_info((disp_info *)arg);
  case DISP_OP_BUFFER_CREATE:
    return op_buffer_create((disp_buffer_create *)arg, owner_pid);
  case DISP_OP_BUFFER_DESTROY:
    return op_buffer_destroy((disp_handle_arg *)arg, owner_pid);
  case DISP_OP_BUFFER_MAP:
    return op_buffer_map((disp_buffer_map *)arg, owner_pid);
  case DISP_OP_BUFFER_UNMAP:
    return op_buffer_unmap((disp_handle_arg *)arg, owner_pid);
  case DISP_OP_BUFFER_UPDATE:
    return op_buffer_update((disp_buffer_update *)arg, owner_pid);
  case DISP_OP_TEXTURE_CREATE:
    return op_texture_create((disp_texture_create *)arg, owner_pid);
  case DISP_OP_TEXTURE_DESTROY:
    return op_texture_destroy((disp_handle_arg *)arg, owner_pid);
  case DISP_OP_TEXTURE_UPLOAD:
    return op_texture_upload((disp_texture_upload *)arg, owner_pid);
  case DISP_OP_TEXTURE_MAP:
    return op_texture_map((disp_texture_map *)arg, owner_pid);
  case DISP_OP_RT_CREATE:
    return op_rt_create((disp_rt_create *)arg, owner_pid);
  case DISP_OP_RT_DESTROY:
    return op_rt_destroy((disp_handle_arg *)arg, owner_pid);
  case DISP_OP_FENCE_CREATE:
    return op_fence_create((disp_fence_create *)arg, owner_pid);
  case DISP_OP_FENCE_DESTROY:
    return op_fence_destroy((disp_handle_arg *)arg, owner_pid);
  case DISP_OP_FENCE_WAIT:
    return op_fence_wait((disp_fence_wait *)arg, owner_pid);
  case DISP_OP_FENCE_SIGNAL:
    return op_fence_signal((disp_handle_arg *)arg, owner_pid);
  case DISP_OP_SCANOUT:
    return op_scanout((disp_scanout *)arg, owner_pid);
  case DISP_OP_EXPORT:
    return op_export((disp_export *)arg, owner_pid);
  case DISP_OP_IMPORT:
    return op_import((disp_import *)arg, owner_pid);
  case DISP_OP_STATS:
    return op_stats((disp_stats *)arg, owner_pid);
  case DISP_OP_SUBMIT:
    return op_submit((disp_submit *)arg, owner_pid);
  default:
    return -1;
  }
}

static void disp_cleanup_pid(uint32_t pid) {
  for (int i = 0; i < DISP_MAX_EXPORT; i++) {
    if (g_exports[i].used && g_exports[i].owner_pid == pid)
      g_exports[i].used = 0;
  }
  for (int i = 0; i < DISP_MAX_FENCE; i++) {
    if (g_fences[i].used && g_fences[i].owner_pid == pid)
      fence_free_slot(&g_fences[i]);
  }
  for (int i = 0; i < DISP_MAX_RT; i++) {
    if (g_rts[i].used && g_rts[i].owner_pid == pid)
      rt_free_slot(&g_rts[i]);
  }
  for (int i = 0; i < DISP_MAX_TEX; i++) {
    if (g_texs[i].used && g_texs[i].owner_pid == pid)
      tex_free_slot(&g_texs[i]);
  }
  for (int i = 0; i < DISP_MAX_BUF; i++) {
    if (g_bufs[i].used && g_bufs[i].owner_pid == pid)
      buf_free_slot(&g_bufs[i]);
  }
}

static const disp_api_t g_api = {
    .call = disp_call,
    .cleanup_pid = disp_cleanup_pid,
};

static int disp_drv_probe(driver_t *drv, void *ctx) {
  (void)drv;
  (void)ctx;
  return (gpu_provider_active() || display_active()) ? 0 : -1;
}

static int disp_drv_init(driver_t *drv, void *ctx) {
  (void)drv;
  (void)ctx;
  memset(g_bufs, 0, sizeof(g_bufs));
  memset(g_texs, 0, sizeof(g_texs));
  memset(g_rts, 0, sizeof(g_rts));
  memset(g_fences, 0, sizeof(g_fences));
  memset(g_exports, 0, sizeof(g_exports));
  disp_api_register(&g_api);
  klog("[display] ready (Reed SYS_DISP_*)\n");
  return 0;
}

int kmod_init(void) {
  driver_t d;
  memset(&d, 0, sizeof(d));
  strncpy(d.name, "display", DRIVER_NAME_MAX - 1);
  strncpy(d.version, "1.0", DRIVER_VERSION_MAX - 1);
  d.kind = DRIVER_KIND_CUSTOM;
  d.class = DRIVER_CLASS_GPU;
  d.flags = 0;
  d.priority = 40; /* after display_virtio (prio via register) */
  d.probe = disp_drv_probe;
  d.init = disp_drv_init;

  if (driver_register(&d) < 0)
    return -1;
  if (driver_load("display", NULL) < 0)
    return -1;
  return 0;
}
