#ifndef KHSHIELD_VISUAL_GEOMETRY_ANALYZER_HPP
#define KHSHIELD_VISUAL_GEOMETRY_ANALYZER_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>

namespace khshield::visual {

struct BoundingBox {
    size_t x{0};
    size_t y{0};
    size_t width{0};
    size_t height{0};
    size_t area{0};
};

struct ConnectedComponent {
    BoundingBox bbox;
    size_t pixel_count{0};
};

class GeometryAnalyzer {
public:
    GeometryAnalyzer() = default;

    // Evaluates connected components from binary skin masks using 8-connectivity BFS
    [[nodiscard]] std::vector<ConnectedComponent> find_connected_components(
        std::span<const uint8_t> binary_mask, 
        size_t width, 
        size_t height, 
        size_t min_pixel_area = 20
    ) const;

    // Calculates non-face exposure ratio after excluding upper bounding boxes (faces)
    [[nodiscard]] float evaluate_exposure_ratio(
        const std::vector<ConnectedComponent>& components,
        size_t total_frame_area
    ) const;
};

} // namespace khshield::visual

#endif // KHSHIELD_VISUAL_GEOMETRY_ANALYZER_HPP