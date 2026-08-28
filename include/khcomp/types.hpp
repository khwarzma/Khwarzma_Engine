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

} // namespace khcomp