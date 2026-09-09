const $ = selector => document.querySelector(selector);
let token = sessionStorage.evChargeToken || '';
let activeOrder = null;
let latitude = 22.5431;
let longitude = 114.0579;
let selectedStation = null;

async function api(path, method = 'GET', body) {
  try {
    const response = await fetch(path, {
      method,
      headers: {
        'Content-Type': 'application/json',
        ...(token ? {
          Authorization: `Bearer ${token}`
        } : {})
      },
      body: body ? JSON.stringify(body) : undefined
    });
    const result = await response.json();
    if (!result.ok) throw new Error(result.error || '请求失败');
    $('#network').textContent = '在线';
    return result.data;
  } catch (error) {
    toast(error.message);
    $('#network').textContent = '连接异常';
    throw error;
  }
}

function toast(text) {
  const element = $('#toast');
  element.textContent = text;
  element.classList.add('show');
  setTimeout(() => element.classList.remove('show'), 2200);
}

async function login() {
  if (location.protocol === 'file:') {
    toast('本地文件只能预览，请启动服务后登录');
    return;
  }
  const phone = $('#phone').value;
  if (!/^1[3-9]\d{9}$/.test(phone)) return toast('请输入有效的 11 位手机号');
  const data = await api('/api/auth/phone', 'POST', {
    phone
  });
  token = data.token;
  sessionStorage.evChargeToken = token;
  $('#login').classList.add('hidden');
  $('#app').classList.remove('hidden');
  await refresh();
}

function logout() {
  token = '';
  activeOrder = null;
  delete sessionStorage.evChargeToken;
  $('#app').classList.add('hidden');
  $('#login').classList.remove('hidden');
  toast('已安全退出');
}

async function loadStations() {
  const rows = await api(`/api/stations?lat=${latitude}&lon=${longitude}`);
  $('#stationList').innerHTML = rows.map(station => `
    <article class="card" data-id="${station.id}" data-name="${station.name}"
      data-address="${station.address}" data-latitude="${station.latitude}"
      data-longitude="${station.longitude}">
      <div><h3>${station.recommended ? '★ ' : ''}${station.name}</h3>
        <div class="meta">${station.address}<br>
          <span class="free">${station.free_count} 个空闲</span> / ${station.pile_count} 个 · ${station.distance_km} km
          <br>预测空闲 ${station.predicted_free_piles ?? '—'} 个
        </div>
      </div>
      <div class="price">¥${Number(station.price).toFixed(2)}<div class="meta">/kWh</div></div>
    </article>`).join('');
  document.querySelectorAll('.card[data-id]').forEach(element => {
    element.onclick = () => loadPiles({
      id: Number(element.dataset.id),
      name: element.dataset.name,
      address: element.dataset.address,
      latitude: Number(element.dataset.latitude),
      longitude: Number(element.dataset.longitude)
    });
  });
}

async function locate() {
  if (!navigator.geolocation) return toast('当前浏览器不支持定位');
  $('#locationText').textContent = '⌖ 正在获取手机定位…';
  navigator.geolocation.getCurrentPosition(async position => {
    latitude = position.coords.latitude;
    longitude = position.coords.longitude;
    $('#locationText').textContent =
    `⌖ 已定位 ${latitude.toFixed(4)}, ${longitude.toFixed(4)}`;
    await loadStations();
    toast('已按当前位置重新排序');
  }, () => {
    $('#locationText').textContent = '⌖ 深圳市民中心 · 模拟定位';
    toast('定位未授权，继续使用模拟位置');
  }, {
    enableHighAccuracy: true,
    timeout: 8000
  });
}

async function loadPiles(station) {
  selectedStation = station;
  $('#stationName').textContent = station.name;
  $('#stationAddress').textContent = station.address;
  const rows = await api(`/api/stations/${station.id}/piles`);
  $('#pileList').innerHTML = rows.map(pile =>
    `
    <div class="pile"><div><strong>${pile.code} · ${pile.type === 'fast' ? '快充' : '慢充'}</strong>
      <small>${pile.power} kW · ${pile.status === 'idle' ? '空闲' : '暂不可用'}</small></div>
      <button ${pile.status !== 'idle' ? 'disabled' : ''} data-pile="${pile.id}">预约</button></div>`).join('');
  document.querySelectorAll('[data-pile]').forEach(button => {
    button.onclick = () => reserve(Number(button.dataset.pile));
  });
  $('#pileDialog').showModal();
}

