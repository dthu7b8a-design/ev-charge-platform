#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
pkill -f "${PROJECT_ROOT}/build-qmake/bin/charge_server" || true
pkill -f "${PROJECT_ROOT}/build-qmake/bin/charge_user" || true
pkill -f "${PROJECT_ROOT}/build-qmake/bin/charge_admin" || true
echo "本项目的演示进程已停止。"
