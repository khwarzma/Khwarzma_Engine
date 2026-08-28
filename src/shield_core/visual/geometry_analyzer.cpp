#include "khshield/visual/geometry_analyzer.hpp"
#include <queue>
#include <algorithm>

namespace khshield::visual {

std::vector<ConnectedComponent> GeometryAnalyzer::find_connected_components(
    std::span<const uint8_t> binary_mask,
    size_t width,
    size_t height,
    size_t min_pixel_area
) const {
    std::vector<ConnectedComponent> components;
    if (binary_mask.empty() || width == 0 || height == 0) return components;

    std::vector<uint8_t> visited(width * height, 0);

    constexpr int dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int dy[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            size_t start_idx = y * width + x;

            if (binary_mask[start_idx] == 1 && visited[start_idx] == 0) {
                size_t min_x = x, max_x = x;
                size_t min_y = y, max_y = y;
                size_t pixel_count = 0;

                std::queue<std::pair<size_t, size_t>> q;
                q.push({x, y});
                visited[start_idx] = 1;

                while (!q.empty()) {
                    auto [cx, cy] = q.front();
                    q.pop();

                    ++pixel_count;
                    min_x = std::min(min_x, cx);
                    max_x = std::max(max_x, cx);
                    min_y = std::min(min_y, cy);
                    max_y = std::max(max_y, cy);

                    for (int i = 0; i < 8; ++i) {
                        int nx = static_cast<int>(cx) + dx[i];
                        int ny = static_cast<int>(cy) + dy[i];

                        if (nx >= 0 && nx < static_cast<int>(width) &&
                            ny >= 0 && ny < static_cast<int>(height)) {
                            size_t n_idx = static_cast<size_t>(ny) * width + static_cast<size_t>(nx);
                            if (binary_mask[n_idx] == 1 && visited[n_idx] == 0) {
                                visited[n_idx] = 1;
                                q.push({static_cast<size_t>(nx), static_cast<size_t>(ny)});
                            }
                        }
                    }
                }

                if (pixel_count >= min_pixel_area) {
                    components.push_back({
                        .bbox = {
                            .x = min_x,
                            .y = min_y,
                            .width = max_x - min_x + 1,
                            .height = max_y - min_y + 1,
                            .area = (max_x - min_x + 1) * (max_y - min_y + 1)
                        },
                        .pixel_count = pixel_count
                    });
                }
            }
        }
    }

    return components;
}

float GeometryAnalyzer::evaluate_exposure_ratio(
    const std::vector<ConnectedComponent>& components,
    size_t total_frame_area
) const {
    if (total_frame_area == 0 || components.empty()) return 0.0f;

    size_t significant_skin_pixels = 0;
    for (const auto& comp : components) {
        // Simple geometric aspect ratio filter: flag contiguous blocks larger than threshold
        if (comp.pixel_count > (total_frame_area / 100)) { // Component > 1% of frame
            significant_skin_pixels += comp.pixel_count;
        }
    }

    return static_cast<float>(significant_skin_pixels) / static_cast<float>(total_frame_area);
}

} // namespace khshield::visual