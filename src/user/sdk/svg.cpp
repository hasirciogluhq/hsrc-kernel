#include <user/sdk/svg.hpp>

#include <user/sdk/fs.hpp>
#include <user/string.h>

namespace hsrc::sdk {
namespace {

constexpr size_t kSvgBytes = 2048;
constexpr int kMaxPts = 96;

struct Pt {
    float x, y;
};

struct Style {
    bool has_fill = false;
    bool has_stroke = false;
    float stroke_w = 1.4f;
};

static const char *find_char(const char *s, char c)
{
    if (!s)
        return nullptr;
    while (*s) {
        if (*s == c)
            return s;
        s++;
    }
    return (c == 0) ? s : nullptr;
}

static const char *find_str(const char *hay, const char *needle)
{
    if (!hay || !needle)
        return nullptr;
    if (!needle[0])
        return hay;
    size_t nlen = strlen(needle);
    for (; *hay; hay++) {
        if (strncmp(hay, needle, nlen) == 0)
            return hay;
    }
    return nullptr;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',';
}

static bool is_cmd(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static const char *skip_ws(const char *p)
{
    while (p && *p && is_space(*p))
        p++;
    return p;
}

static const char *parse_float(const char *p, float *out)
{
    p = skip_ws(p);
    if (!p || !*p)
        return p;
    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    float v = 0;
    bool any = false;
    while (*p >= '0' && *p <= '9') {
        v = v * 10.0f + (float)(*p - '0');
        p++;
        any = true;
    }
    if (*p == '.') {
        p++;
        float place = 0.1f;
        while (*p >= '0' && *p <= '9') {
            v += (float)(*p - '0') * place;
            place *= 0.1f;
            p++;
            any = true;
        }
    }
    if (!any)
        return nullptr;
    *out = v * (float)sign;
    return p;
}

static bool attr_find(const char *tag, const char *key, char *out, size_t out_sz)
{
    if (!tag || !key || !out || out_sz == 0)
        return false;
    out[0] = 0;
    const char *p = find_str(tag, key);
    if (!p)
        return false;
    p += strlen(key);
    p = skip_ws(p);
    if (*p != '=')
        return false;
    p++;
    p = skip_ws(p);
    char q = *p;
    if (q != '"' && q != '\'')
        return false;
    p++;
    size_t n = 0;
    while (*p && *p != q && n + 1 < out_sz) {
        out[n++] = *p++;
    }
    out[n] = 0;
    return n > 0;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int iround(float v)
{
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

static void mask_set(uint8_t *mask, int w, int h, int x, int y, uint8_t a)
{
    if (x < 0 || y < 0 || x >= w || y >= h || a == 0)
        return;
    uint8_t *p = &mask[y * w + x];
    if (a > *p)
        *p = a;
}

static void mask_blend(uint8_t *mask, int w, int h, int x, int y, uint8_t a)
{
    if (x < 0 || y < 0 || x >= w || y >= h || a == 0)
        return;
    uint8_t *p = &mask[y * w + x];
    unsigned sum = (unsigned)*p + (unsigned)a;
    *p = sum > 255u ? 255u : (uint8_t)sum;
}

/* Coverage stamp for soft stroke tips. */
static void stamp(uint8_t *mask, int w, int h, float fx, float fy, float radius)
{
    int x0 = (int)(fx - radius) - 1;
    int y0 = (int)(fy - radius) - 1;
    int x1 = (int)(fx + radius) + 1;
    int y1 = (int)(fy + radius) + 1;
    float rr = radius * radius;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f) - fx;
            float dy = ((float)y + 0.5f) - fy;
            float d2 = dx * dx + dy * dy;
            if (d2 > rr)
                continue;
            float t = 1.0f - (d2 / rr);
            uint8_t a = (uint8_t)clampf(t * 255.0f, 0.0f, 255.0f);
            mask_blend(mask, w, h, x, y, a);
        }
    }
}

static void stroke_segment(uint8_t *mask, int w, int h,
                           float x0, float y0, float x1, float y1, float sw)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = dx * dx + dy * dy;
    if (len < 0.0001f) {
        stamp(mask, w, h, x0, y0, sw * 0.5f);
        return;
    }
    /* Rough length without sqrt: walk in ~0.4px steps via normalize approx. */
    float inv = 1.0f;
    /* Newton-ish inverse sqrt for positive len. */
    float est = len;
    for (int i = 0; i < 4; i++)
        est = 0.5f * (est + len / est);
    if (est > 0.0001f)
        inv = 1.0f / est;
    float ux = dx * inv;
    float uy = dy * inv;
    int steps = (int)(est / 0.35f) + 1;
    if (steps < 1)
        steps = 1;
    if (steps > 256)
        steps = 256;
    float r = sw * 0.55f;
    if (r < 0.6f)
        r = 0.6f;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        stamp(mask, w, h, x0 + ux * est * t, y0 + uy * est * t, r);
    }
}

