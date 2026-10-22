#pragma once
/* Framebuffer + text rendering. Raw linear FB (libnx framebuffer.h),
   glyphs rasterized with FreeType against the Horizon system font
   (pl service — no font files needed). All drawing is plain CPU alpha
   blend — fast enough at 1280x720 for UI overlays. */
#include <switch.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uss {

struct Color { uint8_t r, g, b, a; };
static inline Color rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return {r, g, b, a};
}

class Fb {
public:
    bool init();                       // window + FB + pl + freetype
    void close();

    uint32_t* begin();                 // next FB scanout (nullptr on fail)
    void end();                        // present

    int width() const { return w_; }
    int height() const { return h_; }

    // primitives — operate on the buffer returned by begin()
    void clear(uint32_t* buf, Color c);
    void fill(uint32_t* buf, int x, int y, int w, int h, Color c);
    // premultiplied-alpha blit of an RGBA image into the FB, scaled
    // to (dx,dy,dw,dh) with bilinear sampling; preserves aspect when
    // dw==0/dh==0 against fit-boxes (caller letterboxes)
    void blit(uint32_t* buf, const uint8_t* img, int iw, int ih,
              int dx, int dy, int dw, int dh);
    // horizontal line thickness variant used by the pitch trail
    void hline(uint32_t* buf, int x0, int x1, int y, int th, Color c);

    // text — size in px height; returns pixel width of drawn string
    int text(uint32_t* buf, const std::string& s, int x, int y,
             int px, Color c, bool centered = false);
    int textWidth(const std::string& s, int px);

private:
    NWindow* win_ = nullptr;
    Framebuffer fb_;
    int w_ = 0, h_ = 0;
    int stride_ = 0;                     // pixels per row (from begin)
    void* ftLib_ = nullptr;
    void* ftFace_ = nullptr;
    struct Glyph { int w, h, dx, dy; std::vector<uint8_t> alpha; };
    std::unordered_map<uint64_t, Glyph> glyphs_;  // (px<<20)|codepoint
    const Glyph& glyph(uint32_t cp, int px);
};

} // namespace uss
