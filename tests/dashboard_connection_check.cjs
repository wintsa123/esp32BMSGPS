// Run against the built preview: node tests/dashboard_connection_check.cjs
const assert = require('node:assert/strict');
let chromium;
try { ({ chromium } = require('playwright')); }
catch { ({ chromium } = require('/vol1/1000/.tools/battery-browser/node_modules/playwright')); }

(async () => {
  const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  try {
    const page = await browser.newPage({ viewport: { width: 390, height: 844 }, serviceWorkers: 'block' });
    await page.addInitScript(() => {
      window.links = { http: true, ble: false };
      // Exercise the real native request bridge with two independent device links.
      window.BmsNative = {
        getAppInfo: () => '{}', connectBle: () => { window.links.ble = true; },
        bleConnected: () => window.links.ble, openNative: () => {},
        request: (id, method, path, body, host) => queueMicrotask(() => {
          const link = host === 'ble' ? 'ble' : 'http';
          const value = path === '/api/settings/manifest' ? { protocol_version: 1, sections: [] }
            : path === '/api/status' ? { controller_online: true, bms: 'connected', wifi: 'station_connected' }
            : { ready: true, sessions: [], records: [] };
          window.__bmsNativeReply(id, JSON.stringify(window.links[link]
            ? { ok: true, body: value } : { ok: false, error: `${link} unavailable` }));
        }),
      };
    });
    await page.goto('http://127.0.0.1:8787/');
    const chips = page.locator('.hero-top .chip');
    const expectLinks = async (ble, http) => {
      await page.waitForFunction(([ble, http]) => {
        const chips = [...document.querySelectorAll('.hero-top .chip')];
        return chips.length === 2 && chips[0].classList.contains('accent') === ble
          && chips[1].classList.contains('accent') === http;
      }, [ble, http]);
      assert((await chips.nth(0).getAttribute('aria-label')).includes('蓝牙'));
      assert((await chips.nth(1).getAttribute('aria-label')).includes('Wi-Fi'));
      assert.equal(await page.locator('p.message').count(), 0);
    };
    await expectLinks(false, false);
    assert.equal(await page.locator('.toast').count(), 0);
    await page.getByRole('button', { name: '设置', exact: true }).click();
    await page.getByRole('button', { name: '刷新', exact: true }).click();
    await expectLinks(false, true);
    // The status is the phone–ESP32 link, independent of the ESP32 station state.
    await page.evaluate(() => { window.links.http = false; });
    await expectLinks(false, false);
    await page.getByRole('button', { name: '设置', exact: true }).click();
    assert.equal(await page.locator('p.message').count(), 0);
    await page.getByRole('button', { name: '爱车', exact: true }).click();
    await page.getByRole('button', { name: '最近骑行，查看全部记录', exact: true }).click();
    const history = page.getByRole('dialog', { name: '骑行记录', exact: true });
    await history.waitFor();
    assert.equal(await history.locator('p.message').count(), 0);
    await history.getByRole('button', { name: '返回', exact: true }).click();
    await history.waitFor({ state: 'detached' });
    await page.locator('#scroll').evaluate(el => el.scrollTop = 0);
    await page.getByRole('button', { name: '设置', exact: true }).click();
    await page.getByRole('button', { name: '刷新', exact: true }).click();
    await page.locator('.toast').filter({ hasText: 'http unavailable' }).waitFor();
    await page.locator('.toast').waitFor({ state: 'detached', timeout: 6000 });
    console.log('PASS: Bluetooth/Wi-Fi order, HTTP link status, disconnect polling, no inline status DOM, transient errors');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