static void cubic_stroke(uint8_t *mask, int w, int h,
                         float x0, float y0, float x1, float y1,
                         float x2, float y2, float x3, float y3, float sw)
{
    float px = x0, py = y0;
    for (int i = 1; i <= 16; i++) {
        float t = (float)i / 16.0f;
        float u = 1.0f - t;
        float a = u * u * u;
        float b = 3.0f * u * u * t;
        float c = 3.0f * u * t * t;
        float d = t * t * t;
        float x = a * x0 + b * x1 + c * x2 + d * x3;
        float y = a * y0 + b * y1 + c * y2 + d * y3;
        stroke_segment(mask, w, h, px, py, x, y, sw);
        px = x;
        py = y;
    }
}

static void fill_poly(uint8_t *mask, int w, int h, const Pt *pts, int n)
{
    if (n < 3)
        return;
    float miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < n; i++) {
        if (pts[i].y < miny)
            miny = pts[i].y;
        if (pts[i].y > maxy)
            maxy = pts[i].y;
    }
    int y0 = (int)miny;
    int y1 = (int)maxy + 1;
    if (y0 < 0)
        y0 = 0;
    if (y1 > h)
        y1 = h;
    for (int y = y0; y < y1; y++) {
        float scan = (float)y + 0.5f;
        float xs[kMaxPts];
        int xc = 0;
        for (int i = 0; i < n; i++) {
            const Pt &a = pts[i];
            const Pt &b = pts[(i + 1) % n];
            if ((a.y <= scan && b.y > scan) || (b.y <= scan && a.y > scan)) {
                float t = (scan - a.y) / (b.y - a.y);
                if (xc < kMaxPts)
                    xs[xc++] = a.x + t * (b.x - a.x);
            }
        }
        for (int i = 0; i + 1 < xc; i++) {
            for (int j = i + 1; j < xc; j++) {
                if (xs[j] < xs[i]) {
                    float tmp = xs[i];
                    xs[i] = xs[j];
                    xs[j] = tmp;
                }
            }
        }
        for (int i = 0; i + 1 < xc; i += 2) {
            int x0 = iround(xs[i]);
            int x1 = iround(xs[i + 1]);
            if (x0 > x1) {
                int t = x0;
                x0 = x1;
                x1 = t;
            }
            for (int x = x0; x <= x1; x++)
                mask_set(mask, w, h, x, y, 255);
        }
    }
}

static void fill_circle(uint8_t *mask, int w, int h, float cx, float cy, float r)
{
    int x0 = (int)(cx - r) - 1;
    int y0 = (int)(cy - r) - 1;
    int x1 = (int)(cx + r) + 1;
    int y1 = (int)(cy + r) + 1;
    float rr = r * r;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx = ((float)x + 0.5f) - cx;
            float dy = ((float)y + 0.5f) - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 <= rr)
                mask_set(mask, w, h, x, y, 255);
            else if (d2 <= (r + 0.6f) * (r + 0.6f)) {
                float edge = (r + 0.6f) * (r + 0.6f) - d2;
                uint8_t a = (uint8_t)clampf(edge * 180.0f, 0.0f, 200.0f);
                mask_blend(mask, w, h, x, y, a);
            }
        }
    }
}

static void stroke_circle(uint8_t *mask, int w, int h, float cx, float cy, float r, float sw)
{
    const int segs = 40;
    float px = cx + r;
    float py = cy;
    for (int i = 1; i <= segs; i++) {
        /* Approximate cos/sin via recursive rotation. */
        float ang = (float)i * (6.2831853f / (float)segs);
        /* Taylor-ish via known quarter table free: use recursive. */
        float c = 1.0f, s = 0.0f;
        /* Direct polynomial approx for cos/sin of ang in [0,2pi]. */
        float a = ang;
        while (a > 3.14159265f)
            a -= 6.2831853f;
        while (a < -3.14159265f)
            a += 6.2831853f;
        float a2 = a * a;
        c = 1.0f - a2 / 2.0f + a2 * a2 / 24.0f;
        s = a - a * a2 / 6.0f + a * a2 * a2 / 120.0f;
        float x = cx + c * r;
        float y = cy + s * r;
        stroke_segment(mask, w, h, px, py, x, y, sw);
        px = x;
        py = y;
    }
}

