环境准备

适用范围：Ubuntu 22.04 LTS（jammy）、Qt 6.2.4、GCC 11、x86_64。
包名说明：下表所有包名均已在 \[packages.ubuntu.com (jammy)](https://packages.ubuntu.com/jammy/) 逐条核对，请勿凭 Qt 官方文档或其他发行版的习惯改写。

一键安装

推荐用项目根目录随附的 `setup\_env.sh`：它会在安装**之前**逐个用 `apt-cache` 校验包名，把不存在的包名提前拦下来，并在装完后逐项验证（而不是等 `apt install` 中途报错，或者等运行时才崩）。

bash
chmod +x setup\_env.sh
bash setup\_env.sh                # 安装必需依赖
bash setup\_env.sh --with-node    # 额外装 nodejs（大屏语法检查用，可选）


也可以手工执行等价命令：

bash
sudo add-apt-repository universe        # 本项目的 Qt6 包全部位于 universe 仓库
sudo apt update
sudo apt install -y 
  build-essential 
  qmake6 
  qt6-base-dev 
  libqt6charts6-dev 
  qt6-webengine-dev 
  libqt6webenginecore6-bin 
  libqt6sql6-sqlite 
  sqlite3 
  python3


可选（仅用于 `tests/check\_dashboard\_syntax.js` 大屏语法检查，缺失时 `test\_all.sh` 会自动跳过）：

bash
sudo apt install -y nodejs

