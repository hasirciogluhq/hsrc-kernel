#include <user/mke.h>
#include <user/sdk/process.hpp>
#include <user/sdk/gfx.hpp>
#include <user/sdk/fs.hpp>
#include <user/sdk/net.hpp>
#include <user/sdk/syscall.hpp>
#include <user/string.h>
#include <kernel/vfs.h>
#include <kernel/argv.h>
#include <kernel/socket.h>
#include <kernel/errno.h>

/*
 * HSRC Terminal — ring-3 shell window (root by default).
 * Builtins: help, clear, pwd, cd, ls, mkdir, cat, touch, echo, write, run,
 *           whoami, id, uname, env, export, ping, nc, connect
 * Write:  echo hello > file   |  echo more >> file  |  write path text...
 */

namespace {

using hsrc::sdk::Color;
using hsrc::sdk::Input;
using hsrc::sdk::ScreenInfo;
using hsrc::sdk::Surface;
using hsrc::sdk::Window;
using hsrc::sdk::WindowOptions;
using hsrc::sdk::rgb;
using hsrc::sdk::rgba;

constexpr int kWinW = 720;
constexpr int kWinH = 440;
constexpr int kPad = 10;
constexpr int kLineH = 12;
constexpr int kCols = 86;
constexpr int kRows = 32;
constexpr int kHist = 120;

constexpr Color kBg     = rgb(18, 18, 22);
constexpr Color kFg     = rgb(220, 220, 225);
constexpr Color kDim    = rgb(120, 120, 130);
constexpr Color kAccent = rgb(90, 200, 120);
constexpr Color kErr    = rgb(240, 110, 110);
constexpr Color kTitle  = rgb(40, 40, 48);

Window g_win;
char g_lines[kHist][kCols + 1];
Color g_line_color[kHist];
int g_nlines = 0;
char g_cwd[VFS_PATH_MAX];
char g_input[kCols];
int g_inlen = 0;
bool g_dirty = true;

void line_push(const char *text, Color c)
{
    if (g_nlines >= kHist) {
        for (int i = 1; i < kHist; i++) {
            strcpy(g_lines[i - 1], g_lines[i]);
            g_line_color[i - 1] = g_line_color[i];
        }
        g_nlines = kHist - 1;
    }
    strncpy(g_lines[g_nlines], text ? text : "", kCols);
    g_lines[g_nlines][kCols] = 0;
    g_line_color[g_nlines] = c;
    g_nlines++;
    g_dirty = true;
}

void refresh_cwd()
{
    if (hsrc::sdk::getcwd(g_cwd, sizeof(g_cwd)) < 0)
        strcpy(g_cwd, "/");
}

void make_prompt(char *out, size_t n)
{
    /* root@hsrc:/path# */
    const char *user = (hsrc::sdk::geteuid() == 0) ? "root" : "user";
    char tmp[VFS_PATH_MAX + 32];
    int i = 0;
    const char *p;

    p = user;
    while (*p && i < (int)sizeof(tmp) - 1)
        tmp[i++] = *p++;
    const char *mid = "@hsrc:";
    p = mid;
    while (*p && i < (int)sizeof(tmp) - 1)
        tmp[i++] = *p++;
    p = g_cwd;
    while (*p && i < (int)sizeof(tmp) - 1)
        tmp[i++] = *p++;
    tmp[i++] = (hsrc::sdk::geteuid() == 0) ? '#' : '$';
    tmp[i++] = ' ';
    tmp[i] = 0;
    strncpy(out, tmp, n - 1);
    out[n - 1] = 0;
    (void)n;
}

void paint()
{
    Surface &s = g_win.surface();
    s.clear(kBg);
    s.fill(0, 0, kWinW, 22, kTitle);
    s.text(kPad, 7, "Terminal — root shell", kFg, 1);

    const int top = 28;
    const int visible = (kWinH - top - kPad - kLineH) / kLineH;
    int start = g_nlines > visible ? g_nlines - visible : 0;
    int y = top;
    for (int i = start; i < g_nlines; i++) {
        s.text(kPad, y, g_lines[i], g_line_color[i], 1);
        y += kLineH;
    }

    char prompt[160];
    char row[kCols + 1];
    make_prompt(prompt, sizeof(prompt));
    int pi = 0;
    while (prompt[pi] && pi < kCols)
        row[pi] = prompt[pi], pi++;
    for (int i = 0; i < g_inlen && pi < kCols; i++)
        row[pi++] = g_input[i];
    if (pi < kCols)
        row[pi++] = '_';
    row[pi] = 0;
    s.text(kPad, y, row, kAccent, 1);

    g_win.damage();
    g_dirty = false;
}

const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

bool cmd_is(const char *s, const char *name)
{
    size_t n = 0;
    while (name[n])
        n++;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != name[i])
            return false;
    }
    return s[n] == 0 || s[n] == ' ' || s[n] == '\t';
}