static void stroke_round_rect(uint8_t *mask, int w, int h,
                              float x, float y, float rw, float rh,
                              float rx, float ry, float sw)
{
    if (rx < 0.1f)
        rx = 0.1f;
    if (ry < 0.1f)
        ry = 0.1f;
    if (rx > rw * 0.5f)
        rx = rw * 0.5f;
    if (ry > rh * 0.5f)
        ry = rh * 0.5f;
    float x0 = x, y0 = y, x1 = x + rw, y1 = y + rh;
    stroke_segment(mask, w, h, x0 + rx, y0, x1 - rx, y0, sw);
    stroke_segment(mask, w, h, x1, y0 + ry, x1, y1 - ry, sw);
    stroke_segment(mask, w, h, x1 - rx, y1, x0 + rx, y1, sw);
    stroke_segment(mask, w, h, x0, y1 - ry, x0, y0 + ry, sw);
    /* Corner arcs as cubics (approx quarter circles). */
    const float k = 0.55228475f;
    cubic_stroke(mask, w, h, x1 - rx, y0, x1 - rx + k * rx, y0, x1, y0 + ry - k * ry, x1, y0 + ry, sw);
    cubic_stroke(mask, w, h, x1, y1 - ry, x1, y1 - ry + k * ry, x1 - rx + k * rx, y1, x1 - rx, y1, sw);
    cubic_stroke(mask, w, h, x0 + rx, y1, x0 + rx - k * rx, y1, x0, y1 - ry + k * ry, x0, y1 - ry, sw);
    cubic_stroke(mask, w, h, x0, y0 + ry, x0, y0 + ry - k * ry, x0 + rx - k * rx, y0, x0 + rx, y0, sw);
}

static bool parse_path(const char *d, Pt *pts, int *n_out, bool *closed)
{
    *n_out = 0;
    *closed = false;
    if (!d)
        return false;

    float cx = 0, cy = 0, sx = 0, sy = 0;
    char cmd = 0;
    const char *p = d;
    while (*p) {
        p = skip_ws(p);
        if (!*p)
            break;
        if (is_cmd(*p)) {
            cmd = *p++;
        }
        if (!cmd)
            break;

        if (cmd == 'Z' || cmd == 'z') {
            if (*n_out > 0 && *n_out < kMaxPts) {
                pts[(*n_out)++] = Pt{sx, sy};
            }
            *closed = true;
            cx = sx;
            cy = sy;
            continue;
        }

        float nums[6];
        int need = 0;
        char uc = (char)((cmd >= 'a' && cmd <= 'z') ? cmd - 32 : cmd);
        if (uc == 'M' || uc == 'L')
            need = 2;
        else if (uc == 'H' || uc == 'V')
            need = 1;
        else if (uc == 'C')
            need = 6;
        else
            return *n_out > 0;

        for (int i = 0; i < need; i++) {
            p = parse_float(p, &nums[i]);
            if (!p)
                return *n_out > 0;
        }

        bool rel = cmd >= 'a' && cmd <= 'z';
        if (uc == 'M') {
            cx = rel ? cx + nums[0] : nums[0];
            cy = rel ? cy + nums[1] : nums[1];
            sx = cx;
            sy = cy;
            if (*n_out < kMaxPts)
                pts[(*n_out)++] = Pt{cx, cy};
            cmd = rel ? 'l' : 'L';
        } else if (uc == 'L') {
            cx = rel ? cx + nums[0] : nums[0];
            cy = rel ? cy + nums[1] : nums[1];
            if (*n_out < kMaxPts)
                pts[(*n_out)++] = Pt{cx, cy};
        } else if (uc == 'H') {
            cx = rel ? cx + nums[0] : nums[0];
            if (*n_out < kMaxPts)
                pts[(*n_out)++] = Pt{cx, cy};
        } else if (uc == 'V') {
            cy = rel ? cy + nums[0] : nums[0];
            if (*n_out < kMaxPts)
                pts[(*n_out)++] = Pt{cx, cy};
        } else if (uc == 'C') {
            float x1 = rel ? cx + nums[0] : nums[0];
            float y1 = rel ? cy + nums[1] : nums[1];
            float x2 = rel ? cx + nums[2] : nums[2];
            float y2 = rel ? cy + nums[3] : nums[3];
            float x3 = rel ? cx + nums[4] : nums[4];
            float y3 = rel ? cy + nums[5] : nums[5];
            /* Densify cubic into polyline for fill; stroke handled separately. */
            float ox = cx, oy = cy;
            for (int i = 1; i <= 12; i++) {
                float t = (float)i / 12.0f;
                float u = 1.0f - t;
                float a = u * u * u;
                float b = 3.0f * u * u * t;
                float c = 3.0f * u * t * t;
                float dlt = t * t * t;
                float x = a * ox + b * x1 + c * x2 + dlt * x3;
                float y = a * oy + b * y1 + c * y2 + dlt * y3;
                if (*n_out < kMaxPts)
                    pts[(*n_out)++] = Pt{x, y};
            }
            cx = x3;
            cy = y3;
        }
    }
    return *n_out > 0;
}

