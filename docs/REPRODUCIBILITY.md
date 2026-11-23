# 安装、运行与复现

## 1. 前置条件与构建

目标为Ubuntu22.04 x86_64、Qt6.2.4、GCC11，桌面程序需要图形会话或WSLg；仅服务器/API可无图形运行。不需要外部数据集和模型。依赖见 [ENVIRONMENT](../ENVIRONMENT.md)。

```bash
git clone https://github.com/dthu7b8a-design/ev-charge-platform.git
cd ev-charge-platform
bash setup_env.sh
qmake6 -query QT_VERSION
g++ --version
bash scripts/build_qmake.sh
```

正式推送前，整理版只存在本地分支；远程main不自动包含尚未发布的改动。

## 2. 桌面和手机演示

```bash
bash scripts/run_demo.sh
```

服务与两个桌面程序默认使用本机8080。演示手机号13800138001，管理员admin/123456，只用于隔离演示。数据库自动创建于项目`data/evcharge.db`，日志在`logs/`。已有数据库含订单和余额，不能把它当缓存删除。

手机访问`http://主机IP:8080/mobile/`，同机可用localhost。根地址和`/mobile`也应加载完整资源。PWA的Service Worker依赖浏览器安全上下文，普通局域网HTTP不能保证安装/离线缓存成功；静态页面可访问不等于完整离线功能已验收。

```bash
bash scripts/stop_demo.sh
```

## 3. 服务单独运行

```bash
mkdir -p /tmp/evcharge-demo-data
./build-qmake/bin/charge_server \
  --port 18080 \
  --data /tmp/evcharge-demo-data \
  --web "$PWD/web"
```

另终端执行`curl http://127.0.0.1:18080/api/health`。自定义端口适用于API或浏览器；桌面客户端仍固定8080，不会自动跟随。测试数据目录不得指定为个人或演示数据库。

初始化生成6站点、36电桩、模拟订单及小时负载；随机种子固定但日期取当天，因此跨日期不保证逐字节相同。项目没有正式数据库迁移框架。

## 4. 配置与演示边界

- Qt导航使用在线腾讯地图，需要网络。可选`TENCENT_MAP_KEY`用于地址解析；真实Key只在本机环境设置，不提交。无Key有模拟定位降级。
- 手机定位需要浏览器权限和安全上下文，失败不能冒充真实GPS。
- 头像保存为本地文件路径，不是服务器上传，跨机器不保证存在。
- 登录没有短信验证，充值/设备重启/计量/支付均是模拟；不接真实充电桩协议。
- token仅在服务端内存；重启失效，没有完整生产会话策略。

## 5. 测试

```bash
bash scripts/test_all.sh
python3 tests/check_qt_ui_regressions.py
# 可选：安装Node后执行
node --check web/mobile/app.js
node --check web/mobile/sw.js
```

集成脚本在18080启动本次服务并使用新建测试目录。端口冲突应失败，不能把会修改余额/订单的测试发送到已有实例。SQLite必须`integrity_check=ok`且`foreign_key_check`无行。

```bash
export EVCHARGE_BUILD_DIR="$HOME/evcharge-build"
export EVCHARGE_QA_DIR="$HOME/evcharge-validation"
bash scripts/build_qmake.sh
bash scripts/test_all.sh
```

build/run/stop/test传入同一构建目录。测试涵盖主业务、错误响应、预约设备保护和真实手机资源；27项源码守卫不是图形交互测试。实际结果与未验收范围见 [VALIDATION](VALIDATION.md)。

## 6. 全新克隆验收与排错

从整理后的提交独立clone，使用新build/data/qa，不复制原构建缓存和数据库；核对三个可执行程序、API主链、手机资源及SQLite断言。真实移动端安装、地图联网和原生Windows构建如未测，须保留未验收说明。

| 现象 | 检查 |
|---|---|
| `GL/gl.h`缺失 | 安装libgl-dev后重建 |
| QtCharts/WebEngine/SQLite驱动缺失 | 按环境说明安装开发包和SQLite插件 |
| 桌面无显示 | 检查图形会话/WSLg；API成功不代表桌面交互成功 |
| WebEngine拒绝root运行 | 正常桌面用普通用户；无头QA设置不能当常规部署要求 |
| 端口占用 | 仅停止确认属于本项目的实例，不强杀未知进程 |
| 历史图日期不一致 | 演示数据按初始化日期生成，比较业务而非所有时间戳 |
