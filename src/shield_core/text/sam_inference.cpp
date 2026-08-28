#include "khshield/text/sam_inference.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace khshield::text {

SAMInferenceEngine::SAMInferenceEngine(const std::filesystem::path& model_path) {
    load_model(model_path);
}

bool SAMInferenceEngine::load_model(const std::filesystem::path& model_path) {
    is_loaded_ = false;
    vocabulary_weights_.clear();

    if (!std::filesystem::exists(model_path)) {
        return false;
    }

    std::ifstream file(model_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    KHMHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(KHMHeader));
    if (!file || std::memcmp(header.magic, "KHM1", 4) != 0) {
        return false; // Corrupt or invalid binary header
    }

    threshold_ = header.threshold;
    embedding_dim_ = header.embedding_dim;

    // Safe weight reader
    for (uint32_t i = 0; i < header.vocab_size; ++i) {
        uint16_t word_len = 0;
        file.read(reinterpret_cast<char*>(&word_len), sizeof(word_len));
        if (!file || word_len == 0 || word_len > 1024) {
            vocabulary_weights_.clear();
            return false; // Bound validation failure
        }

        std::string word(word_len, '\0');
        file.read(word.data(), word_len);

        std::vector<int8_t> weights(embedding_dim_);
        file.read(reinterpret_cast<char*>(weights.data()), embedding_dim_);

        if (!file) {
            vocabulary_weights_.clear();
            return false;
        }

        vocabulary_weights_.emplace(std::move(word), std::move(weights));
    }

    is_loaded_ = true;
    return true;
}

std::vector<std::string_view> SAMInferenceEngine::tokenize(std::string_view text) const {
    std::vector<std::string_view> tokens;
    size_t start = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(text[i])) || std::ispunct(static_cast<unsigned char>(text[i]))) {
            if (i > start) {
                tokens.push_back(text.substr(start, i - start));
            }
            start = i + 1;
        }
    }
    if (start < text.size()) {
        tokens.push_back(text.substr(start));
    }

    return tokens;
}

SAMInferenceResult SAMInferenceEngine::evaluate(std::string_view text) const {
    if (!is_loaded_ || text.empty()) {
        return {.is_valid = false, .risk_score = 0.0f, .violation_category = "NONE"};
    }

    auto tokens = tokenize(text);
    if (tokens.empty()) {
        return {.is_valid = true, .risk_score = 0.0f, .violation_category = "NONE"};
    }

    std::vector<float> pooled_embedding(embedding_dim_, 0.0f);
    size_t matched_tokens = 0;

    for (auto token : tokens) {
        std::string key(token);
        if (vocabulary_weights_.find(key) != vocabulary_weights_.end()) {
            const auto& weights = vocabulary_weights_.at(key);
            for (size_t d = 0; d < embedding_dim_; ++d) {
                pooled_embedding[d] += static_cast<float>(weights[d]) / 127.0f;
            }
            matched_tokens++;
        }
    }

    if (matched_tokens == 0) {
        return {.is_valid = true, .risk_score = 0.001f, .violation_category = "NONE"};
    }

    // Sigmoid aggregate activation over pooled feature vector
    float sum = 0.0f;
    for (float val : pooled_embedding) {
        sum += val;
    }
    float norm_sum = sum / static_cast<float>(matched_tokens);
    float score = 1.0f / (1.0f + std::exp(-norm_sum));

    bool is_flagged = score >= threshold_;
    return {
        .is_valid = true,
        .risk_score = score,
        .violation_category = is_flagged ? "PROFANITY_SEMANTIC" : "NONE"
    };
}

} // namespace khshield::text