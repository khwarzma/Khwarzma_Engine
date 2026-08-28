# Source Files: comp_core


--- FILE: src/comp_core/audio_engine.cpp ---
```cpp
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

} // namespace khcomp::audio```


--- FILE: src/comp_core/bit_stream.cpp ---
```cpp
#include <khcomp/bit_stream.hpp>
#include <algorithm>
#include <cstring>

namespace khcomp::core {

// ============================================================================
// BitStreamWriter
// ============================================================================

Result<void> BitStreamWriter::set_buffer(MutableBuffer buffer) noexcept {
    if (!utils::is_aligned(buffer)) {
        return std::unexpected(CompressionError::AlignmentError);
    }
    m_buffer = buffer;
    reset();
    return {};
}

void BitStreamWriter::reset() noexcept {
    m_bit_offset = 0;
    m_bit_accumulator = 0;
    m_bits_in_accumulator = 0;
}

Result<void> BitStreamWriter::write_bits(uint64_t value, uint8_t num_bits) noexcept {
    if (num_bits == 0) {
        return {};
    }
    if (num_bits > 64) {
        return std::unexpected(CompressionError::InvalidInput);
    }
    if (m_bit_offset + num_bits > capacity_bits()) {
        return std::unexpected(CompressionError::Overflow);
    }

    if (num_bits < 64) {
        value &= (1ULL << num_bits) - 1ULL;
    }

    while (num_bits > 0) {
        const uint8_t space_in_acc = 64 - m_bits_in_accumulator;
        const uint8_t bits_to_take = std::min(num_bits, space_in_acc);

        if (bits_to_take == 64) {
            m_bit_accumulator = value;
        } else {
            const uint64_t mask = (bits_to_take == 64) ? ~0ULL : ((1ULL << bits_to_take) - 1ULL);
            const uint64_t chunk = (value >> (num_bits - bits_to_take)) & mask;
            m_bit_accumulator = (m_bit_accumulator << bits_to_take) | chunk;
        }

        m_bits_in_accumulator += bits_to_take;
        m_bit_offset += bits_to_take;
        num_bits -= bits_to_take;

        if (m_bits_in_accumulator == 64) {
            const size_t byte_idx = (m_bit_offset - 64) / 8;
            for (int i = 7; i >= 0; --i) {
                m_buffer[byte_idx + static_cast<size_t>(7 - i)] = static_cast<uint8_t>((m_bit_accumulator >> (i * 8)) & 0xFF);
            }
            m_bit_accumulator = 0;
            m_bits_in_accumulator = 0;
        }
    }

    return {};
}

Result<void> BitStreamWriter::write_bytes(ReadOnlyBuffer bytes) noexcept {
    if (bytes.empty()) {
        return {};
    }

    if (m_bits_in_accumulator % 8 == 0) {
        auto flush_res = flush();
        if (!flush_res) {
            return flush_res;
        }

        const size_t byte_offset = m_bit_offset / 8;
        if (byte_offset + bytes.size() > m_buffer.size()) {
            return std::unexpected(CompressionError::Overflow);
        }

        std::memcpy(m_buffer.data() + byte_offset, bytes.data(), bytes.size());
        m_bit_offset += utils::bytes_to_bits(bytes.size());
        return {};
    }

    for (uint8_t byte : bytes) {
        auto res = write_bits(byte, 8);
        if (!res) {
            return res;
        }
    }

    return {};
}

Result<void> BitStreamWriter::flush() noexcept {
    if (m_bits_in_accumulator == 0) {
        return {};
    }

    const uint8_t remainder = m_bits_in_accumulator % 8;
    if (remainder != 0) {
        const uint8_t padding = 8 - remainder;
        auto res = write_bits(0, padding);
        if (!res) {
            return res;
        }
    }

    size_t bytes_to_flush = m_bits_in_accumulator / 8;
    size_t start_byte_idx = (m_bit_offset - m_bits_in_accumulator) / 8;

    for (size_t i = 0; i < bytes_to_flush; ++i) {
        const uint8_t shift = static_cast<uint8_t>((bytes_to_flush - 1 - i) * 8);
        m_buffer[start_byte_idx + i] = static_cast<uint8_t>((m_bit_accumulator >> shift) & 0xFF);
    }

    m_bit_accumulator = 0;
    m_bits_in_accumulator = 0;
    return {};
}

// ============================================================================
// BitStreamReader
// ============================================================================

Result<void> BitStreamReader::set_buffer(ReadOnlyBuffer buffer) noexcept {
    if (!utils::is_aligned(buffer)) {
        return std::unexpected(CompressionError::AlignmentError);
    }
    m_buffer = buffer;
    reset();
    return {};
}

void BitStreamReader::reset() noexcept {
    m_bit_offset = 0;
    m_bit_accumulator = 0;
    m_bits_in_accumulator = 0;
}

Result<void> BitStreamReader::fill_accumulator() noexcept {
    while (m_bits_in_accumulator <= 56) {
        const size_t current_byte_idx = (m_bit_offset + m_bits_in_accumulator) / 8;
        if (current_byte_idx >= m_buffer.size()) {
            break;
        }
        m_bit_accumulator = (m_bit_accumulator << 8) | static_cast<uint64_t>(m_buffer[current_byte_idx]);
        m_bits_in_accumulator += 8;
    }
    return {};
}

Result<uint64_t> BitStreamReader::peek_bits(uint8_t num_bits) noexcept {
    if (num_bits == 0) {
        return 0;
    }
    if (num_bits > 64) {
        return std::unexpected(CompressionError::InvalidInput);
    }
    if (num_bits > remaining_bits()) {
        return std::unexpected(CompressionError::Overflow);
    }

    auto fill_res = fill_accumulator();
    if (!fill_res) {
        return std::unexpected(fill_res.error());
    }

    if (m_bits_in_accumulator >= num_bits) {
        const uint8_t shift = m_bits_in_accumulator - num_bits;
        const uint64_t mask = (num_bits == 64) ? ~0ULL : ((1ULL << num_bits) - 1ULL);
        return (m_bit_accumulator >> shift) & mask;
    }

    uint64_t result = 0;
    uint8_t bits_needed = num_bits;

    if (m_bits_in_accumulator > 0) {
        const uint64_t mask = (1ULL << m_bits_in_accumulator) - 1ULL;
        result = m_bit_accumulator & mask;
        bits_needed -= m_bits_in_accumulator;
    }

    size_t byte_idx = (m_bit_offset + m_bits_in_accumulator) / 8;
    while (bits_needed > 0) {
        const uint8_t take = std::min(bits_needed, static_cast<uint8_t>(8));
        const uint8_t byte_val = m_buffer[byte_idx++];
        const uint8_t chunk = byte_val >> (8 - take);
        result = (result << take) | chunk;
        bits_needed -= take;
    }

    return result;
}

Result<void> BitStreamReader::advance(uint8_t num_bits) noexcept {
    if (num_bits > remaining_bits()) {
        return std::unexpected(CompressionError::Overflow);
    }

    if (num_bits <= m_bits_in_accumulator) {
        m_bits_in_accumulator -= num_bits;
    } else {
        const uint8_t unconsumed_in_acc = m_bits_in_accumulator;
        m_bits_in_accumulator = 0;
        m_bit_accumulator = 0;
        m_bit_offset += unconsumed_in_acc;
        const uint8_t remaining_to_advance = num_bits - unconsumed_in_acc;
        m_bit_offset += remaining_to_advance;
        return {};
    }

    m_bit_offset += num_bits;
    return {};
}

Result<uint64_t> BitStreamReader::read_bits(uint8_t num_bits) noexcept {
    auto val = peek_bits(num_bits);
    if (!val) {
        return val;
    }
    auto adv = advance(num_bits);
    if (!adv) {
        return std::unexpected(adv.error());
    }
    return val;
}

} // namespace khcomp::core```


