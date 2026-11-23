# 环境说明

目标为Ubuntu22.04 LTS（jammy）x86_64、Qt6.2.4、GCC11、qmake6。桌面程序需要图形会话；API测试可无图形运行。本轮不承诺Windows原生Qt工具链兼容。

## 安装

```bash
bash setup_env.sh
# 可选Node.js，仅用于JS语法检查
bash setup_env.sh --with-node
```

手工等价依赖：

```bash
sudo add-apt-repository universe
sudo apt update
sudo apt install -y \
  build-essential libgl-dev fonts-noto-cjk qmake6 qt6-base-dev \
  libqt6charts6-dev qt6-webengine-dev libqt6webenginecore6-bin \
  libqt6sql6-sqlite sqlite3 python3
```

libgl-dev提供QtCharts包含的GL/gl.h；中文字体保证最小环境中的中文可读。安装脚本检查包解析和实际工具，失败应非零退出。实际版本用 `qmake6 -query QT_VERSION` 与 `g++ --version` 记录。

## 构建与路径

```bash
bash scripts/build_qmake.sh
EVCHARGE_BUILD_DIR="$HOME/evcharge-build" bash scripts/build_qmake.sh
```

run/stop/test传入同一EVCHARGE_BUILD_DIR。默认build-qmake/bin中为charge_server、charge_user、charge_admin，shared为静态库。

data、logs、qa和构建文件不纳入Git；忽略规则不是备份，个人SQLite应单独保护。完整流程见 [复现说明](docs/REPRODUCIBILITY.md)，实测见 [验证记录](docs/VALIDATION.md)。