const char *cmd_args(const char *s, const char *name)
{
    size_t n = 0;
    while (name[n])
        n++;
    return skip_ws(s + n);
}

void cmd_help()
{
    line_push("builtins: help clear pwd cd ls mkdir cat touch echo write run", kDim);
    line_push("          whoami id uname env export ping nc connect", kDim);
    line_push("env:    env   |  env PATH   (lists known keys or prints one)", kDim);
    line_push("export: export NAME=value   |  export -g NAME=value", kDim);
    line_push("write:  echo hi > f.txt   |  echo hi >> f.txt   |  write f.txt hi", kDim);
    line_push("run:    run terminal   |  run /applications/terminal.mke [args...]", kDim);
    line_push("launch: ./app.mke [args...]   |  /applications/app.mke [args...]", kDim);
    line_push("ping:   ping 10.0.2.2", kDim);
    line_push("nc:     nc 10.0.2.2 80   |   nc 10.0.2.2 7 hello", kDim);
    line_push("connect: same as nc", kDim);
    line_push("tip:  >  =  AltGr+.  (or Shift+<> key)   |  no >: use write", kDim);
}

void append_text(char *out, int &j, const char *text)
{
    if (!text)
        return;
    while (*text && j < kCols) {
        out[j++] = *text++;
    }
    out[j] = 0;
}

void append_uint(char *out, int &j, unsigned v)
{
    char tmp[16];
    int n = 0;
    if (v == 0) {
        if (j < kCols)
            out[j++] = '0';
        out[j] = 0;
        return;
    }
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0 && j < kCols)
        out[j++] = tmp[--n];
    out[j] = 0;
}

void append_int(char *out, int &j, int v)
{
    unsigned mag;
    if (v < 0) {
        if (j < kCols)
            out[j++] = '-';
        mag = (unsigned)(-(v + 1)) + 1u;
    } else {
        mag = (unsigned)v;
    }
    out[j] = 0;
    append_uint(out, j, mag);
}

const char *errno_name(long rc)
{
    switch ((int)(rc < 0 ? -rc : rc)) {
    case EAGAIN:
        return "again";
    case EINVAL:
        return "inval";
    case EPIPE:
        return "pipe";
    case ENETDOWN:
        return "netdown";
    case ETIMEDOUT:
        return "timeout";
    case EHOSTUNREACH:
        return "hostunreach";
    case EADDRINUSE:
        return "addrinuse";
    case ENOTSOCK:
        return "notsock";
    case EAFNOSUPPORT:
        return "afnosupport";
    default:
        return "error";
    }
}

void push_wrapped(const char *text, Color c)
{
    char row[kCols + 1];
    int ri = 0;
    char ch;

    if (!text) {
        line_push("", c);
        return;
    }
    for (;;) {
        ch = *text++;
        if (ch == '\r')
            continue;
        if (ch == 0 || ch == '\n') {
            row[ri] = 0;
            line_push(row, c);
            ri = 0;
            if (ch == 0)
                break;
            continue;
        }
        if (ri >= kCols) {
            row[ri] = 0;
            line_push(row, c);
            ri = 0;
        }
        row[ri++] = ch;
    }
}

const char *copy_token(const char *s, char *out, size_t n)
{
    size_t i = 0;
    s = skip_ws(s);
    while (*s && *s != ' ' && *s != '\t') {
        if (i + 1 < n)
            out[i++] = *s;
        s++;
    }
    out[i] = 0;
    return skip_ws(s);
}

size_t str_len(const char *s)
{
    size_t n = 0;
    while (s && s[n])
        n++;
    return n;
}

bool str_ends_with(const char *s, const char *suffix)
{
    size_t slen = str_len(s);
    size_t tlen = str_len(suffix);
    if (tlen > slen)
        return false;
    return strcmp(s + slen - tlen, suffix) == 0;
}

bool path_has_slash(const char *path)
{
    while (*path) {
        if (*path == '/')
            return true;
        path++;
    }
    return false;
}

bool path_exists(const char *path)
{
    long fd;
    if (!path || !*path)
        return false;
    fd = hsrc::sdk::open(path, O_RDONLY);
    if (fd < 0)
        return false;
    hsrc::sdk::close((int)fd);
    return true;
}