--- FILE: src/comp_core/comp_engine.cpp ---
```cpp
#include <khcomp/comp_engine.hpp>

namespace khcomp::core {

// ============================================================================
// ArithmeticEncoder
// ============================================================================

Result<void> ArithmeticEncoder::bit_plus_follow(bool bit) noexcept {
    if (!m_writer) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    auto res = m_writer->write_bits(bit ? 1 : 0, 1);
    if (!res) return res;

    while (m_bits_to_follow > 0) {
        res = m_writer->write_bits(bit ? 0 : 1, 1);
        if (!res) return res;
        --m_bits_to_follow;
    }
    return {};
}

Result<void> ArithmeticEncoder::encode_symbol(uint8_t symbol, ContextModel& model) noexcept {
    if (!m_writer) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    const SymbolStats stats = model.get_stats(symbol);
    const uint64_t range = static_cast<uint64_t>(m_high) - m_low + 1;

    m_high = m_low + static_cast<uint32_t>((range * stats.high) / stats.total) - 1;
    m_low  = m_low + static_cast<uint32_t>((range * stats.low) / stats.total);

    for (;;) {
        if (m_high < kHalf) {
            auto res = bit_plus_follow(false);
            if (!res) return res;
        } else if (m_low >= kHalf) {
            auto res = bit_plus_follow(true);
            if (!res) return res;
            m_low -= kHalf;
            m_high -= kHalf;
        } else if (m_low >= kFirstQtr && m_high < kThirdQtr) {
            ++m_bits_to_follow;
            m_low -= kFirstQtr;
            m_high -= kFirstQtr;
        } else {
            break;
        }
        m_low = m_low << 1;
        m_high = (m_high << 1) | 1;
    }

    model.update(symbol);
    return {};
}

Result<void> ArithmeticEncoder::flush() noexcept {
    if (!m_writer) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    ++m_bits_to_follow;
    if (m_low < kFirstQtr) {
        auto res = bit_plus_follow(false);
        if (!res) return res;
    } else {
        auto res = bit_plus_follow(true);
        if (!res) return res;
    }

    return m_writer->flush();
}

// ============================================================================
// ArithmeticDecoder
// ============================================================================

Result<void> ArithmeticDecoder::init() noexcept {
    if (!m_reader) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    m_low = 0;
    m_high = kTopValue;
    m_value = 0;

    for (size_t i = 0; i < 32; ++i) {
        auto bit = m_reader->read_bits(1);
        if (!bit) {
            return std::unexpected(bit.error());
        }
        m_value = (m_value << 1) | static_cast<uint32_t>(*bit);
    }
    return {};
}

Result<uint8_t> ArithmeticDecoder::decode_symbol(ContextModel& model) noexcept {
    if (!m_reader) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    const uint64_t range = static_cast<uint64_t>(m_high) - m_low + 1;
    const uint16_t total = model.total_frequency();

    const uint64_t cum = (((static_cast<uint64_t>(m_value) - m_low + 1) * total) - 1) / range;
    const uint8_t symbol = model.decode_symbol(static_cast<uint16_t>(cum));

    const SymbolStats stats = model.get_stats(symbol);

    m_high = m_low + static_cast<uint32_t>((range * stats.high) / stats.total) - 1;
    m_low  = m_low + static_cast<uint32_t>((range * stats.low) / stats.total);

    for (;;) {
        if (m_high < kHalf) {
            // Nothing extra required
        } else if (m_low >= kHalf) {
            m_value -= kHalf;
            m_low -= kHalf;
            m_high -= kHalf;
        } else if (m_low >= kFirstQtr && m_high < kThirdQtr) {
            m_value -= kFirstQtr;
            m_low -= kFirstQtr;
            m_high -= kFirstQtr;
        } else {
            break;
        }
        m_low = m_low << 1;
        m_high = (m_high << 1) | 1;

        auto bit = m_reader->read_bits(1);
        uint32_t b = 0;
        if (bit) {
            b = static_cast<uint32_t>(*bit);
        }
        m_value = (m_value << 1) | b;
    }

    model.update(symbol);
    return symbol;
}

} // namespace khcomp::core```