static Style style_from_tag(const char *tag, const Style &inherited)
{
    Style st = inherited;
    char buf[64];
    if (attr_find(tag, "fill", buf, sizeof(buf))) {
        if (strcmp(buf, "none") == 0)
            st.has_fill = false;
        else
            st.has_fill = true;
    }
    if (attr_find(tag, "stroke", buf, sizeof(buf))) {
        if (strcmp(buf, "none") == 0)
            st.has_stroke = false;
        else
            st.has_stroke = true;
    }
    if (attr_find(tag, "stroke-width", buf, sizeof(buf))) {
        float sw = 0;
        if (parse_float(buf, &sw) && sw > 0.0f)
            st.stroke_w = sw;
    }
    return st;
}

static void draw_element(uint8_t *mask, int w, int h, const char *tag, Style st,
                         float sx, float sy, float ox, float oy)
{
    st = style_from_tag(tag, st);
    char buf[512];

    if (strncmp(tag, "<path", 5) == 0) {
        if (!attr_find(tag, "d", buf, sizeof(buf)))
            return;
        if (st.has_fill) {
            Pt pts[kMaxPts];
            int n = 0;
            bool closed = false;
            if (parse_path(buf, pts, &n, &closed) && n >= 3) {
                for (int i = 0; i < n; i++) {
                    pts[i].x = pts[i].x * sx + ox;
                    pts[i].y = pts[i].y * sy + oy;
                }
                fill_poly(mask, w, h, pts, n);
            }
        }
        if (st.has_stroke) {
            float cx = 0, cy = 0, startx = 0, starty = 0;
            float ux = 0, uy = 0;
            char cmd = 0;
            const char *p = buf;
            float sw = st.stroke_w * ((sx + sy) * 0.5f);
            while (*p) {
                p = skip_ws(p);
                if (!*p)
                    break;
                if (is_cmd(*p))
                    cmd = *p++;
                if (!cmd)
                    break;
                if (cmd == 'Z' || cmd == 'z') {
                    stroke_segment(mask, w, h, cx, cy, startx, starty, sw);
                    cx = startx;
                    cy = starty;
                    ux = (cx - ox) / sx;
                    uy = (cy - oy) / sy;
                    continue;
                }
                float nums[6];
                int need = 0;
                char uc = (char)((cmd >= 'a' && cmd <= 'z') ? cmd - 32 : cmd);
                if (uc == 'M' || uc == 'L')
                    need = 2;
                else if (uc == 'H' || uc == 'V')
                    need = 1;
                else if (uc == 'C')
                    need = 6;
                else
                    return;
                for (int i = 0; i < need; i++) {
                    p = parse_float(p, &nums[i]);
                    if (!p)
                        return;
                }
                bool rel = cmd >= 'a' && cmd <= 'z';
                if (uc == 'M') {
                    ux = rel ? ux + nums[0] : nums[0];
                    uy = rel ? uy + nums[1] : nums[1];
                    cx = ux * sx + ox;
                    cy = uy * sy + oy;
                    startx = cx;
                    starty = cy;
                    cmd = rel ? 'l' : 'L';
                } else if (uc == 'L') {
                    float nux = rel ? ux + nums[0] : nums[0];
                    float nuy = rel ? uy + nums[1] : nums[1];
                    float nx = nux * sx + ox;
                    float ny = nuy * sy + oy;
                    stroke_segment(mask, w, h, cx, cy, nx, ny, sw);
                    ux = nux;
                    uy = nuy;
                    cx = nx;
                    cy = ny;
                } else if (uc == 'H') {
                    float nux = rel ? ux + nums[0] : nums[0];
                    float nx = nux * sx + ox;
                    stroke_segment(mask, w, h, cx, cy, nx, cy, sw);
                    ux = nux;
                    cx = nx;
                } else if (uc == 'V') {
                    float nuy = rel ? uy + nums[0] : nums[0];
                    float ny = nuy * sy + oy;
                    stroke_segment(mask, w, h, cx, cy, cx, ny, sw);
                    uy = nuy;
                    cy = ny;
                } else if (uc == 'C') {
                    float ux1 = rel ? ux + nums[0] : nums[0];
                    float uy1 = rel ? uy + nums[1] : nums[1];
                    float ux2 = rel ? ux + nums[2] : nums[2];
                    float uy2 = rel ? uy + nums[3] : nums[3];
                    float ux3 = rel ? ux + nums[4] : nums[4];
                    float uy3 = rel ? uy + nums[5] : nums[5];
                    cubic_stroke(mask, w, h, cx, cy,
                                 ux1 * sx + ox, uy1 * sy + oy,
                                 ux2 * sx + ox, uy2 * sy + oy,
                                 ux3 * sx + ox, uy3 * sy + oy, sw);
                    ux = ux3;
                    uy = uy3;
                    cx = ux * sx + ox;
                    cy = uy * sy + oy;
                }
            }
        }
        return;
    }

    if (strncmp(tag, "<circle", 7) == 0) {
        float cxf = 0, cyf = 0, r = 0;
        char t[32];
        if (!attr_find(tag, "cx", t, sizeof(t)) || !parse_float(t, &cxf))
            return;
        if (!attr_find(tag, "cy", t, sizeof(t)) || !parse_float(t, &cyf))
            return;
        if (!attr_find(tag, "r", t, sizeof(t)) || !parse_float(t, &r))
            return;
        float mx = cxf * sx + ox;
        float my = cyf * sy + oy;
        float mr = r * ((sx + sy) * 0.5f);
        if (st.has_fill)
            fill_circle(mask, w, h, mx, my, mr);
        if (st.has_stroke)
            stroke_circle(mask, w, h, mx, my, mr, st.stroke_w * ((sx + sy) * 0.5f));
        return;
    }

    if (strncmp(tag, "<rect", 5) == 0) {
        float x = 0, y = 0, rw = 0, rh = 0, rx = 0, ry = 0;
        char t[32];
        if (!attr_find(tag, "x", t, sizeof(t)) || !parse_float(t, &x))
            return;
        if (!attr_find(tag, "y", t, sizeof(t)) || !parse_float(t, &y))
            return;
        if (!attr_find(tag, "width", t, sizeof(t)) || !parse_float(t, &rw))
            return;
        if (!attr_find(tag, "height", t, sizeof(t)) || !parse_float(t, &rh))
            return;
        if (attr_find(tag, "rx", t, sizeof(t)))
            (void)parse_float(t, &rx);
        if (attr_find(tag, "ry", t, sizeof(t)))
            (void)parse_float(t, &ry);
        else
            ry = rx;
        float mx = x * sx + ox;
        float my = y * sy + oy;
        float mw = rw * sx;
        float mh = rh * sy;
        float mrx = rx * sx;
        float mry = ry * sy;
        if (st.has_stroke)
            stroke_round_rect(mask, w, h, mx, my, mw, mh, mrx, mry,
                              st.stroke_w * ((sx + sy) * 0.5f));
        if (st.has_fill) {
            Pt pts[8];
            pts[0] = {mx + mrx, my};
            pts[1] = {mx + mw - mrx, my};
            pts[2] = {mx + mw, my + mry};
            pts[3] = {mx + mw, my + mh - mry};
            pts[4] = {mx + mw - mrx, my + mh};
            pts[5] = {mx + mrx, my + mh};
            pts[6] = {mx, my + mh - mry};
            pts[7] = {mx, my + mry};
            fill_poly(mask, w, h, pts, 8);
        }
        return;
    }

    if (strncmp(tag, "<line", 5) == 0) {
        float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        char t[32];
        if (!attr_find(tag, "x1", t, sizeof(t)) || !parse_float(t, &x1))
            return;
        if (!attr_find(tag, "y1", t, sizeof(t)) || !parse_float(t, &y1))
            return;
        if (!attr_find(tag, "x2", t, sizeof(t)) || !parse_float(t, &x2))
            return;
        if (!attr_find(tag, "y2", t, sizeof(t)) || !parse_float(t, &y2))
            return;
        if (st.has_stroke)
            stroke_segment(mask, w, h, x1 * sx + ox, y1 * sy + oy,
                           x2 * sx + ox, y2 * sy + oy,
                           st.stroke_w * ((sx + sy) * 0.5f));
    }
}

