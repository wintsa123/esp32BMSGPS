// Run against the Vite preview on port 8787.
const assert = require('node:assert/strict');
const {chromium} = require('/vol1/1000/.tools/battery-browser/node_modules/playwright');
const storageKey = 'bms.dashboard.home-layout.v1';
const url = 'http://127.0.0.1:8787/';

(async () => {
  const browser = await chromium.launch({headless: true, args: ['--no-sandbox']});
  try {
    for (const width of [320, 390]) {
      const page = await browser.newPage({viewport: {width, height: 568}, serviceWorkers: 'block'});
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.addInitScript(() => {
        window.castCalls = [];
        window.BmsNative = {getAppInfo: () => '{}', bleConnected: () => false,
          openNative: route => window.castCalls.push(route)};
      });
      await page.goto(url);
      const cast = page.locator('[data-home-id="cast"]');
      await cast.getByRole('button', {name: '投屏', exact: true}).click();
      assert.deepEqual(await page.evaluate(() => window.castCalls), ['CAST']);
      assert(await cast.evaluate(el => el.scrollWidth <= el.clientWidth));
      await page.screenshot({path: `preview/cast-home-${width}.png`});
      await page.getByRole('button', {name: '设置', exact: true}).click();
      await page.getByRole('button', {name: '投屏设置', exact: true}).click();
      assert.deepEqual(await page.evaluate(() => window.castCalls), ['CAST', 'CAST']);
      assert(await page.locator('.panel.active').evaluate(el => el.scrollWidth <= el.clientWidth));
      await page.screenshot({path: `preview/cast-settings-${width}.png`});
      assert.deepEqual(errors, []);
      await page.close();
    }

    const page = await browser.newPage({serviceWorkers: 'block'});
    await page.goto(url);
    const original = [{id: 'controller', width: 2, height: 2}, {id: 'vehicle', width: 4, height: 3}];
    await page.evaluate(({storageKey, original}) => {
      localStorage.setItem(storageKey, JSON.stringify(original));
      localStorage.removeItem(`${storageKey}.cast`);
    }, {storageKey, original});
    await page.reload();
    const migrated = await page.evaluate(key => JSON.parse(localStorage.getItem(key)), storageKey);
    assert.deepEqual(migrated.slice(0, 2), original, 'keep custom order and card sizes');
    assert.equal(migrated.filter(item => item.id === 'cast').length, 1, 'add the entry once');
    await page.locator('[data-home-id="cast"] button').click();
    await page.getByText('请在 Android App 中使用投屏', {exact: true}).waitFor();
    await page.locator('#scroll').evaluate(el => el.scrollTop = 0);
    await page.getByRole('button', {name: '编辑首页', exact: true}).click();
    await page.getByRole('button', {name: '删除 投屏', exact: true}).click();
    await page.getByRole('button', {name: '完成', exact: true}).click();
    await page.reload();
    assert.equal(await page.locator('[data-home-id="cast"]').count(), 0, 'do not restore a deleted entry');
    assert.deepEqual(await page.evaluate(key => JSON.parse(localStorage.getItem(key)), storageKey), original);
    await page.getByRole('button', {name: '设置', exact: true}).click();
    await page.getByRole('button', {name: '投屏设置', exact: true}).click();
    await page.getByText('请在 Android App 中使用投屏', {exact: true}).waitFor();
    await page.close();
    console.log('PASS: both cast entry points, native CAST route, 320/390 px short screens, legacy layout migration, deletion persistence, browser fallback');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
