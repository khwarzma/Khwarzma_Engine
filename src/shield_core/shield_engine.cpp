#include "khshield/shield_engine.hpp"
#include "khshield/text/arabizi.hpp"
#include <algorithm>
#include <sstream>

namespace khshield {

ShieldEngine::ShieldEngine(const ShieldConfig& config)
    : preset_(config.preset), ocr_bridge_(&text_matcher_) {

    if (!config.initial_keywords.empty()) {
        add_keywords(config.initial_keywords);
    }

    if (!config.model_path.empty()) {
        sam_engine_.load_model(config.model_path);
    }
}

ShieldEngine::ShieldEngine(Preset preset)
    : ShieldEngine(ShieldConfig{.preset = preset}) {}

void ShieldEngine::add_keyword(std::string_view keyword) {
    if (keyword.empty()) return;
    text_matcher_.add_keyword(keyword);
    text_matcher_.build();
}

void ShieldEngine::add_keywords(const std::vector<std::string>& keywords) {
    if (keywords.empty()) return;
    for (const auto& kw : keywords) {
        if (!kw.empty()) {
            text_matcher_.add_keyword(kw);
        }
    }
    text_matcher_.build();
}

bool ShieldEngine::load_model(const std::filesystem::path& model_path) {
    if (model_path.empty()) return false;
    return sam_engine_.load_model(model_path);
}

ContentReport ShieldEngine::analyze_text(std::string_view text) {
    ContentReport report;
    if (text.empty()) return report;

    // 1. Arabizi Normalization
    std::string normalized_text = text::ArabiziNormalizer::normalize(text);

    // 2. Exact Keyword Search via Aho-Corasick Automaton
    auto matches = text_matcher_.search(normalized_text);
    
    // Check tokens if raw stream didn't capture tokenized arabizi variations
    if (matches.empty()) {
        std::istringstream stream(normalized_text);
        std::string token;
        while (stream >> token) {
            auto token_matches = text_matcher_.search(token);
            if (!token_matches.empty()) {
                matches = std::move(token_matches);
                break;
            }
        }
    }

    if (!matches.empty()) {
        report.text.flagged = true;
        report.text.matched_pattern = matches[0].pattern;
        report.is_safe = false;
        report.violation_type = "PROFANITY_EXACT";
        report.risk_score = 1.0f;
        return report;
    }

    // 3. Quantized Semantic Evaluation via SAM Text Model
    if (sam_engine_.is_loaded()) {
        auto sam_result = sam_engine_.evaluate(normalized_text);
        if (sam_result.is_valid && sam_result.risk_score >= 0.5f) {
            report.text.flagged = true;
            report.text.matched_pattern = "SEMANTIC_RISK";
            report.is_safe = false;
            report.violation_type = sam_result.violation_category;
            report.risk_score = sam_result.risk_score;
            return report;
        }
    }

    report.is_safe = true;
    report.risk_score = 0.0f;
    return report;
}

ContentReport ShieldEngine::analyze_image_buffer(const visual::ImageBuffer& image) {
    ContentReport report;
    if (image.data.empty() || image.width == 0 || image.height == 0) return report;

    float skin_percentage = skin_analyzer_.calculate_skin_ratio(image);
    report.visual.skin_percentage = skin_percentage;

    float exposure_threshold = (preset_ == Preset::STRICT) ? 0.25f : 0.45f;

    if (skin_percentage >= exposure_threshold) {
        report.visual.flagged = true;
        report.is_safe = false;
        report.violation_type = "EXPLICIT_BODY";
        report.risk_score = std::min(1.0f, skin_percentage / exposure_threshold);
    }

    auto ocr_result = ocr_bridge_.process_frame(image);
    if (ocr_result.violation_detected) {
        report.text.flagged = true;
        report.text.matched_pattern = ocr_result.matched_pattern;
        report.is_safe = false;
        report.violation_type = "OCR_PROFANITY";
        report.risk_score = 1.0f;
    }

    return report;
}

ContentReport ShieldEngine::analyze_audio_buffer(std::span<const float> pcm_samples, uint32_t sample_rate) {
    ContentReport report;
    if (pcm_samples.empty() || sample_rate == 0) return report;

    auto spec_res = spectrum_analyzer_.analyze(pcm_samples, sample_rate);
    if (spec_res.is_corrupted) {
        report.audio.corrupted = true;
        report.is_safe = false;
        report.violation_type = "CORRUPTED_AUDIO";
        report.risk_score = 0.0f;
        return report;
    }

    auto bpm_res = bpm_analyzer_.calculate_bpm(pcm_samples, sample_rate);
    report.audio.bpm = bpm_res.bpm;

    if (bpm_res.music_detected) {
        if (preset_ == Preset::STRICT) {
            report.audio.flagged = true;
            report.is_safe = false;
            report.violation_type = "MUSIC_DETECTED";
            report.risk_score = bpm_res.confidence;
        } else if (preset_ == Preset::MODERATE && spec_res.rms_energy > 0.80f) {
            report.audio.flagged = true;
            report.is_safe = false;
            report.violation_type = "HIGH_ENERGY_AUDIO";
            report.risk_score = bpm_res.confidence;
        }
    }

    return report;
}

} // namespace khshield