static bool rasterize_svg(const char *svg, uint8_t *mask, int out_w, int out_h)
{
    memset(mask, 0, (size_t)out_w * (size_t)out_h);

    float vb_x = 0, vb_y = 0, vb_w = 16, vb_h = 16;
    char vb[64];
    const char *svg_tag = find_str(svg, "<svg");
    if (!svg_tag)
        return false;
    const char *gt = find_char(svg_tag, '>');
    if (!gt)
        return false;
    char open[256];
    size_t olen = (size_t)(gt - svg_tag + 1);
    if (olen >= sizeof(open))
        olen = sizeof(open) - 1;
    memcpy(open, svg_tag, olen);
    open[olen] = 0;

    Style root{};
    root.has_fill = false;
    root.has_stroke = true;
    root.stroke_w = 1.4f;
    root = style_from_tag(open, root);

    if (attr_find(open, "viewBox", vb, sizeof(vb))) {
        const char *p = vb;
        float a = 0, b = 0, c = 0, d = 0;
        p = parse_float(p, &a);
        if (p)
            p = parse_float(p, &b);
        if (p)
            p = parse_float(p, &c);
        if (p)
            p = parse_float(p, &d);
        if (c > 0.0f && d > 0.0f) {
            vb_x = a;
            vb_y = b;
            vb_w = c;
            vb_h = d;
        }
    }

    float pad = 1.0f;
    float sx = ((float)out_w - pad * 2.0f) / vb_w;
    float sy = ((float)out_h - pad * 2.0f) / vb_h;
    float ox = pad - vb_x * sx;
    float oy = pad - vb_y * sy;

    const char *p = gt + 1;
    while (*p) {
        const char *lt = find_char(p, '<');
        if (!lt)
            break;
        if (lt[1] == '/' || lt[1] == '!' || lt[1] == '?') {
            p = lt + 1;
            continue;
        }
        const char *end = find_char(lt, '>');
        if (!end)
            break;
        char tag[640];
        size_t n = (size_t)(end - lt + 1);
        if (n >= sizeof(tag))
            n = sizeof(tag) - 1;
        memcpy(tag, lt, n);
        tag[n] = 0;
        if (strncmp(tag, "<path", 5) == 0 || strncmp(tag, "<circle", 7) == 0 ||
            strncmp(tag, "<rect", 5) == 0 || strncmp(tag, "<line", 5) == 0) {
            draw_element(mask, out_w, out_h, tag, root, sx, sy, ox, oy);
        }
        p = end + 1;
    }
    return true;
}

} // namespace

