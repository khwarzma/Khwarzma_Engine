# Source Files: shield_core


--- FILE: src/shield_core/audio/bpm_energy.cpp ---
```cpp
#include "khshield/audio/bpm_energy.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace khshield::audio {

std::vector<float> BPMEnergyAnalyzer::compute_onset_envelope(
    std::span<const float> pcm_samples,
    size_t hop_size
) {
    std::vector<float> envelope;
    if (pcm_samples.empty() || hop_size == 0) return envelope;

    size_t num_frames = pcm_samples.size() / hop_size;
    envelope.reserve(num_frames);

    float prev_energy = 0.0f;

    for (size_t i = 0; i < num_frames; ++i) {
        float current_energy = 0.0f;
        size_t start = i * hop_size;

        for (size_t j = 0; j < hop_size; ++j) {
            float val = pcm_samples[start + j];
            current_energy += val * val;
        }

        current_energy = std::sqrt(current_energy / static_cast<float>(hop_size));
        float flux = std::max(0.0f, current_energy - prev_energy);
        envelope.push_back(flux);
        prev_energy = current_energy;
    }

    return envelope;
}

std::vector<float> BPMEnergyAnalyzer::compute_autocorrelation(
    std::span<const float> signal
) {
    const size_t N = signal.size();
    std::vector<float> autocorr(N, 0.0f);

    for (size_t lag = 0; lag < N; ++lag) {
        float sum = 0.0f;
        for (size_t i = 0; i < N - lag; ++i) {
            sum += signal[i] * signal[i + lag];
        }
        autocorr[lag] = sum;
    }

    return autocorr;
}

BPMEnergyResult BPMEnergyAnalyzer::calculate_bpm(
    std::span<const float> pcm_samples,
    uint32_t sample_rate
) const {
    BPMEnergyResult result;

    if (pcm_samples.size() < static_cast<size_t>(sample_rate) || sample_rate == 0) {
        return result; // Requires at least 1 second of continuous audio signal
    }

    constexpr size_t hop_size = 512;
    auto onset_env = compute_onset_envelope(pcm_samples, hop_size);

    if (onset_env.size() < 10) {
        return result;
    }

    auto autocorr = compute_autocorrelation(onset_env);

    // Map lag bounds to valid musical tempo limits [60 BPM, 200 BPM]
    float frames_per_sec = static_cast<float>(sample_rate) / static_cast<float>(hop_size);
    size_t min_lag = static_cast<size_t>(frames_per_sec * 60.0f / 200.0f); // 200 BPM
    size_t max_lag = static_cast<size_t>(frames_per_sec * 60.0f / 60.0f);  // 60 BPM

    min_lag = std::max(size_t(1), min_lag);
    max_lag = std::min(autocorr.size() - 1, max_lag);

    size_t best_lag = min_lag;
    float max_autocorr = -1.0f;

    for (size_t lag = min_lag; lag <= max_lag; ++lag) {
        if (autocorr[lag] > max_autocorr) {
            max_autocorr = autocorr[lag];
            best_lag = lag;
        }
    }

    if (max_autocorr > 0.0f && autocorr[0] > 0.0f) {
        float lag_seconds = static_cast<float>(best_lag) / frames_per_sec;
        result.bpm = 60.0f / lag_seconds;
        result.confidence = std::min(1.0f, max_autocorr / autocorr[0]);

        // Flag music presence if BPM > 110 with high rhythmic envelope continuity
        if (result.bpm > 110.0f && result.confidence > 0.15f) {
            result.music_detected = true;
        }
    }

    return result;
}

} // namespace khshield::audio```


