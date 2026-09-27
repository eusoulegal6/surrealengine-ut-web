#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Decode S3TC as specified by EXT_texture_compression_s3tc. CPU fallback keeps
// original packages usable when the browser lacks the S3TC GPU extension.
inline std::vector<uint8_t> DecodeS3TC(const std::vector<uint8_t>& data, int width, int height, int format)
{
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || (format != 1 && format != 2 && format != 3))
        throw std::runtime_error("Invalid S3TC image");
    const size_t blockBytes = format == 1 ? 8 : 16;
    const size_t columns = (width + 3) / 4, rows = (height + 3) / 4;
    if (data.size() != columns * rows * blockBytes) throw std::runtime_error("Truncated S3TC image");
    std::vector<uint8_t> result(size_t(width) * height * 4);
    auto word = [](const uint8_t* p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); };
    auto color = [](unsigned c) -> std::array<uint8_t, 4> {
        unsigned r = c >> 11, g = (c >> 5) & 63, b = c & 31;
        return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2)), 255};
    };
    for (size_t by = 0; by < rows; by++) for (size_t bx = 0; bx < columns; bx++) {
        const uint8_t* block = data.data() + (by * columns + bx) * blockBytes;
        const uint8_t* rgb = block + (format == 1 ? 0 : 8);
        unsigned c0 = word(rgb), c1 = word(rgb + 2);
        std::array<std::array<uint8_t, 4>, 4> colors{color(c0), color(c1), {0,0,0,255}, {0,0,0,255}};
        for (int channel = 0; channel < 3; channel++) {
            if (c0 > c1 || format != 1) {
                colors[2][channel] = (2 * colors[0][channel] + colors[1][channel]) / 3;
                colors[3][channel] = (colors[0][channel] + 2 * colors[1][channel]) / 3;
            } else colors[2][channel] = (colors[0][channel] + colors[1][channel]) / 2;
        }
        if (format == 1 && c0 <= c1) colors[3] = {0,0,0,0};
        uint32_t indices = uint32_t(rgb[4]) | (uint32_t(rgb[5]) << 8) | (uint32_t(rgb[6]) << 16) | (uint32_t(rgb[7]) << 24);
        std::array<uint8_t,8> alpha{};
        uint64_t alphaBits = 0;
        if (format == 3) {
            alpha[0] = block[0]; alpha[1] = block[1];
            if (alpha[0] > alpha[1]) {
                for (int i = 1; i <= 6; i++) alpha[i+1] = ((7-i)*alpha[0] + i*alpha[1]) / 7;
            } else {
                for (int i = 1; i <= 4; i++) alpha[i+1] = ((5-i)*alpha[0] + i*alpha[1]) / 5;
                alpha[6] = 0; alpha[7] = 255;
            }
            for (int i = 0; i < 6; i++) alphaBits |= uint64_t(block[i+2]) << (8*i);
        }
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
            const int pixel = y * 4 + x;
            size_t px = bx * 4 + x, py = by * 4 + y;
            if (px >= size_t(width) || py >= size_t(height)) continue;
            auto c = colors[(indices >> (2 * pixel)) & 3];
            if (format == 2) c[3] = ((block[pixel / 2] >> ((pixel % 2) * 4)) & 15) * 17;
            if (format == 3) c[3] = alpha[(alphaBits >> (pixel * 3)) & 7];
            for (int channel = 0; channel < 4; channel++) result[(py * width + px) * 4 + channel] = c[channel];
        }
    }
    return result;
}
