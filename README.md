# khwarzma Core Engine

**Engineered and maintained by khwarzma (شركة خوارزمة)**

## 1. Executive Summary & Brand Identity

khwarzma Core Engine is a native C++23 foundation for two performance-sensitive backend capabilities:

- **KhShield** — low-latency, multimodal content moderation for text, images, and PCM audio.
- **KhComp** — caller-buffered media compression primitives designed for predictable memory behavior and edge deployment.

khwarzma is open-sourcing this core engine to empower developers, make security-relevant behavior inspectable, and demonstrate the scalable architecture used across the khwarzma ecosystem.

## 2. Non-Technical Overview

Online communities need moderation decisions before harmful content reaches users. KhShield examines text, image buffers, and audio samples directly in memory and returns a structured safety report. Its text pipeline supports Arabic, Arabizi normalization, and English dictionary categories; its visual pipeline combines lightweight skin-pixel analysis with text extraction; and its audio pipeline evaluates signal integrity, spectral characteristics, and tempo.

The design favors fast deterministic decisions for common violations, with an optional semantic tier for cases that do not match an exact rule. Rules can be loaded from JSON or added at runtime, making profanity protection updateable without rebuilding the daemon.

KhComp complements moderation workloads by reducing the cost of moving media through constrained infrastructure. It uses caller-provided buffers, fixed-size working storage in its core context model, cache-line alignment checks, and arithmetic coding to keep allocations and failure modes explicit.

## 3. Deep Technical Architecture

### 3.1 KhShield moderation engine

`khshield::ShieldEngine` exposes direct in-process APIs:

```cpp
khshield::ShieldEngine engine(khshield::Preset::STRICT);

engine.load_profanity_dictionary("config/profanity_words.json");
const auto text_report = engine.analyze_text("text to inspect");
const auto image_report = engine.analyze_image_buffer(image);
const auto audio_report = engine.analyze_audio_buffer(pcm_samples, 44100);
```

Every operation returns a `ContentReport` with:

- `is_safe` and a normalized `risk_score` in the range `0.0`–`1.0`.
- A `violation_type` such as `PROFANITY_EXACT`, `PROFANITY_SEMANTIC`, `OCR_PROFANITY`, `EXPLICIT_BODY`, `CORRUPTED_AUDIO`, or `MUSIC_DETECTED`.
- Text, visual, and audio sub-reports containing the matched pattern, skin ratio, corruption state, BPM, and related details.

The `STRICT` and `MODERATE` presets alter visual and audio thresholds at runtime.

#### Text: deterministic rules plus semantic inference

1. **Arabizi normalization** canonicalizes mixed Latinized Arabic and UTF-8 input.
2. **Aho–Corasick matching** searches the normalized stream and, when necessary, individual tokens. The automaton supports failure links, output links, shared-read locking, and dynamic dictionary loading.
3. **SAM inference** is an optional statistical language-model tier. A `.khm` model is loaded from a compact binary format with a `KHM1` header, quantized vocabulary weights, and a configurable threshold. Matched token weights are pooled and evaluated with a sigmoid activation.

The bundled `config/profanity_words.json` organizes Arabic dialect and English terms into language and severity categories, including `EGY`, `SAU`, `GULF`, `MSA`, `ENG_SEVERE`, `ENG_EXPLICIT`, `ENG_SLURS`, `ENG_HATE`, and `ENG_COMMON`.

#### Visual and audio analysis

The visual path evaluates raw image buffers using YCbCr and HSV skin-color criteria. The repository also includes binary skin-mask generation, connected-component geometry analysis, and an OCR bridge that normalizes extracted text and checks it against the same Aho–Corasick matcher.

The audio path accepts floating-point PCM samples. It calculates RMS energy and spectral centroid through a DFT, detects corrupted input, and estimates BPM from onset envelopes and autocorrelation. Strict mode flags detected music; moderate mode applies a higher-energy condition.

### 3.2 KhComp compression engine

KhComp is organized around `std::span`-based APIs and explicit `std::expected` errors:

- **Bit streams:** `BitStreamWriter` and `BitStreamReader` write, peek, advance, and flush arbitrary-width values while checking capacity and 64-byte buffer alignment.
- **Adaptive arithmetic coding:** `ArithmeticEncoder` and `ArithmeticDecoder` use 32-bit range coding and update a context model after each symbol.
- **Context modeling:** `ContextModel` keeps a bounded order-1 through order-4 history and a preallocated 256-context × 256-symbol frequency table. Frequency rescaling prevents overflow without allocating during coding.
- **Image compression:** `ImageFramePipeline` processes grayscale frames in 8×8 blocks using a DCT-II, JPEG-style luminance quantization, zig-zag ordering, delta-coded DC coefficients, run-length AC encoding, and arithmetic coding. Width and height must be multiples of 8.
- **Audio compression:** `AudioCodecEngine` encodes interleaved signed PCM16 using per-channel first-order temporal prediction followed by arithmetic coding. The supported channel count is one or two.
- **Video primitives:** `VideoRingBuffer`, `MotionEstimator`, and `VideoCodecEngine` provide caller-backed reference storage, zero-allocation SAD motion estimation, I/P frame types, motion vectors, and DCT residual support.

