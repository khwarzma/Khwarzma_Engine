#include <khcomp/context_model.hpp>
#include <algorithm>
#include <numeric>

namespace khcomp::core {

void ContextModel::reset() noexcept {
    m_history.fill(0);

    for (size_t ctx = 0; ctx < 256; ++ctx) {
        m_frequencies[ctx].fill(1); // Uniform initialization
        m_total_frequencies[ctx] = kAlphabetSize;
    }
}

size_t ContextModel::get_context_index() const noexcept {
    // Hash context history deterministically down to a 8-bit table index [0, 255]
    uint32_t hash = 2166136261U; // FNV-1a 32-bit prime initial bias
    for (size_t i = 0; i < m_order; ++i) {
        hash ^= m_history[i];
        hash *= 16777619U;
    }
    return static_cast<size_t>(hash & 0xFF);
}

void ContextModel::rescale_frequencies(size_t table_idx) noexcept {
    uint32_t new_total = 0;
    auto& table = m_frequencies[table_idx];

    for (size_t i = 0; i < kAlphabetSize; ++i) {
        table[i] = static_cast<uint16_t>((table[i] >> 1) | 1); // Scale down while preventing zero counts
        new_total += table[i];
    }

    m_total_frequencies[table_idx] = new_total;
}

void ContextModel::update(uint8_t symbol) noexcept {
    const size_t ctx = get_context_index();

    if (m_total_frequencies[ctx] + 2 >= kMaxFrequency) {
        rescale_frequencies(ctx);
    }

    m_frequencies[ctx][symbol] += 2; // Incremental adaptation step
    m_total_frequencies[ctx] += 2;

    // Shift history window for byte-granularity Order-N context evaluation
    for (size_t i = m_order - 1; i > 0; --i) {
        m_history[i] = m_history[i - 1];
    }
    m_history[0] = symbol;
}

SymbolStats ContextModel::get_stats(uint8_t symbol) const noexcept {
    const size_t ctx = get_context_index();
    const auto& table = m_frequencies[ctx];

    uint16_t low = 0;
    for (size_t i = 0; i < symbol; ++i) {
        low = static_cast<uint16_t>(low + table[i]);
    }

    const uint16_t high = static_cast<uint16_t>(low + table[symbol]);
    const uint16_t total = static_cast<uint16_t>(m_total_frequencies[ctx]);

    return SymbolStats{
        .low = low,
        .high = high,
        .total = total
    };
}

uint8_t ContextModel::decode_symbol(uint16_t target_count) const noexcept {
    const size_t ctx = get_context_index();
    const auto& table = m_frequencies[ctx];

    uint16_t running_total = 0;
    for (size_t i = 0; i < kAlphabetSize; ++i) {
        running_total = static_cast<uint16_t>(running_total + table[i]);
        if (target_count < running_total) {
            return static_cast<uint8_t>(i);
        }
    }

    return 255; // Boundary fallback
}

uint16_t ContextModel::total_frequency() const noexcept {
    const size_t ctx = get_context_index();
    return static_cast<uint16_t>(m_total_frequencies[ctx]);
}

} // namespace khcomp::core