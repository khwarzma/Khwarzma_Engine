# Header Files: khcomp


--- FILE: include/khcomp/audio_engine.hpp ---
```cpp
#pragma once

#include <khcomp/comp_engine.hpp>
#include <khcomp/context_model.hpp>
#include <khcomp/types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace khcomp::audio {

inline constexpr uint8_t kMaxAudioChannels = 2;

struct AudioHeader {
    uint32_t sample_rate{44100};
    uint8_t channels{2};
    uint32_t num_samples{0}; // Total samples per channel
};

class AudioCodecEngine {
public:
    constexpr AudioCodecEngine() noexcept = default;

    // Encodes 16-bit signed PCM interleaved audio using dynamic temporal linear prediction and arithmetic coding
    Result<size_t> encode_pcm16(
        AudioHeader header,
        ReadOnlyBuffer pcm_in,
        core::BitStreamWriter& writer) noexcept;

    // Decodes arithmetic bitstream back into 16-bit signed PCM interleaved audio
    Result<size_t> decode_pcm16(
        AudioHeader header,
        core::BitStreamReader& reader,
        MutableBuffer pcm_out) noexcept;

private:
    [[nodiscard]] static constexpr uint16_t map_int16_to_uint16(int16_t val) noexcept {
        return static_cast<uint16_t>((val << 1) ^ (val >> 15));
    }

    [[nodiscard]] static constexpr int16_t unmap_uint16_to_int16(uint16_t val) noexcept {
        return static_cast<int16_t>((val >> 1) ^ (-(val & 1)));
    }
};

} // namespace khcomp::audio```


--- FILE: include/khcomp/bit_stream.hpp ---
```cpp
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

} // namespace khcomp::core```


--- FILE: include/khcomp/comp_engine.hpp ---
```cpp
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

} // namespace khcomp::core```


--- FILE: include/khcomp/context_model.hpp ---
```cpp
#pragma once

#include <khcomp/types.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace khcomp::core {

// Max context depth N in range [1, 4]
inline constexpr size_t kMaxContextOrder = 4;
inline constexpr size_t kAlphabetSize    = 256;
inline constexpr uint16_t kMaxFrequency  = 0xFFFF; // 2^16 - 1

struct SymbolStats {
    uint16_t low{0};
    uint16_t high{0};
    uint16_t total{0};
};

class ContextModel {
public:
    constexpr ContextModel() noexcept {
        reset();
    }

    constexpr explicit ContextModel(size_t order) noexcept {
        set_order(order);
        reset();
    }

    constexpr void set_order(size_t order) noexcept {
        if (order < 1) {
            m_order = 1;
        } else if (order > kMaxContextOrder) {
            m_order = kMaxContextOrder;
        } else {
            m_order = order;
        }
    }

    [[nodiscard]] constexpr size_t order() const noexcept {
        return m_order;
    }

    void reset() noexcept;

    void update(uint8_t symbol) noexcept;

    [[nodiscard]] SymbolStats get_stats(uint8_t symbol) const noexcept;

    [[nodiscard]] uint8_t decode_symbol(uint16_t target_count) const noexcept;

    [[nodiscard]] uint16_t total_frequency() const noexcept;

private:
    void rescale_frequencies(size_t table_idx) noexcept;

    [[nodiscard]] size_t get_context_index() const noexcept;

    size_t m_order{2};
    std::array<uint8_t, kMaxContextOrder> m_history{};
    
    // Static pre-allocation: 256 contexts * 256 symbols = 65,536 frequency entries
    // Uses 128 KB of stack/flat member layout, guaranteeing zero dynamic allocations.
    alignas(64) std::array<std::array<uint16_t, kAlphabetSize>, 256> m_frequencies{};
    alignas(64) std::array<uint32_t, 256> m_total_frequencies{};
};

} // namespace khcomp::core```


--- FILE: include/khcomp/image_engine.hpp ---
```cpp
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

} // namespace khcomp::image```


--- FILE: include/khcomp/image_frame.hpp ---
```cpp
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

} // namespace khcomp::image```


--- FILE: include/khcomp/types.hpp ---
```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace khcomp {

enum class CompressionError : uint8_t {
    None = 0,
    BufferTooSmall,
    InvalidInput,
    CorruptedData,
    HighEntropyRejected,
    AlignmentError,
    Overflow
};

constexpr std::string_view error_to_string(CompressionError err) noexcept {
    switch (err) {
        case CompressionError::None:
            return "No error";
        case CompressionError::BufferTooSmall:
            return "Destination buffer is too small";
        case CompressionError::InvalidInput:
            return "Invalid input parameters or buffer state";
        case CompressionError::CorruptedData:
            return "Corrupted bitstream or data structure";
        case CompressionError::HighEntropyRejected:
            return "High entropy input rejected during pre-flight check";
        case CompressionError::AlignmentError:
            return "Buffer does not satisfy required 64-byte alignment";
        case CompressionError::Overflow:
            return "Bitstream write operation exceeded capacity";
    }
    return "Unknown error";
}

template <typename T>
using Result = std::expected<T, CompressionError>;

struct CompressionReport {
    bool is_completed{false};
    size_t original_size{0};
    size_t compressed_size{0};
    double compression_ratio{0.0};
    double throughput_mbps{0.0};
    double latency_ms{0.0};
};

using ReadOnlyBuffer = std::span<const uint8_t>;
using MutableBuffer  = std::span<uint8_t>;

} // namespace khcomp```


--- FILE: include/khcomp/utils.hpp ---
```cpp
#pragma once

#include <khcomp/types.hpp>
#include <cstddef>
#include <cstdint>
#include <bit>
#include <span>

namespace khcomp::utils {

constexpr size_t kCacheLineAlignment = 64;

[[nodiscard]] constexpr bool is_aligned(const void* ptr, size_t alignment = kCacheLineAlignment) noexcept {
    return (reinterpret_cast<uintptr_t>(ptr) % alignment) == 0;
}

template <typename T>
[[nodiscard]] constexpr bool is_aligned(std::span<T> buffer, size_t alignment = kCacheLineAlignment) noexcept {
    return is_aligned(buffer.data(), alignment);
}

[[nodiscard]] constexpr size_t bytes_to_bits(size_t bytes) noexcept {
    return bytes * 8;
}

[[nodiscard]] constexpr size_t bits_to_bytes(size_t bits) noexcept {
    return (bits + 7) / 8;
}

} // namespace khcomp::utils```


--- FILE: include/khcomp/video_ring_buffer.hpp ---
```cpp
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

} // namespace khcomp::video```
