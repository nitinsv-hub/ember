#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ember {

struct ImageF {
    int width = 0, height = 0;
    std::vector<float> rgb;

    float& at(int x, int y, int c) { return rgb[(static_cast<size_t>(y) * width + x) * 3 + c]; }
    const float& at(int x, int y, int c) const {
        return rgb[(static_cast<size_t>(y) * width + x) * 3 + c];
    }
};

void write_png(const std::string& path, const std::vector<std::uint8_t>& rgb8,
               int width, int height);
void write_pfm(const std::string& path, const ImageF& image);
ImageF read_pfm(const std::string& path);

std::vector<std::uint8_t> tonemap_aces(const ImageF& hdr, float exposure);

}
