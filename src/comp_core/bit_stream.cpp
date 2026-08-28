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

} // namespace khcomp::core