void copy_cstr(char *dst, size_t dst_n, const char *src)
{
    size_t i = 0;
    if (dst_n == 0)
        return;
    while (src && src[i] && i + 1 < dst_n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void build_candidate(char *out, size_t out_n, const char *prefix, const char *name, const char *suffix)
{
    size_t i = 0;
    if (out_n == 0)
        return;
    while (prefix && *prefix && i + 1 < out_n)
        out[i++] = *prefix++;
    while (name && *name && i + 1 < out_n)
        out[i++] = *name++;
    while (suffix && *suffix && i + 1 < out_n)
        out[i++] = *suffix++;
    out[i] = 0;
}

bool resolve_run_target(const char *name, char *out, size_t out_n)
{
    char candidate[VFS_PATH_MAX];

    if (!name || !*name)
        return false;

    if (path_has_slash(name)) {
        copy_cstr(out, out_n, name);
        return true;
    }

    build_candidate(candidate, sizeof(candidate), "/applications/", name, ".mke");
    if (path_exists(candidate)) {
        copy_cstr(out, out_n, candidate);
        return true;
    }

    build_candidate(candidate, sizeof(candidate), "/applications/", name, "");
    if (path_exists(candidate)) {
        copy_cstr(out, out_n, candidate);
        return true;
    }

    build_candidate(candidate, sizeof(candidate), "/usr/bin/", name, "");
    if (path_exists(candidate)) {
        copy_cstr(out, out_n, candidate);
        return true;
    }

    return false;
}

bool should_spawn_direct_path(const char *cmd)
{
    if (!cmd || !*cmd)
        return false;
    if (!((*cmd == '.' && cmd[1] == '/') || *cmd == '/'))
        return false;
    if (str_ends_with(cmd, ".mke"))
        return true;
    return path_exists(cmd);
}

int build_spawn_argv(const char *prog, const char *rest, const char *argv[],
                     char storage[][PROC_ARGV_MAX], int maxargc)
{
    int argc = 0;
    const char *p = rest;

    if (prog && prog[0] && argc < maxargc) {
        argv[argc] = storage[argc];
        strncpy(storage[argc], prog, PROC_ARGV_MAX - 1);
        storage[argc][PROC_ARGV_MAX - 1] = 0;
        argc++;
    }
    while (p && *p && argc < maxargc) {
        argv[argc] = storage[argc];
        p = copy_token(p, storage[argc], PROC_ARGV_MAX);
        if (!storage[argc][0])
            break;
        argc++;
    }
    if (argc < maxargc)
        argv[argc] = nullptr;
    return argc;
}

void push_spawn_status(const char *prefix, const char *path, long rc)
{
    char msg[kCols + 1];
    int j = 0;

    msg[0] = 0;
    append_text(msg, j, prefix);
    append_text(msg, j, " ");
    append_text(msg, j, path);
    if (rc >= 0) {
        append_text(msg, j, " pid=");
        append_int(msg, j, (int)rc);
        line_push(msg, kAccent);
        return;
    }

    append_text(msg, j, " failed: ");
    append_text(msg, j, errno_name(rc));
    append_text(msg, j, " (");
    append_int(msg, j, (int)rc);
    append_text(msg, j, ")");
    line_push(msg, kErr);
}

void cmd_run(const char *arg)
{
    char name[VFS_PATH_MAX];
    char path[VFS_PATH_MAX];
    const char *rest;
    const char *spawn_argv[PROC_ARGC_MAX + 1];
    char spawn_storage[PROC_ARGC_MAX][PROC_ARGV_MAX];
    long rc;

    rest = copy_token(arg, name, sizeof(name));
    if (!name[0]) {
        line_push("run: usage: run <name|path> [args...]", kErr);
        return;
    }
    if (!resolve_run_target(name, path, sizeof(path))) {
        line_push("run: target not found", kErr);
        return;
    }

    (void)build_spawn_argv(name, rest, spawn_argv, spawn_storage, PROC_ARGC_MAX);
    rc = hsrc::sdk::process::spawn_ex(path, hsrc::sdk::process::ConsoleVisible, spawn_argv);
    push_spawn_status("run:", path, rc);
}

bool parse_port_token(const char *s, uint16_t *port_out, const char **rest_out)
{
    unsigned port = 0;
    int ndig = 0;
    const char *p = skip_ws(s);

    while (*p >= '0' && *p <= '9') {
        port = port * 10u + (unsigned)(*p - '0');
        if (port > 65535u)
            return false;
        ndig++;
        p++;
    }
    if (ndig == 0)
        return false;
    if (*p && *p != ' ' && *p != '\t')
        return false;
    if (port == 0)
        return false;
    *port_out = (uint16_t)port;
    if (rest_out)
        *rest_out = skip_ws(p);
    return true;
}

void format_endpoint_status(char *out, const char *verb, const char *host, uint16_t port,
                            const char *suffix)
{
    int j = 0;
    out[0] = 0;
    append_text(out, j, verb);
    append_text(out, j, " ");
    append_text(out, j, host);
    append_text(out, j, ":");
    append_uint(out, j, port);
    if (suffix && *suffix) {
        append_text(out, j, " ");
        append_text(out, j, suffix);
    }
    out[j] = 0;
}

long tcp_connect_ipv4(uint32_t dip, uint16_t port)
{
    long s;
    sockaddr_in_t dst;

    s = hsrc::sdk::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s < 0)
        return s;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr = htonl(dip);

    {
        long rc = hsrc::sdk::connect((int)s, &dst);
        if (rc < 0) {
            hsrc::sdk::close((int)s);
            return rc;
        }
    }
    return s;
}

void cmd_ping_tcp_fallback(const char *host, uint32_t dip)
{
    char msg[kCols + 1];
    long s = tcp_connect_ipv4(dip, 80);

    if (s >= 0) {
        format_endpoint_status(msg, "ping:", host, 80, "tcp connect ok");
        line_push(msg, kAccent);
        hsrc::sdk::close((int)s);
        return;
    }

    format_endpoint_status(msg, "ping:", host, 80, "tcp connect failed");
    line_push(msg, kErr);
    {
        int j = 0;
        msg[0] = 0;
        append_text(msg, j, "ping: ");
        append_text(msg, j, errno_name(s));
        append_text(msg, j, " (");
        append_int(msg, j, (int)s);
        append_text(msg, j, ")");
        line_push(msg, kErr);
    }
}

static uint16_t icmp_checksum(const uint8_t *b, size_t len)
{
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)b[i] << 8 | b[i + 1];
    if (i < len)
        sum += (uint32_t)b[i] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

void cmd_ping(const char *arg)
{
    char host[64];
    uint32_t dip = 0;
    int j = 0;
    int sent = 0, recv = 0;
    long s;
    const char *p = skip_ws(arg);
    char msg[kCols + 1];

    if (!*p) {
        line_push("ping: usage: ping <ipv4>", kErr);
        return;
    }
    while (p[j] && p[j] != ' ' && p[j] != '\t' && j < (int)sizeof(host) - 1) {
        host[j] = p[j];
        j++;
    }
    host[j] = 0;
    if (hsrc::sdk::inet_aton(host, &dip) < 0) {
        line_push("ping: bad address", kErr);
        return;
    }

    s = hsrc::sdk::socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (s < 0) {
        line_push("ping: raw icmp unavailable, trying tcp/80", kDim);
        cmd_ping_tcp_fallback(host, dip);
        return;
    }

    line_push("PING (SOCK_RAW/ICMP) ...", kDim);
    for (int i = 0; i < 4; i++) {
        uint8_t icmp[16];
        sockaddr_in_t dst, src;
        uint16_t id = (uint16_t)(0xBEE0 + i);
        uint16_t seq = (uint16_t)(i + 1);
        uint16_t csum, v;
        long n;

        memset(icmp, 0, sizeof(icmp));
        icmp[0] = 8; /* echo */
        v = htons(id);
        memcpy(icmp + 4, &v, 2);
        v = htons(seq);
        memcpy(icmp + 6, &v, 2);
        memcpy(icmp + 8, "pinghsrc", 8);
        csum = icmp_checksum(icmp, sizeof(icmp));
        icmp[2] = (uint8_t)(csum >> 8);
        icmp[3] = (uint8_t)(csum & 0xFF);

        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_addr = htonl(dip);

        if (hsrc::sdk::sendto((int)s, icmp, sizeof(icmp), &dst) < 0)
            continue;
        sent++;

        n = hsrc::sdk::recvfrom((int)s, icmp, sizeof(icmp), &src);
        if (n >= 8 && icmp[0] == 0) {
            uint16_t rid, rseq;
            memcpy(&rid, icmp + 4, 2);
            memcpy(&rseq, icmp + 6, 2);
            if (ntohs(rid) == id && ntohs(rseq) == seq)
                recv++;
        }
    }
    hsrc::sdk::close((int)s);

    j = 0;
    {
        const char *hp = host;
        const char *mid = ": sent=";
        while (*hp && j < kCols)
            msg[j++] = *hp++;
        while (*mid && j < kCols)
            msg[j++] = *mid++;
        if (sent < 10 && j < kCols)
            msg[j++] = (char)('0' + sent);
        mid = " recv=";
        while (*mid && j < kCols)
            msg[j++] = *mid++;
        if (recv < 10 && j < kCols)
            msg[j++] = (char)('0' + recv);
        msg[j] = 0;
    }
    line_push(msg, recv > 0 ? kAccent : kErr);
    if (recv == 0)
        line_push("ping: timeout / unreachable", kErr);
}

void cmd_connect(const char *arg)
{
    char host[64];
    char msg[kCols + 1];
    char buf[129];
    uint32_t dip = 0;
    uint16_t port = 0;
    const char *rest;
    long s;
    long n;
    size_t send_len = 0;

    rest = copy_token(arg, host, sizeof(host));
    if (!host[0]) {
        line_push("nc: usage: nc <ipv4> <port> [line...]", kErr);
        return;
    }
    if (hsrc::sdk::inet_aton(host, &dip) < 0) {
        line_push("nc: bad address", kErr);
        return;
    }
    if (!parse_port_token(rest, &port, &rest)) {
        line_push("nc: usage: nc <ipv4> <port> [line...]", kErr);
        return;
    }

    s = tcp_connect_ipv4(dip, port);
    if (s < 0) {
        format_endpoint_status(msg, "nc:", host, port, "connect failed");
        line_push(msg, kErr);
        {
            int j = 0;
            msg[0] = 0;
            append_text(msg, j, "nc: ");
            append_text(msg, j, errno_name(s));
            append_text(msg, j, " (");
            append_int(msg, j, (int)s);
            append_text(msg, j, ")");
            line_push(msg, kErr);
        }
        return;
    }

    format_endpoint_status(msg, "nc:", host, port, "connected");
    line_push(msg, kAccent);

    while (rest[send_len])
        send_len++;
    if (send_len > 0) {
        n = hsrc::sdk::send((int)s, rest, send_len);
        if (n < 0) {
            line_push("nc: send failed", kErr);
            hsrc::sdk::close((int)s);
            return;
        }
        (void)hsrc::sdk::send((int)s, "\n", 1);
        (void)hsrc::sdk::shutdown((int)s, SHUT_WR);

        {
            int j = 0;
            msg[0] = 0;
            append_text(msg, j, "nc: sent ");
            append_int(msg, j, (int)n);
            append_text(msg, j, " bytes");
            line_push(msg, kDim);
        }

        n = hsrc::sdk::recv((int)s, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            push_wrapped(buf, kFg);
        } else if (n == 0) {
            line_push("nc: peer closed", kDim);
        } else {
            {
                int j = 0;
                msg[0] = 0;
                append_text(msg, j, "nc: recv ");
                append_text(msg, j, errno_name(n));
                append_text(msg, j, " (");
                append_int(msg, j, (int)n);
                append_text(msg, j, ")");
                line_push(msg, n == -EAGAIN ? kDim : kErr);
            }
        }
    }

    hsrc::sdk::close((int)s);
}

constexpr size_t kEnvKeyMax = 32;
constexpr size_t kEnvValMax = 128;

static const char *const k_known_env_keys[] = {
    "PATH", "HOME", "USER", "SHELL", "TERM", "PWD", nullptr
};

void push_env_pair(const char *key, const char *val)
{
    char row[kCols + 1];
    int j = 0;

    row[0] = 0;
    append_text(row, j, key);
    append_text(row, j, "=");
    append_text(row, j, val);
    line_push(row, kFg);
}

void cmd_env(const char *arg)
{
    char key[kEnvKeyMax];
    char val[kEnvValMax];
    long rc;

    arg = skip_ws(arg);
    if (*arg) {
        copy_token(arg, key, sizeof(key));
        if (!key[0]) {
            line_push("env: usage: env [NAME]", kErr);
            return;
        }
        rc = hsrc::sdk::process::getenv(key, val, sizeof(val));
        if (rc < 0) {
            char msg[kCols + 1];
            int j = 0;
            msg[0] = 0;
            append_text(msg, j, "env: ");
            append_text(msg, j, key);
            append_text(msg, j, ": not set");
            line_push(msg, kErr);
            return;
        }
        push_env_pair(key, val);
        return;
    }

    for (int i = 0; k_known_env_keys[i]; i++) {
        rc = hsrc::sdk::process::getenv(k_known_env_keys[i], val, sizeof(val));
        if (rc >= 0)
            push_env_pair(k_known_env_keys[i], val);
    }
}

void cmd_export(const char *arg)
{
    char name[kEnvKeyMax];
    char val[kEnvValMax];
    const char *eq;
    size_t nlen;
    int global = 0;
    long rc;

    arg = skip_ws(arg);
    if (!*arg) {
        line_push("export: usage: export [-g] NAME=value", kErr);
        return;
    }
    if (arg[0] == '-' && arg[1] == 'g' &&
        (arg[2] == 0 || arg[2] == ' ' || arg[2] == '\t')) {
        global = 1;
        arg = skip_ws(arg + 2);
    }
    if (!*arg) {
        line_push("export: usage: export [-g] NAME=value", kErr);
        return;
    }

    eq = arg;
    while (*eq && *eq != '=')
        eq++;
    if (*eq != '=') {
        line_push("export: expected NAME=value", kErr);
        return;
    }

    nlen = (size_t)(eq - arg);
    if (nlen == 0 || nlen >= kEnvKeyMax) {
        line_push("export: bad variable name", kErr);
        return;
    }
    memcpy(name, arg, nlen);
    name[nlen] = 0;

    copy_cstr(val, sizeof(val), eq + 1);
    if (str_len(val) >= kEnvValMax) {
        line_push("export: value too long", kErr);
        return;
    }

    rc = hsrc::sdk::process::setenv(name, val, global);
    if (rc < 0) {
        char msg[kCols + 1];
        int j = 0;
        msg[0] = 0;
        append_text(msg, j, "export: failed (");
        append_int(msg, j, (int)rc);
        append_text(msg, j, ")");
        line_push(msg, kErr);
    }
}

void cmd_ls(const char *arg)
{
    const char *path = (*arg) ? arg : ".";
    int fd = (int)hsrc::sdk::open(path, O_RDONLY | O_DIRECTORY);
    vfs_dirent_t ents[32];
    long n;
    char row[kCols + 1];

    if (fd < 0) {
        line_push("ls: cannot open directory", kErr);
        return;
    }
    n = hsrc::sdk::getdents(fd, ents, 32);
    hsrc::sdk::close(fd);
    if (n < 0) {
        line_push("ls: readdir failed", kErr);
        return;
    }
    if (n == 0) {
        line_push("(empty)", kDim);
        return;
    }
    for (long i = 0; i < n; i++) {
        int j = 0;
        row[j++] = S_ISDIR(ents[i].type) ? 'd' : '-';
        row[j++] = ' ';
        const char *nm = ents[i].name;
        while (*nm && j < kCols)
            row[j++] = *nm++;
        row[j] = 0;
        line_push(row, S_ISDIR(ents[i].type) ? kAccent : kFg);
    }
}

void cmd_cat(const char *arg)
{
    char buf[128];
    char line[kCols + 1];
    int fd, li = 0;
    long n;

    if (!*arg) {
        line_push("cat: missing path", kErr);
        return;
    }
    fd = (int)hsrc::sdk::open(arg, O_RDONLY);
    if (fd < 0) {
        line_push("cat: open failed", kErr);
        return;
    }
    while ((n = hsrc::sdk::read(fd, buf, sizeof(buf))) > 0) {
        for (long i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || li >= kCols) {
                line[li] = 0;
                line_push(line, kFg);
                li = 0;
                if (c == '\n')
                    continue;
            }
            if (c == '\r')
                continue;
            line[li++] = c;
        }
    }
    if (li > 0) {
        line[li] = 0;
        line_push(line, kFg);
    }
    hsrc::sdk::close(fd);
    if (n < 0)
        line_push("cat: read error", kErr);
}

