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

#endif // KHSHIELD_AUDIO_SPECTRUM_ANALYZER_HPP