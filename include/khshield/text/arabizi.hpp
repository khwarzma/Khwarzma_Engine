#ifndef KHSHIELD_TEXT_ARABIZI_HPP
#define KHSHIELD_TEXT_ARABIZI_HPP

#include <string>
#include <string_view>

namespace khshield::text {

class ArabiziNormalizer {
public:
    ArabiziNormalizer() = default;

    // Normalizes mixed Latinized Arabic (Arabizi) and UTF-8 strings into canonical Arabic
    [[nodiscard]] static std::string normalize(std::string_view input);
};

} // namespace khshield::text

#endif // KHSHIELD_TEXT_ARABIZI_HPP