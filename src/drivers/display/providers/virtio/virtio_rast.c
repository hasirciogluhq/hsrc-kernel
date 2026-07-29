#include "virtio_rast.h"
#include <drivers/display/gpu_cmd.h>
#include <kernel/string.h>
#include <user/disp.h>

/*
 * VirtIO-GPU renderer backend — CPU Rasterizer.
 *
 * Executes most gpu_cmd_* into guest RT/texture backing:
 * CLEAR, DRAW, DRAW_INDEXED (u32), BLIT, binds, scissor.
 * Uniform/viewport/depth are no-ops (2D/UI does not need them).
 * Present stays on the virtio ring (TRANSFER + FLUSH).
 */

#define VERT_STRIDE 36u

typedef struct {
  int32_t x, y, u_q16, v_q16;
  uint32_t color;
} vtx_t;

typedef struct {
  uint32_t topology, blend, shade;
  int has_rt, has_vb, has_ib, has_tex, has_scissor;
  gpu_tex_view_t rt, tex;
  gpu_buf_view_t vb, ib;
  int32_t sc_x, sc_y, sc_w, sc_h;
} st_t;

static int clampi(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static uint32_t blend_alpha(uint32_t src, uint32_t dst) {
  uint32_t a = (src >> 24) & 0xffu, ia, sr, sg, sb, dr, dg, db;
  if (a == 0)
    return dst;
  if (a == 255)
    return src;
  ia = 255u - a;
  sr = src & 0xffu;
  sg = (src >> 8) & 0xffu;
  sb = (src >> 16) & 0xffu;
  dr = dst & 0xffu;
  dg = (dst >> 8) & 0xffu;
  db = (dst >> 16) & 0xffu;
  return 0xff000000u | (((sb * a + db * ia) / 255u) << 16) |
         (((sg * a + dg * ia) / 255u) << 8) | ((sr * a + dr * ia) / 255u);
}

static void put_px(gpu_tex_view_t *rt, int x, int y, uint32_t c,
                   uint32_t blend) {
  uint32_t *row;
  if (!rt || !rt->data || x < 0 || y < 0 || (uint32_t)x >= rt->width ||
      (uint32_t)y >= rt->height)
    return;
  row = (uint32_t *)((uint8_t *)rt->data + (uint32_t)y * rt->stride);
  row[x] = (blend == 0) ? (c | 0xff000000u) : blend_alpha(c, row[x]);
}

static uint32_t sample_tex(const gpu_tex_view_t *tex, int32_t u_q16,
                           int32_t v_q16, uint32_t vert_color) {
  int32_t ui, vi;
  const uint8_t *p;
  uint32_t texel, ta, tr, tg, tb, va, vr, vg, vb;

  if (!tex || !tex->data || tex->width == 0 || tex->height == 0)
    return vert_color;
  if (u_q16 < 0)
    u_q16 = 0;
  if (v_q16 < 0)
    v_q16 = 0;
  ui = (int32_t)(((int64_t)u_q16 * (int32_t)tex->width) >> 16);
  vi = (int32_t)(((int64_t)v_q16 * (int32_t)tex->height) >> 16);
  if (ui >= (int32_t)tex->width)
    ui = (int32_t)tex->width - 1;
  if (vi >= (int32_t)tex->height)
    vi = (int32_t)tex->height - 1;
  p = (const uint8_t *)tex->data + (uint32_t)vi * tex->stride;
  if (tex->format == DISP_FMT_A8) {
    ta = p[ui];
    tr = vert_color & 0xffu;
    tg = (vert_color >> 8) & 0xffu;
    tb = (vert_color >> 16) & 0xffu;
    va = (vert_color >> 24) & 0xffu;
    ta = (ta * va) / 255u;
    return (ta << 24) | (tb << 16) | (tg << 8) | tr;
  }
  texel = *(const uint32_t *)(const void *)(p + (uint32_t)ui * 4u);
  tr = texel & 0xffu;
  tg = (texel >> 8) & 0xffu;
  tb = (texel >> 16) & 0xffu;
  ta = (texel >> 24) & 0xffu;
  vr = vert_color & 0xffu;
  vg = (vert_color >> 8) & 0xffu;
  vb = (vert_color >> 16) & 0xffu;
  va = (vert_color >> 24) & 0xffu;
  return (((ta * va) / 255u) << 24) | (((tb * vb) / 255u) << 16) |
         (((tg * vg) / 255u) << 8) | ((tr * vr) / 255u);
}

static int32_t f32_to_i32(uint32_t bits) {
  int32_t exp = (int32_t)((bits >> 23) & 0xff) - 127;
  uint32_t mant = (bits & 0x7fffffu) | 0x800000u;
  int32_t v;
  if ((bits & 0x7fffffffu) == 0 || exp < 0)
    return 0;
  v = (exp >= 30) ? 0x3fffffff : (int32_t)(mant >> (23 - exp));
  return (bits & 0x80000000u) ? -v : v;
}

static int32_t f32_to_q16(uint32_t bits) {
  int32_t exp = (int32_t)((bits >> 23) & 0xff) - 127;
  uint32_t mant = (bits & 0x7fffffu) | 0x800000u;
  int64_t v;
  if ((bits & 0x7fffffffu) == 0)
    return 0;
  if (exp >= 14)
    v = 65536;
  else if (exp >= -16)
    v = ((int64_t)mant << 16) >> (23 - exp);
  else
    v = 0;
  if (v > 65536)
    v = 65536;
  if (v < 0)
    v = 0;
  return (bits & 0x80000000u) ? (int32_t)(-v) : (int32_t)v;
}

static void load_vtx(const uint8_t *base, uint32_t first, uint32_t i,
                     vtx_t *o) {
  const uint8_t *p = base + (first + i) * VERT_STRIDE;
  uint32_t xb, yb, ub, vb, col;
  memcpy(&xb, p + 0, 4);
  memcpy(&yb, p + 4, 4);
  memcpy(&ub, p + 24, 4);
  memcpy(&vb, p + 28, 4);
  memcpy(&col, p + 32, 4);
  o->x = f32_to_i32(xb);
  o->y = f32_to_i32(yb);
  o->u_q16 = f32_to_q16(ub);
  o->v_q16 = f32_to_q16(vb);
  o->color = col;
}

static int64_t edge(int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t cx,
                    int32_t cy) {
  return (int64_t)(cx - ax) * (by - ay) - (int64_t)(cy - ay) * (bx - ax);
}

static void draw_tri(st_t *st, const vtx_t *a, const vtx_t *b, const vtx_t *c) {
  int32_t minx, maxx, miny, maxy, x, y, cl0x, cl0y, cl1x, cl1y;
  int64_t area, w0, w1, w2;
  /* shade 0=color, 1=textured; VertexLit/other → vertex color. */
  int textured = (st->shade == 1) && st->has_tex;

  area = edge(a->x, a->y, b->x, b->y, c->x, c->y);
  if (area == 0)
    return;
  minx = a->x < b->x ? a->x : b->x;
  if (c->x < minx)
    minx = c->x;
  maxx = a->x > b->x ? a->x : b->x;
  if (c->x > maxx)
    maxx = c->x;
  miny = a->y < b->y ? a->y : b->y;
  if (c->y < miny)
    miny = c->y;
  maxy = a->y > b->y ? a->y : b->y;
  if (c->y > maxy)
    maxy = c->y;
  cl0x = 0;
  cl0y = 0;
  cl1x = (int32_t)st->rt.width;
  cl1y = (int32_t)st->rt.height;
  if (st->has_scissor) {
    cl0x = clampi(st->sc_x, cl0x, cl1x);
    cl0y = clampi(st->sc_y, cl0y, cl1y);
    cl1x = clampi(st->sc_x + st->sc_w, cl0x, cl1x);
    cl1y = clampi(st->sc_y + st->sc_h, cl0y, cl1y);
  }
  if (minx < cl0x)
    minx = cl0x;
  if (miny < cl0y)
    miny = cl0y;
  if (maxx >= cl1x)
    maxx = cl1x - 1;
  if (maxy >= cl1y)
    maxy = cl1y - 1;
  if (minx > maxx || miny > maxy)
    return;
  for (y = miny; y <= maxy; y++) {
    for (x = minx; x <= maxx; x++) {
      uint32_t col;
      w0 = edge(b->x, b->y, c->x, c->y, x, y);
      w1 = edge(c->x, c->y, a->x, a->y, x, y);
      w2 = edge(a->x, a->y, b->x, b->y, x, y);
      if (area > 0) {
        if (w0 < 0 || w1 < 0 || w2 < 0)
          continue;
      } else if (w0 > 0 || w1 > 0 || w2 > 0) {
        continue;
      }
      if (textured) {
        int32_t uq =
            (int32_t)((w0 * a->u_q16 + w1 * b->u_q16 + w2 * c->u_q16) / area);
        int32_t vq =
            (int32_t)((w0 * a->v_q16 + w1 * b->v_q16 + w2 * c->v_q16) / area);
        col = sample_tex(&st->tex, uq, vq, a->color);
      } else {
        col = a->color;
      }
      put_px(&st->rt, x, y, col, st->blend);
    }
  }
}

static int do_clear(st_t *st, uint32_t color) {
  int32_t x0, y0, x1, y1, x, y;
  uint32_t *row;
  if (!st->has_rt || !st->rt.data)
    return -1;
  x0 = 0;
  y0 = 0;
  x1 = (int32_t)st->rt.width;
  y1 = (int32_t)st->rt.height;
  if (st->has_scissor) {
    x0 = clampi(st->sc_x, x0, x1);
    y0 = clampi(st->sc_y, y0, y1);
    x1 = clampi(st->sc_x + st->sc_w, x0, x1);
    y1 = clampi(st->sc_y + st->sc_h, y0, y1);
  }
  for (y = y0; y < y1; y++) {
    row = (uint32_t *)((uint8_t *)st->rt.data + (uint32_t)y * st->rt.stride);
    for (x = x0; x < x1; x++)
      row[x] = color;
  }
  return 0;
}

static int do_draw(st_t *st, uint32_t count, uint32_t first) {
  uint32_t i;
  const uint8_t *base;
  if (!st->has_rt || !st->has_vb || !st->vb.data)
    return -1;
  /* Triangles only; lit/blur shades fall back to vertex color / textured. */
  if (st->topology != 0)
    return 0;
  if (count < 3 || (count % 3u) != 0)
    return -1;
  if ((first + count) * VERT_STRIDE > st->vb.size)
    return -1;
  base = (const uint8_t *)st->vb.data;
  for (i = 0; i + 2 < count; i += 3) {
    vtx_t a, b, c;
    load_vtx(base, first, i, &a);
    load_vtx(base, first, i + 1, &b);
    load_vtx(base, first, i + 2, &c);
    draw_tri(st, &a, &b, &c);
  }
  return 0;
}

/* Kilim Index buffers are uint32_t (index_count * 4). */
static uint32_t read_index(const gpu_buf_view_t *ib, uint32_t i) {
  uint32_t v = 0;
  if (!ib || !ib->data || (i + 1u) * 4u > ib->size)
    return 0;
  memcpy(&v, (const uint8_t *)ib->data + i * 4u, 4);
  return v;
}

static int do_draw_indexed(st_t *st, uint32_t count, uint32_t first_index,
                           int32_t base_vertex) {
  uint32_t i;
  const uint8_t *base;
  if (!st->has_rt || !st->has_vb || !st->vb.data || !st->has_ib || !st->ib.data)
    return -1;
  if (st->topology != 0)
    return 0;
  if (count < 3 || (count % 3u) != 0)
    return -1;
  base = (const uint8_t *)st->vb.data;
  for (i = 0; i + 2 < count; i += 3) {
    vtx_t a, b, c;
    uint32_t ia = read_index(&st->ib, first_index + i);
    uint32_t i1 = read_index(&st->ib, first_index + i + 1);
    uint32_t i2 = read_index(&st->ib, first_index + i + 2);
    int32_t va = (int32_t)ia + base_vertex;
    int32_t vb = (int32_t)i1 + base_vertex;
    int32_t vc = (int32_t)i2 + base_vertex;
    if (va < 0 || vb < 0 || vc < 0)
      continue;
    if ((uint32_t)(va + 1) * VERT_STRIDE > st->vb.size ||
        (uint32_t)(vb + 1) * VERT_STRIDE > st->vb.size ||
        (uint32_t)(vc + 1) * VERT_STRIDE > st->vb.size)
      continue;
    load_vtx(base, 0, (uint32_t)va, &a);
    load_vtx(base, 0, (uint32_t)vb, &b);
    load_vtx(base, 0, (uint32_t)vc, &c);
    draw_tri(st, &a, &b, &c);
  }
  return 0;
}

static int do_blit(const gpu_cmd_blit_t *cmd) {
  int32_t dx, dy, sw, sh, sx, sy, x, y;
  if (!cmd->src.data || !cmd->dst.data)
    return -1;
  sx = cmd->src_x;
  sy = cmd->src_y;
  sw = cmd->src_w;
  sh = cmd->src_h;
  dx = cmd->dst_x;
  dy = cmd->dst_y;
  if (sw <= 0 || sh <= 0)
    return 0;
  if (sx < 0) {
    dx -= sx;
    sw += sx;
    sx = 0;
  }
  if (sy < 0) {
    dy -= sy;
    sh += sy;
    sy = 0;
  }
  if (sx + sw > (int32_t)cmd->src.width)
    sw = (int32_t)cmd->src.width - sx;
  if (sy + sh > (int32_t)cmd->src.height)
    sh = (int32_t)cmd->src.height - sy;
  if (dx < 0) {
    sx -= dx;
    sw += dx;
    dx = 0;
  }
  if (dy < 0) {
    sy -= dy;
    sh += dy;
    dy = 0;
  }
  if (dx + sw > (int32_t)cmd->dst.width)
    sw = (int32_t)cmd->dst.width - dx;
  if (dy + sh > (int32_t)cmd->dst.height)
    sh = (int32_t)cmd->dst.height - dy;
  if (sw <= 0 || sh <= 0)
    return 0;
  for (y = 0; y < sh; y++) {
    const uint32_t *srow =
        (const uint32_t *)((const uint8_t *)cmd->src.data +
                           (uint32_t)(sy + y) * cmd->src.stride);
    uint32_t *drow = (uint32_t *)((uint8_t *)cmd->dst.data +
                                  (uint32_t)(dy + y) * cmd->dst.stride);
    for (x = 0; x < sw; x++) {
      uint32_t s = srow[sx + x];
      drow[dx + x] =
          (cmd->blend == 0) ? (s | 0xff000000u) : blend_alpha(s, drow[dx + x]);
    }
  }
  return 0;
}

int virtio_rast_exec(const void *gpu_cmds, uint32_t size) {
  const uint8_t *p = (const uint8_t *)gpu_cmds;
  const uint8_t *end;
  st_t st;

  if (!gpu_cmds || size < sizeof(gpu_cmd_hdr_t))
    return -1;
  memset(&st, 0, sizeof(st));
  end = p + size;
  while (p + sizeof(gpu_cmd_hdr_t) <= end) {
    const gpu_cmd_hdr_t *h = (const gpu_cmd_hdr_t *)p;
    uint32_t psz;
    if (h->size < sizeof(gpu_cmd_hdr_t) || (h->size & 3u))
      return -1;
    psz = h->size;
    if (p + psz > end)
      return -1;
    switch (h->op) {
    case GPU_CMD_BIND_PIPELINE: {
      const gpu_cmd_bind_pipeline_t *c = (const gpu_cmd_bind_pipeline_t *)p;
      if (psz < sizeof(*c))
        return -1;
      st.topology = c->topology;
      st.blend = c->blend;
      st.shade = c->shade;
      break;
    }
    case GPU_CMD_BIND_VB: {
      const gpu_cmd_bind_vb_t *c = (const gpu_cmd_bind_vb_t *)p;
      if (psz < sizeof(*c))
        return -1;
      st.vb = c->vb;
      st.has_vb = c->vb.data ? 1 : 0;
      break;
    }
    case GPU_CMD_BIND_IB: {
      const gpu_cmd_bind_ib_t *c = (const gpu_cmd_bind_ib_t *)p;
      if (psz < sizeof(*c))
        return -1;
      st.ib = c->ib;
      st.has_ib = c->ib.data ? 1 : 0;
      break;
    }
    case GPU_CMD_BIND_TEX: {
      const gpu_cmd_bind_tex_t *c = (const gpu_cmd_bind_tex_t *)p;
      if (psz < sizeof(*c))
        return -1;
      if (c->slot == 0) {
        st.tex = c->tex;
        st.has_tex = c->tex.data ? 1 : 0;
      }
      break;
    }
    case GPU_CMD_BIND_RT: {
      const gpu_cmd_bind_rt_t *c = (const gpu_cmd_bind_rt_t *)p;
      if (psz < sizeof(*c))
        return -1;
      st.rt = c->color;
      st.has_rt = c->color.data ? 1 : 0;
      break;
    }
    case GPU_CMD_SET_UNIFORM:
    case GPU_CMD_SET_VIEWPORT:
      break;
    case GPU_CMD_SET_SCISSOR: {
      const gpu_cmd_set_scissor_t *c = (const gpu_cmd_set_scissor_t *)p;
      if (psz < sizeof(*c))
        return -1;
      st.sc_x = c->x;
      st.sc_y = c->y;
      st.sc_w = c->w;
      st.sc_h = c->h;
      st.has_scissor = (c->w > 0 && c->h > 0) ? 1 : 0;
      break;
    }
    case GPU_CMD_CLEAR: {
      const gpu_cmd_clear_t *c = (const gpu_cmd_clear_t *)p;
      if (psz < sizeof(*c))
        return -1;
      if (do_clear(&st, c->color_rgba) < 0)
        return -1;
      break;
    }
    case GPU_CMD_DRAW: {
      const gpu_cmd_draw_t *c = (const gpu_cmd_draw_t *)p;
      if (psz < sizeof(*c))
        return -1;
      (void)do_draw(&st, c->count, c->first);
      break;
    }
    case GPU_CMD_DRAW_INDEXED: {
      const gpu_cmd_draw_indexed_t *c = (const gpu_cmd_draw_indexed_t *)p;
      if (psz < sizeof(*c))
        return -1;
      (void)do_draw_indexed(&st, c->count, c->first_index, c->base_vertex);
      break;
    }
    case GPU_CMD_BLIT: {
      const gpu_cmd_blit_t *c = (const gpu_cmd_blit_t *)p;
      if (psz < sizeof(*c))
        return -1;
      (void)do_blit(c);
      break;
    }
    case GPU_CMD_PRESENT:
      break;
    default:
      /* Skip unknown ops so one exotic packet does not abort the frame. */
      break;
    }
    p += psz;
  }
  return 0;
}
