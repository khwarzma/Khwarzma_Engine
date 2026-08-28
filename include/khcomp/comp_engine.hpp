#pragma once

#include <khcomp/bit_stream.hpp>
#include <khcomp/context_model.hpp>
#include <khcomp/types.hpp>
#include <cstddef>
#include <cstdint>

namespace khcomp::core {

// 32-bit precision range bounds for Arithmetic Coding
inline constexpr uint32_t kTopValue    = 0xFFFFFFFFU;
inline constexpr uint32_t kFirstQtr   = (kTopValue / 4) + 1;
inline constexpr uint32_t kHalf       = 2 * kFirstQtr;
inline constexpr uint32_t kThirdQtr  = 3 * kFirstQtr;

class ArithmeticEncoder {
public:
    constexpr ArithmeticEncoder() noexcept = default;

    constexpr explicit ArithmeticEncoder(BitStreamWriter* writer) noexcept 
        : m_writer(writer) {}

    constexpr void set_writer(BitStreamWriter* writer) noexcept {
        m_writer = writer;
        reset();
    }

    constexpr void reset() noexcept {
        m_low = 0;
        m_high = kTopValue;
        m_bits_to_follow = 0;
    }

    Result<void> encode_symbol(uint8_t symbol, ContextModel& model) noexcept;
    Result<void> flush() noexcept;

private:
    Result<void> bit_plus_follow(bool bit) noexcept;

    BitStreamWriter* m_writer{nullptr};
    uint32_t m_low{0};
    uint32_t m_high{kTopValue};
    uint32_t m_bits_to_follow{0};
};

class ArithmeticDecoder {
public:
    constexpr ArithmeticDecoder() noexcept = default;

    constexpr explicit ArithmeticDecoder(BitStreamReader* reader) noexcept 
        : m_reader(reader) {}

    Result<void> set_reader(BitStreamReader* reader) noexcept {
        m_reader = reader;
        return init();
    }

    Result<void> init() noexcept;

    Result<uint8_t> decode_symbol(ContextModel& model) noexcept;

private:
    BitStreamReader* m_reader{nullptr};
    uint32_t m_low{0};
    uint32_t m_high{kTopValue};
    uint32_t m_value{0};
};

} // namespace khcomp::core