--- FILE: src/comp_core/context_model.cpp ---
```cpp
#include <khcomp/context_model.hpp>
#include <algorithm>
#include <numeric>

namespace khcomp::core {

void ContextModel::reset() noexcept {
    m_history.fill(0);

    for (size_t ctx = 0; ctx < 256; ++ctx) {
        m_frequencies[ctx].fill(1); // Uniform initialization
        m_total_frequencies[ctx] = kAlphabetSize;
    }
}

size_t ContextModel::get_context_index() const noexcept {
    // Hash context history deterministically down to a 8-bit table index [0, 255]
    uint32_t hash = 2166136261U; // FNV-1a 32-bit prime initial bias
    for (size_t i = 0; i < m_order; ++i) {
        hash ^= m_history[i];
        hash *= 16777619U;
    }
    return static_cast<size_t>(hash & 0xFF);
}

void ContextModel::rescale_frequencies(size_t table_idx) noexcept {
    uint32_t new_total = 0;
    auto& table = m_frequencies[table_idx];

    for (size_t i = 0; i < kAlphabetSize; ++i) {
        table[i] = static_cast<uint16_t>((table[i] >> 1) | 1); // Scale down while preventing zero counts
        new_total += table[i];
    }

    m_total_frequencies[table_idx] = new_total;
}

void ContextModel::update(uint8_t symbol) noexcept {
    const size_t ctx = get_context_index();

    if (m_total_frequencies[ctx] + 2 >= kMaxFrequency) {
        rescale_frequencies(ctx);
    }

    m_frequencies[ctx][symbol] += 2; // Incremental adaptation step
    m_total_frequencies[ctx] += 2;

    // Shift history window for byte-granularity Order-N context evaluation
    for (size_t i = m_order - 1; i > 0; --i) {
        m_history[i] = m_history[i - 1];
    }
    m_history[0] = symbol;
}

SymbolStats ContextModel::get_stats(uint8_t symbol) const noexcept {
    const size_t ctx = get_context_index();
    const auto& table = m_frequencies[ctx];

    uint16_t low = 0;
    for (size_t i = 0; i < symbol; ++i) {
        low = static_cast<uint16_t>(low + table[i]);
    }

    const uint16_t high = static_cast<uint16_t>(low + table[symbol]);
    const uint16_t total = static_cast<uint16_t>(m_total_frequencies[ctx]);

    return SymbolStats{
        .low = low,
        .high = high,
        .total = total
    };
}

uint8_t ContextModel::decode_symbol(uint16_t target_count) const noexcept {
    const size_t ctx = get_context_index();
    const auto& table = m_frequencies[ctx];

    uint16_t running_total = 0;
    for (size_t i = 0; i < kAlphabetSize; ++i) {
        running_total = static_cast<uint16_t>(running_total + table[i]);
        if (target_count < running_total) {
            return static_cast<uint8_t>(i);
        }
    }

    return 255; // Boundary fallback
}

uint16_t ContextModel::total_frequency() const noexcept {
    const size_t ctx = get_context_index();
    return static_cast<uint16_t>(m_total_frequencies[ctx]);
}

} // namespace khcomp::core```


