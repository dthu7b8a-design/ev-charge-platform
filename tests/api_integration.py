#!/usr/bin/env python3
import json
import socket
import sys
import urllib.error
import urllib.parse
import urllib.request

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:18080"
passed = 0


def request(path, method="GET", body=None, token=None, expected=200):
    data = None if body is None else json.dumps(body).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(BASE + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=8) as response:
            status, raw = response.status, response.read()
    except urllib.error.HTTPError as error:
        status, raw = error.code, error.read()
    if status != expected:
        raise AssertionError(f"{method} {path}: expected {expected}, got {status}: {raw!r}")
    return json.loads(raw) if raw else {}


def check(name, condition):
    global passed
    if not condition:
        raise AssertionError(name)
    passed += 1
    print(f"PASS {passed:02d} {name}")


health = request("/api/health")
check("服务健康检查", health["ok"] and health["data"]["database"])

invalid = request("/api/auth/phone", "POST", {"phone": "123"}, expected=422)
check("手机号格式校验", not invalid["ok"] and invalid["code"] == "invalid_phone")

phone = "13900009991"
login = request("/api/auth/phone", "POST", {"phone": phone})["data"]
user_token = login["token"]
user_id = login["user"]["id"]
check("首次手机号自动注册", login["created"] and user_id > 0)

login_again = request("/api/auth/phone", "POST", {"phone": phone})["data"]
check("已注册手机号直接登录", not login_again["created"])

stations = request("/api/stations?lat=22.5431&lon=114.0579")["data"]
check("附近充电站完整返回", len(stations) >= 6 and all("distance_km" in row for row in stations))
check("充电站按距离排序", [x["distance_km"] for x in stations] == sorted(x["distance_km"] for x in stations))
check("预测空闲桩与智能推荐", sum(1 for x in stations if x["recommended"]) == 1 and all("predicted_free_piles" in x for x in stations))

piles = request(f"/api/stations/{stations[0]['id']}/piles")["data"]
idle = next(row for row in piles if row["status"] == "idle")
check("站点设备明细", len(piles) >= 6 and idle["power"] > 0)

reserved = request("/api/orders/reserve", "POST", {"pile_id": idle["id"]}, user_token, 201)["data"]
order_id = reserved["order_id"]
check("预约空闲充电桩", order_id > 0)

duplicate = request("/api/orders/reserve", "POST", {"pile_id": piles[1]["id"]}, user_token, 409)
check("阻止同一用户重复未结订单", duplicate["code"] == "active_order_exists")

active = request("/api/orders/active", token=user_token)["data"]
check("读取当前预约", len(active) == 1 and active[0]["status"] == "reserved")

request(f"/api/orders/{order_id}/start", "POST", {}, user_token)
active = request("/api/orders/active", token=user_token)["data"]
check("启动充电", active[0]["status"] == "charging")
check(
    "实时计费字段完整",
    bool(active[0]["started_at"])
    and active[0]["pile_power"] > 0
    and active[0]["price"] > 0,
)

stopped = request(f"/api/orders/{order_id}/stop", "POST", {}, user_token)["data"]
check("结束充电并计量", stopped["amount"] > 0 and stopped["energy_kwh"] > 0)

request("/api/me/recharge", "POST", {"amount": 500}, user_token)
me = request("/api/me", token=user_token)["data"]
check("钱包充值", me["balance"] >= 500)

request(f"/api/orders/{order_id}/settle", "POST", {}, user_token)
active = request("/api/orders/active", token=user_token)["data"]
orders = request("/api/orders", token=user_token)["data"]
check("钱包结算并释放设备", not active and orders[0]["status"] == "settled")

bad_admin = request("/api/admin/login", "POST", {"username": "admin", "password": "bad"}, expected=401)
check("管理员错误密码拦截", bad_admin["code"] == "invalid_credentials")
admin_token = request("/api/admin/login", "POST", {"username": "admin", "password": "123456"})["data"]["token"]
check("管理员默认账号登录", bool(admin_token))

summary = request("/api/admin/summary", token=admin_token)["data"]
check("营收与资产汇总", summary["piles"] >= 36 and summary["users"] >= 6 and summary["total_revenue"] > 0)

sales7 = request("/api/admin/sales?days=7", token=admin_token)["data"]
sales30 = request("/api/admin/sales?days=30", token=admin_token)["data"]
check("七日与三十日销售趋势", len(sales7) == 7 and len(sales30) == 30)

distribution = request("/api/admin/pile-distribution", token=admin_token)["data"]
check("设备状态分布", sum(x["count"] for x in distribution) >= 36)

