// Run against the Vite preview on port 8787.
const assert = require('node:assert/strict');
const {chromium} = require('/vol1/1000/.tools/battery-browser/node_modules/playwright');

(async () => {
  const browser = await chromium.launch({headless: true, args: ['--no-sandbox']});
  try {
    for (const width of [320, 390]) {
      const page = await browser.newPage({viewport: {width, height: 740}, serviceWorkers: 'block'});
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.addInitScript(() => {
        window.companionCalls = [];
        window.BmsNative = {
          getAppInfo: () => '{}', bleConnected: () => false,
          openCompanion: (page, language) => window.companionCalls.push([page, language]),
        };
      });
      await page.goto('http://127.0.0.1:8787/');
      await page.getByRole('button', {name: '功能', exact: true}).click();
      const card = page.locator('.companion-card');
      await card.getByRole('button', {name: /音乐与歌词/}).click();
      await card.getByRole('button', {name: /实时导航/}).click();
      assert.deepEqual(await page.evaluate(() => window.companionCalls), [['music', 'zh'], ['navigation', 'zh']]);
      assert(await card.evaluate(el => el.scrollWidth <= el.clientWidth + 1));
      await page.screenshot({path: `preview/companion-web-features-${width}.png`});
      await page.getByRole('button', {name: '设置', exact: true}).click();
      await page.getByRole('button', {name: '同步与地图设置', exact: true}).click();
      assert.deepEqual((await page.evaluate(() => window.companionCalls)).at(-1), ['settings', 'zh']);
      assert.deepEqual(errors, []);
      await page.close();
    }
    const browserPage = await browser.newPage();
    await browserPage.goto('http://127.0.0.1:8787/');
    await browserPage.getByRole('button', {name: '功能', exact: true}).click();
    await browserPage.locator('.companion-card').getByRole('button', {name: /音乐与歌词/}).click();
    await browserPage.getByText('请使用新版 Android App 打开音乐与导航', {exact: true}).waitFor();
    console.log('Companion entry points, native bridge arguments, 320/390 px layout, and browser fallback passed');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
