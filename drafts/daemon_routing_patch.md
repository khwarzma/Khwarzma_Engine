# Production-ready daemon routing draft

This draft replaces the static daemon response with bounded, thread-safe routing.
It intentionally does **not** modify `src/server_daemon/main_daemon.cpp`.

## Design notes

- Uses a fixed two-worker queue. The accept loop never creates unbounded detached
  threads; when the queue is full, the client receives a `busy` response.
- Uses `engine_mutex` around all ShieldEngine and compression-engine calls.
- Reads configuration from the environment, with the required defaults:
  `/tmp/kh_core.sock`, `/home/kh-core/app/models/ar_v1.khm`, and
  `/tmp/kh_processing/`.
- Media requests use `file_path`, constrained to the configured processing
  directory. The file is read from disk and is not accepted as Base64 JSON.
- The current repository has no `CompEngine` class. Compression is implemented
  with the existing `khcomp::image::ImageFramePipeline` and
  `khcomp::audio::AudioCodecEngine` APIs; both are protected by the same mutex.
- Image compression expects raw grayscale pixels and `width`/`height` divisible
  by 8. Audio compression expects little-endian signed PCM16 and `channels`,
  `sample_rate`, and `num_samples`.
- `safe_write()` handles partial writes, `EINTR`, zero-byte writes, and errors.

## Full proposed `src/server_daemon/main_daemon.cpp`

