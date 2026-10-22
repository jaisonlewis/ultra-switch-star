#include "img.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO          // we feed it memory — sdmc/ums paths
#define STBI_FAILURE_USERMSG
#include "../thirdparty/stb/stb_image.h"

#include <cstdio>

namespace uss {

Image loadImage(const std::string& path) {
    Image out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 32L * 1024 * 1024) { fclose(f); return out; }
    std::vector<uint8_t> buf((size_t)n);
    if (fread(buf.data(), 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return out;
    }
    fclose(f);
    int w = 0, h = 0, ch = 0;
    stbi_uc* px = stbi_load_from_memory(buf.data(), (int)n, &w, &h, &ch, 4);
    if (!px) return out;
    out.w = w; out.h = h;
    out.rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return out;
}

} // namespace uss
