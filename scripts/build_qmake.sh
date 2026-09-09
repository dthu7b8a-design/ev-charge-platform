#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="${PROJECT_ROOT}/build-qmake"

mkdir -p "$BUILD_ROOT"
cd "$BUILD_ROOT"
qmake6 "${PROJECT_ROOT}/EVChargePlatform.pro" CONFIG+=debug
make -j"$(nproc)"

echo "qmake 构建完成：${BUILD_ROOT}/bin"
