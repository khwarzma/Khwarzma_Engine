#ifndef KHSHIELD_VISUAL_OCR_BRIDGE_HPP
#define KHSHIELD_VISUAL_OCR_BRIDGE_HPP

#include <string>
#include <vector>
#include <memory>
#include "khshield/visual/skin_contour.hpp"
#include "khshield/text/aho_corasick.hpp"
#include "khshield/text/arabizi.hpp"

namespace khshield::visual {

struct OCRResult {
    std::string extracted_text;
    std::string normalized_text;
    bool violation_detected{false};
    std::string matched_pattern;
};

class OCRBridge {
public:
    explicit OCRBridge(const text::AhoCorasick* text_matcher = nullptr);
    ~OCRBridge() = default;

    void set_text_matcher(const text::AhoCorasick* text_matcher) noexcept {
        text_matcher_ = text_matcher;
    }

    // Process raw image buffer, extract text tokens via fast bitmap OCR scan, and moderate
    [[nodiscard]] OCRResult process_frame(const ImageBuffer& image) const;

private:
    const text::AhoCorasick* text_matcher_{nullptr};

    // Lightweight CPU-bound bitmap pattern extract for text overlay region detection
    [[nodiscard]] std::string extract_text_lightweight(const ImageBuffer& image) const;
};

} // namespace khshield::visual

#endif // KHSHIELD_VISUAL_OCR_BRIDGE_HPP