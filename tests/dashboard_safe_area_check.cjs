const assert = require('node:assert/strict');
const {chromium} = require('/vol1/1000/.tools/battery-browser/node_modules/playwright');
(async () => {
  const browser = await chromium.launch({headless:true,args:['--no-sandbox']});
  try {
    for (const [width,height] of [[320,568],[390,844]]) {
      const page = await browser.newPage({viewport:{width,height},serviceWorkers:'block'});
      await page.goto('http://127.0.0.1:8787/');
      await page.addStyleTag({content:':root { --sat-raw: 59px; --sab-raw: 34px; }'});
      await page.waitForFunction(() => parseFloat(getComputedStyle(document.querySelector('#scroll')).paddingTop) >= document.querySelector('#topbar').offsetHeight);
      const layout = await page.evaluate(() => {
        const phone=document.querySelector('#phone'), bar=document.querySelector('#topbar');
        return {safeTop:parseFloat(getComputedStyle(bar).paddingTop),safeBottom:parseFloat(getComputedStyle(document.querySelector('#scroll')).paddingBottom),top:phone.getBoundingClientRect().top};
      });
      assert(layout.safeTop>=69,'header must include the full iOS notch inset');
      assert(layout.safeBottom>=34,'scroll content must clear the home indicator');
      await page.locator('[data-home-id="cast"] button').click();
      const dialog=page.getByRole('dialog',{name:'投屏',exact:true});
      await dialog.waitFor();
      await page.waitForFunction(() => document.querySelector('.dashboard-sheet.show')?.getBoundingClientRect().top <= document.querySelector('#phone').getBoundingClientRect().top + 60);
      const back=await dialog.getByRole('button',{name:'返回',exact:true}).boundingBox();
      assert(back.y >= layout.top+59,'secondary page back button must clear the status bar');
      await page.screenshot({path:`preview/safe-area-${width}.png`});
      await page.close();
    }
    const native = await browser.newPage({viewport:{width:390,height:844},serviceWorkers:'block'});
    await native.addInitScript(() => {
      localStorage.setItem('bms.dashboard.theme', JSON.stringify({theme:'dark',themeV:2,accentV:2}));
      window.bars=[];
      window.BmsNative={getAppInfo:()=>'{}',bleConnected:()=>false,setSystemBars:(color,light)=>window.bars.push({color,light})};
    });
    await native.goto('http://127.0.0.1:8787/');
    await native.waitForFunction(() => window.bars.length > 0);
    assert.equal(await native.evaluate(() => window.bars.at(-1).light),false);
    await native.getByRole('button',{name:'主题',exact:true}).click();
    await native.getByRole('button',{name:/^浅色/}).click();
    await native.waitForFunction(() => window.bars.at(-1).light === true);
    assert(await native.evaluate(() => /^#[0-9a-f]{6}$/i.test(window.bars.at(-1).color)));
    await native.close();
    console.log('PASS: system bars follow light/dark page colors; 59px notch / 34px home indicator, header, scroll and secondary page at 320/390px');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
