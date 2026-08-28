#include "khshield/text/arabizi.hpp"
#include <unordered_map>
#include <cctype>

namespace khshield::text {

namespace {

// Standard Arabizi substitution mapping to Arabic UTF-8 string sequences
const std::unordered_map<std::string, std::string> ARABIZI_MULTI_MAP = {
    {"3'", "غ"}, {"7'", "خ"}, {"2'", "خ"}, {"5", "خ"},
    {"3", "ع"},  {"7", "ح"},  {"2", "أ"},  {"8", "ق"},
    {"6", "ط"},  {"9", "ص"},  {"9'", "ض"}, {"4", "ش"}
};

const std::unordered_map<char, std::string> ARABIZI_SINGLE_MAP = {
    {'a', "ا"}, {'b', "ب"}, {'t', "ت"}, {'g', "ج"}, {'j', "ج"},
    {'h', "ه"}, {'d', "د"}, {'r', "ر"}, {'z', "ز"}, {'s', "س"},
    {'f', "ف"}, {'q', "ق"}, {'k', "ك"}, {'l', "ل"}, {'m', "م"},
    {'n', "ن"}, {'w', "و"}, {'y', "ي"}, {'e', "ا"}, {'i', "ي"}, {'o', "و"}, {'u', "و"}
};

} // namespace

std::string ArabiziNormalizer::normalize(std::string_view input) {
    std::string result;
    result.reserve(input.size() * 2);

    for (size_t i = 0; i < input.size(); ++i) {
        // First check for 2-character numeric/punctuation patterns (e.g., 3', 7', 9')
        if (i + 1 < input.size()) {
            std::string sub = std::string(input.substr(i, 2));
            if (ARABIZI_MULTI_MAP.contains(sub)) {
                result += ARABIZI_MULTI_MAP.at(sub);
                ++i;
                continue;
            }
        }

        // Single digit mappings
        std::string single_num(1, input[i]);
        if (ARABIZI_MULTI_MAP.contains(single_num)) {
            result += ARABIZI_MULTI_MAP.at(single_num);
            continue;
        }

        // Single letter character mappings
        char lower_c = static_cast<char>(std::tolower(static_cast<unsigned char>(input[i])));
        if (ARABIZI_SINGLE_MAP.contains(lower_c)) {
            result += ARABIZI_SINGLE_MAP.at(lower_c);
            continue;
        }

        // Preserve already canonical Arabic UTF-8 code points or unrecognized characters
        result += input[i];
    }

    return result;
}

} // namespace khshield::text