function navigate(mode) {
  if (!selectedStation) return;
  const params = new URLSearchParams({
    type: mode,
    from: '当前位置',
    fromcoord: `${latitude},${longitude}`,
    to: selectedStation.name,
    tocoord: `${selectedStation.latitude},${selectedStation.longitude}`,
    policy: '0',
    referer: 'ev-charge-platform'
  });
  window.open(`https://apis.map.qq.com/uri/v1/routeplan?${params}`, '_blank', 'noopener');
}

async function reserve(pile_id) {
  await api('/api/orders/reserve', 'POST', {
    pile_id
  });
  $('#pileDialog').close();
  toast('预约成功');
  showView('order');
  await loadOrder();
  await loadStations();
}

async function loadMe() {
  const user = await api('/api/me');
  $('#hello').textContent = `${user.nickname}，欢迎回来`;
  $('#nickname').value = user.nickname;
  $('#balance').textContent = `¥ ${Number(user.balance).toFixed(2)}`;
}

async function loadOrder() {
  const rows = await api('/api/orders/active');
  activeOrder = rows[0] || null;
  const names = {
    reserved: '已预约',
    charging: '充电中',
    pending_payment: '待结算'
  };
  $('#orderState').textContent = activeOrder ? names[activeOrder.status] : '暂无进行中订单';
  $('#orderInfo').textContent = activeOrder ?
    `${activeOrder.station_name} · ${activeOrder.pile_code}\n${activeOrder.duration_minutes} 分钟 · ${activeOrder.energy_kwh} kWh · ¥${Number(activeOrder.amount).toFixed(2)}` :
    '预约后，可在这里完成充电与结算。';
  for (const [id, state] of [
      ['startBtn', 'reserved'],
      ['stopBtn', 'charging'],
      ['settleBtn', 'pending_payment'],
      ['cancelBtn', 'reserved']
    ]) {
    $('#' + id).disabled = !activeOrder || activeOrder.status !== state;
  }
}

async function orderAction(action) {
  if (!activeOrder) return;
  await api(`/api/orders/${activeOrder.id}/${action}`, 'POST', {});
  toast({
    start: '充电已启动',
    stop: '充电已结束，请结算',
    settle: '结算成功',
    cancel: '预约已取消'
  } [action]);
  await Promise.all([loadOrder(), loadMe(), loadStations()]);
}

async function refresh() {
  await Promise.all([loadStations(), loadMe(), loadOrder()]);
}

function showView(id) {
  document.querySelectorAll('.view').forEach(view => view.classList.toggle('active', view.id ===
    id));
  document.querySelectorAll('nav button').forEach(button => button.classList.toggle('active', button
    .dataset.view === id));
  $('#pageTitle').textContent = {
    stations: '附近充电站',
    order: '充电订单',
    profile: '个人中心'
  } [id];
}

$('#loginBtn').onclick = login;
$('#logoutBtn').onclick = logout;
$('#locateBtn').onclick = locate;
$('#driveBtn').onclick = () => navigate('drive');
$('#walkBtn').onclick = () => navigate('walk');
$('#closeDialog').onclick = () => $('#pileDialog').close();
document.querySelectorAll('nav button').forEach(button => button.onclick = () => showView(button
  .dataset.view));
$('#startBtn').onclick = () => orderAction('start');
$('#stopBtn').onclick = () => orderAction('stop');
$('#settleBtn').onclick = () => orderAction('settle');
$('#cancelBtn').onclick = () => orderAction('cancel');
$('#rechargeBtn').onclick = async () => {
  await api('/api/me/recharge', 'POST', {
    amount: 50
  });
  toast('充值成功');
  await loadMe();
};
$('#saveProfile').onclick = async () => {
  await api('/api/me', 'PATCH', {
    nickname: $('#nickname').value,
    avatar: ''
  });
  toast('资料已保存');
  await loadMe();
};

window.addEventListener('online', () => $('#network').textContent = '在线');
window.addEventListener('offline', () => $('#network').textContent = '离线');
if ('serviceWorker' in navigator && location.protocol.startsWith('http')) {
  navigator.serviceWorker.register('./sw.js');
}
if (location.protocol === 'file:') {
  $('#previewNotice').classList.remove('hidden');
  token = '';
  delete sessionStorage.evChargeToken;
} else if (token) {
  $('#login').classList.add('hidden');
  $('#app').classList.remove('hidden');
  refresh().catch(() => logout());
}