#!/usr/bin/env python3
"""Fast source-level guards for the Qt UI regressions reported during visual QA."""

from pathlib import Path
import re


project_root = Path(__file__).resolve().parents[1]
admin = (project_root / "admin" / "adminwindow.cpp").read_text(encoding="utf-8")
user = (project_root / "user" / "userwindow.cpp").read_text(encoding="utf-8")
header = (project_root / "user" / "userwindow.h").read_text(encoding="utf-8")
router = (project_root / "server" / "router.cpp").read_text(encoding="utf-8")
user_main = (project_root / "user" / "main.cpp").read_text(encoding="utf-8")
admin_main = (project_root / "admin" / "main.cpp").read_text(encoding="utf-8")


def has_call(source: str, name: str, *arguments: str) -> bool:
    """Match a C++ call without depending on whitespace inserted by formatters."""
    argument_pattern = r"\s*,\s*".join(re.escape(argument) for argument in arguments)
    pattern = rf"{re.escape(name)}\s*\(\s*{argument_pattern}\s*\)"
    return re.search(pattern, source) is not None


def call_count(source: str, name: str, *arguments: str) -> int:
    """Count matching C++ calls while allowing normal formatting whitespace."""
    argument_pattern = r"\s*,\s*".join(re.escape(argument) for argument in arguments)
    pattern = rf"{re.escape(name)}\s*\(\s*{argument_pattern}\s*\)"
    return len(re.findall(pattern, source))

checks = {
    "管理端操作列固定宽度": has_call(admin, "setColumnWidth", "8", "190"),
    "管理端按钮行高": "setDefaultSectionSize(40)" in admin,
    "管理端操作按钮最小尺寸": call_count(admin, "setMinimumSize", "72", "30") >= 2,
    "用户管理操作列固定宽度": has_call(admin, "setColumnWidth", "6", "120"),
    "用户管理按钮行高": "m_users->verticalHeader()->setDefaultSectionSize(40)" in admin,
    "冻结解冻按钮最小尺寸": has_call(admin, "setMinimumSize", "92", "30"),
    "用户端预约列宽": has_call(user, "setColumnWidth", "3", "108"),
    "用户端预约按钮行高": "setDefaultSectionSize(42)" in user,
    "用户端预约按钮最小尺寸": has_call(user, "setMinimumSize", "88", "32"),
    "主动预约不触发遗留订单提示": (
        "loadOrder(false)" in user and "notifyExisting && !m_orderPrompted" in user
    ),
    "定位状态完整展示": "m_locationStatus" in header and "当前位置：" in user,
    "近距离不是含糊的 0km": 'QStringLiteral("< 0.1 km")' in user,
    "找桩与地图使用独立页面": "m_stationFlow->addWidget(page)" in user and "m_stationFlow->addWidget(mapPage)" in user,
    "地图页占据主要空间": has_call(user, "mapRoot->addWidget", "m_map", "1"),
    "窄屏地图缩放适配": "setZoomFactor(0.80)" in user and "m_mapTitle->setWordWrap(true)" in user,
    "地图页隐藏滚动条": has_call(
        user, "setAttribute", "QWebEngineSettings::ShowScrollBars", "false"
    ),
    "导航切换到地图页": "m_stationFlow->setCurrentIndex(1)" in user,
    "地图页可返回找桩": "← 返回找桩" in user and "m_stationFlow->setCurrentIndex(0)" in user,
    "我的页面提供退出登录": "退出登录" in user and "&UserWindow::logout" in user,
    "退出登录清除认证令牌": "m_api.setToken(QString())" in user,
    "充电数据每秒刷新": "m_chargeTimer->setInterval(1000)" in user,
    "充电时间实时显示": "m_liveDuration->setText(duration)" in user,
    "消费金额实时显示": "m_liveAmount->setText" in user and "m_chargePrice" in header,
    "实时计费规则与服务端一致": "qMax<qint64>(60, elapsedSeconds)" in user,
    "当前订单返回充电功率": "p.power pile_power" in router,
    "用户端顶栏使用安全应用标识": (
        "EVChargeUser" in user_main and "evcharge-user" in user_main
    ),
    "管理端顶栏使用安全应用标识": (
        "EVChargeAdmin" in admin_main and "evcharge-admin" in admin_main
    ),
}

failed = [name for name, passed in checks.items() if not passed]
if failed:
    raise SystemExit("FAIL Qt UI regression guards: " + "、".join(failed))

print(f"PASS Qt UI regression guards: {len(checks)}/{len(checks)}")
