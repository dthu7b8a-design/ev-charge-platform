#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 充电无忧平台 — Ubuntu 环境一键安装（带包名校验，防止装到错误/不存在的包）
#
# 用法：
#   bash setup_env.sh              # 安装必需依赖
#   bash setup_env.sh --with-node  # 额外安装 nodejs（手机 PWA 脚本语法检查用，可选）
#
# 设计原则：
#   1. 装之前先用 apt-cache 逐个校验包名，绝不把不存在的包名丢给 apt install
#   2. 对历史上改过名的包提供候选回退（如 charts：libqt6charts6-dev / qt6-charts-dev）
#   3. 装完逐项验证，明确指出哪一项没到位、会导致什么症状
# ---------------------------------------------------------------------------
set -uo pipefail

WITH_NODE=0
[ "${1:-}" = "--with-node" ] && WITH_NODE=1

RED='\033[0;31m'; GRN='\033[0;32m'; YEL='\033[1;33m'; DIM='\033[2m'; NC='\033[0m'
ok()   { printf "${GRN}  [OK]${NC}   %s\n" "$1"; }
warn() { printf "${YEL}  [警告]${NC} %s\n" "$1"; }
err()  { printf "${RED}  [失败]${NC} %s\n" "$1"; }
info() { printf "${DIM}  [信息]${NC} %s\n" "$1"; }
line() { printf '\n===== %s =====\n' "$1"; }

# ---------------------------------------------------------------------------
line "0. 环境检查"
# ---------------------------------------------------------------------------
command -v apt-get >/dev/null 2>&1 || { err "未找到 apt-get，本脚本仅适用于 Debian/Ubuntu"; exit 1; }

DISTRO=$(. /etc/os-release && echo "${ID:-unknown}")
CODENAME=$(. /etc/os-release && echo "${VERSION_CODENAME:-unknown}")
info "发行版：$DISTRO  代号：$CODENAME"

if [ "$DISTRO" != "ubuntu" ] && [ "$DISTRO" != "debian" ]; then
  warn "非 Debian/Ubuntu 系，包名可能不同，请人工核对后再继续"
fi
if [ "$CODENAME" != "jammy" ]; then
  warn "当前不是 Ubuntu 22.04 (jammy)，包名与 Qt 版本可能不同"
  warn "本清单针对 jammy / Qt 6.2.4 核对，其他版本请核对 packages.ubuntu.com"
fi

# ---------------------------------------------------------------------------
line "1. 检查 universe 仓库（本项目 Qt6 包均位于 universe）"
# ---------------------------------------------------------------------------
if grep -RqsE '^[^#]*\buniverse\b' /etc/apt/sources.list /etc/apt/sources.list.d/ 2>/dev/null; then
  ok "universe 仓库已启用"
else
  warn "未检测到 universe 仓库，尝试启用"
  if command -v add-apt-repository >/dev/null 2>&1; then
    sudo add-apt-repository -y universe
  else
    sudo apt-get install -y software-properties-common && sudo add-apt-repository -y universe
  fi
fi

# ---------------------------------------------------------------------------
line "2. 更新软件索引"
# ---------------------------------------------------------------------------
if sudo apt-get update; then
  ok "软件索引已更新"
else
  err "apt-get update 失败"
  info "常见原因：DNS 解析失败或网络不通。可尝试："
  info "  echo 'nameserver 223.5.5.5' | sudo tee /etc/resolv.conf"
  info "  echo 'nameserver 114.114.114.114' | sudo tee -a /etc/resolv.conf"
  info "若完全离线，请在有网的机器下载 deb 后用 dpkg -i 安装"
  exit 1
fi

# ---------------------------------------------------------------------------
# 包名校验工具
# ---------------------------------------------------------------------------
pkg_available() { apt-cache show "$1" >/dev/null 2>&1; }

# 从候选列表中解析出第一个真实存在的包名
# 用法：resolve_pkg <输出变量名> <候选1> [候选2 ...]
resolve_pkg() {
  local __out="$1"; shift
  local cand
  for cand in "$@"; do
    if pkg_available "$cand"; then
      info "候选 '$cand' 存在 ✓"
      printf -v "$__out" '%s' "$cand"
      return 0
    fi
    info "候选 '$cand' 不存在，尝试下一个"
  done
  printf -v "$__out" '%s' "$1"
  return 1
}

FINAL_PKGS=()
FAILED_RESOLVE=()

add_pkg() {           # add_pkg <候选1> [候选2 ...]
  local resolved
  if resolve_pkg resolved "$@"; then
    FINAL_PKGS+=("$resolved")
  else
    FAILED_RESOLVE+=("$1")
    err "所有候选包名均不存在：$*"
  fi
}

