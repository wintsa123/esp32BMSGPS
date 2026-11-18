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
        window.castState = {state: 'stopped'};
        window.BmsNative = {getAppInfo: () => '{}', bleConnected: () => false,
          openNative: () => { throw new Error('legacy native screen must never open'); },
          getCastState: () => JSON.stringify(window.castState),
          startCast: host => { window.castCalls.push(['start', host]); window.castState = {state:'starting'}; },
          stopCast: () => { window.castCalls.push(['stop']); window.castState = {state:'stopped'}; }};
      });
      await page.goto(url);
      const cast = page.locator('[data-home-id="cast"]');
      await cast.getByRole('button', {name: '投屏', exact: true}).click();
      const dialog = page.getByRole('dialog', {name:'投屏',exact:true});
      await dialog.getByRole('button', {name:'开始投屏',exact:true}).click();
      assert.deepEqual(await page.evaluate(() => window.castCalls), [['start','http://192.168.4.1']]);
      await page.evaluate(() => window.castState = {state:'stopped',detail:'屏幕共享已取消'});
      await dialog.getByText('屏幕共享已取消',{exact:true}).waitFor();
      assert(await dialog.getByRole('button',{name:'开始投屏',exact:true}).isEnabled());
      assert(await dialog.evaluate(el => el.scrollWidth <= el.clientWidth));
      await page.screenshot({path: `preview/cast-home-${width}.png`});
      await dialog.getByRole('button',{name:'返回',exact:true}).click();
      await dialog.waitFor({state:'detached'});
      await page.getByRole('button', {name: '功能', exact: true}).click();
      await page.getByRole('region', {name: '功能', exact: true}).getByRole('button', {name: '投屏', exact: true}).click();
      await dialog.getByRole('button',{name:'开始投屏',exact:true}).click();
      await page.evaluate(() => window.castState = {state:'streaming'});
      await dialog.getByText('正在向面板投屏',{exact:true}).waitFor();
      await dialog.getByRole('button',{name:'结束投屏',exact:true}).click();
      await dialog.getByRole('button',{name:'开始投屏',exact:true}).waitFor();
      assert.deepEqual(await page.evaluate(() => window.castCalls), [['start','http://192.168.4.1'],['start','http://192.168.4.1'],['stop']]);
      await page.screenshot({path: `preview/cast-features-${width}.png`});
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
    const castPage = page.getByRole('dialog', {name: '投屏', exact: true});
    await castPage.getByRole('button', {name: '开始网页投屏', exact: true}).waitFor();
    await castPage.getByRole('button', {name: '返回', exact: true}).click();
    await castPage.waitFor({state: 'detached'});
    await page.locator('#scroll').evaluate(el => el.scrollTop = 0);
    await page.getByRole('button', {name: '编辑首页', exact: true}).click();
    await page.getByRole('button', {name: '删除 投屏', exact: true}).click();
    await page.getByRole('button', {name: '完成', exact: true}).click();
    await page.reload();
    assert.equal(await page.locator('[data-home-id="cast"]').count(), 0, 'do not restore a deleted entry');
    assert.deepEqual(await page.evaluate(key => JSON.parse(localStorage.getItem(key)), storageKey), original);
    await page.getByRole('button', {name: '功能', exact: true}).click();
    await page.getByRole('region', {name: '功能', exact: true}).getByRole('button', {name: '投屏', exact: true}).click();
    await castPage.getByRole('button', {name: '开始网页投屏', exact: true}).waitFor();
    await page.close();
    console.log('PASS: both cast entry points, web UI with native capture, permission cancellation and stop, 320/390 px short screens, legacy layout migration, deletion persistence, browser casting page');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
