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

} // namespace khcomp::image