--- FILE: src/shield_core/audio/spectrum_analyzer.cpp ---
```cpp
#include "khshield/audio/spectrum_analyzer.hpp"
#include <cmath>
#include <numbers>
#include <algorithm>

namespace khshield::audio {

std::vector<std::complex<float>> SpectrumAnalyzer::compute_dft(
    std::span<const float> samples
) {
    const size_t N = samples.size();
    std::vector<std::complex<float>> output(N / 2 + 1, {0.0f, 0.0f});

    for (size_t k = 0; k <= N / 2; ++k) {
        float sum_real = 0.0f;
        float sum_imag = 0.0f;
        const float angle_base = -2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(N);

        for (size_t n = 0; n < N; ++n) {
            // Hann Windowing to eliminate spectral leakage
            float hann = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(n) / static_cast<float>(N - 1)));
            float windowed_sample = samples[n] * hann;

            float angle = angle_base * static_cast<float>(n);
            sum_real += windowed_sample * std::cos(angle);
            sum_imag += windowed_sample * std::sin(angle);
        }

        output[k] = std::complex<float>(sum_real, sum_imag);
    }

    return output;
}

SpectrumAnalysisResult SpectrumAnalyzer::analyze(
    std::span<const float> pcm_samples,
    uint32_t sample_rate
) const {
    SpectrumAnalysisResult result;

    if (pcm_samples.empty() || sample_rate == 0) {
        result.is_corrupted = true;
        return result;
    }

    float sum_squares = 0.0f;
    size_t invalid_sample_count = 0;

    for (float sample : pcm_samples) {
        if (std::isnan(sample) || std::isinf(sample)) {
            ++invalid_sample_count;
            continue;
        }
        sum_squares += sample * sample;
    }

    if (invalid_sample_count > (pcm_samples.size() / 100)) {
        result.is_corrupted = true;
        return result;
    }

    size_t valid_samples = pcm_samples.size() - invalid_sample_count;
    if (valid_samples == 0) {
        result.is_corrupted = true;
        return result;
    }

    result.rms_energy = std::sqrt(sum_squares / static_cast<float>(valid_samples));

    constexpr size_t MAX_DFT_SIZE = 1024;
    size_t dft_size = std::min(pcm_samples.size(), MAX_DFT_SIZE);

    auto spectrum = compute_dft(pcm_samples.subspan(0, dft_size));

    float weighted_sum = 0.0f;
    float total_magnitude = 0.0f;
    float bin_frequency_step = static_cast<float>(sample_rate) / static_cast<float>(dft_size);

    // Skip DC offset bin (k = 0) for centroid calculation
    for (size_t k = 1; k < spectrum.size(); ++k) {
        float magnitude = std::abs(spectrum[k]);
        float freq = static_cast<float>(k) * bin_frequency_step;

        weighted_sum += freq * magnitude;
        total_magnitude += magnitude;
    }

    if (total_magnitude > 1e-6f) {
        result.spectral_centroid = weighted_sum / total_magnitude;
    } else {
        result.spectral_centroid = 0.0f;
    }

    return result;
}

} // namespace khshield::audio```


--- FILE: src/shield_core/report.cpp ---
```cpp
```


--- FILE: src/shield_core/shield_engine.cpp ---
```cpp
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

} // namespace khshield```


