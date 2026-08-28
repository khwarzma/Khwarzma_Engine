#pragma once

#include <khcomp/comp_engine.hpp>
#include <khcomp/context_model.hpp>
#include <khcomp/image_engine.hpp>
#include <khcomp/types.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace khcomp::image {

enum class PixelFormat : uint8_t {
    Grayscale = 0,
    YCbCr444  = 1
};

struct ImageHeader {
    uint16_t width{0};
    uint16_t height{0};
    PixelFormat format{PixelFormat::Grayscale};
    uint8_t quality_factor{50};
};

class ImageFramePipeline {
public:
    constexpr ImageFramePipeline() noexcept = default;

    // Encodes a grayscale byte frame buffer into an arithmetic-coded bitstream
    Result<size_t> encode_grayscale_frame(
        ImageHeader header,
        ReadOnlyBuffer raw_pixels,
        core::BitStreamWriter& writer) noexcept;

    // Decodes an arithmetic-coded bitstream back into a grayscale byte frame buffer
    Result<size_t> decode_grayscale_frame(
        ImageHeader header,
        core::BitStreamReader& reader,
        MutableBuffer output_pixels) noexcept;

private:
    // Helper to process an 8x8 block from source byte frame
    static void extract_8x8_block(
        ReadOnlyBuffer src,
        uint16_t width,
        uint16_t block_x,
        uint16_t block_y,
        std::span<float, kBlockElements> block_out) noexcept;

    // Helper to store an 8x8 reconstructed float block back to destination byte frame
    static void store_8x8_block(
        std::span<const float, kBlockElements> block_in,
        uint16_t width,
        uint16_t block_x,
        uint16_t block_y,
        MutableBuffer dst) noexcept;

    // Lossless mapping of signed int16_t to unsigned uint16_t (ZigZag integer encoding)
    [[nodiscard]] static constexpr uint16_t map_int16_to_uint16(int16_t val) noexcept {
        return static_cast<uint16_t>((val << 1) ^ (val >> 15));
    }

    // Unmapping unsigned uint16_t back to signed int16_t
    [[nodiscard]] static constexpr int16_t unmap_uint16_to_int16(uint16_t val) noexcept {
        return static_cast<int16_t>((val >> 1) ^ (-(val & 1)));
    }
};

} // namespace khcomp::image