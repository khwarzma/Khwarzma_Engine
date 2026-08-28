#pragma once

#include <khcomp/comp_engine.hpp>
#include <khcomp/context_model.hpp>
#include <khcomp/image_engine.hpp>
#include <khcomp/image_frame.hpp>
#include <khcomp/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace khcomp::video {

inline constexpr int32_t kDefaultSearchWindow = 8;

enum class FrameType : uint8_t {
    IFrame = 0, // Intra-coded frame (Full DCT image frame)
    PFrame = 1  // Predicted frame (Motion Vectors + DCT Residuals)
};

struct VideoHeader {
    uint16_t width{0};
    uint16_t height{0};
    uint8_t quality_factor{50};
    int32_t search_window{kDefaultSearchWindow};
};

struct MotionVector {
    int8_t dx{0};
    int8_t dy{0};

    constexpr bool operator==(const MotionVector& other) const noexcept = default;
};

class VideoRingBuffer {
public:
    constexpr VideoRingBuffer() noexcept = default;

    constexpr Result<void> allocate(uint16_t width, uint16_t height, std::span<uint8_t> backing_store) noexcept {
        const size_t frame_size = static_cast<size_t>(width) * height;
        if (backing_store.size() < frame_size) {
            return std::unexpected(CompressionError::InvalidInput);
        }
        m_frame_buffer = backing_store.subspan(0, frame_size);
        m_width = width;
        m_height = height;
        m_has_reference = false;
        return {};
    }

    [[nodiscard]] constexpr MutableBuffer current_reference() noexcept {
        return MutableBuffer(m_frame_buffer.data(), m_frame_buffer.size());
    }

    [[nodiscard]] constexpr ReadOnlyBuffer current_reference() const noexcept {
        return ReadOnlyBuffer(m_frame_buffer.data(), m_frame_buffer.size());
    }

    [[nodiscard]] constexpr bool has_reference() const noexcept {
        return m_has_reference;
    }

    constexpr void set_reference_valid(bool valid = true) noexcept {
        m_has_reference = valid;
    }

private:
    std::span<uint8_t> m_frame_buffer{};
    uint16_t m_width{0};
    uint16_t m_height{0};
    bool m_has_reference{false};
};

class MotionEstimator {
public:
    constexpr MotionEstimator() noexcept = default;

    // Zero-allocation Sum of Absolute Differences (SAD) block matching algorithm
    [[nodiscard]] static MotionVector find_best_motion_vector(
        ReadOnlyBuffer curr_frame,
        ReadOnlyBuffer ref_frame,
        uint16_t width,
        uint16_t height,
        uint16_t block_x,
        uint16_t block_y,
        int32_t search_window) noexcept;

    static void compensate_block(
        ReadOnlyBuffer ref_frame,
        uint16_t width,
        uint16_t block_x,
        uint16_t block_y,
        MotionVector mv,
        std::span<float, image::kBlockElements> pred_out) noexcept;

private:
    [[nodiscard]] static uint32_t compute_sad(
        ReadOnlyBuffer curr_frame,
        ReadOnlyBuffer ref_frame,
        uint16_t width,
        uint16_t block_x,
        uint16_t block_y,
        int32_t ref_x,
        int32_t ref_y) noexcept;
};

class VideoCodecEngine {
public:
    constexpr VideoCodecEngine() noexcept = default;

    // Encodes an Intra (I-Frame) or Predicted (P-Frame) to an arithmetic bitstream
    Result<size_t> encode_frame(
        VideoHeader header,
        FrameType type,
        ReadOnlyBuffer curr_frame,
        VideoRingBuffer& ref_buffer,
        core::BitStreamWriter& writer) noexcept;

    // Decodes an Intra (I-Frame) or Predicted (P-Frame) from an arithmetic bitstream
    Result<size_t> decode_frame(
        VideoHeader header,
        core::BitStreamReader& reader,
        VideoRingBuffer& ref_buffer,
        MutableBuffer output_frame) noexcept;

private:
    // Helper for 8-bit Motion Vector integer mapping
    [[nodiscard]] static constexpr uint16_t map_int8_to_uint16(int8_t val) noexcept {
        const int16_t v = val;
        return static_cast<uint16_t>((v << 1) ^ (v >> 15));
    }

    [[nodiscard]] static constexpr int8_t unmap_uint16_to_int8(uint16_t val) noexcept {
        const int16_t res = static_cast<int16_t>((val >> 1) ^ (-(val & 1)));
        return static_cast<int8_t>(res);
    }

    // Helpers for 16-bit DCT residual coefficient mapping
    [[nodiscard]] static constexpr uint16_t map_int16_to_uint16(int16_t val) noexcept {
        return static_cast<uint16_t>((val << 1) ^ (val >> 15));
    }

    [[nodiscard]] static constexpr int16_t unmap_uint16_to_int16(uint16_t val) noexcept {
        return static_cast<int16_t>((val >> 1) ^ (-(val & 1)));
    }

    image::ImageFramePipeline m_image_pipeline{};
};

} // namespace khcomp::video