# 东软电动汽车充电桩应用管理平台

本目录是 Ubuntu 项目的源码与答辩材料根目录。项目严格采用课程指定的 Qt 6/C++、SQLite、Socket、多线程、Qt Charts 与 QWebEngine 技术。

## qmake 构建与启动

本项目只使用 qmake 作为正式构建方式。在 Qt Creator 中打开根目录的
`EVChargePlatform.pro`，或在 Ubuntu 终端执行：

进入项目根目录
```bash
chmod +x scripts/*.sh
./scripts/build_qmake.sh
./scripts/run_demo.sh
```

- Qt 用户端和管理员端会直接显示在 Ubuntu 桌面。
- 手机端/PWA：`http://虚拟机IP:8080`
- Web 运营大屏：`http://虚拟机IP:8080/dashboard`
- 用户演示手机号：`13800138001`
- 管理员账号：`admin`，密码：`123456`

详细测试证据与答辩说明位于根目录 `docs`。
