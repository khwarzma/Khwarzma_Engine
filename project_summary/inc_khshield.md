# Header Files: khshield


--- FILE: include/khshield/audio/bpm_energy.hpp ---
```cpp
#ifndef KHSHIELD_AUDIO_BPM_ENERGY_HPP
#define KHSHIELD_AUDIO_BPM_ENERGY_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include <span>

namespace khshield::audio {

struct BPMEnergyResult {
    float bpm{0.0f};
    bool music_detected{false};
    float confidence{0.0f};
};

class BPMEnergyAnalyzer {
public:
    BPMEnergyAnalyzer() = default;

    // Evaluates track tempo (BPM) and flags explicit high-intensity audio music tracks
    [[nodiscard]] BPMEnergyResult calculate_bpm(
        std::span<const float> pcm_samples,
        uint32_t sample_rate = 44100
    ) const;

private:
    [[nodiscard]] static std::vector<float> compute_onset_envelope(
        std::span<const float> pcm_samples,
        size_t hop_size
    );

    [[nodiscard]] static std::vector<float> compute_autocorrelation(
        std::span<const float> signal
    );
};

} // namespace khshield::audio

#endif // KHSHIELD_AUDIO_BPM_ENERGY_HPP```


--- FILE: include/khshield/audio/spectrum_analyzer.hpp ---
```cpp
#ifndef KHSHIELD_AUDIO_SPECTRUM_ANALYZER_HPP
#define KHSHIELD_AUDIO_SPECTRUM_ANALYZER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include <span>
#include <complex>

namespace khshield::audio {

struct SpectrumAnalysisResult {
    float rms_energy{0.0f};
    float spectral_centroid{0.0f};
    bool is_corrupted{false};
};

class SpectrumAnalyzer {
public:
    SpectrumAnalyzer() = default;

    // Evaluates RMS energy, spectral centroid via DFT, and detects PCM audio corruption
    [[nodiscard]] SpectrumAnalysisResult analyze(
        std::span<const float> pcm_samples,
        uint32_t sample_rate = 44100
    ) const;

private:
    // CPU-bound Discrete Fourier Transform for real PCM frames
    [[nodiscard]] static std::vector<std::complex<float>> compute_dft(
        std::span<const float> samples
    );
};

} // namespace khshield::audio

#endif // KHSHIELD_AUDIO_SPECTRUM_ANALYZER_HPP```


--- FILE: include/khshield/report.hpp ---
```cpp
#ifndef KHSHIELD_REPORT_HPP
#define KHSHIELD_REPORT_HPP

#include <string>

namespace khshield {

struct ContentReport {
    bool is_safe{true};
    float risk_score{0.0f}; // Range: [0.0, 1.0]
    std::string violation_type{"NONE"}; // e.g., "EXPLICIT_BODY", "MUSIC_DETECTED", "PROFANITY"
    
    // Sub-reports
    struct TextDetails { 
        bool flagged{false}; 
        std::string matched_pattern; 
    } text;

    struct VisualDetails { 
        bool flagged{false}; 
        float skin_percentage{0.0f}; 
    } visual;

    struct AudioDetails { 
        bool flagged{false}; 
        bool corrupted{false}; 
        float bpm{0.0f}; 
    } audio;
};

} // namespace khshield

#endif // KHSHIELD_REPORT_HPP```


--- FILE: include/khshield/shield_engine.hpp ---
```cpp
#ifndef KHSHIELD_ENGINE_HPP
#define KHSHIELD_ENGINE_HPP

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <memory>
#include <span>
#include "khshield/report.hpp"
#include "khshield/text/aho_corasick.hpp"
#include "khshield/text/sam_inference.hpp"
#include "khshield/visual/skin_contour.hpp"
#include "khshield/visual/ocr_bridge.hpp"
#include "khshield/audio/spectrum_analyzer.hpp"
#include "khshield/audio/bpm_energy.hpp"

namespace khshield {

enum class Preset { STRICT, MODERATE };

struct ShieldConfig {
    Preset preset{Preset::STRICT};
    std::filesystem::path model_path{};
    std::vector<std::string> initial_keywords{};
};

class ShieldEngine {
public:
    explicit ShieldEngine(const ShieldConfig& config = {});
    explicit ShieldEngine(Preset preset);
    ~ShieldEngine() = default;

    // Dynamic Rule Management APIs
    void add_keyword(std::string_view keyword);
    void add_keywords(const std::vector<std::string>& keywords);
    bool load_model(const std::filesystem::path& model_path);

    // Direct memory/string processing routines
    ContentReport analyze_text(std::string_view text);
    ContentReport analyze_image_buffer(const visual::ImageBuffer& image);
    ContentReport analyze_audio_buffer(std::span<const float> pcm_samples, uint32_t sample_rate = 44100);

    // Dynamic preset runtime updates
    void set_preset(Preset preset) noexcept { preset_ = preset; }
    [[nodiscard]] Preset get_preset() const noexcept { return preset_; }

private:
    Preset preset_{Preset::STRICT};

    // Subsystem engine instances
    text::AhoCorasick text_matcher_;
    text::SAMInferenceEngine sam_engine_;
    visual::SkinContourAnalyzer skin_analyzer_;
    visual::OCRBridge ocr_bridge_;
    audio::SpectrumAnalyzer spectrum_analyzer_;
    audio::BPMEnergyAnalyzer bpm_analyzer_;
};

} // namespace khshield

#endif // KHSHIELD_ENGINE_HPP```


