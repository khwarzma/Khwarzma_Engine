#include "khshield/text/aho_corasick.hpp"
#include <mutex>
#include <queue>
#include <stdexcept>

namespace khshield::text {
namespace {

void skip_whitespace(std::string_view text, size_t& pos) {
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n' ||
                                 text[pos] == '\r' || text[pos] == '\t')) {
        ++pos;
    }
}

std::string parse_json_string(std::string_view text, size_t& pos) {
    if (pos >= text.size() || text[pos] != '"') {
        return {};
    }
    ++pos;
    std::string output;
    output.reserve(text.size() - pos);
    while (pos < text.size()) {
        const char ch = text[pos++];
        if (ch == '"') {
            return output;
        }
        if (ch == '\\') {
            if (pos >= text.size()) {
                return {};
            }
            const char escaped = text[pos++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                default: output.push_back(escaped); break;
            }
            continue;
        }
        output.push_back(ch);
    }
    return {};
}

} // namespace

void AhoCorasick::add_keyword(std::string_view keyword) {
    if (keyword.empty()) return;

    std::unique_lock lock(mutex_);
    is_built_ = false;

    size_t current = 0;
    for (char c : keyword) {
        if (!nodes_[current].children.contains(c)) {
            nodes_[current].children[c] = nodes_.size();
            nodes_.emplace_back();
        }
        current = nodes_[current].children[c];
    }
    nodes_[current].output_patterns.emplace_back(keyword);
}

void AhoCorasick::clear() {
    std::unique_lock lock(mutex_);
    nodes_.assign(1, Node{});
    is_built_ = false;
}

void AhoCorasick::load_dictionary_from_json(std::string_view json_text) {
    clear();
    if (json_text.empty()) {
        return;
    }

    std::string_view text = json_text;
    size_t pos = 0;
    skip_whitespace(text, pos);
    if (pos >= text.size() || text[pos] != '{') {
        return;
    }
    ++pos;

    while (true) {
        skip_whitespace(text, pos);
        if (pos >= text.size()) {
            break;
        }
        if (text[pos] == '}') {
            ++pos;
            break;
        }

        const std::string key = parse_json_string(text, pos);
        if (key.empty()) {
            break;
        }
        skip_whitespace(text, pos);
        if (pos >= text.size() || text[pos] != ':') {
            break;
        }
        ++pos;
        skip_whitespace(text, pos);
        if (pos >= text.size() || text[pos] != '[') {
            break;
        }
        ++pos;

        while (true) {
            skip_whitespace(text, pos);
            if (pos >= text.size()) {
                break;
            }
            if (text[pos] == ']') {
                ++pos;
                break;
            }

            const std::string value = parse_json_string(text, pos);
            if (!value.empty()) {
                add_keyword(value);
            }

            skip_whitespace(text, pos);
            if (pos >= text.size()) {
                break;
            }
            if (text[pos] == ',') {
                ++pos;
                continue;
            }
            if (text[pos] == ']') {
                ++pos;
                break;
            }
            break;
        }

        skip_whitespace(text, pos);
        if (pos >= text.size()) {
            break;
        }
        if (text[pos] == ',') {
            ++pos;
            continue;
        }
        if (text[pos] == '}') {
            ++pos;
            break;
        }
        break;
    }

    build();
}

void AhoCorasick::build() {
    std::unique_lock lock(mutex_);
    if (nodes_.size() <= 1) {
        is_built_ = true;
        return;
    }

    std::queue<size_t> q;

    // Direct children of root point their failure links back to root (node 0)
    for (auto& [ch, child_idx] : nodes_[0].children) {
        nodes_[child_idx].fail_link = 0;
        q.push(child_idx);
    }

    while (!q.empty()) {
        size_t current = q.front();
        q.pop();

        for (auto& [ch, child_idx] : nodes_[current].children) {
            size_t fail = nodes_[current].fail_link;

            while (fail != 0 && !nodes_[fail].children.contains(ch)) {
                fail = nodes_[fail].fail_link;
            }

            if (nodes_[fail].children.contains(ch)) {
                nodes_[child_idx].fail_link = nodes_[fail].children.at(ch);
            } else {
                nodes_[child_idx].fail_link = 0;
            }

            // Output link optimization
            size_t fail_child = nodes_[child_idx].fail_link;
            if (!nodes_[fail_child].output_patterns.empty()) {
                nodes_[child_idx].output_link = fail_child;
            } else {
                nodes_[child_idx].output_link = nodes_[fail_child].output_link;
            }

            q.push(child_idx);
        }
    }

    is_built_ = true;
}

std::vector<MatchResult> AhoCorasick::search(std::string_view text) const {
    std::shared_lock lock(mutex_);
    std::vector<MatchResult> results;
    if (!is_built_ || text.empty()) return results;

    size_t current = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];

        while (current != 0 && !nodes_[current].children.contains(c)) {
            current = nodes_[current].fail_link;
        }

        if (nodes_[current].children.contains(c)) {
            current = nodes_[current].children.at(c);
        }

        // Collect direct matches
        for (const auto& pattern : nodes_[current].output_patterns) {
            results.push_back({
                .pattern = pattern,
                .start_pos = i + 1 - pattern.size(),
                .end_pos = i + 1
            });
        }

        // Collect matches from output links
        size_t temp = nodes_[current].output_link;
        while (temp != 0) {
            for (const auto& pattern : nodes_[temp].output_patterns) {
                results.push_back({
                    .pattern = pattern,
                    .start_pos = i + 1 - pattern.size(),
                    .end_pos = i + 1
                });
            }
            temp = nodes_[temp].output_link;
        }
    }

    return results;
}

bool AhoCorasick::contains_any(std::string_view text) const {
    std::shared_lock lock(mutex_);
    if (!is_built_ || text.empty()) return false;

    size_t current = 0;
    for (char c : text) {
        while (current != 0 && !nodes_[current].children.contains(c)) {
            current = nodes_[current].fail_link;
        }

        if (nodes_[current].children.contains(c)) {
            current = nodes_[current].children.at(c);
        }

        if (!nodes_[current].output_patterns.empty() || nodes_[current].output_link != 0) {
            return true;
        }
    }

    return false;
}

} // namespace khshield::text