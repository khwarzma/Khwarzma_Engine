#!/usr/bin/env bash
set -euo pipefail

APP_DIR="/home/kh-core/app"
BUILD_DIR="${APP_DIR}/build"
MODEL_DIR="${APP_DIR}/models"
CONFIG_DIR="${APP_DIR}/config"
PROCESSING_DIR="/tmp/kh_processing"
MODEL_PATH="${MODEL_DIR}/ar_v1.khm"
PROFANITY_LIST_PATH="${CONFIG_DIR}/profanity_words.json"

mkdir -p "${PROCESSING_DIR}" "${MODEL_DIR}" "${CONFIG_DIR}" "${BUILD_DIR}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake is required but was not found" >&2
    exit 1
fi

if ! command -v g++-12 >/dev/null 2>&1; then
    echo "error: g++-12 is required but was not found" >&2
    exit 1
fi

if [[ ! -f "${MODEL_PATH}" ]]; then
    echo "warning: SAM model not found at ${MODEL_PATH}" >&2
    echo "Place ar_v1.khm at that path before running model-backed analysis." >&2
fi

find "${BUILD_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +

cd "${BUILD_DIR}"
cmake -DCMAKE_CXX_COMPILER=g++-12 ..
make -j"$(nproc)"

echo "Build complete: ${BUILD_DIR}/kh_core_daemon"