--- FILE: include/khshield/text/aho_corasick.hpp ---
```cpp
#ifndef KHSHIELD_TEXT_AHO_CORASICK_HPP
#define KHSHIELD_TEXT_AHO_CORASICK_HPP

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <queue>
#include <shared_mutex>
#include <span>

namespace khshield::text {

struct MatchResult {
    std::string pattern;
    size_t start_pos{0};
    size_t end_pos{0};
};

class AhoCorasick {
public:
    AhoCorasick() = default;
    ~AhoCorasick() = default;

    // Prevent copying to maintain state consistency across threads
    AhoCorasick(const AhoCorasick&) = delete;
    AhoCorasick& operator=(const AhoCorasick&) = delete;
    AhoCorasick(AhoCorasick&&) noexcept = default;
    AhoCorasick& operator=(AhoCorasick&&) noexcept = default;

    void add_keyword(std::string_view keyword);
    void build();
    [[nodiscard]] std::vector<MatchResult> search(std::string_view text) const;
    [[nodiscard]] bool contains_any(std::string_view text) const;

private:
    struct Node {
        std::unordered_map<char, size_t> children;
        size_t fail_link{0};
        size_t output_link{0};
        std::vector<std::string> output_patterns;
    };

    std::vector<Node> nodes_{1}; // Root node at index 0
    bool is_built_{false};
    mutable std::shared_mutex mutex_;
};

} // namespace khshield::text

#endif // KHSHIELD_TEXT_AHO_CORASICK_HPP```


--- FILE: include/khshield/text/arabizi.hpp ---
```cpp
#ifndef KHSHIELD_TEXT_ARABIZI_HPP
#define KHSHIELD_TEXT_ARABIZI_HPP

#include <string>
#include <string_view>

namespace khshield::text {

class ArabiziNormalizer {
public:
    ArabiziNormalizer() = default;

    // Normalizes mixed Latinized Arabic (Arabizi) and UTF-8 strings into canonical Arabic
    [[nodiscard]] static std::string normalize(std::string_view input);
};

} // namespace khshield::text

#endif // KHSHIELD_TEXT_ARABIZI_HPP```


--- FILE: include/khshield/text/sam_inference.hpp ---
```cpp
#ifndef KHSHIELD_TEXT_SAM_INFERENCE_HPP
#define KHSHIELD_TEXT_SAM_INFERENCE_HPP

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <memory>

namespace khshield::text {

struct SAMInferenceResult {
    bool is_valid{false};
    float risk_score{0.0f};
    std::string violation_category{"NONE"};
};

class SAMInferenceEngine {
public:
    SAMInferenceEngine() = default;
    explicit SAMInferenceEngine(const std::filesystem::path& model_path);
    ~SAMInferenceEngine() = default;

    bool load_model(const std::filesystem::path& model_path);
    [[nodiscard]] bool is_loaded() const noexcept { return is_loaded_; }

    [[nodiscard]] SAMInferenceResult evaluate(std::string_view text) const;

private:
    #pragma pack(push, 1)
    struct KHMHeader {
        char magic[4];       // "KHM1"
        uint32_t version;
        uint32_t vocab_size;
        uint32_t embedding_dim;
        float threshold;
    };
    #pragma pack(pop)

    bool is_loaded_{false};
    float threshold_{0.5f};
    uint32_t embedding_dim_{0};
    std::unordered_map<std::string, std::vector<int8_t>> vocabulary_weights_;

    [[nodiscard]] std::vector<std::string_view> tokenize(std::string_view text) const;
};

} // namespace khshield::text

#endif // KHSHIELD_TEXT_SAM_INFERENCE_HPP```


--- FILE: include/khshield/visual/geometry_analyzer.hpp ---
```cpp
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

#endif // KHSHIELD_VISUAL_GEOMETRY_ANALYZER_HPP```


--- FILE: include/khshield/visual/ocr_bridge.hpp ---
```cpp
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

#endif // KHSHIELD_VISUAL_OCR_BRIDGE_HPP```


--- FILE: include/khshield/visual/skin_contour.hpp ---
```cpp
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

#endif // KHSHIELD_VISUAL_SKIN_CONTOUR_HPP```
