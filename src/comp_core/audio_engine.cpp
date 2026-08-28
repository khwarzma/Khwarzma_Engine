#include <khcomp/audio_engine.hpp>
#include <algorithm>
#include <cstring>

namespace khcomp::audio {

Result<size_t> AudioCodecEngine::encode_pcm16(
    AudioHeader header,
    ReadOnlyBuffer pcm_in,
    core::BitStreamWriter& writer) noexcept
{
    const size_t required_bytes = static_cast<size_t>(header.num_samples) * header.channels * sizeof(int16_t);

    if (pcm_in.data() == nullptr || pcm_in.size() < required_bytes) {
        return std::unexpected(CompressionError::InvalidInput);
    }
    if (header.channels == 0 || header.channels > kMaxAudioChannels || header.num_samples == 0) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    const size_t start_bits = writer.bits_written();
    const auto* pcm_samples = reinterpret_cast<const int16_t*>(pcm_in.data());
    const size_t total_interleaved_samples = static_cast<size_t>(header.num_samples) * header.channels;

    core::ContextModel model(2);
    core::ArithmeticEncoder encoder(&writer);

    std::array<int16_t, kMaxAudioChannels> prev_samples{};

    for (size_t i = 0; i < total_interleaved_samples; ++i) {
        const uint8_t ch = static_cast<uint8_t>(i % header.channels);
        const int16_t current_sample = pcm_samples[i];
        
        // First-order linear temporal prediction delta
        const int16_t residual = static_cast<int16_t>(current_sample - prev_samples[ch]);
        prev_samples[ch] = current_sample;

        const uint16_t u_res = map_int16_to_uint16(residual);
        const uint8_t hi = static_cast<uint8_t>((u_res >> 8) & 0xFF);
        const uint8_t lo = static_cast<uint8_t>(u_res & 0xFF);

        auto r_hi = encoder.encode_symbol(hi, model);
        if (!r_hi) return std::unexpected(r_hi.error());

        auto r_lo = encoder.encode_symbol(lo, model);
        if (!r_lo) return std::unexpected(r_lo.error());
    }

    auto res_flush = encoder.flush();
    if (!res_flush) return std::unexpected(res_flush.error());

    return (writer.bits_written() - start_bits) / 8;
}

Result<size_t> AudioCodecEngine::decode_pcm16(
    AudioHeader header,
    core::BitStreamReader& reader,
    MutableBuffer pcm_out) noexcept
{
    const size_t required_bytes = static_cast<size_t>(header.num_samples) * header.channels * sizeof(int16_t);

    if (pcm_out.data() == nullptr || pcm_out.size() < required_bytes) {
        return std::unexpected(CompressionError::InvalidInput);
    }
    if (header.channels == 0 || header.channels > kMaxAudioChannels || header.num_samples == 0) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    auto* out_samples = reinterpret_cast<int16_t*>(pcm_out.data());
    const size_t total_interleaved_samples = static_cast<size_t>(header.num_samples) * header.channels;

    core::ContextModel model(2);
    core::ArithmeticDecoder decoder;

    auto res_init = decoder.set_reader(&reader);
    if (!res_init) return std::unexpected(res_init.error());

    std::array<int16_t, kMaxAudioChannels> prev_samples{};

    for (size_t i = 0; i < total_interleaved_samples; ++i) {
        const uint8_t ch = static_cast<uint8_t>(i % header.channels);

        auto sym_hi = decoder.decode_symbol(model);
        if (!sym_hi) return std::unexpected(sym_hi.error());

        auto sym_lo = decoder.decode_symbol(model);
        if (!sym_lo) return std::unexpected(sym_lo.error());

        const uint16_t u_res = static_cast<uint16_t>((static_cast<uint16_t>(*sym_hi) << 8) | static_cast<uint16_t>(*sym_lo));
        const int16_t residual = unmap_uint16_to_int16(u_res);

        const int16_t reconstructed_sample = static_cast<int16_t>(prev_samples[ch] + residual);
        prev_samples[ch] = reconstructed_sample;
        out_samples[i] = reconstructed_sample;
    }

    return required_bytes;
}

} // namespace khcomp::audio