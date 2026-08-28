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

} // namespace khshield::audio