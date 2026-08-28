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

} // namespace khcomp::core