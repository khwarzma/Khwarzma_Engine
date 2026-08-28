#pragma once

#include <khcomp/comp_engine.hpp>
#include <khcomp/context_model.hpp>
#include <khcomp/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace khcomp::audio {

inline constexpr uint8_t kMaxAudioChannels = 2;

struct AudioHeader {
    uint32_t sample_rate{44100};
    uint8_t channels{2};
    uint32_t num_samples{0}; // Total samples per channel
};

class AudioCodecEngine {
public:
    constexpr AudioCodecEngine() noexcept = default;

    // Encodes 16-bit signed PCM interleaved audio using dynamic temporal linear prediction and arithmetic coding
    Result<size_t> encode_pcm16(
        AudioHeader header,
        ReadOnlyBuffer pcm_in,
        core::BitStreamWriter& writer) noexcept;

    // Decodes arithmetic bitstream back into 16-bit signed PCM interleaved audio
    Result<size_t> decode_pcm16(
        AudioHeader header,
        core::BitStreamReader& reader,
        MutableBuffer pcm_out) noexcept;

private:
    [[nodiscard]] static constexpr uint16_t map_int16_to_uint16(int16_t val) noexcept {
        return static_cast<uint16_t>((val << 1) ^ (val >> 15));
    }

    [[nodiscard]] static constexpr int16_t unmap_uint16_to_int16(uint16_t val) noexcept {
        return static_cast<int16_t>((val >> 1) ^ (-(val & 1)));
    }
};

} // namespace khcomp::audio