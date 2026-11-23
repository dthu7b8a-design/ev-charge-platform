# 实际验证记录

日期：2026-11-23（Asia/Shanghai）。目标Ubuntu22.04 x86_64（WSL2）、Qt6.2.4、GCC11、SQLite3.37.2、Python3.10。没有把源码检查当成图形交互测试。

## 整理前后对照

| 项目 | 整理前基线 | 整理后实测 |
|---|---|---|
| qmake四子工程 | 首次缺GL/gl.h；安装libgl-dev后全部成功 | 全部成功 |
| API集成 | 37/37通过 | 69/69通过 |
| Qt源码回归守卫 | 27/27 | 27/27 |
| SQLite | integrity_check=ok，无FK违规 | 同样通过，脚本增加实际断言 |
| 手机根路径资源 | 根和/mobile的CSS/JS/manifest均404 | 根、/mobile、/mobile/真实资源及SW请求通过 |
| 同桩预约保护 | 临时DB复现A预约→管理员重启→B同桩预约 | 重启/改idle/fault/offline均409，B预约409；A取消后可正常操作 |

新增资源测试和预约测试均先针对原始server出现实际失败，再针对修复版本通过；未删除旧测试或放宽业务预期。预约修复得到用户明确同意，未改变数据库结构。

## 执行命令与范围

```bash
EVCHARGE_BUILD_DIR=/path/to/isolated-build bash scripts/build_qmake.sh
EVCHARGE_BUILD_DIR=/path/to/isolated-build \
EVCHARGE_QA_DIR=/path/to/isolated-evidence bash scripts/test_all.sh
python3 tests/check_qt_ui_regressions.py
```

测试使用新建SQLite，未触碰原个人数据库。脚本失败分支实测5项：缺包停止安装、apt失败非零、安装后验证失败非零、测试端口已有服务时退出且对哨兵服务请求为0、缺演示程序非零。

run_demo/stop_demo在独立harness与Xvfb中实际启动/停止本项目服务及两个桌面程序；health正常，退出后本次进程已清理。现有Qt自动登录入口用于截图，没有新增产品开关。

## 界面与环境

[管理端](assets/admin.png)、[用户找桩](assets/user.png)是当前构建在独立模拟数据下的真实Qt截图，中文字体来自fonts-noto-cjk。已目视核对文字、站点与图表，不是生成式图片，也不是旧预测界面。截图营收与定位均属模拟，不是实测运营成果。

两JS的Node语法、5个Shell语法与git diff --check通过。新环境发现并补齐libgl-dev与中文字体，这属于复现依赖修正。

## 全新克隆

最终本地提交的独立检出、全新构建和API验证在收尾阶段执行，结果随后记录于本节；当前不预先宣称通过。

## 未验收与边界

- 未连接实物充电桩、真实支付或短信；系统本身是模拟实现。
- 未验证真实手机上的PWA安装/离线行为及外网地图服务可用性。
- 无原生Windows Qt构建或生产压测、TLS、完整鉴权安全证明。
- 图形截图/启停不是每个Qt控件的完整交互自动化。

完整原始日志保留在本地维护目录的validation/evcharge；公开仓库只保留可复现命令、摘要和真实截图。最终SHA及回滚方式见 [整理报告](PROJECT_CLEANUP_REPORT.md)。
