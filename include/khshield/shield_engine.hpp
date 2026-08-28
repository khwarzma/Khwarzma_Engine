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

#endif // KHSHIELD_ENGINE_HPP