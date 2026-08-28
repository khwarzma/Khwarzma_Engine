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

} // namespace khcomp::core