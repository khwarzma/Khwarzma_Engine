#ifndef KHSHIELD_VISUAL_SKIN_CONTOUR_HPP
#define KHSHIELD_VISUAL_SKIN_CONTOUR_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>

namespace khshield::visual {

struct ImageBuffer {
    std::span<const uint8_t> data;
    size_t width{0};
    size_t height{0};
    size_t channels{3}; // 3 for RGB/BGR, 4 for RGBA
};

class SkinContourAnalyzer {
public:
    SkinContourAnalyzer() = default;

    // Evaluates YCbCr + HSV skin color criteria on raw image buffer
    [[nodiscard]] float calculate_skin_ratio(const ImageBuffer& image) const;

    // Generates a binary mask (1 for skin, 0 for non-skin) for geometric analysis
    [[nodiscard]] std::vector<uint8_t> generate_skin_mask(const ImageBuffer& image) const;

private:
    [[nodiscard]] static bool is_skin_pixel_rgb(uint8_t r, uint8_t g, uint8_t b) noexcept;
};

} // namespace khshield::visual

#endif // KHSHIELD_VISUAL_SKIN_CONTOUR_HPP