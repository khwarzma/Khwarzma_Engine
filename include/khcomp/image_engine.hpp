#pragma once

#include <khcomp/types.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace khcomp::image {

inline constexpr size_t kBlockSize = 8;
inline constexpr size_t kBlockElements = kBlockSize * kBlockSize; // 64

// Standard JPEG Luminance Quantization Matrix
inline constexpr std::array<uint16_t, kBlockElements> kDefaultLuminanceQuantTable = {
    16, 11, 10, 16, 24,  40,  51,  61,
    12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,
    14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,
    24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99
};

// 8x8 Zig-Zag Mapping Indices (2D Matrix -> 1D Sequence)
inline constexpr std::array<uint8_t, kBlockElements> kZigZagIndices = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

class DctQuantEngine {
public:
    constexpr DctQuantEngine() noexcept {
        set_quality_factor(50);
    }

    constexpr explicit DctQuantEngine(uint32_t quality_factor) noexcept {
        set_quality_factor(quality_factor);
    }

    constexpr void set_quality_factor(uint32_t quality_factor) noexcept {
        uint32_t q = quality_factor;
        if (q < 1) q = 1;
        if (q > 100) q = 100;

        uint32_t scale = (q < 50) ? (5000 / q) : (200 - q * 2);

        for (size_t i = 0; i < kBlockElements; ++i) {
            uint32_t val = (static_cast<uint32_t>(kDefaultLuminanceQuantTable[i]) * scale + 50) / 100;
            if (val < 1) val = 1;
            if (val > 255) val = 255;
            m_quant_table[i] = static_cast<uint16_t>(val);
        }
    }

    [[nodiscard]] constexpr const std::array<uint16_t, kBlockElements>& quant_table() const noexcept {
        return m_quant_table;
    }

    // Fast 8x8 Block 2D DCT-II Transform using precomputed lookup tables
    Result<void> forward_dct(std::span<const float, kBlockElements> input, std::span<float, kBlockElements> output) const noexcept;

    // Fast 8x8 Block 2D Inverse DCT (IDCT) Transform using precomputed lookup tables
    Result<void> inverse_dct(std::span<const float, kBlockElements> input, std::span<float, kBlockElements> output) const noexcept;

    // Quantize floating point DCT coefficients to 16-bit signed integers
    Result<void> quantize(std::span<const float, kBlockElements> dct_in, std::span<int16_t, kBlockElements> quant_out) const noexcept;

    // Dequantize 16-bit signed quantized coefficients back to float
    Result<void> dequantize(std::span<const int16_t, kBlockElements> quant_in, std::span<float, kBlockElements> dct_out) const noexcept;

    // Zig-zag ordering transformation: 2D 8x8 matrix -> 1D sequence
    Result<void> zigzag_serialize(std::span<const int16_t, kBlockElements> input, std::span<int16_t, kBlockElements> output) const noexcept;

    // Zig-zag deserialization transformation: 1D sequence -> 2D 8x8 matrix
    Result<void> zigzag_deserialize(std::span<const int16_t, kBlockElements> input, std::span<int16_t, kBlockElements> output) const noexcept;

private:
    std::array<uint16_t, kBlockElements> m_quant_table{};
};

} // namespace khcomp::image