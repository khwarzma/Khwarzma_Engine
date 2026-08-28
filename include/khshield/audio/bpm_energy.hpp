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

#endif // KHSHIELD_AUDIO_BPM_ENERGY_HPP