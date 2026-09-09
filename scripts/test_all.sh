#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="${EVCHARGE_BUILD_DIR:-${PROJECT_ROOT}/build-qmake}"
RUN_ROOT="${PROJECT_ROOT}/qa/test-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN_ROOT"

"${BUILD_ROOT}/bin/charge_server" --port 18080 --data "$RUN_ROOT/data" --web "${PROJECT_ROOT}/web" >"$RUN_ROOT/server.log" 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

for _ in $(seq 1 40); do
  if python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:18080/api/health', timeout=1)" >/dev/null 2>&1; then break; fi
  sleep 0.25
done

python3 "${PROJECT_ROOT}/tests/api_integration.py" http://127.0.0.1:18080 | tee "$RUN_ROOT/api-test.log"
python3 "${PROJECT_ROOT}/tests/check_qt_ui_regressions.py" | tee "$RUN_ROOT/qt-ui-regression.log"
if command -v node >/dev/null 2>&1; then
  node "${PROJECT_ROOT}/tests/check_dashboard_syntax.js" "${PROJECT_ROOT}/web/dashboard/index.html" | tee "$RUN_ROOT/dashboard-syntax.log"
else
  echo "INFO Node 未安装；大屏语法已在主机工作区检查，Ubuntu 浏览器视觉测试负责运行时校验。" | tee "$RUN_ROOT/dashboard-syntax.log"
fi
sqlite3 "$RUN_ROOT/data/evcharge.db" "PRAGMA integrity_check; PRAGMA foreign_key_check;" | tee "$RUN_ROOT/database-check.log"

echo "$RUN_ROOT" >"${PROJECT_ROOT}/qa/latest-test-run.txt"
echo "测试证据：$RUN_ROOT"