bool SvgIcon::load(const char *path, int out_w, int out_h)
{
    ready_ = false;
    w_ = 0;
    h_ = 0;
    if (!path || out_w <= 0 || out_h <= 0 || out_w > kMaxW || out_h > kMaxH)
        return false;

    int fd = (int)open(path, O_RDONLY);
    if (fd < 0)
        return false;

    char buf[kSvgBytes];
    memset(buf, 0, sizeof(buf));
    long n = read(fd, buf, sizeof(buf) - 1);
    (void)close(fd);
    if (n <= 0)
        return false;
    buf[n] = 0;

    if (!rasterize_svg(buf, mask_, out_w, out_h))
        return false;

    w_ = out_w;
    h_ = out_h;
    ready_ = true;
    return true;
}

uint8_t SvgIcon::alpha_at(int x, int y) const
{
    if (!ready_ || x < 0 || y < 0 || x >= w_ || y >= h_)
        return 0;
    return mask_[y * w_ + x];
}

void SvgIcon::blit(Surface &dst, int x, int y, Color tint) const
{
    if (!ready_ || !dst.valid())
        return;
    for (int j = 0; j < h_; j++) {
        for (int i = 0; i < w_; i++) {
            uint8_t a = mask_[j * w_ + i];
            if (a == 0)
                continue;
            dst.blend(x + i, y + j, color_mul_alpha(tint, a));
        }
    }
}

} // namespace hsrc::sdk
