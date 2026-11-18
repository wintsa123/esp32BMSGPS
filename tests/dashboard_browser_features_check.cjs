// Exercise the production browser entry points with a simulated panel and map SDK.
const assert = require('node:assert/strict');
let chromium;
try { ({ chromium } = require('playwright')); }
catch { ({ chromium } = require('/vol1/1000/.tools/battery-browser/node_modules/playwright')); }

(async () => {
  const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  try {
    const page = await browser.newPage({ viewport: { width: 320, height: 568 }, serviceWorkers: 'block' });
    const errors = [], requests = [], streams = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route('**/_AMapService/config', route => route.fulfill({ json: { key: '0'.repeat(32) } }));
    await page.route('http://192.168.4.1/api/**', route => {
      const { pathname } = new URL(route.request().url());
      requests.push(pathname);
      const answers = {
        '/api/status': { version: 'browser-fixture' },
        '/api/settings/manifest': { protocol_version: 1, sections: [] },
        '/api/history/sessions': { ready: true, sessions: [] },
        '/api/controls': { controls: [] },
        '/api/cast/info': { protocol_version: 4, max_frame_bytes: 262144, active: false, orientations: [{ rotation: 1, width: 480, height: 320 }] },
      };
      return route.fulfill({ json: answers[pathname] ?? {}, headers: { 'Access-Control-Allow-Origin': '*' } });
    });
    await page.routeWebSocket('ws://192.168.4.1/cast', socket => {
      const stream = { socket, frames: 0, closed: false };
      streams.push(stream);
      socket.onClose(() => { stream.closed = true; });
      socket.onMessage(packet => {
        assert(Buffer.isBuffer(packet));
        assert.deepEqual([...packet.subarray(0, 2)], [1, 4]);
        assert.equal(packet[6], 1);
        assert.deepEqual([...packet.subarray(7, 9)], [0xff, 0xd8]);
        assert.equal(packet.readUInt32BE(2), ++stream.frames, 'next frame follows ACK');
        socket.send(Buffer.concat([Buffer.from([0x81]), packet.subarray(2, 6)]));
      });
    });
    await page.addInitScript(() => {
      Object.defineProperty(navigator.mediaDevices, 'getDisplayMedia', { value: async () => {
        if (window.delayCapture) await new Promise(resolve => { window.allowCapture = resolve; });
        const canvas = document.createElement('canvas');
        canvas.width = 640; canvas.height = 360;
        const ctx = canvas.getContext('2d');
        const stream = canvas.captureStream(15);
        window.captureTrack = stream.getVideoTracks()[0];
        ctx.fillRect(0, 0, 640, 360);
        setTimeout(() => ctx.fillRect(10, 10, 30, 30), 50);
        return stream;
      } });
      Object.defineProperty(navigator, 'geolocation', { value: {
        watchPosition(success) {
          window.setFix = longitude => success({ coords: { longitude, latitude: 0, accuracy: 5 }, timestamp: Date.now() });
          setTimeout(() => window.setFix(0), 0);
          return 1;
        },
        clearWatch(id) { window.clearedWatch = id; },
      } });
      const point = lon => ({ getLng: () => lon, getLat: () => 0 });
      window.AMapLoader = { load: async ({ key }) => {
        window.loadedMapKey = key;
        return {
        AutoComplete: class { search(text, done) {
          setTimeout(() => done('complete', { tips: [
            { name: `${text}东门`, district: '杭州市西湖区', address: '测试路 1 号', location: point(.005) },
            { name: `${text}西门`, district: '杭州市西湖区', location: point(.006) },
            { name: '无坐标', location: '' },
          ] }), text === '旧目的地' ? 700 : 0);
        } },
        Geocoder: class { getLocation(_, done) { window.geocoded = true; done('complete', { geocodes: [{ location: point(.006) }] }); } },
        Riding: class {
          search(from, to, done) {
            window.rideSearch = { from, to };
            done('complete', { routes: [{ rides: [
              { path: [point(0), point(.001), point(.002)], instruction: '前方左转' },
              { path: [point(.002), point(.004), point(.006)], instruction: '沿终点道路直行' },
            ] }] });
          }
        },
        };
      } };
    });
    const url = process.env.DASHBOARD_URL || 'http://127.0.0.1:8787/';
    await page.goto(url);
    await page.getByRole('button', {name:'设置', exact:true}).click();
    assert.equal(await page.getByLabel('设备地址', {exact:true}).isVisible(), false);
    await page.getByRole('button', {name:'连接 Wi-Fi 设备', exact:true}).click();
    await page.waitForFunction(() => document.querySelector('.hero-top .chip:last-child')?.classList.contains('accent'));
    assert(requests.includes('/api/status'), 'blank address connects to the setup gateway');
    assert.equal(await page.getByRole('button', {name:/^App 更新/}).count(), 0);
    assert(await page.getByRole('button', {name:/^Web 前端更新/}).count());
    assert(await page.getByRole('button', {name:/^固件更新/}).count());

    const language = page.getByRole('button', {name:'网页语言: 中文', exact:true});
    await language.focus();
    await language.press('ArrowDown');
    await page.getByRole('button', {name:'中文', exact:true}).press('ArrowDown');
    await page.getByRole('button', {name:'English', exact:true}).press('Enter');
    await page.getByRole('button', {name:'Page language: English', exact:true}).click();
    await page.getByRole('button', {name:'中文', exact:true}).click();
    assert.equal(await language.getAttribute('aria-expanded'), 'false');

    const settings = page.getByRole('region', {name:'设置', exact:true});
    assert.equal(await settings.getByRole('button', {name:/投屏|骑行导航/}).count(), 0);
    await page.getByRole('button', {name:'功能', exact:true}).click();
    const features = page.getByRole('region', {name:'功能', exact:true});
    await features.getByRole('button', {name:'投屏', exact:true}).click();
    const cast = page.getByRole('dialog', {name:'投屏', exact:true});
    await cast.getByRole('button', {name:'开始网页投屏', exact:true}).click();
    await cast.getByText('正在向面板投屏', {exact:true}).waitFor();
    assert(streams[0].frames > 0);
    streams[0].socket.send(Buffer.from([3, 4]));
    await cast.getByText('面板已结束投屏。', {exact:true}).waitFor();
    assert.equal(await page.evaluate(() => window.captureTrack.readyState), 'ended');
    await page.evaluate(() => { window.delayCapture = true; window.captureTrack = undefined; });
    await cast.getByRole('button', {name:'开始网页投屏', exact:true}).click();
    await cast.getByRole('button', {name:'结束投屏', exact:true}).click();
    await page.evaluate(() => window.allowCapture());
    await page.waitForFunction(() => window.captureTrack?.readyState === 'ended');
    assert.equal(streams.length, 1, 'late screen permission must not open another connection');
    await cast.getByRole('button', {name:'返回', exact:true}).click();
    await cast.waitFor({state:'detached'});

    const navigationEntry = features.getByRole('button', {name:'骑行导航', exact:true});
    await navigationEntry.click();
    const nav = page.getByRole('dialog', {name:'实时导航', exact:true});
    const destination = nav.getByRole('combobox', {name:'骑行目的地', exact:true});
    await destination.fill('旧目的地');
    await nav.getByText('正在搜索地点…').waitFor();
    await destination.fill('测试目的地');
    const list = nav.getByRole('listbox', {name:'目的地候选'});
    await list.getByRole('option', {name:/测试目的地西门/}).waitFor();
    assert.equal(await list.getByRole('option').count(), 2);
    assert.equal(await list.getByText('测试路 1 号', {exact:false}).count(), 1);
    await destination.press('ArrowDown');
    await destination.press('ArrowDown');
    await destination.press('Enter');
    assert.equal(await destination.inputValue(), '测试目的地西门');
    assert.equal(await destination.getAttribute('aria-expanded'), 'false');
    assert.equal(await nav.getByLabel(/高德.*密钥|高德.*Key/).count(), 0);
    await nav.getByRole('button', {name:'开始骑行导航', exact:true}).click();
    await nav.getByText(/前方左转/).waitFor();
    assert.deepEqual(await page.evaluate(() => [window.rideSearch, window.loadedMapKey.length, window.geocoded]), [{ from: [0, 0], to: [.006, 0] }, 32, undefined]);
    await nav.getByRole('button', {name:'在面板显示', exact:true}).click();
    await nav.getByText('导航已显示在面板上', {exact:true}).waitFor();
    await page.evaluate(() => window.setFix(.0021));
    await nav.getByText(/沿终点道路直行/).waitFor();
    assert(await nav.evaluate(element => element.scrollWidth <= element.clientWidth + 1));
    await nav.getByRole('button', {name:'返回', exact:true}).click();
    await nav.waitFor({state:'detached'});
    await page.waitForFunction(() => window.clearedWatch === 1);
    assert.equal(await page.evaluate(() => window.clearedWatch), 1);
    await navigationEntry.evaluate(element => { if (document.activeElement !== element) throw new Error('Focus not restored'); });
    assert(streams[0].closed && streams[1].closed);
    await page.goto(new URL('/cast?v=1&lang=en', url).href);
    await page.getByRole('dialog', {name:'Cast', exact:true}).getByRole('button', {name:'Start browser casting', exact:true}).waitFor();
    assert.deepEqual(errors, []);
    const app = await browser.newPage({ serviceWorkers: 'block' });
    await app.addInitScript(() => {
      window.connectionSettingsCalls = [];
      window.BmsNative = { getAppInfo: () => '{}', bleConnected: () => false, openConnectionSettings: kind => window.connectionSettingsCalls.push(kind) };
    });
    await app.goto(url);
    await app.getByRole('button', {name:'设置', exact:true}).click();
    await app.getByRole('button', {name:/^App 更新/}).waitFor();
    await app.getByRole('button', {name:'连接 Wi-Fi 设备', exact:true}).click();
    await app.getByRole('button', {name:'连接面板蓝牙', exact:true}).click();
    assert.deepEqual(await app.evaluate(() => window.connectionSettingsCalls), ['http', 'ble']);
    console.log('PASS: default hotspot, language dropdown, browser update visibility, JPEG ACK/exit, live route progress, panel frames, cleanup and cast URL');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
