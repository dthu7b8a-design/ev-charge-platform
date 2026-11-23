#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="${EVCHARGE_BUILD_DIR:-${PROJECT_ROOT}/build-qmake}"
for executable in charge_server charge_user charge_admin; do
  if [ ! -x "${BUILD_ROOT}/bin/${executable}" ]; then
    echo "缺少可执行文件：${BUILD_ROOT}/bin/${executable}，请先构建项目。" >&2
    exit 1
  fi
done
mkdir -p "${PROJECT_ROOT}/data" "${PROJECT_ROOT}/logs"

SERVER_PID=""
if ! pgrep -f "${BUILD_ROOT}/bin/charge_server" >/dev/null; then
  nohup "${BUILD_ROOT}/bin/charge_server" --data "${PROJECT_ROOT}/data" --web "${PROJECT_ROOT}/web" >"${PROJECT_ROOT}/logs/server.log" 2>&1 &
  SERVER_PID=$!
fi

READY=0
for _ in $(seq 1 40); do
  if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "服务端启动失败，请检查 ${PROJECT_ROOT}/logs/server.log。" >&2
    exit 1
  fi
  if { [ -z "$SERVER_PID" ] || grep -q '^charge_server 已启动：' "${PROJECT_ROOT}/logs/server.log" 2>/dev/null; } &&
     python3 -c "import json, urllib.request; r = json.load(urllib.request.urlopen('http://127.0.0.1:8080/api/health', timeout=1)); assert r['ok'] and r['data']['service'] == 'charge_server'" >/dev/null 2>&1; then
    READY=1
    break
  fi
  sleep 0.25
done
if [ "$READY" != 1 ]; then
  [ -z "$SERVER_PID" ] || kill "$SERVER_PID" 2>/dev/null || true
  echo "服务端未就绪，请检查 ${PROJECT_ROOT}/logs/server.log 和 8080 端口。" >&2
  exit 1
fi

"${BUILD_ROOT}/bin/charge_user" &
"${BUILD_ROOT}/bin/charge_admin" &

echo "Qt 用户端与管理端已启动。"
echo "手机/PWA：http://$(hostname -I | awk '{print $1}'):8080"
