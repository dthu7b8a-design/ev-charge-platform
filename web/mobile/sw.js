const CACHE = 'evcharge-phase1-v2',
  ASSETS = ['/', '/mobile/index.html', '/mobile/app.css', '/mobile/app.js'];
self.addEventListener('install', e => e.waitUntil(caches.open(CACHE).then(c => c.addAll(ASSETS))));
self.addEventListener('activate', e => e.waitUntil(caches.keys().then(xs => Promise.all(xs.filter(
  x => x !== CACHE).map(x => caches.delete(x))))));
self.addEventListener('fetch', e => {
  if (e.request.method === 'GET' && !e.request.url.includes('/api/')) e.respondWith(fetch(e
    .request).then(r => {
    const c = r.clone();
    caches.open(CACHE).then(x => x.put(e.request, c));
    return r
  }).catch(() => caches.match(e.request)))
})