void cmd_touch(const char *arg)
{
    int fd;
    if (!*arg) {
        line_push("touch: missing path", kErr);
        return;
    }
    fd = (int)hsrc::sdk::open(arg, O_WRONLY | O_CREAT);
    if (fd < 0) {
        line_push("touch: failed", kErr);
        return;
    }
    hsrc::sdk::close(fd);
}

/* Write text to path. append=0 truncates; append=1 seeks to end. */
void cmd_write_file(const char *path, const char *text, int append)
{
    int fd;
    size_t len = 0;
    int flags = O_WRONLY | O_CREAT;
    char nl = '\n';

    if (!path || !*path) {
        line_push("write: missing path", kErr);
        return;
    }
    if (!text)
        text = "";
    if (!append)
        flags |= O_TRUNC;

    fd = (int)hsrc::sdk::open(path, flags);
    if (fd < 0) {
        line_push("write: open failed", kErr);
        return;
    }
    if (append && hsrc::sdk::lseek(fd, 0, SEEK_END) < 0) {
        hsrc::sdk::close(fd);
        line_push("write: seek failed", kErr);
        return;
    }

    while (text[len])
        len++;
    if (len > 0 && hsrc::sdk::write(fd, text, len) < 0) {
        hsrc::sdk::close(fd);
        line_push("write: failed", kErr);
        return;
    }
    (void)hsrc::sdk::write(fd, &nl, 1);
    hsrc::sdk::close(fd);
}

