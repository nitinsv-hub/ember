#include "image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace ember {
namespace {

std::uint32_t crc32_of(const std::uint8_t* data, size_t n, std::uint32_t crc = 0xFFFFFFFFu) {
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

std::uint32_t adler32_of(const std::uint8_t* data, size_t n) {
    std::uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void push_be32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void push_chunk(std::vector<std::uint8_t>& out, const char tag[4],
                const std::vector<std::uint8_t>& data) {
    push_be32(out, static_cast<std::uint32_t>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data.begin(), data.end());
    const std::uint32_t crc =
        crc32_of(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
    push_be32(out, crc);
}
std::vector<std::uint8_t> deflate_stored(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> z;
    z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    z.push_back(0x78);
    z.push_back(0x01);

    size_t offset = 0;
    if (raw.empty()) {
        z.push_back(0x01);
        z.push_back(0x00); z.push_back(0x00);
        z.push_back(0xFF); z.push_back(0xFF);
    }
    while (offset < raw.size()) {
        const size_t chunk = std::min<size_t>(65535, raw.size() - offset);
        const bool last = (offset + chunk == raw.size());
        z.push_back(last ? 0x01 : 0x00);
        const std::uint16_t len = static_cast<std::uint16_t>(chunk);
        const std::uint16_t nlen = static_cast<std::uint16_t>(~len);
        z.push_back(static_cast<std::uint8_t>(len & 0xFF));
        z.push_back(static_cast<std::uint8_t>(len >> 8));
        z.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
        z.push_back(static_cast<std::uint8_t>(nlen >> 8));
        z.insert(z.end(), raw.begin() + offset, raw.begin() + offset + chunk);
        offset += chunk;
    }
    push_be32(z, adler32_of(raw.data(), raw.size()));
    return z;
}

}

void write_png(const std::string& path, const std::vector<std::uint8_t>& rgb8,
               int width, int height) {
    const size_t expect = static_cast<size_t>(width) * height * 3;
    if (rgb8.size() != expect) throw std::runtime_error("write_png: pixel buffer size mismatch");
    std::vector<std::uint8_t> raw;
    raw.reserve(expect + static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::uint8_t* row = rgb8.data() + static_cast<size_t>(y) * width * 3;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 3);
    }

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::vector<std::uint8_t> ihdr;
    push_be32(ihdr, static_cast<std::uint32_t>(width));
    push_be32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    push_chunk(png, "IHDR", ihdr);
    push_chunk(png, "IDAT", deflate_stored(raw));
    push_chunk(png, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open for writing: " + path);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

void write_pfm(const std::string& path, const ImageF& image) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open for writing: " + path);
    f << "PF\n" << image.width << " " << image.height << "\n-1.0\n";
    for (int y = image.height - 1; y >= 0; --y) {
        const float* row = image.rgb.data() + static_cast<size_t>(y) * image.width * 3;
        f.write(reinterpret_cast<const char*>(row),
                static_cast<std::streamsize>(sizeof(float) * 3 * image.width));
    }
}

ImageF read_pfm(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);

    std::string magic;
    int w = 0, h = 0;
    double scale = 0.0;
    f >> magic >> w >> h >> scale;
    if (magic != "PF") throw std::runtime_error("not a colour PFM: " + path);
    f.get();

    ImageF img;
    img.width = w;
    img.height = h;
    img.rgb.resize(static_cast<size_t>(w) * h * 3);
    for (int y = h - 1; y >= 0; --y) {
        f.read(reinterpret_cast<char*>(img.rgb.data() + static_cast<size_t>(y) * w * 3),
               static_cast<std::streamsize>(sizeof(float) * 3 * w));
    }
    if (!f) throw std::runtime_error("truncated PFM: " + path);
    return img;
}

std::vector<std::uint8_t> tonemap_aces(const ImageF& hdr, float exposure) {
    constexpr float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    std::vector<std::uint8_t> out(hdr.rgb.size());
    for (size_t i = 0; i < hdr.rgb.size(); ++i) {
        float x = std::max(0.0f, hdr.rgb[i]) * exposure;
        x = std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
        const float srgb = (x <= 0.0031308f) ? (x * 12.92f)
                                             : (1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f);
        out[i] = static_cast<std::uint8_t>(std::clamp(srgb, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return out;
}

}