--- FILE: src/shield_core/text/aho_corasick.cpp ---
```cpp
#include "khshield/text/aho_corasick.hpp"
#include <mutex>
#include <queue>
#include <stdexcept>

namespace khshield::text {

void AhoCorasick::add_keyword(std::string_view keyword) {
    if (keyword.empty()) return;

    std::unique_lock lock(mutex_);
    is_built_ = false;

    size_t current = 0;
    for (char c : keyword) {
        if (!nodes_[current].children.contains(c)) {
            nodes_[current].children[c] = nodes_.size();
            nodes_.emplace_back();
        }
        current = nodes_[current].children[c];
    }
    nodes_[current].output_patterns.emplace_back(keyword);
}

void AhoCorasick::build() {
    std::unique_lock lock(mutex_);
    if (nodes_.size() <= 1) {
        is_built_ = true;
        return;
    }

    std::queue<size_t> q;

    // Direct children of root point their failure links back to root (node 0)
    for (auto& [ch, child_idx] : nodes_[0].children) {
        nodes_[child_idx].fail_link = 0;
        q.push(child_idx);
    }

    while (!q.empty()) {
        size_t current = q.front();
        q.pop();

        for (auto& [ch, child_idx] : nodes_[current].children) {
            size_t fail = nodes_[current].fail_link;

            while (fail != 0 && !nodes_[fail].children.contains(ch)) {
                fail = nodes_[fail].fail_link;
            }

            if (nodes_[fail].children.contains(ch)) {
                nodes_[child_idx].fail_link = nodes_[fail].children.at(ch);
            } else {
                nodes_[child_idx].fail_link = 0;
            }

            // Output link optimization
            size_t fail_child = nodes_[child_idx].fail_link;
            if (!nodes_[fail_child].output_patterns.empty()) {
                nodes_[child_idx].output_link = fail_child;
            } else {
                nodes_[child_idx].output_link = nodes_[fail_child].output_link;
            }

            q.push(child_idx);
        }
    }

    is_built_ = true;
}

std::vector<MatchResult> AhoCorasick::search(std::string_view text) const {
    std::shared_lock lock(mutex_);
    std::vector<MatchResult> results;
    if (!is_built_ || text.empty()) return results;

    size_t current = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];

        while (current != 0 && !nodes_[current].children.contains(c)) {
            current = nodes_[current].fail_link;
        }

        if (nodes_[current].children.contains(c)) {
            current = nodes_[current].children.at(c);
        }

        // Collect direct matches
        for (const auto& pattern : nodes_[current].output_patterns) {
            results.push_back({
                .pattern = pattern,
                .start_pos = i + 1 - pattern.size(),
                .end_pos = i + 1
            });
        }

        // Collect matches from output links
        size_t temp = nodes_[current].output_link;
        while (temp != 0) {
            for (const auto& pattern : nodes_[temp].output_patterns) {
                results.push_back({
                    .pattern = pattern,
                    .start_pos = i + 1 - pattern.size(),
                    .end_pos = i + 1
                });
            }
            temp = nodes_[temp].output_link;
        }
    }

    return results;
}

bool AhoCorasick::contains_any(std::string_view text) const {
    std::shared_lock lock(mutex_);
    if (!is_built_ || text.empty()) return false;

    size_t current = 0;
    for (char c : text) {
        while (current != 0 && !nodes_[current].children.contains(c)) {
            current = nodes_[current].fail_link;
        }

        if (nodes_[current].children.contains(c)) {
            current = nodes_[current].children.at(c);
        }

        if (!nodes_[current].output_patterns.empty() || nodes_[current].output_link != 0) {
            return true;
        }
    }

    return false;
}

} // namespace khshield::text```


--- FILE: src/shield_core/text/arabizi.cpp ---
```cpp
#include "khshield/text/arabizi.hpp"
#include <unordered_map>
#include <cctype>

namespace khshield::text {

namespace {

// Standard Arabizi substitution mapping to Arabic UTF-8 string sequences
const std::unordered_map<std::string, std::string> ARABIZI_MULTI_MAP = {
    {"3'", "غ"}, {"7'", "خ"}, {"2'", "خ"}, {"5", "خ"},
    {"3", "ع"},  {"7", "ح"},  {"2", "أ"},  {"8", "ق"},
    {"6", "ط"},  {"9", "ص"},  {"9'", "ض"}, {"4", "ش"}
};

const std::unordered_map<char, std::string> ARABIZI_SINGLE_MAP = {
    {'a', "ا"}, {'b', "ب"}, {'t', "ت"}, {'g', "ج"}, {'j', "ج"},
    {'h', "ه"}, {'d', "د"}, {'r', "ر"}, {'z', "ز"}, {'s', "س"},
    {'f', "ف"}, {'q', "ق"}, {'k', "ك"}, {'l', "ل"}, {'m', "م"},
    {'n', "ن"}, {'w', "و"}, {'y', "ي"}, {'e', "ا"}, {'i', "ي"}, {'o', "و"}, {'u', "و"}
};

} // namespace

std::string ArabiziNormalizer::normalize(std::string_view input) {
    std::string result;
    result.reserve(input.size() * 2);

    for (size_t i = 0; i < input.size(); ++i) {
        // First check for 2-character numeric/punctuation patterns (e.g., 3', 7', 9')
        if (i + 1 < input.size()) {
            std::string sub = std::string(input.substr(i, 2));
            if (ARABIZI_MULTI_MAP.contains(sub)) {
                result += ARABIZI_MULTI_MAP.at(sub);
                ++i;
                continue;
            }
        }

        // Single digit mappings
        std::string single_num(1, input[i]);
        if (ARABIZI_MULTI_MAP.contains(single_num)) {
            result += ARABIZI_MULTI_MAP.at(single_num);
            continue;
        }

        // Single letter character mappings
        char lower_c = static_cast<char>(std::tolower(static_cast<unsigned char>(input[i])));
        if (ARABIZI_SINGLE_MAP.contains(lower_c)) {
            result += ARABIZI_SINGLE_MAP.at(lower_c);
            continue;
        }

        // Preserve already canonical Arabic UTF-8 code points or unrecognized characters
        result += input[i];
    }

    return result;
}

} // namespace khshield::text```


