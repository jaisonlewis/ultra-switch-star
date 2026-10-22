#pragma once
/* JPEG/PNG cover loader — stb_image, returns RGBA8 rows. */
#include <cstdint>
#include <string>
#include <vector>

namespace uss {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool ok() const { return w > 0 && !rgba.empty(); }
};

Image loadImage(const std::string& path);

} // namespace uss
