// Production UI: explicit actions, selected-source firmware downloads, small viewports and focus restoration.
const assert = require('node:assert/strict');
const fs = require('node:fs');
let playwright;try {playwright=require('playwright');} catch {playwright=require('/vol1/1000/.tools/battery-browser/node_modules/playwright');}
(async()=>{
  const browser=await playwright.chromium.launch({headless:true,args:['--no-sandbox']});
  try {
    for (const viewport of [{width:320,height:568},{width:390,height:844},{width:844,height:390}]) {
      const context=await browser.newContext({viewport,serviceWorkers:'block'});const page=await context.newPage();const errors=[],requests=[];
      page.on('pageerror',error=>errors.push(error.message));
      await context.route('**/firmware/firmware.json',route=>{requests.push(route.request().url());return route.fulfill({contentType:'application/json',body:JSON.stringify({firmwares:[{profile:'esp32',name:'ESP32 测试固件',url:'/firmware/esp32.bin',version:'fixture',code:'1234'}]})});});
      await context.route('**/firmware/esp32.bin',route=>{requests.push(route.request().url());return route.fulfill({contentType:'application/octet-stream',body:Buffer.from('test fixture, never uploaded to hardware')});});
      await page.goto(process.env.DASHBOARD_URL||'http://127.0.0.1:8787/');
      await page.getByRole('button',{name:'设置',exact:true}).click();
      const entry=page.getByRole('button',{name:'固件更新 · 查看更新',exact:true});await entry.click();
      let dialog=page.getByRole('dialog',{name:'固件更新',exact:true});await dialog.waitFor();
      assert.equal(requests.length,0,'opening a card must not check or download');
      await dialog.getByRole('button',{name:'GitHub',exact:true}).click();assert.equal(requests.length,0);
      const check=dialog.getByRole('button',{name:'检查更新',exact:true});await check.click();
      await dialog.getByText('找到 1 款固件',{exact:true}).waitFor();assert(requests[0].startsWith('https://raw.githubusercontent.com/wintsa123/espBmsSetting/updates/firmware/'));
      await dialog.locator('select').selectOption('esp32');assert.equal(requests.length,1,'choosing a firmware must not download it');
      await dialog.getByRole('button',{name:'下载所选固件',exact:true}).click();await dialog.getByText('已准备 · esp32.bin · 0 KB',{exact:true}).waitFor();
      assert.equal(requests[1],'https://raw.githubusercontent.com/wintsa123/espBmsSetting/updates/firmware/esp32.bin');
      await dialog.getByRole('button',{name:'Cloudflare',exact:true}).click();assert.equal(await dialog.getByText('已准备 · esp32.bin · 0 KB',{exact:true}).count(),0,'source changes discard downloaded online bytes');
      assert(await dialog.evaluate(el=>el.scrollWidth<=el.clientWidth+1),'no horizontal dialog overflow');
      await dialog.getByRole('button',{name:'关闭 固件更新',exact:true}).click();await dialog.waitFor({state:'detached'});
      assert(await entry.evaluate(el=>el===document.activeElement),'closing restores focus to the card');
      await entry.scrollIntoViewIfNeeded();
      fs.mkdirSync('preview',{recursive:true});await page.screenshot({path:`preview/updates-${viewport.width}x${viewport.height}.png`});
      assert.deepEqual(errors,[]);await context.close();
    }
    console.log('PASS: 320px, portrait and short landscape; explicit check/download; selected-source firmware; reset stale file; close/focus; no horizontal overflow');
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1});
