#ifndef KHSHIELD_TEXT_SAM_INFERENCE_HPP
#define KHSHIELD_TEXT_SAM_INFERENCE_HPP

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <memory>

namespace khshield::text {

struct SAMInferenceResult {
    bool is_valid{false};
    float risk_score{0.0f};
    std::string violation_category{"NONE"};
};

class SAMInferenceEngine {
public:
    SAMInferenceEngine() = default;
    explicit SAMInferenceEngine(const std::filesystem::path& model_path);
    ~SAMInferenceEngine() = default;

    bool load_model(const std::filesystem::path& model_path);
    [[nodiscard]] bool is_loaded() const noexcept { return is_loaded_; }

    [[nodiscard]] SAMInferenceResult evaluate(std::string_view text) const;

private:
    #pragma pack(push, 1)
    struct KHMHeader {
        char magic[4];       // "KHM1"
        uint32_t version;
        uint32_t vocab_size;
        uint32_t embedding_dim;
        float threshold;
    };
    #pragma pack(pop)

    bool is_loaded_{false};
    float threshold_{0.5f};
    uint32_t embedding_dim_{0};
    std::unordered_map<std::string, std::vector<int8_t>> vocabulary_weights_;

    [[nodiscard]] std::vector<std::string_view> tokenize(std::string_view text) const;
};

} // namespace khshield::text

#endif // KHSHIELD_TEXT_SAM_INFERENCE_HPP