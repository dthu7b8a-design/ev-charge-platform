#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_ROOT="${EVCHARGE_BUILD_DIR:-${PROJECT_ROOT}/build-qmake}"
QA_ROOT="${EVCHARGE_QA_DIR:-${PROJECT_ROOT}/qa}"
RUN_ROOT="${QA_ROOT}/test-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN_ROOT"

"${BUILD_ROOT}/bin/charge_server" --port 18080 --data "$RUN_ROOT/data" --web "${PROJECT_ROOT}/web" >"$RUN_ROOT/server.log" 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

READY=0
for _ in $(seq 1 40); do
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    cat "$RUN_ROOT/server.log" >&2
    exit 1
  fi
  if grep -q '^charge_server 已启动：' "$RUN_ROOT/server.log" 2>/dev/null &&
     python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:18080/api/health', timeout=1)" >/dev/null 2>&1; then
    READY=1
    break
  fi
  sleep 0.25
done
if [ "$READY" != 1 ]; then
  cat "$RUN_ROOT/server.log" >&2
  echo "测试服务未就绪，未执行写入测试。" >&2
  exit 1
fi

python3 "${PROJECT_ROOT}/tests/api_integration.py" http://127.0.0.1:18080 | tee "$RUN_ROOT/api-test.log"
python3 "${PROJECT_ROOT}/tests/check_qt_ui_regressions.py" | tee "$RUN_ROOT/qt-ui-regression.log"
INTEGRITY=$(sqlite3 "$RUN_ROOT/data/evcharge.db" "PRAGMA integrity_check;")
FOREIGN_KEYS=$(sqlite3 "$RUN_ROOT/data/evcharge.db" "PRAGMA foreign_key_check;")
printf '%s\n' "$INTEGRITY" | tee "$RUN_ROOT/database-check.log"
if [ "$INTEGRITY" != "ok" ] || [ -n "$FOREIGN_KEYS" ]; then
  printf '%s\n' "$FOREIGN_KEYS" | tee -a "$RUN_ROOT/database-check.log" >&2
  echo "数据库一致性检查失败。" >&2
  exit 1
fi

echo "$RUN_ROOT" >"${QA_ROOT}/latest-test-run.txt"
echo "测试证据：$RUN_ROOT"