new_station = request("/api/admin/stations", "POST", {"name": "自动测试充电站", "address": "深圳市测试路 1 号", "latitude": 22.55, "longitude": 114.06, "price": 1.35, "pile_count": 3}, admin_token, 201)["data"]
station_rows = request("/api/admin/stations", token=admin_token)["data"]
created_station = next(x for x in station_rows if x["id"] == new_station["id"])
check("新增电站并批量创建电桩", created_station["pile_count"] == 3 and len(request(f"/api/stations/{new_station['id']}/piles")["data"]) == 3)
check("充电站在线率", created_station["online_rate"] == 100)

admin_piles = request("/api/admin/piles", token=admin_token)["data"]
request(f"/api/admin/piles/{admin_piles[0]['id']}/restart", "POST", {}, admin_token)
check("远程重启充电桩", True)
new_pile = request("/api/admin/piles", "POST", {"station_id": stations[0]["id"], "code": "QA-AUTO-01", "type": "fast", "power": 160}, admin_token, 201)["data"]
check("新增充电桩", new_pile["id"] > 0)
request(f"/api/admin/piles/{new_pile['id']}/status", "PATCH", {"status": "fault"}, admin_token)
check("充电桩故障状态管理", any(x["status"] == "fault" and x["id"] == new_pile["id"] for x in request("/api/admin/piles", token=admin_token)["data"]))

users = request("/api/admin/users?search=" + phone, token=admin_token)["data"]
check("用户搜索", len(users) == 1 and users[0]["phone"] == phone)
request(f"/api/admin/users/{user_id}/status", "PATCH", {"status": "frozen"}, admin_token)
blocked = request("/api/auth/phone", "POST", {"phone": phone}, expected=403)
check("冻结用户禁止登录", blocked["code"] == "account_frozen")
frozen_session = request("/api/me", token=user_token, expected=401)
check("冻结后已有会话立即失效", frozen_session["code"] == "unauthorized")
request(f"/api/admin/users/{user_id}/status", "PATCH", {"status": "normal"}, admin_token)
check("解冻用户", request("/api/auth/phone", "POST", {"phone": phone})["ok"])

for horizon in (1, 6, 24):
    forecast = request(f"/api/admin/forecast?hours={horizon}", token=admin_token)["data"]
    check(f"未来 {horizon} 小时负荷预测", len(forecast) == horizon and all(x["predicted_kwh"] >= 0 and x["predicted_free_piles"] >= 0 and x["congestion"] in ("low", "medium", "high") for x in forecast))
station_forecast = request(f"/api/admin/forecast?hours=6&station_id={stations[0]['id']}", token=admin_token)["data"]
check("分站点负荷与空闲桩预测", len(station_forecast) == 6 and all(x["station_id"] == stations[0]["id"] for x in station_forecast))

parsed_base = urllib.parse.urlparse(BASE)
with socket.create_connection((parsed_base.hostname, parsed_base.port), timeout=8) as connection:
    connection.sendall(b"POST /api/auth/phone HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 1048577\r\n\r\n")
    oversized_response = connection.recv(4096)
check("请求正文大小限制", b"413 Payload Too Large" in oversized_response and b"payload_too_large" in oversized_response)

mobile = urllib.request.urlopen(BASE + "/", timeout=8).read().decode("utf-8")
mobile_directory = urllib.request.urlopen(BASE + "/mobile/", timeout=8).read().decode("utf-8")
mobile_js = urllib.request.urlopen(BASE + "/mobile/app.js", timeout=8).read().decode("utf-8")
mobile_entry = urllib.request.urlopen(BASE + "/mobile.html", timeout=8).read().decode("utf-8")
dashboard = urllib.request.urlopen(BASE + "/dashboard", timeout=8).read().decode("utf-8")
echarts_bundle = urllib.request.urlopen(BASE + "/dashboard/echarts.min.js", timeout=8).read()
check("Android/iOS PWA 页面", "apple-mobile-web-app-capable" in mobile and "manifest" in mobile)
check("手机端目录地址可访问", "loginBtn" in mobile_directory)
check("手机端登录与退出", "loginBtn" in mobile and "logoutBtn" in mobile and "sessionStorage.evChargeToken" in mobile_js and "localStorage" not in mobile_js)
check("手机定位和腾讯路线导航", "navigator.geolocation" in mobile_js and "apis.map.qq.com/uri/v1/routeplan" in mobile_js)
check("mobile.html 便捷入口", "mobile/index.html" in mobile_entry)
check("本地文件相对资源路径", 'href="./app.css"' in mobile and 'src="./app.js"' in mobile)
check("ECharts Web 运营数据大屏", "运营数据大屏" in dashboard and "echarts.init" in dashboard)
check("ECharts 本地离线资源", len(echarts_bundle) > 500_000 and b"Apache Software Foundation" in echarts_bundle[:1000])

print(f"ALL PASS: {passed} checks")
