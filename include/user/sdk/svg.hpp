#pragma once

#include <user/sdk/color.hpp>
#include <user/sdk/gfx.hpp>

namespace hsrc::sdk {

/* Lightweight menubar icon: load simple SVG → alpha mask → tinted blit. */
class SvgIcon {
public:
    static constexpr int kMaxW = 32;
    static constexpr int kMaxH = 24;
    static constexpr int kMaxPixels = kMaxW * kMaxH;

    SvgIcon() = default;

    bool load(const char *path, int out_w = 16, int out_h = 16);
    bool valid() const { return ready_ && w_ > 0 && h_ > 0; }
    int width() const { return w_; }
    int height() const { return h_; }

    /* Draw with theme tint (alpha from mask). */
    void blit(Surface &dst, int x, int y, Color tint) const;

    /* Soft alpha coverage 0..255 at pixel. */
    uint8_t alpha_at(int x, int y) const;

private:
    uint8_t mask_[kMaxPixels]{};
    int w_ = 0;
    int h_ = 0;
    bool ready_ = false;
};

} // namespace hsrc::sdk