```cpp
#include <atomic>
#include <array>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "khcomp/audio_engine.hpp"
#include "khcomp/bit_stream.hpp"
#include "khcomp/image_frame.hpp"
#include "khshield/shield_engine.hpp"

namespace {

constexpr size_t kMaxWorkers = 2;
constexpr size_t kMaxRequestBytes = 64 * 1024;
constexpr size_t kMaxFileBytes = 128 * 1024 * 1024;

std::string socket_path = "/tmp/kh_core.sock";
std::filesystem::path model_path = "/home/kh-core/app/models/ar_v1.khm";
std::filesystem::path processing_dir = "/tmp/kh_processing/";
int server_fd = -1;
std::atomic<bool> shutting_down{false};

khshield::ShieldEngine shield_engine(khshield::Preset::STRICT);
khcomp::image::ImageFramePipeline image_engine;
khcomp::audio::AudioCodecEngine audio_engine;
std::mutex engine_mutex;

std::mutex queue_mutex;
std::condition_variable queue_cv;
std::queue<int> client_queue;

std::string json_escape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                escaped += '?';
            } else {
                escaped += ch;
            }
        }
    }
    return escaped;
}

std::string error_response(std::string_view message)
{
    return "{\"status\":\"error\",\"message\":\"" + json_escape(message) + "\"}\n";
}

std::string env_or_default(const char* name, std::string fallback)
{
    const char* value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::move(fallback) : value;
}

class JsonObject {
public:
    explicit JsonObject(std::string_view input) : input_(input) {}

    std::optional<std::string> string_value(std::string_view key) const
    {
        const std::string needle = "\"" + std::string(key) + "\"";
        size_t pos = input_.find(needle);
        if (pos == std::string_view::npos) return std::nullopt;
        pos = input_.find(':', pos + needle.size());
        if (pos == std::string_view::npos) return std::nullopt;
        ++pos;
        skip_space(pos);
        if (pos >= input_.size() || input_[pos] != '"') return std::nullopt;
        ++pos;

        std::string result;
        while (pos < input_.size()) {
            const char ch = input_[pos++];
            if (ch == '"') return result;
            if (ch != '\\') {
                result += ch;
                continue;
            }
            if (pos >= input_.size()) return std::nullopt;
            const char escaped = input_[pos++];
            switch (escaped) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<size_t> size_value(std::string_view key) const
    {
        const auto str_val = string_value(key);
        if (str_val.has_value()) return std::nullopt;

        const std::string needle = "\"" + std::string(key) + "\"";
        size_t pos = input_.find(needle);
        if (pos == std::string_view::npos) return std::nullopt;
        pos = input_.find(':', pos + needle.size());
        if (pos == std::string_view::npos) return std::nullopt;
        ++pos;
        skip_space(pos);
        if (pos >= input_.size() || input_[pos] < '0' || input_[pos] > '9') {
            return std::nullopt;
        }

        size_t parsed_val = 0;
        while (pos < input_.size() && input_[pos] >= '0' && input_[pos] <= '9') {
            const size_t digit = static_cast<size_t>(input_[pos++] - '0');
            if (parsed_val > (std::numeric_limits<size_t>::max() - digit) / 10) {
                return std::nullopt;
            }
            parsed_val = parsed_val * 10 + digit;
        }
        return parsed_val;
    }

private:
    void skip_space(size_t& pos) const
    {
        while (pos < input_.size() &&
               (input_[pos] == ' ' || input_[pos] == '\t' ||
                input_[pos] == '\r' || input_[pos] == '\n')) {
            ++pos;
        }
    }

    std::string_view input_;
};

bool read_file(const std::filesystem::path& path, std::vector<uint8_t>& data,
               std::string& error)
{
    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(processing_dir, ec);
    if (ec) {
        error = "processing directory is unavailable";
        return false;
    }
    const auto requested_path = path.is_absolute() ? path : processing_dir / path;
    ec.clear();
    const auto canonical_path = std::filesystem::weakly_canonical(requested_path, ec);
    const auto relative_path = ec ? std::filesystem::path{} :
        std::filesystem::relative(canonical_path, canonical_root, ec);
    if (ec || relative_path.empty() || relative_path == ".." ||
        relative_path.begin()->string() == "..") {
        error = "file_path must be inside TEMP_PROCESSING_DIR";
        return false;
    }

    const auto size = std::filesystem::file_size(canonical_path, ec);
    if (ec || size > kMaxFileBytes) {
        error = "input file is missing or exceeds the size limit";
        return false;
    }
    data.resize(static_cast<size_t>(size));
    std::ifstream input(canonical_path, std::ios::binary);
    if (!input || (!data.empty() &&
                   !input.read(reinterpret_cast<char*>(data.data()),
                               static_cast<std::streamsize>(data.size())))) {
        error = "unable to read input file";
        return false;
    }
    return true;
}

std::string shield_response(const khshield::ContentReport& report)
{
    std::ostringstream out;
    out << "{\"status\":\"success\",\"engine\":\"khshield\",\"result\":{"
        << "\"is_safe\":" << (report.is_safe ? "true" : "false")
        << ",\"risk_score\":" << report.risk_score
        << ",\"violation_type\":\"" << json_escape(report.violation_type) << "\""
        << ",\"text\":{\"flagged\":" << (report.text.flagged ? "true" : "false")
        << ",\"matched_pattern\":\"" << json_escape(report.text.matched_pattern) << "\"}"
        << ",\"visual\":{\"flagged\":" << (report.visual.flagged ? "true" : "false")
        << ",\"skin_percentage\":" << report.visual.skin_percentage << "}"
        << ",\"audio\":{\"flagged\":" << (report.audio.flagged ? "true" : "false")
        << ",\"corrupted\":" << (report.audio.corrupted ? "true" : "false")
        << ",\"bpm\":" << report.audio.bpm << "}}\n";
    return out.str();
}

std::string compression_response(const khcomp::CompressionReport& report)
{
    std::ostringstream out;
    out << "{\"status\":\"success\",\"engine\":\"khcomp\",\"result\":{"
        << "\"is_completed\":" << (report.is_completed ? "true" : "false")
        << ",\"original_size\":" << report.original_size
        << ",\"compressed_size\":" << report.compressed_size
        << ",\"compression_ratio\":" << report.compression_ratio
        << ",\"throughput_mbps\":" << report.throughput_mbps
        << ",\"latency_ms\":" << report.latency_ms << "}}\n";
    return out.str();
}

bool safe_write(int client_fd, std::string_view response)
{
    size_t offset = 0;
    while (offset < response.size()) {
        const ssize_t written = write(client_fd, response.data() + offset,
                                      response.size() - offset);
        if (written > 0) {
            offset += static_cast<size_t>(written);
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            std::cerr << "[KH-Core Daemon] write failed: " << std::strerror(errno)
                      << '\n';
            return false;
        }
    }
    return true;
}

std::string route_request(std::string_view request)
{
    JsonObject json(request);
    const auto action = json.string_value("action");
    if (!action.has_value() || action->empty()) return error_response("missing action");

    std::lock_guard lock(engine_mutex);
    if (*action == "analyze_text") {
        const auto text = json.string_value("text");
        if (!text.has_value()) return error_response("missing text");
        return shield_response(shield_engine.analyze_text(*text));
    }

    const auto file_name = json.string_value("file_path");
    if (!file_name.has_value() || file_name->empty()) {
        return error_response("missing file_path");
    }
    std::vector<uint8_t> input;
    std::string file_error;
    if (!read_file(std::filesystem::path(*file_name), input, file_error)) {
        return error_response(file_error);
    }

    if (*action == "analyze_image") {
        const auto width = json.size_value("width");
        const auto height = json.size_value("height");
        if (!width || !height || *width == 0 || *height == 0 ||
            *width > std::numeric_limits<uint16_t>::max() ||
            *height > std::numeric_limits<uint16_t>::max() ||
            input.size() < *width * *height) {
            return error_response("invalid image dimensions or file size");
        }
        khshield::visual::ImageBuffer image{
            std::span<const uint8_t>(input.data(), *width * *height),
            *width, *height, 1};
        return shield_response(shield_engine.analyze_image_buffer(image));
    }

    if (*action == "analyze_audio") {
        const auto sample_rate = json.size_value("sample_rate");
        if (!sample_rate || *sample_rate == 0 || input.size() % sizeof(int16_t) != 0) {
            return error_response("invalid audio sample rate or PCM16 file");
        }
        std::vector<float> samples(input.size() / sizeof(int16_t));
        for (size_t i = 0; i < samples.size(); ++i) {
            const uint16_t raw = static_cast<uint16_t>(input[2 * i]) |
                                 (static_cast<uint16_t>(input[2 * i + 1]) << 8);
            samples[i] = static_cast<float>(static_cast<int16_t>(raw)) / 32768.0f;
        }
        return shield_response(shield_engine.analyze_audio_buffer(samples, *sample_rate));
    }

    if (*action == "compress_image") {
        const auto width = json.size_value("width");
        const auto height = json.size_value("height");
        const auto quality = json.size_value("quality_factor").value_or(50);
        if (!width || !height || *width == 0 || *height == 0 ||
            *width % khcomp::image::kBlockSize != 0 ||
            *height % khcomp::image::kBlockSize != 0 ||
            *width > std::numeric_limits<uint16_t>::max() ||
            *height > std::numeric_limits<uint16_t>::max() ||
            quality == 0 || quality > 100 ||
            input.size() < *width * *height) {
            return error_response("invalid image compression parameters");
        }
        std::vector<uint8_t> compressed(input.size() + input.size() / 2 + 1024);
        khcomp::core::BitStreamWriter writer(compressed);
        khcomp::image::ImageHeader header{
            static_cast<uint16_t>(*width), static_cast<uint16_t>(*height),
            khcomp::image::PixelFormat::Grayscale,
            static_cast<uint8_t>(quality)};
        const auto result = image_engine.encode_grayscale_frame(
            header, std::span<const uint8_t>(input.data(), *width * *height), writer);
        if (!result) return error_response(khcomp::error_to_string(result.error()));
        khcomp::CompressionReport report;
        report.is_completed = true;
        report.original_size = *width * *height;
        report.compressed_size = *result;
        report.compression_ratio = report.original_size == 0
            ? 0.0 : static_cast<double>(report.compressed_size) / report.original_size;
        return compression_response(report);
    }

    if (*action == "compress_audio") {
        const auto sample_rate = json.size_value("sample_rate");
        const auto channels = json.size_value("channels");
        if (!sample_rate || !channels || *sample_rate == 0 || *channels == 0 ||
            *channels > khcomp::audio::kMaxAudioChannels ||
            input.size() % (sizeof(int16_t) * *channels) != 0 ||
            input.size() / (sizeof(int16_t) * *channels) >
                std::numeric_limits<uint32_t>::max()) {
            return error_response("invalid audio compression parameters");
        }
        std::vector<uint8_t> compressed(input.size() + input.size() / 2 + 1024);
        khcomp::core::BitStreamWriter writer(compressed);
        khcomp::audio::AudioHeader header{
            static_cast<uint32_t>(*sample_rate), static_cast<uint8_t>(*channels),
            static_cast<uint32_t>(input.size() / (sizeof(int16_t) * *channels))};
        const auto result = audio_engine.encode_pcm16(header, input, writer);
        if (!result) return error_response(khcomp::error_to_string(result.error()));
        khcomp::CompressionReport report;
        report.is_completed = true;
        report.original_size = input.size();
        report.compressed_size = *result;
        report.compression_ratio = report.original_size == 0
            ? 0.0 : static_cast<double>(report.compressed_size) / report.original_size;
        return compression_response(report);
    }

    return error_response("unknown action: " + *action);
}

void handle_client(int client_fd)
{
    std::string request;
    request.reserve(kMaxRequestBytes);
    std::array<char, 4096> buffer{};
    bool request_complete = false;
    while (request.size() < kMaxRequestBytes) {
        const ssize_t count = read(client_fd, buffer.data(), buffer.size());
        if (count > 0) {
            request.append(buffer.data(), static_cast<size_t>(count));
            if (request.find('}') != std::string::npos) {
                request_complete = true;
                break;
            }
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            if (count < 0) std::cerr << "[KH-Core Daemon] read failed: "
                                      << std::strerror(errno) << '\n';
            break;
        }
    }
    const std::string response = request.empty()
        ? error_response("empty request")
        : (!request_complete && request.size() >= kMaxRequestBytes
            ? error_response("request too large") : route_request(request));
    static_cast<void>(safe_write(client_fd, response));
    close(client_fd);
}

void worker_loop()
{
    for (;;) {
        int client_fd = -1;
        {
            std::unique_lock lock(queue_mutex);
            queue_cv.wait(lock, [] {
                return shutting_down.load() || !client_queue.empty();
            });
            if (client_queue.empty() && shutting_down.load()) return;
            client_fd = client_queue.front();
            client_queue.pop();
        }
        handle_client(client_fd);
    }
}

void handle_signal(int)
{
    shutting_down.store(true);
    if (server_fd != -1) close(server_fd);
    queue_cv.notify_all();
}

} // namespace

int main()
{
    socket_path = env_or_default("UNIX_SOCKET_PATH", socket_path);
    model_path = env_or_default("MODELS_PATH", model_path.string());
    processing_dir = env_or_default("TEMP_PROCESSING_DIR", processing_dir.string());

    std::error_code ec;
    std::filesystem::create_directories(processing_dir, ec);
    if (ec) {
        std::cerr << "Failed to create processing directory: " << ec.message() << '\n';
        return 1;
    }
    if (std::filesystem::exists(model_path, ec) && !shield_engine.load_model(model_path)) {
        std::cerr << "Failed to load model: " << model_path << '\n';
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    unlink(socket_path.c_str());

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create UNIX socket: " << std::strerror(errno) << '\n';
        return 1;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(address.sun_path)) {
        std::cerr << "UNIX_SOCKET_PATH is too long\n";
        close(server_fd);
        return 1;
    }
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (bind(server_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(server_fd, static_cast<int>(kMaxWorkers)) < 0) {
        std::cerr << "Failed to bind/listen on UNIX socket: " << std::strerror(errno) << '\n';
        close(server_fd);
        unlink(socket_path.c_str());
        return 1;
    }

    std::vector<std::thread> workers;
    workers.reserve(kMaxWorkers);
    for (size_t i = 0; i < kMaxWorkers; ++i) workers.emplace_back(worker_loop);

    while (!shutting_down.load()) {
        const int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (!shutting_down.load() && errno != EINTR) {
                std::cerr << "accept failed: " << std::strerror(errno) << '\n';
            }
            continue;
        }

        bool queued = false;
        {
            std::lock_guard lock(queue_mutex);
            if (!shutting_down.load() && client_queue.size() < kMaxWorkers) {
                client_queue.push(client_fd);
                queued = true;
            }
        }
        if (queued) {
            queue_cv.notify_one();
        } else {
            static_cast<void>(safe_write(client_fd, error_response("server busy")));
            close(client_fd);
        }
    }

    shutting_down.store(true);
    queue_cv.notify_all();
    for (auto& worker : workers) worker.join();
    unlink(socket_path.c_str());
    return 0;
}
```

## Integration checklist

- Review the request contract for raw grayscale images and PCM16 audio.
- Confirm clients provide `width`, `height`, `sample_rate`, `channels`, and
  `quality_factor` where applicable.
- Compile the draft after copying it into `main_daemon.cpp`; this draft itself is
  deliberately not integrated.
- Add protocol-level tests for queue saturation, traversal attempts, malformed
  JSON, partial writes, and model-load failure.
