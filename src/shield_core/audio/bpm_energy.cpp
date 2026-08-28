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

} // namespace khshield::audio