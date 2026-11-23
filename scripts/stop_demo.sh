#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="${EVCHARGE_BUILD_DIR:-${PROJECT_ROOT}/build-qmake}"
pkill -f "${BUILD_ROOT}/bin/charge_server" || true
pkill -f "${BUILD_ROOT}/bin/charge_user" || true
pkill -f "${BUILD_ROOT}/bin/charge_admin" || true
echo "本项目的演示进程已停止。"
