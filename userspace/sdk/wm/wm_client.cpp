#include <user/sdk/wm.hpp>
#include <user/sdk/fs.hpp>
#include <user/sdk/syscall.hpp>
#include <kernel/syscall.h>
#include <kernel/string.h>
#include <kernel/vfs.h>

namespace wm {

namespace {

constexpr const char *kWmIn = "/tmp/wm/in";
constexpr const char *kWmOut = "/tmp/wm/out";
constexpr const char *kWmPid = "/tmp/wm/pid";
/* Wait for WM readiness + response: ~30s at 1ms sleep (boot race / slow compose). */
constexpr int kReadyTries = 30000;
constexpr int kTransactTries = 30000;

static void str_cat(char *dst, const char *src)
{
    dst += strlen(dst);
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

static void append_u32(char *dst, uint32_t v)
{
    char tmp[12];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v) {
            tmp[n++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    while (n--)
        *dst++ = tmp[n];
    *dst = '\0';
}

static void make_req_path(char *out, int pid)
{
    strcpy(out, kWmIn);
    str_cat(out, "/");
    append_u32(out + strlen(out), (uint32_t)pid);
    str_cat(out, ".req");
}

static void make_rsp_path(char *out, int pid)
{
    strcpy(out, kWmOut);
    str_cat(out, "/");
    append_u32(out + strlen(out), (uint32_t)pid);
    str_cat(out, ".rsp");
}

static void copy_str(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;
    size_t i = 0;
    while (src[i] && i + 1 < dst_sz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int write_all(const char *path, const void *data, size_t len)
{
    int fd = (int)hsrc::sdk::open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return -1;
    long n = hsrc::sdk::write(fd, (void *)data, len);
    hsrc::sdk::close(fd);
    return (n == (long)len) ? 0 : -1;
}

static int read_all(const char *path, void *data, size_t len)
{
    int fd = (int)hsrc::sdk::open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    long n = hsrc::sdk::read(fd, data, len);
    hsrc::sdk::close(fd);
    return (n == (long)len) ? 0 : -1;
}

} /* namespace */

WindowOptions::WindowOptions() = default;

void WindowOptions::set_title(const char *s)
{
    copy_str(title, sizeof(title), s);
}

void WindowOptions::set_class_name(const char *s)
{
    copy_str(class_name, sizeof(class_name), s);
}

bool input_snapshot(Input &out)
{
    input_state_t st;
    memset(&st, 0, sizeof(st));
    if (hsrc::sdk::syscall1(SYS_INPUT_STATE, (long)&st) < 0)
        return false;
    out.mouse_x = st.mouse_x;
    out.mouse_y = st.mouse_y;
    out.buttons = st.buttons;
    out.mods = st.mods;
    out.focus_id = st.focus_id;
    out.hit_id = st.hit_id;
    out.wheel = st.wheel;
    out.seq = st.seq;
    memcpy(out.keys, st.keys, sizeof(out.keys));
    out.drag_id = st.drag_id;
    return true;
}

/* True once window-manager has written /tmp/wm/pid (setup_dirs done). */
static int wm_is_ready(void)
{
    int fd = (int)hsrc::sdk::open(kWmPid, O_RDONLY);
    if (fd < 0)
        return 0;
    hsrc::sdk::close(fd);
    return 1;
}

static int wait_wm_ready(void)
{
    for (int i = 0; i < kReadyTries; i++) {
        if (wm_is_ready())
            return 0;
        hsrc::sdk::sleep(1);
    }
    return -1;
}

bool Connection::connect()
{
    pid_ = (int)hsrc::sdk::getpid();
    if (pid_ < 0)
        return false;
    if (wait_wm_ready() < 0)
        return false;
    ready_ = true;
    return true;
}

int Connection::transact(Request &req, Response &rsp)
{
    char req_path[96];
    char rsp_path[96];

    if (!ready_ && !connect())
        return -1;

    req.magic = kProtoMagicReq;
    req.pid = pid_;
    make_req_path(req_path, pid_);
    make_rsp_path(rsp_path, pid_);

    (void)hsrc::sdk::unlink(rsp_path);

    /* Retry open/write if /tmp/wm/in is not ready yet (WM just started). */
    int wrote = -1;
    for (int i = 0; i < kReadyTries; i++) {
        wrote = write_all(req_path, &req, sizeof(req));
        if (wrote == 0)
            break;
        hsrc::sdk::sleep(1);
    }
    if (wrote < 0)
        return -1;

    for (int i = 0; i < kTransactTries; i++) {
        if (read_all(rsp_path, &rsp, sizeof(rsp)) == 0) {
            (void)hsrc::sdk::unlink(rsp_path);
            if (rsp.magic != kProtoMagicRsp)
                return -1;
            return rsp.status;
        }
        hsrc::sdk::sleep(1);
    }
    (void)hsrc::sdk::unlink(req_path);
    return -1;
}

bool Window::create(const WindowOptions &opts)
{
    /* A few outer retries; Connection::transact already waits for /tmp/wm/pid
     * and polls the response for ~30s. Avoid exit-on-first-timeout storms. */
    for (int attempt = 0; attempt < 8; attempt++) {
        Request req{};
        Response rsp{};
        req.op = (uint32_t)Op::Create;
        req.window_id = -1;
        req.opts = opts;
        if (conn_.transact(req, rsp) == 0 && rsp.window_id >= 0) {
            id_ = rsp.window_id;
            return true;
        }
        conn_ = Connection();
        hsrc::sdk::sleep(100);
    }
    return false;
}

bool Window::destroy()
{
    if (id_ < 0)
        return true;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Destroy;
    req.window_id = id_;
    int st = conn_.transact(req, rsp);
    id_ = -1;
    surface_token_ = 0;
    return st == 0;
}

bool Window::close()
{
    if (id_ < 0)
        return true;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Close;
    req.window_id = id_;
    int st = conn_.transact(req, rsp);
    id_ = -1;
    surface_token_ = 0;
    return st == 0;
}

bool Window::set_options(const WindowOptions &opts)
{
    if (id_ < 0)
        return false;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Set;
    req.window_id = id_;
    req.opts = opts;
    return conn_.transact(req, rsp) == 0;
}

bool Window::get_options(WindowOptions &out) const
{
    if (id_ < 0)
        return false;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Get;
    req.window_id = id_;
    if (conn_.transact(req, rsp) < 0)
        return false;
    out = rsp.opts;
    return true;
}

bool Window::show(bool visible)
{
    if (id_ < 0)
        return false;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Show;
    req.window_id = id_;
    req.show = visible ? 1 : 0;
    return conn_.transact(req, rsp) == 0;
}

bool Window::focus()
{
    if (id_ < 0)
        return false;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Focus;
    req.window_id = id_;
    return conn_.transact(req, rsp) == 0;
}

bool Window::move(int32_t x, int32_t y)
{
    if (id_ < 0)
        return false;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Move;
    req.window_id = id_;
    req.x = x;
    req.y = y;
    return conn_.transact(req, rsp) == 0;
}

bool Window::resize(int32_t w, int32_t h)
{
    if (id_ < 0)
        return false;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Resize;
    req.window_id = id_;
    req.w = w;
    req.h = h;
    return conn_.transact(req, rsp) == 0;
}

bool Window::damage()
{
    return damage(0, 0, 0, 0);
}

bool Window::damage(int32_t x, int32_t y, int32_t w, int32_t h)
{
    if (id_ < 0)
        return false;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Damage;
    req.window_id = id_;
    req.x = x;
    req.y = y;
    req.w = w;
    req.h = h;
    return conn_.transact(req, rsp) == 0;
}

bool Window::attach_surface(uint32_t export_token)
{
    if (id_ < 0 || export_token == 0)
        return false;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::AttachSurface;
    req.window_id = id_;
    req.surface_token = export_token;
    if (conn_.transact(req, rsp) < 0)
        return false;
    surface_token_ = export_token;
    return true;
}

bool Window::map_surface(reed::Device &dev, reed::RenderTarget &rt)
{
    if (!dev.valid() || !rt.valid() || !rt.color().valid())
        return false;
    uint32_t token = 0;
    if (dev.export_handle(rt.color().handle(), &token) < 0 || token == 0)
        return false;
    return attach_surface(token);
}

int Window::find(const char *title)
{
    Connection c;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::Find;
    req.window_id = -1;
    copy_str(req.name, sizeof(req.name), title);
    if (c.transact(req, rsp) < 0)
        return -1;
    return rsp.window_id;
}

int Window::find_class(const char *class_name)
{
    Connection c;
    Request req{};
    Response rsp{};
    req.op = (uint32_t)Op::FindClass;
    req.window_id = -1;
    copy_str(req.name, sizeof(req.name), class_name);
    if (c.transact(req, rsp) < 0)
        return -1;
    return rsp.window_id;
}

} /* namespace wm */