--- FILE: src/comp_core/image_engine.cpp ---
```cpp
#include <khcomp/image_engine.hpp>
#include <cmath>

namespace khcomp::image {

namespace {

// Precomputed 8x8 Cosine Basis Matrix: cos((2*pos + 1) * freq * PI / 16)
// Dimension: [freq (0..7)][pos (0..7)]
struct DctCosTable {
    float data[kBlockSize][kBlockSize];

    constexpr DctCosTable() noexcept : data{} {
        // Compile-time or static initial construction of cosine basis terms
        constexpr double kPi = 3.14159265358979323846;
        for (size_t freq = 0; freq < kBlockSize; ++freq) {
            for (size_t pos = 0; pos < kBlockSize; ++pos) {
                data[freq][pos] = static_cast<float>(
                    std::cos((2.0 * static_cast<double>(pos) + 1.0) * static_cast<double>(freq) * kPi / 16.0)
                );
            }
        }
    }
};

inline const DctCosTable kDctCosLUT{};

// Precomputed scaling constants: C(0) = 1/sqrt(2), C(u>0) = 1.0
inline constexpr std::array<float, kBlockSize> kCScale = {
    0.70710678118654752440f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
};

} // namespace

Result<void> DctQuantEngine::forward_dct(
    std::span<const float, kBlockElements> input,
    std::span<float, kBlockElements> output) const noexcept
{
    if (input.data() == nullptr || output.data() == nullptr) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    for (size_t u = 0; u < kBlockSize; ++u) {
        const float cu = kCScale[u];
        for (size_t v = 0; v < kBlockSize; ++v) {
            const float cv = kCScale[v];
            float sum = 0.0f;

            for (size_t x = 0; x < kBlockSize; ++x) {
                const float cos_x = kDctCosLUT.data[u][x];
                for (size_t y = 0; y < kBlockSize; ++y) {
                    const float cos_y = kDctCosLUT.data[v][y];
                    sum += input[x * kBlockSize + y] * cos_x * cos_y;
                }
            }

            output[u * kBlockSize + v] = 0.25f * cu * cv * sum;
        }
    }

    return {};
}

Result<void> DctQuantEngine::inverse_dct(
    std::span<const float, kBlockElements> input,
    std::span<float, kBlockElements> output) const noexcept
{
    if (input.data() == nullptr || output.data() == nullptr) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    for (size_t x = 0; x < kBlockSize; ++x) {
        for (size_t y = 0; y < kBlockSize; ++y) {
            float sum = 0.0f;

            for (size_t u = 0; u < kBlockSize; ++u) {
                const float cu = kCScale[u];
                const float cos_x = kDctCosLUT.data[u][x];

                for (size_t v = 0; v < kBlockSize; ++v) {
                    const float cv = kCScale[v];
                    const float cos_y = kDctCosLUT.data[v][y];

                    sum += cu * cv * input[u * kBlockSize + v] * cos_x * cos_y;
                }
            }

            output[x * kBlockSize + y] = 0.25f * sum;
        }
    }

    return {};
}

Result<void> DctQuantEngine::quantize(
    std::span<const float, kBlockElements> dct_in,
    std::span<int16_t, kBlockElements> quant_out) const noexcept
{
    if (dct_in.data() == nullptr || quant_out.data() == nullptr) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    for (size_t i = 0; i < kBlockElements; ++i) {
        const float val = dct_in[i] / static_cast<float>(m_quant_table[i]);
        quant_out[i] = static_cast<int16_t>(std::round(val));
    }

    return {};
}

Result<void> DctQuantEngine::dequantize(
    std::span<const int16_t, kBlockElements> quant_in,
    std::span<float, kBlockElements> dct_out) const noexcept
{
    if (quant_in.data() == nullptr || dct_out.data() == nullptr) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    for (size_t i = 0; i < kBlockElements; ++i) {
        dct_out[i] = static_cast<float>(quant_in[i]) * static_cast<float>(m_quant_table[i]);
    }

    return {};
}

Result<void> DctQuantEngine::zigzag_serialize(
    std::span<const int16_t, kBlockElements> input,
    std::span<int16_t, kBlockElements> output) const noexcept
{
    if (input.data() == nullptr || output.data() == nullptr) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    for (size_t i = 0; i < kBlockElements; ++i) {
        output[i] = input[kZigZagIndices[i]];
    }

    return {};
}

Result<void> DctQuantEngine::zigzag_deserialize(
    std::span<const int16_t, kBlockElements> input,
    std::span<int16_t, kBlockElements> output) const noexcept
{
    if (input.data() == nullptr || output.data() == nullptr) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    for (size_t i = 0; i < kBlockElements; ++i) {
        output[kZigZagIndices[i]] = input[i];
    }

    return {};
}

} // namespace khcomp::image```


