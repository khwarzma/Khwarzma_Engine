#pragma once

#include <khcomp/types.hpp>
#include <khcomp/utils.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace khcomp::core {

class alignas(utils::kCacheLineAlignment) BitStreamWriter {
public:
    constexpr BitStreamWriter() noexcept = default;
    
    constexpr explicit BitStreamWriter(MutableBuffer buffer) noexcept {
        static_cast<void>(set_buffer(buffer));
    }

    Result<void> set_buffer(MutableBuffer buffer) noexcept;

    Result<void> write_bits(uint64_t value, uint8_t num_bits) noexcept;
    Result<void> write_bytes(ReadOnlyBuffer bytes) noexcept;

    Result<void> flush() noexcept;

    [[nodiscard]] constexpr size_t bits_written() const noexcept { return m_bit_offset; }
    [[nodiscard]] constexpr size_t bytes_written() const noexcept { return utils::bits_to_bytes(m_bit_offset); }
    [[nodiscard]] constexpr size_t capacity_bits() const noexcept { return utils::bytes_to_bits(m_buffer.size()); }
    [[nodiscard]] constexpr size_t capacity_bytes() const noexcept { return m_buffer.size(); }
    [[nodiscard]] constexpr MutableBuffer raw_buffer() const noexcept { return m_buffer; }

    void reset() noexcept;

private:
    MutableBuffer m_buffer{};
    size_t m_bit_offset{0};
    uint64_t m_bit_accumulator{0};
    uint8_t m_bits_in_accumulator{0};
};

class alignas(utils::kCacheLineAlignment) BitStreamReader {
public:
    constexpr BitStreamReader() noexcept = default;
    
    constexpr explicit BitStreamReader(ReadOnlyBuffer buffer) noexcept {
        static_cast<void>(set_buffer(buffer));
    }

    Result<void> set_buffer(ReadOnlyBuffer buffer) noexcept;

    Result<uint64_t> read_bits(uint8_t num_bits) noexcept;
    Result<uint64_t> peek_bits(uint8_t num_bits) noexcept;
    Result<void> advance(uint8_t num_bits) noexcept;

    [[nodiscard]] constexpr size_t bits_read() const noexcept { return m_bit_offset; }
    [[nodiscard]] constexpr size_t bytes_read() const noexcept { return utils::bits_to_bytes(m_bit_offset); }
    [[nodiscard]] constexpr size_t remaining_bits() const noexcept {
        const size_t total = utils::bytes_to_bits(m_buffer.size());
        return m_bit_offset >= total ? 0 : total - m_bit_offset;
    }
    [[nodiscard]] constexpr ReadOnlyBuffer raw_buffer() const noexcept { return m_buffer; }

    void reset() noexcept;

private:
    Result<void> fill_accumulator() noexcept;

    ReadOnlyBuffer m_buffer{};
    size_t m_bit_offset{0};
    uint64_t m_bit_accumulator{0};
    uint8_t m_bits_in_accumulator{0};
};

} // namespace khcomp::core