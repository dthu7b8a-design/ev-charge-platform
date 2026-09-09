#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="${PROJECT_ROOT}/build-qmake"
mkdir -p "${PROJECT_ROOT}/data" "${PROJECT_ROOT}/logs"

if ! pgrep -f "${BUILD_ROOT}/bin/charge_server" >/dev/null; then
  nohup "${BUILD_ROOT}/bin/charge_server" --data "${PROJECT_ROOT}/data" --web "${PROJECT_ROOT}/web" >"${PROJECT_ROOT}/logs/server.log" 2>&1 &
  sleep 1
fi

"${BUILD_ROOT}/bin/charge_user" &
"${BUILD_ROOT}/bin/charge_admin" &

echo "Qt 用户端与管理端已启动。"
echo "手机/PWA：http://$(hostname -I | awk '{print $1}'):8080"
echo "运营大屏：http://$(hostname -I | awk '{print $1}'):8080/dashboard"