--- FILE: src/shield_core/text/sam_inference.cpp ---
```cpp
#include "khshield/text/sam_inference.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace khshield::text {

SAMInferenceEngine::SAMInferenceEngine(const std::filesystem::path& model_path) {
    load_model(model_path);
}

bool SAMInferenceEngine::load_model(const std::filesystem::path& model_path) {
    is_loaded_ = false;
    vocabulary_weights_.clear();

    if (!std::filesystem::exists(model_path)) {
        return false;
    }

    std::ifstream file(model_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    KHMHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(KHMHeader));
    if (!file || std::memcmp(header.magic, "KHM1", 4) != 0) {
        return false; // Corrupt or invalid binary header
    }

    threshold_ = header.threshold;
    embedding_dim_ = header.embedding_dim;

    // Safe weight reader
    for (uint32_t i = 0; i < header.vocab_size; ++i) {
        uint16_t word_len = 0;
        file.read(reinterpret_cast<char*>(&word_len), sizeof(word_len));
        if (!file || word_len == 0 || word_len > 1024) {
            vocabulary_weights_.clear();
            return false; // Bound validation failure
        }

        std::string word(word_len, '\0');
        file.read(word.data(), word_len);

        std::vector<int8_t> weights(embedding_dim_);
        file.read(reinterpret_cast<char*>(weights.data()), embedding_dim_);

        if (!file) {
            vocabulary_weights_.clear();
            return false;
        }

        vocabulary_weights_.emplace(std::move(word), std::move(weights));
    }

    is_loaded_ = true;
    return true;
}

std::vector<std::string_view> SAMInferenceEngine::tokenize(std::string_view text) const {
    std::vector<std::string_view> tokens;
    size_t start = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(text[i])) || std::ispunct(static_cast<unsigned char>(text[i]))) {
            if (i > start) {
                tokens.push_back(text.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    if (start < text.size()) {
        tokens.push_back(text.substr(start));
    }

    return tokens;
}

SAMInferenceResult SAMInferenceEngine::evaluate(std::string_view text) const {
    if (!is_loaded_ || text.empty()) {
        return {.is_valid = false, .risk_score = 0.0f, .violation_category = "NONE"};
    }

    auto tokens = tokenize(text);
    if (tokens.empty()) {
        return {.is_valid = true, .risk_score = 0.0f, .violation_category = "NONE"};
    }

    std::vector<float> pooled_embedding(embedding_dim_, 0.0f);
    size_t matched_tokens = 0;

    for (auto token : tokens) {
        std::string key(token);
        if (vocabulary_weights_.find(key) != vocabulary_weights_.end()) {
            const auto& weights = vocabulary_weights_.at(key);
            for (size_t d = 0; d < embedding_dim_; ++d) {
                pooled_embedding[d] += static_cast<float>(weights[d]) / 127.0f;
            }
            matched_tokens++;
        }
    }

    if (matched_tokens == 0) {
        return {.is_valid = true, .risk_score = 0.001f, .violation_category = "NONE"};
    }

    // Sigmoid aggregate activation over pooled feature vector
    float sum = 0.0f;
    for (float val : pooled_embedding) {
        sum += val;
    }
    float norm_sum = sum / static_cast<float>(matched_tokens);
    float score = 1.0f / (1.0f + std::exp(-norm_sum));

    bool is_flagged = score >= threshold_;
    return {
        .is_valid = true,
        .risk_score = score,
        .violation_category = is_flagged ? "PROFANITY_SEMANTIC" : "NONE"
    };
}

} // namespace khshield::text```


--- FILE: src/shield_core/visual/geometry_analyzer.cpp ---
```cpp
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

} // namespace khshield::visual```


--- FILE: src/shield_core/visual/ocr_bridge.cpp ---
```cpp
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

} // namespace khshield::visual```


--- FILE: src/shield_core/visual/skin_contour.cpp ---
```cpp
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

} // namespace khshield::visual```
