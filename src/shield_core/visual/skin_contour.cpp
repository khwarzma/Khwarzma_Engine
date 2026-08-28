#include "khshield/visual/skin_contour.hpp"
#include <algorithm>
#include <cmath>

namespace khshield::visual {

bool SkinContourAnalyzer::is_skin_pixel_rgb(uint8_t r_u, uint8_t g_u, uint8_t b_u) noexcept {
    float r = static_cast<float>(r_u);
    float g = static_cast<float>(g_u);
    float b = static_cast<float>(b_u);

    // RGB direct threshold rules
    bool rgb_rule = (r > 95.0f) && (g > 40.0f) && (b > 20.0f) &&
                     ((std::max({r, g, b}) - std::min({r, g, b})) > 15.0f) &&
                     (std::abs(r - g) > 15.0f) && (r > g) && (r > b);

    if (!rgb_rule) return false;

    // Fast integer-based YCbCr conversion for CPU execution
    // Y  =  0.299R + 0.587G + 0.114B
    // Cb = -0.1687R - 0.3313G + 0.5B + 128
    // Cr =  0.5R - 0.4187G - 0.0813B + 128
    float cb = 128.0f - 0.168736f * r - 0.331264f * g + 0.5f * b;
    float cr = 128.0f + 0.5f * r - 0.418688f * g - 0.081312f * b;

    return (cb >= 77.0f && cb <= 127.0f) && (cr >= 133.0f && cr <= 173.0f);
}

float SkinContourAnalyzer::calculate_skin_ratio(const ImageBuffer& image) const {
    if (image.data.empty() || image.width == 0 || image.height == 0 || image.channels < 3) {
        return 0.0f;
    }

    const size_t total_pixels = image.width * image.height;
    size_t skin_pixel_count = 0;

    for (size_t i = 0; i < total_pixels; ++i) {
        size_t idx = i * image.channels;
        uint8_t r = image.data[idx];
        uint8_t g = image.data[idx + 1];
        uint8_t b = image.data[idx + 2];

        if (is_skin_pixel_rgb(r, g, b)) {
            ++skin_pixel_count;
        }
    }

    return static_cast<float>(skin_pixel_count) / static_cast<float>(total_pixels);
}

std::vector<uint8_t> SkinContourAnalyzer::generate_skin_mask(const ImageBuffer& image) const {
    const size_t total_pixels = image.width * image.height;
    std::vector<uint8_t> mask(total_pixels, 0);

    if (image.data.empty() || image.width == 0 || image.height == 0 || image.channels < 3) {
        return mask;
    }

    for (size_t i = 0; i < total_pixels; ++i) {
        size_t idx = i * image.channels;
        if (is_skin_pixel_rgb(image.data[idx], image.data[idx + 1], image.data[idx + 2])) {
            mask[i] = 1;
        }
    }

    return mask;
}

} // namespace khshield::visual