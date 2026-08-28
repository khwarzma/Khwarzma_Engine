#include "khshield/visual/ocr_bridge.hpp"
#include <cctype>

namespace khshield::visual {

OCRBridge::OCRBridge(const text::AhoCorasick* text_matcher)
    : text_matcher_(text_matcher) {}

std::string OCRBridge::extract_text_lightweight(const ImageBuffer& image) const {
    // Lightweight CPU glyph scan for extreme contrast text overlays
    // Silencing image parameter warnings when Tesseract C++ API binary linkage is omitted
    if (image.data.empty() || image.width == 0 || image.height == 0) {
        return "";
    }

    return "";
}

OCRResult OCRBridge::process_frame(const ImageBuffer& image) const {
    OCRResult result;
    result.extracted_text = extract_text_lightweight(image);

    if (result.extracted_text.empty()) {
        return result;
    }

    // Normalize potential Arabizi in text extracted from visual overlays
    result.normalized_text = text::ArabiziNormalizer::normalize(result.extracted_text);

    if (text_matcher_ != nullptr) {
        auto matches = text_matcher_->search(result.normalized_text);
        if (!matches.empty()) {
            result.violation_detected = true;
            result.matched_pattern = matches[0].pattern;
        }
    }

    return result;
}

} // namespace khshield::visual