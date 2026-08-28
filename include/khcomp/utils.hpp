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

} // namespace khcomp::utils