#!/usr/bin/env python3
import json
import re
import socket
import sys
import urllib.error
import urllib.parse
import urllib.request
from html.parser import HTMLParser

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
check("实时空闲桩与总桩数", all(0 <= x["free_count"] <= x["pile_count"] for x in stations))

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

second_token = request("/api/auth/phone", "POST", {"phone": "13900009992"})["data"]["token"]
guard_pile = next(row for row in request("/api/admin/piles", token=admin_token)["data"]
                  if row["status"] == "idle")
guard_order = request("/api/orders/reserve", "POST", {"pile_id": guard_pile["id"]}, user_token, 201)["data"]["order_id"]
for operation, method, suffix, body in (
    ("重启", "POST", "restart", {}),
    ("设为空闲", "PATCH", "status", {"status": "idle"}),
    ("设为故障", "PATCH", "status", {"status": "fault"}),
    ("设为离线", "PATCH", "status", {"status": "offline"}),
):
    blocked = request(f"/api/admin/piles/{guard_pile['id']}/{suffix}", method, body, admin_token, 409)
    check(f"预约中的电桩不可{operation}", not blocked["ok"])
    competing = request("/api/orders/reserve", "POST", {"pile_id": guard_pile["id"]}, second_token, 409)
    check(f"{operation}失败后其他用户仍不可重复预约", competing["code"] == "pile_unavailable")
check("管理操作未改变原预约", request("/api/orders/active", token=user_token)["data"][0]["id"] == guard_order)
request(f"/api/orders/{guard_order}/cancel", "POST", {}, user_token)
request(f"/api/admin/piles/{guard_pile['id']}/restart", "POST", {}, admin_token)
check("取消预约后可正常重启电桩", True)
for status in ("fault", "offline", "idle"):
    request(f"/api/admin/piles/{guard_pile['id']}/status", "PATCH", {"status": status}, admin_token)
    changed_pile = next(row for row in request("/api/admin/piles", token=admin_token)["data"]
                        if row["id"] == guard_pile["id"])
    check(f"取消预约后可设为{status}", changed_pile["status"] == status)
second_order = request("/api/orders/reserve", "POST", {"pile_id": guard_pile["id"]}, second_token, 201)["data"]["order_id"]
check("取消预约后其他用户可正常预约", second_order != guard_order)
request(f"/api/orders/{second_order}/cancel", "POST", {}, second_token)

parsed_base = urllib.parse.urlparse(BASE)
with socket.create_connection((parsed_base.hostname, parsed_base.port), timeout=8) as connection:
    connection.sendall(b"POST /api/auth/phone HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 1048577\r\n\r\n")
    oversized_response = connection.recv(4096)
check("请求正文大小限制", b"413 Payload Too Large" in oversized_response and b"payload_too_large" in oversized_response)

mobile = urllib.request.urlopen(BASE + "/", timeout=8).read().decode("utf-8")
mobile_directory = urllib.request.urlopen(BASE + "/mobile/", timeout=8).read().decode("utf-8")
mobile_js = urllib.request.urlopen(BASE + "/mobile/app.js", timeout=8).read().decode("utf-8")
mobile_entry = urllib.request.urlopen(BASE + "/mobile.html", timeout=8).read().decode("utf-8")
check("Android/iOS PWA 页面", "apple-mobile-web-app-capable" in mobile and "manifest" in mobile)
check("手机端目录地址可访问", "loginBtn" in mobile_directory)
check("手机端登录与退出", "loginBtn" in mobile and "logoutBtn" in mobile and "sessionStorage.evChargeToken" in mobile_js and "localStorage" not in mobile_js)
check("手机定位和腾讯路线导航", "navigator.geolocation" in mobile_js and "apis.map.qq.com/uri/v1/routeplan" in mobile_js)
check("mobile.html 便捷入口", "mobile/index.html" in mobile_entry)
check("本地文件相对资源路径", 'href="./app.css"' in mobile and 'src="./app.js"' in mobile)


class MobileResources(HTMLParser):
    def __init__(self):
        super().__init__()
        self.paths = {}

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "link" and attrs.get("rel") in ("stylesheet", "manifest"):
            self.paths[attrs["rel"]] = attrs["href"]
        if tag == "script" and attrs.get("src"):
            self.paths["script"] = attrs["src"]


for entry in ("/", "/mobile", "/mobile/"):
    page_url = BASE + entry
    parser = MobileResources()
    parser.feed(urllib.request.urlopen(page_url, timeout=8).read().decode("utf-8"))
    check(f"{entry} 手机资源声明完整", set(parser.paths) == {"stylesheet", "manifest", "script"})
    resources = {}
    for kind, accepted_types in (
        ("stylesheet", {"text/css"}),
        ("script", {"application/javascript", "text/javascript"}),
        ("manifest", {"application/json", "application/manifest+json"}),
    ):
        url = urllib.parse.urljoin(page_url, parser.paths[kind])
        with urllib.request.urlopen(url, timeout=8) as response:
            resources[kind] = response.read().decode("utf-8")
            check(f"{entry} 实际加载 {kind}", response.status == 200 and bool(resources[kind])
                  and response.headers.get_content_type() in accepted_types)
    manifest = json.loads(resources["manifest"])
    check(f"{entry} PWA 启动路径与作用域", manifest["start_url"] == "/mobile/"
          and manifest["scope"] == "/mobile/")
    registration = re.search(r"serviceWorker\.register\(['\"]([^'\"]+)['\"]", resources["script"])
    if not registration:
        raise AssertionError(f"{entry} 缺少 Service Worker 注册")
    worker_url = urllib.parse.urljoin(page_url, registration.group(1))
    with urllib.request.urlopen(worker_url, timeout=8) as response:
        worker = response.read().decode("utf-8")
        check(f"{entry} 实际加载 Service Worker", response.status == 200
              and "self.addEventListener" in worker
              and urllib.parse.urlparse(worker_url).path == "/mobile/sw.js")

print(f"ALL PASS: {passed} checks")