/* Parse "echo ... > path" / "echo ... >> path". Returns 1 if handled. */
int try_echo_redirect(const char *args)
{
    char text[kCols + 1];
    char path[VFS_PATH_MAX];
    const char *gt;
    int append = 0;
    int ti = 0, pi = 0;

    gt = args;
    while (*gt && !(*gt == '>' && (gt == args || gt[-1] == ' ' || gt[-1] == '\t')))
        gt++;
    if (*gt != '>')
        return 0;
    if (gt[1] == '>') {
        append = 1;
        /* text is args[0 .. gt) trimmed */
    }

    /* copy text before > */
    {
        const char *end = gt;
        while (end > args && (end[-1] == ' ' || end[-1] == '\t'))
            end--;
        while (args < end && ti < kCols)
            text[ti++] = *args++;
        text[ti] = 0;
    }

    if (append)
        gt += 2;
    else
        gt += 1;
    gt = skip_ws(gt);
    if (!*gt) {
        line_push("echo: missing redirect path", kErr);
        return 1;
    }
    while (*gt && *gt != ' ' && *gt != '\t' && pi < (int)sizeof(path) - 1)
        path[pi++] = *gt++;
    path[pi] = 0;

    cmd_write_file(path, text, append);
    return 1;
}

void run_line(const char *line)
{
    const char *s = skip_ws(line);
    const char *arg_rest;
    char arg[VFS_PATH_MAX];

    if (!*s)
        return;
    arg_rest = copy_token(s, arg, sizeof(arg));

    if (strcmp(s, "help") == 0 || strcmp(s, "?") == 0) {
        cmd_help();
        return;
    }
    if (strcmp(s, "clear") == 0) {
        g_nlines = 0;
        g_dirty = true;
        return;
    }
    if (strcmp(s, "pwd") == 0) {
        refresh_cwd();
        line_push(g_cwd, kFg);
        return;
    }
    if (strcmp(s, "whoami") == 0) {
        line_push(hsrc::sdk::geteuid() == 0 ? "root" : "user", kFg);
        return;
    }
    if (strcmp(s, "id") == 0) {
        if (hsrc::sdk::getuid() == 0 && hsrc::sdk::geteuid() == 0)
            line_push("uid=0(root) euid=0(root)", kFg);
        else
            line_push("uid!=0", kFg);
        return;
    }
    if (strcmp(s, "uname") == 0) {
        line_push("HSRC OS mykernel i386", kFg);
        return;
    }
    if (cmd_is(s, "env")) {
        cmd_env(cmd_args(s, "env"));
        return;
    }
    if (cmd_is(s, "export")) {
        cmd_export(cmd_args(s, "export"));
        return;
    }
    if (cmd_is(s, "cd")) {
        const char *rest = cmd_args(s, "cd");
        if (!*rest)
            rest = "/root";
        if (hsrc::sdk::chdir(rest) < 0)
            line_push("cd: no such directory", kErr);
        else
            refresh_cwd();
        return;
    }
    if (cmd_is(s, "ls")) {
        cmd_ls(cmd_args(s, "ls"));
        return;
    }
    if (cmd_is(s, "mkdir")) {
        const char *rest = cmd_args(s, "mkdir");
        if (!*rest) {
            line_push("mkdir: missing operand", kErr);
            return;
        }
        if (hsrc::sdk::mkdir(rest, 0755) < 0)
            line_push("mkdir: failed", kErr);
        return;
    }
    if (cmd_is(s, "cat")) {
        cmd_cat(cmd_args(s, "cat"));
        return;
    }
    if (cmd_is(s, "touch")) {
        cmd_touch(cmd_args(s, "touch"));
        return;
    }
    if (cmd_is(s, "echo")) {
        const char *rest = cmd_args(s, "echo");
        if (try_echo_redirect(rest))
            return;
        line_push(rest, kFg);
        return;
    }
    if (cmd_is(s, "write")) {
        /* write <path> <text...> */
        const char *rest = cmd_args(s, "write");
        char path[VFS_PATH_MAX];
        int pi = 0;
        if (!*rest) {
            line_push("write: usage: write <path> <text>", kErr);
            return;
        }
        while (*rest && *rest != ' ' && *rest != '\t' && pi < (int)sizeof(path) - 1)
            path[pi++] = *rest++;
        path[pi] = 0;
        rest = skip_ws(rest);
        cmd_write_file(path, rest, 0);
        return;
    }
    if (cmd_is(s, "run")) {
        cmd_run(cmd_args(s, "run"));
        return;
    }
    if (cmd_is(s, "ping")) {
        cmd_ping(cmd_args(s, "ping"));
        return;
    }
    if (cmd_is(s, "nc")) {
        cmd_connect(cmd_args(s, "nc"));
        return;
    }
    if (cmd_is(s, "connect")) {
        cmd_connect(cmd_args(s, "connect"));
        return;
    }
    if (should_spawn_direct_path(arg)) {
        const char *spawn_argv[PROC_ARGC_MAX + 1];
        char spawn_storage[PROC_ARGC_MAX][PROC_ARGV_MAX];
        long rc;

        (void)build_spawn_argv(arg, arg_rest, spawn_argv, spawn_storage, PROC_ARGC_MAX);
        rc = hsrc::sdk::process::spawn_ex(arg, hsrc::sdk::process::ConsoleVisible, spawn_argv);
        push_spawn_status("spawn:", arg, rc);
        return;
    }

    /* unknown */
    {
        char msg[kCols + 1];
        int j = 0;
        const char *p = arg;
        const char *pre = "command not found: ";
        while (*pre && j < kCols)
            msg[j++] = *pre++;
        while (*p && j < kCols)
            msg[j++] = *p++;
        msg[j] = 0;
        line_push(msg, kErr);
    }
}

