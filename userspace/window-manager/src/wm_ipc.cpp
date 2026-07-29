#include "wm_ipc.hpp"
#include "wm_state.hpp"

#include <kernel/string.h>
#include <kernel/vfs.h>
#include <user/sdk/fs.hpp>
#include <user/sdk/syscall.hpp>
#include <user/sdk/wm.hpp>

namespace wms {


static int handle_create(wm::Request &req, wm::Response &rsp) {
  Slot *s = alloc_slot();
  if (!s)
    return -1;
  s->id = g_next_id++;
  s->owner_pid = req.pid;
  s->opts = req.opts;
  clamp_geom(s->opts);
  raise_window(s);
  if (s->opts.visible && s->opts.accept_focus && !s->opts.background)
    g_focus_id = s->id;
  s->damaged = 1;
  rsp.window_id = s->id;
  rsp.opts = s->opts;
  g_compose_dirty = 1;
  return 0;
}

static int handle_op(wm::Request &req, wm::Response &rsp) {
  Slot *s;
  rsp.window_id = req.window_id;
  rsp.opts = req.opts;

  switch ((wm::Op)req.op) {
  case wm::Op::Create:
    return handle_create(req, rsp);
  case wm::Op::Destroy:
  case wm::Op::Close:
    return handle_destroy(req.window_id);
  case wm::Op::Set:
    s = slot_by_id(req.window_id);
    if (!s)
      return -1;
    s->opts = req.opts;
    clamp_geom(s->opts);
    raise_window(s);
    mark_damage(s, 0, 0, 0, 0);
    rsp.opts = s->opts;
    g_compose_dirty = 1;
    return 0;
  case wm::Op::Get:
    s = slot_by_id(req.window_id);
    if (!s)
      return -1;
    rsp.opts = s->opts;
    return 0;
  case wm::Op::Show:
    s = slot_by_id(req.window_id);
    if (!s)
      return -1;
    s->opts.visible = req.show != 0;
    mark_damage(s, 0, 0, 0, 0);
    g_compose_dirty = 1;
    return 0;
  case wm::Op::Focus:
    focus_window(req.window_id);
    g_compose_dirty = 1;
    return slot_by_id(req.window_id) ? 0 : -1;
  case wm::Op::Move:
    s = slot_by_id(req.window_id);
    if (!s)
      return -1;
    s->opts.x = req.x;
    s->opts.y = req.y;
    mark_damage(s, 0, 0, 0, 0);
    g_compose_dirty = 1;
    return 0;
  case wm::Op::Resize:
    s = slot_by_id(req.window_id);
    if (!s)
      return -1;
    s->opts.w = req.w;
    s->opts.h = req.h;
    clamp_geom(s->opts);
    mark_damage(s, 0, 0, 0, 0);
    g_compose_dirty = 1;
    return 0;
  case wm::Op::Damage:
    s = slot_by_id(req.window_id);
    if (!s)
      return -1;
    mark_damage(s, req.x, req.y, req.w, req.h);
    g_compose_dirty = 1;
    return 0;
  case wm::Op::Find: {
    rsp.window_id = -1;
    for (int i = 0; i < kMaxWin; i++) {
      if (!g_slots[i].used)
        continue;
      if (strcmp(g_slots[i].opts.title, req.name) == 0) {
        rsp.window_id = g_slots[i].id;
        return 0;
      }
    }
    return -1;
  }
  case wm::Op::FindClass: {
    rsp.window_id = -1;
    for (int i = 0; i < kMaxWin; i++) {
      if (!g_slots[i].used)
        continue;
      if (strcmp(g_slots[i].opts.class_name, req.name) == 0) {
        rsp.window_id = g_slots[i].id;
        return 0;
      }
    }
    return -1;
  }
  case wm::Op::AttachSurface:
    s = slot_by_id(req.window_id);
    if (!s)
      return -1;
    if (import_surface(s, req.surface_token) == 0)
      g_compose_dirty = 1;
    return s->surface.valid() ? 0 : -1;
  default:
    return -1;
  }
}

static void write_rsp(int pid, const wm::Response &rsp) {
  char path[96];
  strcpy(path, "/tmp/wm/out/");
  append_u32(path + strlen(path), (uint32_t)pid);
  str_cat(path, ".rsp");
  int fd = (int)hsrc::sdk::open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0)
    return;
  (void)hsrc::sdk::write(fd, (void *)&rsp, sizeof(rsp));
  hsrc::sdk::close(fd);
}

void poll_requests(void) {
  int dfd = (int)hsrc::sdk::open("/tmp/wm/in", O_RDONLY);
  if (dfd < 0)
    return;

  vfs_dirent_t ents[32];
  for (;;) {
    long n = hsrc::sdk::getdents(dfd, ents, 32);
    if (n <= 0)
      break;
    for (long i = 0; i < n; i++) {
      int pid = 0;
      char path[96];
      wm::Request req{};
      wm::Response rsp{};
      int fd;
      long rn;

      if (ents[i].name[0] == '.')
        continue;
      if (parse_pid_from_name(ents[i].name, &pid) < 0)
        continue;

      strcpy(path, "/tmp/wm/in/");
      str_cat(path, ents[i].name);
      fd = (int)hsrc::sdk::open(path, O_RDONLY);
      if (fd < 0)
        continue;
      rn = hsrc::sdk::read(fd, &req, sizeof(req));
      hsrc::sdk::close(fd);
      (void)hsrc::sdk::unlink(path);

      memset(&rsp, 0, sizeof(rsp));
      rsp.magic = wm::kProtoMagicRsp;
      if (rn != (long)sizeof(req) || req.magic != wm::kProtoMagicReq) {
        rsp.status = -1;
        rsp.window_id = -1;
      } else {
        rsp.status = handle_op(req, rsp);
      }
      write_rsp(pid, rsp);
    }
  }
  hsrc::sdk::close(dfd);
}

int setup_dirs(void) {
  (void)hsrc::sdk::mkdir("/tmp", 0755);
  (void)hsrc::sdk::mkdir("/tmp/wm", 0755);
  (void)hsrc::sdk::mkdir("/tmp/wm/in", 0755);
  (void)hsrc::sdk::mkdir("/tmp/wm/out", 0755);

  char pidbuf[16];
  int pid = (int)hsrc::sdk::getpid();
  pidbuf[0] = '\0';
  append_u32(pidbuf, (uint32_t)pid);
  str_cat(pidbuf, "\n");
  int fd = (int)hsrc::sdk::open("/tmp/wm/pid", O_WRONLY | O_CREAT | O_TRUNC);
  if (fd >= 0) {
    (void)hsrc::sdk::write(fd, pidbuf, strlen(pidbuf));
    hsrc::sdk::close(fd);
  }
  return 0;
}

} /* namespace wms */