# ---------------------------------------------------------------------------
line "3. 校验包名是否存在（避免 apt install 中途失败）"
# ---------------------------------------------------------------------------
add_pkg build-essential
add_pkg qmake6
add_pkg qt6-base-dev
add_pkg libqt6charts6-dev qt6-charts-dev      # 易错：jammy 上正确名是 libqt6charts6-dev
add_pkg qt6-webengine-dev
add_pkg libqt6webenginecore6-bin              # WebEngine 渲染进程，dev 包不自动带
add_pkg libqt6sql6-sqlite                     # Qt6 SQLite 驱动，独立于 qt6-base-dev
add_pkg sqlite3                               # 易错：不是 sqlite
add_pkg python3
[ "$WITH_NODE" = "1" ] && add_pkg nodejs

if [ "${#FAILED_RESOLVE[@]}" -gt 0 ]; then
  err "以下包名在软件源中不存在，已跳过（请核对发行版与仓库配置）："
  printf '    - %s\n' "${FAILED_RESOLVE[@]}"
fi

line "4. 待安装清单"
printf '  %s\n' "${FINAL_PKGS[@]}"

# ---------------------------------------------------------------------------
line "5. 开始安装"
# ---------------------------------------------------------------------------
# shellcheck disable=SC2068
if sudo apt-get install -y "${FINAL_PKGS[@]}"; then
  ok "安装完成"
else
  err "安装过程中出现错误，请根据上方 apt 输出处理"
fi

# ---------------------------------------------------------------------------
line "6. 安装后验证"
# ---------------------------------------------------------------------------
MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo x86_64-linux-gnu)
QT_LIBEXECS=$(qmake6 -query QT_INSTALL_LIBEXECS 2>/dev/null || echo /usr/lib/qt6/libexec)
FAILED=0

check() {   # check <说明> <命令...>
  local desc="$1"; shift
  if "$@" >/dev/null 2>&1; then ok "$desc"; else err "$desc"; FAILED=$((FAILED+1)); fi
}

command -v qmake6 >/dev/null 2>&1 && info "qmake6: $(qmake6 --version | tail -1)" \
                                  || { err "qmake6 未安装"; FAILED=$((FAILED+1)); }
command -v g++    >/dev/null 2>&1 && ok "g++ $(g++ --version | head -1 | awk '{print $NF}')" \
                                  || { err "g++ 缺失"; FAILED=$((FAILED+1)); }

check "WebEngine 渲染进程 $QT_LIBEXECS/QtWebEngineProcess" \
      test -x "$QT_LIBEXECS/QtWebEngineProcess"

check "Qt6 SQLite 驱动 /usr/lib/$MULTIARCH/qt6/plugins/sqldrivers/libqsqlite.so" \
      test -f "/usr/lib/$MULTIARCH/qt6/plugins/sqldrivers/libqsqlite.so"

check "Qt6 Charts 运行库"        test -f "/usr/lib/$MULTIARCH/libQt6Charts.so.6"
check "Qt6 WebEngineWidgets 运行库" test -f "/usr/lib/$MULTIARCH/libQt6WebEngineWidgets.so.6"

command -v sqlite3 >/dev/null 2>&1 && ok "sqlite3 $(sqlite3 --version | awk '{print $1}')" \
                                    || { err "sqlite3 缺失（注意不是 sqlite）"; FAILED=$((FAILED+1)); }
command -v python3 >/dev/null 2>&1 && ok "python3 $(python3 --version | awk '{print $2}')" \
                                    || { err "python3 缺失"; FAILED=$((FAILED+1)); }
if [ "$WITH_NODE" = "1" ]; then
  command -v node >/dev/null 2>&1 && ok "node $(node --version)" || warn "node 缺失（不影响主流程）"
fi

# ---------------------------------------------------------------------------
line "结果"
# ---------------------------------------------------------------------------
if [ "$FAILED" -eq 0 ]; then
  printf "${GRN}全部检查通过，可以开始构建：${NC}\n"
  printf '  cd <项目根目录> && ./scripts/build_qmake.sh && ./scripts/run_demo.sh\n'
else
  printf "${RED}有 %s 项未通过${NC}，对照表：\n" "$FAILED"
  printf '  - WebEngine 渲染进程缺失 -> 用户端弹不出来，报 Could not find QtWebEngineProcess\n'
  printf '  - SQLite 驱动缺失        -> 服务端报 QSQLITE driver not loaded\n'
  printf '  - Charts 运行库缺失      -> 管理端图表空白\n'
fi

printf "\n${DIM}虚拟机提示：若用户端能启动但地图区域白屏，在 scripts/run_demo.sh 中\n"
printf "启动程序之前加入：\n"
printf "  export QTWEBENGINE_CHROMIUM_FLAGS=\"--no-sandbox --disable-gpu \
--disable-software-rasterizer --use-gl=swiftshader\"\n"
printf "  export LIBGL_ALWAYS_SOFTWARE=1${NC}\n"