--- FILE: src/comp_core/image_frame.cpp ---
```cpp
#include <khcomp/image_frame.hpp>
#include <algorithm>
#include <cmath>

namespace khcomp::image {

void ImageFramePipeline::extract_8x8_block(
    ReadOnlyBuffer src,
    uint16_t width,
    uint16_t block_x,
    uint16_t block_y,
    std::span<float, kBlockElements> block_out) noexcept
{
    const uint8_t* raw_ptr = src.data();
    for (size_t y = 0; y < kBlockSize; ++y) {
        const size_t row_offset = (static_cast<size_t>(block_y) + y) * width + block_x;
        for (size_t x = 0; x < kBlockSize; ++x) {
            // Level shift byte [0, 255] -> [-128.0, 127.0]
            block_out[y * kBlockSize + x] = static_cast<float>(raw_ptr[row_offset + x]) - 128.0f;
        }
    }
}

void ImageFramePipeline::store_8x8_block(
    std::span<const float, kBlockElements> block_in,
    uint16_t width,
    uint16_t block_x,
    uint16_t block_y,
    MutableBuffer dst) noexcept
{
    uint8_t* raw_ptr = dst.data();
    for (size_t y = 0; y < kBlockSize; ++y) {
        const size_t row_offset = (static_cast<size_t>(block_y) + y) * width + block_x;
        for (size_t x = 0; x < kBlockSize; ++x) {
            // Level unshift [-128.0, 127.0] -> [0, 255] with clamping
            const float val = std::round(block_in[y * kBlockSize + x] + 128.0f);
            const float clamped = std::clamp(val, 0.0f, 255.0f);
            raw_ptr[row_offset + x] = static_cast<uint8_t>(clamped);
        }
    }
}

Result<size_t> ImageFramePipeline::encode_grayscale_frame(
    ImageHeader header,
    ReadOnlyBuffer raw_pixels,
    core::BitStreamWriter& writer) noexcept
{
    if (raw_pixels.data() == nullptr || raw_pixels.size() < static_cast<size_t>(header.width) * header.height) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    if (header.width % kBlockSize != 0 || header.height % kBlockSize != 0 || header.width == 0 || header.height == 0) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    DctQuantEngine dct_engine(header.quality_factor);
    core::ContextModel model(2);
    core::ArithmeticEncoder encoder(&writer);

    alignas(64) std::array<float, kBlockElements> block_input{};
    alignas(64) std::array<float, kBlockElements> dct_coeffs{};
    alignas(64) std::array<int16_t, kBlockElements> quant_coeffs{};
    alignas(64) std::array<int16_t, kBlockElements> zigzag_coeffs{};

    int16_t prev_dc = 0;

    for (uint16_t by = 0; by < header.height; by += kBlockSize) {
        for (uint16_t bx = 0; bx < header.width; bx += kBlockSize) {
            extract_8x8_block(raw_pixels, header.width, bx, by, block_input);

            auto res_dct = dct_engine.forward_dct(block_input, dct_coeffs);
            if (!res_dct) return std::unexpected(res_dct.error());

            auto res_q = dct_engine.quantize(dct_coeffs, quant_coeffs);
            if (!res_q) return std::unexpected(res_q.error());

            auto res_zz = dct_engine.zigzag_serialize(quant_coeffs, zigzag_coeffs);
            if (!res_zz) return std::unexpected(res_zz.error());

            // 1. Delta DC Coding with ZigZag integer mapping for sign preservation
            const int16_t current_dc = zigzag_coeffs[0];
            const int16_t dc_delta = static_cast<int16_t>(current_dc - prev_dc);
            prev_dc = current_dc;

            const uint16_t u_dc = map_int16_to_uint16(dc_delta);
            auto r1 = encoder.encode_symbol(static_cast<uint8_t>((u_dc >> 8) & 0xFF), model);
            if (!r1) return std::unexpected(r1.error());
            auto r2 = encoder.encode_symbol(static_cast<uint8_t>(u_dc & 0xFF), model);
            if (!r2) return std::unexpected(r2.error());

            // 2. Find last non-zero AC coefficient index for EOB optimization
            size_t last_nonzero = 0;
            for (size_t i = kBlockElements - 1; i >= 1; --i) {
                if (zigzag_coeffs[i] != 0) {
                    last_nonzero = i;
                    break;
                }
            }

            // 3. Encode AC Coefficients
            if (last_nonzero > 0) {
                uint8_t zero_run = 0;
                for (size_t i = 1; i <= last_nonzero; ++i) {
                    const int16_t ac_val = zigzag_coeffs[i];
                    if (ac_val == 0) {
                        if (zero_run == 254) {
                            // Emit max run marker (run=254, val=0)
                            auto r = encoder.encode_symbol(254, model);
                            if (!r) return std::unexpected(r.error());
                            r = encoder.encode_symbol(0, model);
                            if (!r) return std::unexpected(r.error());
                            r = encoder.encode_symbol(0, model);
                            if (!r) return std::unexpected(r.error());
                            zero_run = 0;
                        } else {
                            ++zero_run;
                        }
                    } else {
                        auto r = encoder.encode_symbol(zero_run, model);
                        if (!r) return std::unexpected(r.error());

                        const uint16_t u_ac = map_int16_to_uint16(ac_val);
                        r = encoder.encode_symbol(static_cast<uint8_t>((u_ac >> 8) & 0xFF), model);
                        if (!r) return std::unexpected(r.error());

                        r = encoder.encode_symbol(static_cast<uint8_t>(u_ac & 0xFF), model);
                        if (!r) return std::unexpected(r.error());

                        zero_run = 0;
                    }
                }
            }

            // 4. Emit End-Of-Block (EOB) marker (0xFF marker byte)
            auto reob = encoder.encode_symbol(0xFF, model);
            if (!reob) return std::unexpected(reob.error());
        }
    }

    auto res_flush = encoder.flush();
    if (!res_flush) return std::unexpected(res_flush.error());

    return writer.bits_written() / 8;
}

Result<size_t> ImageFramePipeline::decode_grayscale_frame(
    ImageHeader header,
    core::BitStreamReader& reader,
    MutableBuffer output_pixels) noexcept
{
    if (output_pixels.data() == nullptr || output_pixels.size() < static_cast<size_t>(header.width) * header.height) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    if (header.width % kBlockSize != 0 || header.height % kBlockSize != 0 || header.width == 0 || header.height == 0) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    DctQuantEngine dct_engine(header.quality_factor);
    core::ContextModel model(2);
    core::ArithmeticDecoder decoder;

    auto res_init = decoder.set_reader(&reader);
    if (!res_init) return std::unexpected(res_init.error());

    alignas(64) std::array<int16_t, kBlockElements> zigzag_coeffs{};
    alignas(64) std::array<int16_t, kBlockElements> quant_coeffs{};
    alignas(64) std::array<float, kBlockElements> dequant_coeffs{};
    alignas(64) std::array<float, kBlockElements> block_output{};

    int16_t prev_dc = 0;

    for (uint16_t by = 0; by < header.height; by += kBlockSize) {
        for (uint16_t bx = 0; bx < header.width; bx += kBlockSize) {
            zigzag_coeffs.fill(0);

            // 1. Decode DC Delta
            auto dc_hi = decoder.decode_symbol(model);
            if (!dc_hi) return std::unexpected(dc_hi.error());
            auto dc_lo = decoder.decode_symbol(model);
            if (!dc_lo) return std::unexpected(dc_lo.error());

            const uint16_t u_dc = static_cast<uint16_t>((static_cast<uint16_t>(*dc_hi) << 8) | static_cast<uint16_t>(*dc_lo));
            const int16_t dc_delta = unmap_uint16_to_int16(u_dc);
            const int16_t current_dc = static_cast<int16_t>(prev_dc + dc_delta);
            zigzag_coeffs[0] = current_dc;
            prev_dc = current_dc;

            // 2. Decode AC Coefficients
            size_t ac_idx = 1;
            while (ac_idx < kBlockElements) {
                auto run_sym = decoder.decode_symbol(model);
                if (!run_sym) return std::unexpected(run_sym.error());

                const uint8_t run = *run_sym;
                if (run == 0xFF) {
                    // EOB marker reached
                    break;
                }

                ac_idx += run;
                if (ac_idx >= kBlockElements) break;

                auto val_hi = decoder.decode_symbol(model);
                if (!val_hi) return std::unexpected(val_hi.error());
                auto val_lo = decoder.decode_symbol(model);
                if (!val_lo) return std::unexpected(val_lo.error());

                const uint16_t u_ac = static_cast<uint16_t>((static_cast<uint16_t>(*val_hi) << 8) | static_cast<uint16_t>(*val_lo));
                const int16_t ac_val = unmap_uint16_to_int16(u_ac);

                if (ac_val != 0) {
                    zigzag_coeffs[ac_idx] = ac_val;
                    ++ac_idx;
                }
            }

            // 3. Deserialization -> Dequantization -> IDCT -> Store Frame
            auto res_des = dct_engine.zigzag_deserialize(zigzag_coeffs, quant_coeffs);
            if (!res_des) return std::unexpected(res_des.error());

            auto res_dq = dct_engine.dequantize(quant_coeffs, dequant_coeffs);
            if (!res_dq) return std::unexpected(res_dq.error());

            auto res_idct = dct_engine.inverse_dct(dequant_coeffs, block_output);
            if (!res_idct) return std::unexpected(res_idct.error());

            store_8x8_block(block_output, header.width, bx, by, output_pixels);
        }
    }

    return output_pixels.size();
}

} // namespace khcomp::image```