void on_enter()
{
    char shown[kCols + 1];
    char prompt[160];
    int i = 0;

    make_prompt(prompt, sizeof(prompt));
    while (prompt[i] && i < kCols) {
        shown[i] = prompt[i];
        i++;
    }
    for (int j = 0; j < g_inlen && i < kCols; j++)
        shown[i++] = g_input[j];
    shown[i] = 0;
    line_push(shown, kDim);

    g_input[g_inlen] = 0;
    run_line(g_input);
    g_inlen = 0;
    g_dirty = true;
}

void handle_key(int ch)
{
    if (ch < 0)
        return;
    if (ch == '\n' || ch == '\r') {
        on_enter();
        return;
    }
    if (ch == 8 || ch == 127) { /* backspace */
        if (g_inlen > 0) {
            g_inlen--;
            g_dirty = true;
        }
        return;
    }
    if (ch == 3) { /* ctrl-c */
        line_push("^C", kDim);
        g_inlen = 0;
        g_dirty = true;
        return;
    }
    if (ch >= 32 && ch < 127 && g_inlen < kCols - 1) {
        g_input[g_inlen++] = (char)ch;
        g_dirty = true;
    }
}

} // namespace

extern "C" void mke_main(void)
{
    ScreenInfo info{};
    if (!hsrc::sdk::screen_info(info) || info.width == 0) {
        for (;;)
            hsrc::sdk::yield();
    }

    const int x = ((int)info.width - kWinW) / 2;
    const int y = ((int)info.height - kWinH) / 2;

    WindowOptions opts;
    opts.x = x;
    opts.y = y;
    opts.w = kWinW;
    opts.h = kWinH;
    opts.radius = 10;
    opts.rounded = true;
    opts.set_title("Terminal");
    if (!g_win.create(opts)) {
        for (;;)
            hsrc::sdk::yield();
    }

    g_win.show(true);
    g_win.focus();

    /* Start in /root as root */
    (void)hsrc::sdk::chdir("/root");
    refresh_cwd();

    line_push("HSRC Terminal — running as root (uid 0)", kAccent);
    line_push("Type 'help' for builtins.", kDim);

    paint();
    (void)hsrc::sdk::present();

    for (;;) {
        Input in{};
        (void)hsrc::sdk::input(in);

        if (in.focus_id == g_win.id()) {
            for (;;) {
                long k = hsrc::sdk::syscall1(SYS_WM_POP_KEY, g_win.id());
                if (k < 0)
                    break;
                handle_key((int)k);
            }
        }

        if (g_dirty)
            paint();

        (void)hsrc::sdk::present();
        hsrc::sdk::yield();
    }
}
