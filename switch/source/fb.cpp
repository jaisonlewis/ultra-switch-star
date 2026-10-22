#include "fb.h"
#include <algorithm>
#include <cstring>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace uss {

static inline uint32_t pack(Color c) {
    return (uint32_t)c.a << 24 | c.r << 16 | c.g << 8 | c.b;
}

bool Fb::init() {
    win_ = nwindowGetDefault();
    w_ = 1280; h_ = 720;
    Result rc = framebufferCreate(&fb_, win_, w_, h_,
                                  PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) return false;
    framebufferMakeLinear(&fb_);

    if (R_FAILED(plInitialize(PlServiceType_User))) return false;
    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))
        && R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_NintendoExt)))
        return false;

    if (FT_Init_FreeType((FT_Library*)&ftLib_)) return false;
    if (FT_New_Memory_Face(*(FT_Library*)&ftLib_, (const FT_Byte*)fd.address,
                           (FT_Long)fd.size, 0, (FT_Face*)&ftFace_))
        return false;
    return true;
}

void Fb::close() {
    if (ftFace_) FT_Done_Face(*(FT_Face*)&ftFace_);
    if (ftLib_) FT_Done_FreeType(*(FT_Library*)&ftLib_);
    framebufferClose(&fb_);
    plExit();
}

uint32_t* Fb::begin() {
    u32 stride = 0;                      // bytes
    uint32_t* buf = (uint32_t*)framebufferBegin(&fb_, &stride);
    if (buf) stride_ = stride / sizeof(uint32_t);
    return buf;
}

void Fb::end() { framebufferEnd(&fb_); }

void Fb::clear(uint32_t* buf, Color c) {
    uint32_t px = pack(c);
    for (int i = 0; i < stride_ * h_; ++i) buf[i] = px;
}

void Fb::fill(uint32_t* buf, int x, int y, int w, int h, Color c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > w_) w = w_ - x;
    if (y + h > h_) h = h_ - y;
    if (w <= 0 || h <= 0) return;
    uint32_t px = pack(c);
    if (c.a == 255) {
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i) buf[j * stride_ + i] = px;
    } else {
        int ia = 255 - c.a;
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i) {
                uint32_t& d = buf[j * stride_ + i];
                uint8_t dr = d >> 16, dg = d >> 8, db = d;
                d = 0xff000000
                    | (((c.r * c.a + dr * ia) / 255) << 16)
                    | (((c.g * c.a + dg * ia) / 255) << 8)
                    | ((c.b * c.a + db * ia) / 255);
            }
    }
}

void Fb::blit(uint32_t* buf, const uint8_t* img, int iw, int ih,
              int dx, int dy, int dw, int dh) {
    if (dw <= 0 || dh <= 0 || !img) return;
    for (int j = 0; j < dh; ++j) {
        int sy = j * ih / dh, ty = dy + j;
        if (ty < 0 || ty >= h_) continue;
        for (int i = 0; i < dw; ++i) {
            int tx = dx + i;
            if (tx < 0 || tx >= w_) continue;
            const uint8_t* s = img + (sy * iw + i * iw / dw) * 4;
            int a = s[3], ia = 255 - a;
            uint32_t& d = buf[ty * stride_ + tx];
            d = 0xff000000
                | (((s[0] * a + ((d >> 16) & 0xff) * ia) / 255) << 16)
                | (((s[1] * a + ((d >> 8) & 0xff) * ia) / 255) << 8)
                | ((s[2] * a + (d & 0xff) * ia) / 255);
        }
    }
}

void Fb::hline(uint32_t* buf, int x0, int x1, int y, int th, Color c) {
    if (x1 < x0) std::swap(x0, x1);
    fill(buf, x0, y - th / 2, x1 - x0 + 1, th, c);
}

const Fb::Glyph& Fb::glyph(uint32_t cp, int px) {
    uint64_t key = (uint64_t)px << 20 | cp;
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) return it->second;
    Glyph g{};
    FT_Face face = *(FT_Face*)&ftFace_;
    FT_Set_Pixel_Sizes(face, 0, px);
    if (FT_Load_Char(face, cp, FT_LOAD_RENDER) == 0) {
        auto& bm = face->glyph->bitmap;
        g.w = bm.width; g.h = bm.rows;
        g.dx = face->glyph->bitmap_left;
        g.dy = px - face->glyph->bitmap_top;
        g.alpha.assign(bm.buffer, bm.buffer + g.w * g.h);
    }
    return glyphs_.emplace(key, std::move(g)).first->second;
}

int Fb::textWidth(const std::string& s, int px) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp;
        uint8_t c = s[i];
        if (c < 0x80) { cp = c; ++i; }
        else if (c < 0xE0) { cp = c & 31; cp = cp << 6 | (s[++i] & 63); ++i; }
        else if (c < 0xF0) { cp = c & 15; cp = cp << 6 | (s[++i] & 63); cp = cp << 6 | (s[++i] & 63); ++i; }
        else { cp = c & 7; cp = cp << 6 | (s[++i] & 63); cp = cp << 6 | (s[++i] & 63); cp = cp << 6 | (s[++i] & 63); ++i; }
        w += glyph(cp, px).w + 1;
    }
    return w;
}

int Fb::text(uint32_t* buf, const std::string& s, int x, int y,
             int px, Color c, bool centered) {
    if (centered) x -= textWidth(s, px) / 2;
    int pen = x;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp;
        uint8_t ch = s[i];
        if (ch < 0x80) { cp = ch; ++i; }
        else if (ch < 0xE0) { cp = ch & 31; cp = cp << 6 | (s[++i] & 63); ++i; }
        else if (ch < 0xF0) { cp = ch & 15; cp = cp << 6 | (s[++i] & 63); cp = cp << 6 | (s[++i] & 63); ++i; }
        else { cp = ch & 7; cp = cp << 6 | (s[++i] & 63); cp = cp << 6 | (s[++i] & 63); cp = cp << 6 | (s[++i] & 63); ++i; }
        const Glyph& g = glyph(cp, px);
        for (int gy = 0; gy < g.h; ++gy) {
            int ty = y + g.dy + gy;
            if (ty < 0 || ty >= h_) continue;
            for (int gx = 0; gx < g.w; ++gx) {
                int tx = pen + g.dx + gx;
                if (tx < 0 || tx >= w_) continue;
                int a = g.alpha[gy * g.w + gx] * c.a / 255;
                int ia = 255 - a;
                uint32_t& d = buf[ty * stride_ + tx];
                d = 0xff000000
                    | (((c.r * a + ((d >> 16) & 0xff) * ia) / 255) << 16)
                    | (((c.g * a + ((d >> 8) & 0xff) * ia) / 255) << 8)
                    | ((c.b * a + (d & 0xff) * ia) / 255);
            }
        }
        pen += g.w + 1;
    }
    return pen - x;
}

} // namespace uss
