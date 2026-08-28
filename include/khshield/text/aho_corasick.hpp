#ifndef KHSHIELD_TEXT_AHO_CORASICK_HPP
#define KHSHIELD_TEXT_AHO_CORASICK_HPP

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_map>
#include <queue>
#include <shared_mutex>
#include <span>

namespace khshield::text {

struct MatchResult {
    std::string pattern;
    size_t start_pos{0};
    size_t end_pos{0};
};

class AhoCorasick {
public:
    AhoCorasick() = default;
    ~AhoCorasick() = default;

    // Prevent copying to maintain state consistency across threads
    AhoCorasick(const AhoCorasick&) = delete;
    AhoCorasick& operator=(const AhoCorasick&) = delete;
    AhoCorasick(AhoCorasick&&) noexcept = default;
    AhoCorasick& operator=(AhoCorasick&&) noexcept = default;

    void add_keyword(std::string_view keyword);
    void build();
    [[nodiscard]] std::vector<MatchResult> search(std::string_view text) const;
    [[nodiscard]] bool contains_any(std::string_view text) const;

private:
    struct Node {
        std::unordered_map<char, size_t> children;
        size_t fail_link{0};
        size_t output_link{0};
        std::vector<std::string> output_patterns;
    };

    std::vector<Node> nodes_{1}; // Root node at index 0
    bool is_built_{false};
    mutable std::shared_mutex mutex_;
};

} // namespace khshield::text

#endif // KHSHIELD_TEXT_AHO_CORASICK_HPP