--- FILE: src/comp_core/video_ring_buffer.cpp ---
```cpp
#include <khcomp/video_ring_buffer.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace khcomp::video {

uint32_t MotionEstimator::compute_sad(
    ReadOnlyBuffer curr_frame,
    ReadOnlyBuffer ref_frame,
    uint16_t width,
    uint16_t block_x,
    uint16_t block_y,
    int32_t ref_x,
    int32_t ref_y) noexcept
{
    const uint8_t* curr_ptr = curr_frame.data();
    const uint8_t* ref_ptr = ref_frame.data();
    uint32_t sad = 0;

    for (size_t y = 0; y < image::kBlockSize; ++y) {
        const size_t curr_row = (static_cast<size_t>(block_y) + y) * width + block_x;
        const size_t ref_row = (static_cast<size_t>(ref_y) + y) * width + ref_x;

        for (size_t x = 0; x < image::kBlockSize; ++x) {
            const int32_t diff = static_cast<int32_t>(curr_ptr[curr_row + x]) - static_cast<int32_t>(ref_ptr[ref_row + x]);
            sad += static_cast<uint32_t>(std::abs(diff));
        }
    }

    return sad;
}

MotionVector MotionEstimator::find_best_motion_vector(
    ReadOnlyBuffer curr_frame,
    ReadOnlyBuffer ref_frame,
    uint16_t width,
    uint16_t height,
    uint16_t block_x,
    uint16_t block_y,
    int32_t search_window) noexcept
{
    uint32_t min_sad = 0xFFFFFFFFU;
    MotionVector best_mv{0, 0};

    const int32_t min_dx = std::max(-search_window, -static_cast<int32_t>(block_x));
    const int32_t max_dx = std::min(search_window, static_cast<int32_t>(width - block_x - image::kBlockSize));

    const int32_t min_dy = std::max(-search_window, -static_cast<int32_t>(block_y));
    const int32_t max_dy = std::min(search_window, static_cast<int32_t>(height - block_y - image::kBlockSize));

    for (int32_t dy = min_dy; dy <= max_dy; ++dy) {
        for (int32_t dx = min_dx; dx <= max_dx; ++dx) {
            const int32_t ref_x = static_cast<int32_t>(block_x) + dx;
            const int32_t ref_y = static_cast<int32_t>(block_y) + dy;

            const uint32_t sad = compute_sad(curr_frame, ref_frame, width, block_x, block_y, ref_x, ref_y);
            if (sad < min_sad) {
                min_sad = sad;
                best_mv = MotionVector{static_cast<int8_t>(dx), static_cast<int8_t>(dy)};
            }
        }
    }

    return best_mv;
}

void MotionEstimator::compensate_block(
    ReadOnlyBuffer ref_frame,
    uint16_t width,
    uint16_t block_x,
    uint16_t block_y,
    MotionVector mv,
    std::span<float, image::kBlockElements> pred_out) noexcept
{
    const uint8_t* ref_ptr = ref_frame.data();
    const size_t ref_x = static_cast<size_t>(static_cast<int32_t>(block_x) + mv.dx);
    const size_t ref_y = static_cast<size_t>(static_cast<int32_t>(block_y) + mv.dy);

    for (size_t y = 0; y < image::kBlockSize; ++y) {
        const size_t ref_row = (ref_y + y) * width + ref_x;
        for (size_t x = 0; x < image::kBlockSize; ++x) {
            pred_out[y * image::kBlockSize + x] = static_cast<float>(ref_ptr[ref_row + x]);
        }
    }
}

Result<size_t> VideoCodecEngine::encode_frame(
    VideoHeader header,
    FrameType type,
    ReadOnlyBuffer curr_frame,
    VideoRingBuffer& ref_buffer,
    core::BitStreamWriter& writer) noexcept
{
    if (curr_frame.data() == nullptr || curr_frame.size() < static_cast<size_t>(header.width) * header.height) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    if (header.width % image::kBlockSize != 0 || header.height % image::kBlockSize != 0 || header.width == 0 || header.height == 0) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    // Write frame header descriptor byte
    auto res_hdr = writer.write_bits(static_cast<uint64_t>(type), 8);
    if (!res_hdr) return std::unexpected(res_hdr.error());

    if (type == FrameType::IFrame || !ref_buffer.has_reference()) {
        const image::ImageHeader img_hdr{
            .width = header.width,
            .height = header.height,
            .format = image::PixelFormat::Grayscale,
            .quality_factor = header.quality_factor
        };

        const size_t start_bits = writer.bits_written();
        auto enc_res = m_image_pipeline.encode_grayscale_frame(img_hdr, curr_frame, writer);
        if (!enc_res) return std::unexpected(enc_res.error());

        auto ref_mut = ref_buffer.current_reference();
        std::copy_n(curr_frame.data(), curr_frame.size(), ref_mut.data());
        ref_buffer.set_reference_valid(true);

        return (writer.bits_written() - start_bits) / 8;
    }

    // Encode P-Frame (Motion Vectors + DCT Quantized Residuals)
    const image::DctQuantEngine dct_engine(header.quality_factor);
    core::ContextModel model(2);
    core::ArithmeticEncoder encoder(&writer);

    alignas(64) std::array<float, image::kBlockElements> pred_block{};
    alignas(64) std::array<float, image::kBlockElements> residual_block{};
    alignas(64) std::array<float, image::kBlockElements> dct_coeffs{};
    alignas(64) std::array<int16_t, image::kBlockElements> quant_coeffs{};
    alignas(64) std::array<int16_t, image::kBlockElements> zigzag_coeffs{};

    MotionVector prev_mv{0, 0};
    int16_t prev_dc = 0;

    const ReadOnlyBuffer ref_frame = ref_buffer.current_reference();
    const uint8_t* curr_ptr = curr_frame.data();

    const size_t start_bits = writer.bits_written();

    for (uint16_t by = 0; by < header.height; by += image::kBlockSize) {
        for (uint16_t bx = 0; bx < header.width; bx += image::kBlockSize) {
            // 1. Motion Vector Estimation
            const MotionVector mv = MotionEstimator::find_best_motion_vector(
                curr_frame, ref_frame, header.width, header.height, bx, by, header.search_window);

            const int8_t mv_dx_delta = static_cast<int8_t>(mv.dx - prev_mv.dx);
            const int8_t mv_dy_delta = static_cast<int8_t>(mv.dy - prev_mv.dy);
            prev_mv = mv;

            const uint16_t u_dx = map_int8_to_uint16(mv_dx_delta);
            const uint16_t u_dy = map_int8_to_uint16(mv_dy_delta);

            auto r_mv1 = encoder.encode_symbol(static_cast<uint8_t>(u_dx & 0xFF), model);
            if (!r_mv1) return std::unexpected(r_mv1.error());
            auto r_mv2 = encoder.encode_symbol(static_cast<uint8_t>(u_dy & 0xFF), model);
            if (!r_mv2) return std::unexpected(r_mv2.error());

            // 2. Motion Compensation & Residual Extraction
            MotionEstimator::compensate_block(ref_frame, header.width, bx, by, mv, pred_block);

            for (size_t y = 0; y < image::kBlockSize; ++y) {
                const size_t row_offset = (static_cast<size_t>(by) + y) * header.width + bx;
                for (size_t x = 0; x < image::kBlockSize; ++x) {
                    const float curr_val = static_cast<float>(curr_ptr[row_offset + x]);
                    residual_block[y * image::kBlockSize + x] = curr_val - pred_block[y * image::kBlockSize + x];
                }
            }

            // 3. DCT + Quantization + ZigZag Serialization
            auto res_dct = dct_engine.forward_dct(residual_block, dct_coeffs);
            if (!res_dct) return std::unexpected(res_dct.error());

            auto res_q = dct_engine.quantize(dct_coeffs, quant_coeffs);
            if (!res_q) return std::unexpected(res_q.error());

            auto res_zz = dct_engine.zigzag_serialize(quant_coeffs, zigzag_coeffs);
            if (!res_zz) return std::unexpected(res_zz.error());

            // 4. Encode DC Residual Delta using full 16-bit resolution
            const int16_t current_dc = zigzag_coeffs[0];
            const int16_t dc_delta = static_cast<int16_t>(current_dc - prev_dc);
            prev_dc = current_dc;

            const uint16_t u_dc = map_int16_to_uint16(dc_delta);
            auto r1 = encoder.encode_symbol(static_cast<uint8_t>((u_dc >> 8) & 0xFF), model);
            if (!r1) return std::unexpected(r1.error());
            auto r2 = encoder.encode_symbol(static_cast<uint8_t>(u_dc & 0xFF), model);
            if (!r2) return std::unexpected(r2.error());

            // 5. Encode AC Residual Stream using full 16-bit resolution
            size_t last_nonzero = 0;
            for (size_t i = image::kBlockElements - 1; i >= 1; --i) {
                if (zigzag_coeffs[i] != 0) {
                    last_nonzero = i;
                    break;
                }
            }

            if (last_nonzero > 0) {
                uint8_t zero_run = 0;
                for (size_t i = 1; i <= last_nonzero; ++i) {
                    const int16_t ac_val = zigzag_coeffs[i];
                    if (ac_val == 0) {
                        if (zero_run == 254) {
                            auto r = encoder.encode_symbol(254, model);
                            if (!r) return std::unexpected(r.error());
                            r = encoder.encode_symbol(0, model);
                            if (!r) return std::unexpected(r.error());
                            r = encoder.encode_symbol(0, model);
                            if (!r) return std::unexpected(r.error());
                            zero_run = 0;
                        } else {
                            ++zero_run;
                        }
                    } else {
                        auto r = encoder.encode_symbol(zero_run, model);
                        if (!r) return std::unexpected(r.error());

                        const uint16_t u_ac = map_int16_to_uint16(ac_val);
                        r = encoder.encode_symbol(static_cast<uint8_t>((u_ac >> 8) & 0xFF), model);
                        if (!r) return std::unexpected(r.error());

                        r = encoder.encode_symbol(static_cast<uint8_t>(u_ac & 0xFF), model);
                        if (!r) return std::unexpected(r.error());

                        zero_run = 0;
                    }
                }
            }

            auto reob = encoder.encode_symbol(0xFF, model);
            if (!reob) return std::unexpected(reob.error());
        }
    }

    auto res_flush = encoder.flush();
    if (!res_flush) return std::unexpected(res_flush.error());

    auto ref_mut = ref_buffer.current_reference();
    std::copy_n(curr_frame.data(), curr_frame.size(), ref_mut.data());
    ref_buffer.set_reference_valid(true);

    return (writer.bits_written() - start_bits) / 8;
}

Result<size_t> VideoCodecEngine::decode_frame(
    VideoHeader header,
    core::BitStreamReader& reader,
    VideoRingBuffer& ref_buffer,
    MutableBuffer output_frame) noexcept
{
    if (output_frame.data() == nullptr || output_frame.size() < static_cast<size_t>(header.width) * header.height) {
        return std::unexpected(CompressionError::InvalidInput);
    }

    auto frame_type_bits = reader.read_bits(8);
    if (!frame_type_bits) return std::unexpected(frame_type_bits.error());

    const auto type = static_cast<FrameType>(*frame_type_bits);

    if (type == FrameType::IFrame || !ref_buffer.has_reference()) {
        const image::ImageHeader img_hdr{
            .width = header.width,
            .height = header.height,
            .format = image::PixelFormat::Grayscale,
            .quality_factor = header.quality_factor
        };

        auto dec_res = m_image_pipeline.decode_grayscale_frame(img_hdr, reader, output_frame);
        if (!dec_res) return std::unexpected(dec_res.error());

        auto ref_mut = ref_buffer.current_reference();
        std::copy_n(output_frame.data(), output_frame.size(), ref_mut.data());
        ref_buffer.set_reference_valid(true);

        return output_frame.size();
    }

    // Decode P-Frame
    const image::DctQuantEngine dct_engine(header.quality_factor);
    core::ContextModel model(2);
    core::ArithmeticDecoder decoder;

    auto res_init = decoder.set_reader(&reader);
    if (!res_init) return std::unexpected(res_init.error());

    alignas(64) std::array<int16_t, image::kBlockElements> zigzag_coeffs{};
    alignas(64) std::array<int16_t, image::kBlockElements> quant_coeffs{};
    alignas(64) std::array<float, image::kBlockElements> dequant_coeffs{};
    alignas(64) std::array<float, image::kBlockElements> residual_reconstructed{};
    alignas(64) std::array<float, image::kBlockElements> pred_block{};

    MotionVector prev_mv{0, 0};
    int16_t prev_dc = 0;

    const ReadOnlyBuffer ref_frame = ref_buffer.current_reference();
    uint8_t* out_ptr = output_frame.data();

    for (uint16_t by = 0; by < header.height; by += image::kBlockSize) {
        for (uint16_t bx = 0; bx < header.width; bx += image::kBlockSize) {
            zigzag_coeffs.fill(0);

            // 1. Decode Motion Vector Delta
            auto dx_sym = decoder.decode_symbol(model);
            if (!dx_sym) return std::unexpected(dx_sym.error());
            auto dy_sym = decoder.decode_symbol(model);
            if (!dy_sym) return std::unexpected(dy_sym.error());

            const int8_t dx_delta = unmap_uint16_to_int8(*dx_sym);
            const int8_t dy_delta = unmap_uint16_to_int8(*dy_sym);

            const MotionVector mv{
                static_cast<int8_t>(prev_mv.dx + dx_delta),
                static_cast<int8_t>(prev_mv.dy + dy_delta)
            };
            prev_mv = mv;

            // 2. Decode DC Residual Delta across full 16-bit range
            auto dc_hi = decoder.decode_symbol(model);
            if (!dc_hi) return std::unexpected(dc_hi.error());
            auto dc_lo = decoder.decode_symbol(model);
            if (!dc_lo) return std::unexpected(dc_lo.error());

            const uint16_t u_dc = static_cast<uint16_t>((static_cast<uint16_t>(*dc_hi) << 8) | static_cast<uint16_t>(*dc_lo));
            const int16_t dc_delta = unmap_uint16_to_int16(u_dc);
            const int16_t current_dc = static_cast<int16_t>(prev_dc + dc_delta);
            zigzag_coeffs[0] = current_dc;
            prev_dc = current_dc;

            // 3. Decode AC Residual Stream across full 16-bit range
            size_t ac_idx = 1;
            while (ac_idx < image::kBlockElements) {
                auto run_sym = decoder.decode_symbol(model);
                if (!run_sym) return std::unexpected(run_sym.error());

                const uint8_t run = *run_sym;
                if (run == 0xFF) break;

                ac_idx += run;
                if (ac_idx >= image::kBlockElements) break;

                auto val_hi = decoder.decode_symbol(model);
                if (!val_hi) return std::unexpected(val_hi.error());
                auto val_lo = decoder.decode_symbol(model);
                if (!val_lo) return std::unexpected(val_lo.error());

                const uint16_t u_ac = static_cast<uint16_t>((static_cast<uint16_t>(*val_hi) << 8) | static_cast<uint16_t>(*val_lo));
                const int16_t ac_val = unmap_uint16_to_int16(u_ac);

                if (ac_val != 0) {
                    zigzag_coeffs[ac_idx] = ac_val;
                    ++ac_idx;
                }
            }

            // 4. Residual Inverse Transformation
            auto res_des = dct_engine.zigzag_deserialize(zigzag_coeffs, quant_coeffs);
            if (!res_des) return std::unexpected(res_des.error());

            auto res_dq = dct_engine.dequantize(quant_coeffs, dequant_coeffs);
            if (!res_dq) return std::unexpected(res_dq.error());

            auto res_idct = dct_engine.inverse_dct(dequant_coeffs, residual_reconstructed);
            if (!res_idct) return std::unexpected(res_idct.error());

            // 5. Synthesize Motion Predicted Block with Residual
            MotionEstimator::compensate_block(ref_frame, header.width, bx, by, mv, pred_block);

            for (size_t y = 0; y < image::kBlockSize; ++y) {
                const size_t row_offset = (static_cast<size_t>(by) + y) * header.width + bx;
                for (size_t x = 0; x < image::kBlockSize; ++x) {
                    const float final_val = pred_block[y * image::kBlockSize + x] + residual_reconstructed[y * image::kBlockSize + x];
                    out_ptr[row_offset + x] = static_cast<uint8_t>(std::clamp(std::round(final_val), 0.0f, 255.0f));
                }
            }
        }
    }

    auto ref_mut = ref_buffer.current_reference();
    std::copy_n(output_frame.data(), output_frame.size(), ref_mut.data());
    ref_buffer.set_reference_valid(true);

    return output_frame.size();
}

} // namespace khcomp::video```