KhComp’s zero-allocation objective applies to its core coding workspace: working arrays and model state are fixed or caller-owned, while the caller supplies output storage. APIs report `InvalidInput`, `AlignmentError`, `Overflow`, and other failures instead of silently growing buffers.

### 3.3 Daemon and IPC contract

`kh_core_daemon` exposes the engines over a Unix domain stream socket. The daemon starts a bounded worker pool, accepts newline-independent JSON objects, routes the `action` field, and returns one JSON response per connection. Requests are limited to 64 KiB; file-backed operations are limited to 128 MiB and are restricted to `TEMP_PROCESSING_DIR`.

### Text request

```json
{"action":"analyze_text","text":"text to inspect"}
```

Example response shape:

```json
{
  "status": "success",
  "engine": "khshield",
  "result": {
    "is_safe": false,
    "risk_score": 1,
    "violation_type": "PROFANITY_EXACT",
    "text": {"flagged": true, "matched_pattern": "matched rule"},
    "visual": {"flagged": false, "skin_percentage": 0},
    "audio": {"flagged": false, "corrupted": false, "bpm": 0}
  }
}
```

Supported actions are:

| Action | Required fields | Input |
|---|---|---|
| `analyze_text` | `text` | Inline text |
| `analyze_image` | `file_path`, `width`, `height` | Grayscale bytes in the processing directory |
| `analyze_audio` | `file_path`, `sample_rate` | Little-endian PCM16 bytes |
| `compress_image` | `file_path`, `width`, `height`, optional `quality_factor` | Grayscale frame; dimensions must be multiples of 8 |
| `compress_audio` | `file_path`, `sample_rate`, `channels` | Interleaved PCM16 bytes; one or two channels |

Responses use `{"status":"error","message":"..."}` for invalid actions, malformed fields, missing files, capacity violations, and server-busy conditions.

Applications can integrate through any Unix-socket client, including a Python bridge layer. The current repository contains the native daemon and protocol implementation; no `kh_bridge.py` file is included in this checkout.

### 3.4 Configuration

The daemon reads environment variables at startup and falls back to development defaults:

| Variable | Purpose | Default |
|---|---|---|
| `UNIX_SOCKET_PATH` | Unix domain socket path | `/tmp/kh_core.sock` |
| `MODELS_PATH` | Optional SAM `.khm` model path | `/home/kh-core/app/models/ar_v1.khm` |
| `TEMP_PROCESSING_DIR` | Root for file-backed requests | `/tmp/kh_processing/` |
| `PROFANITY_LIST_PATH` | JSON profanity dictionary | `/home/kh-core/app/config/profanity_words.json` |
| `MAX_WORKERS` | Deployment setting reserved for worker sizing | `2` |

The daemon currently creates a two-worker pool. `MAX_WORKERS` is read and retained as configuration, but the compiled worker and listen limits remain `2`.

### 3.5 Build and run

### Requirements

- Linux with Unix domain socket support
- CMake 3.20 or newer
- GCC 12 or newer
- C++23 standard library support

Build with CMake:

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=g++-12
cmake --build build --parallel
```

The resulting executable is `build/kh_core_daemon`. The repository also provides:

```bash
./scripts/build_daemon.sh
```

That helper prepares `build/`, `models/`, `config/`, and `/tmp/kh_processing`, checks for CMake and `g++-12`, and builds the daemon. A SAM model is optional for exact dictionary moderation; place `ar_v1.khm` under `models/` to enable semantic evaluation.

Start the daemon with an explicit deployment configuration:

```bash
UNIX_SOCKET_PATH=/tmp/kh_core.sock \
TEMP_PROCESSING_DIR=/tmp/kh_processing \
PROFANITY_LIST_PATH="$PWD/config/profanity_words.json" \
MODELS_PATH="$PWD/models/ar_v1.khm" \
./build/kh_core_daemon
```

For a direct native smoke test, compile `tests/test_harness.cpp` against the generated static libraries. The harness exercises a KhShield text request and the KhComp arithmetic encoder; it is currently a standalone source file rather than a CMake target.

### 3.6 Repository layout

```text
include/khshield/     Moderation API, text, visual, and audio interfaces
src/shield_core/      KhShield implementations
include/khcomp/       Compression API and fixed-layout primitives
src/comp_core/        KhComp implementations
src/server_daemon/    Unix domain socket daemon and JSON routing
config/               Runtime profanity dictionary
tests/                Native smoke/benchmark harness
scripts/              Build helper scripts
```

### 3.7 Operational boundaries

KhShield’s visual and audio analyzers are lightweight heuristic components, not a replacement for a trained safety classifier or full OCR/media decoding stack. The daemon expects raw grayscale image bytes and raw PCM16 audio for file-backed actions; image codecs and container demultiplexing are outside this repository. Integrators should validate thresholds against their domain, protect the Unix socket with appropriate filesystem permissions, and treat moderation output as one layer in a broader safety system.

### 3.8 License and contributions

This repository is the khwarzma Core Engine source distribution. Add project-specific licensing and contribution terms before publishing a release under the organization’s public open-source policy.
