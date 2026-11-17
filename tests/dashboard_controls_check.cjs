const assert = require('node:assert/strict');
let chromium;
try { ({ chromium } = require('playwright')); }
catch { ({ chromium } = require('/vol1/1000/.tools/battery-browser/node_modules/playwright')); }
(async () => {
  const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
  try {
    const page = await browser.newPage({ viewport: { width: 390, height: 844 }, serviceWorkers: 'block' });
    await page.addInitScript(() => {
      window.sent = [];
      window.BmsNative = { getAppInfo: () => '{}', bleConnected: () => false, openNative: () => {}, request: (id, method, path, body) => queueMicrotask(() => {
        let result = {};
        if (path === '/api/settings/manifest') result = { protocol_version: 1, sections: [{id:'device', label:{zh:'设备',en:'Device'},submit:{endpoint:'/api/config',method:'POST'}, items:[
          {id:'brightness',kind:'range',label:{zh:'亮度',en:'Brightness'},value:80,min:10,max:100,step:1},
          {id:'bms_type',kind:'select',label:{zh:'保护板类型',en:'BMS type'},value:'ant',options:[{value:'ant',label:{zh:'蚂蚁 ANT',en:'ANT'}},{value:'jk',label:{zh:'极空 JK',en:'JK'}}]},
          {id:'language',kind:'select',label:{zh:'语言',en:'Language'},value:'zh',options:[{value:'zh',label:{zh:'中文',en:'Chinese'}}]},
        ]}, {id:'bms',label:{zh:'BMS',en:'BMS'}, items:[{id:'bms_mac',kind:'choice',label:{zh:'蓝牙连接',en:'Bluetooth'},value:null,options_endpoint:'/api/bms/candidates',submit_endpoint:'/api/bms/bind',submit_key:'mac'}]}] };
        if (path === '/api/settings/manifest') result.sections.push({id:'update',label:{zh:'固件更新',en:'Firmware update'},items:[{id:'firmware_file',kind:'upload',label:{zh:'上传固件',en:'Upload firmware'},endpoint:'/api/ota',code_header:'X-OTA-Code',accept:'.bin'}]});
        if (path === '/api/status') result = {gps_fix:true,gps_satellite_info_valid:true,gps_fix_dimension:3,gps_constellation_mask:7,gps_satellites_visible:18,gps_satellites_used:9,gps_average_cn0:32,gps_hdop_valid:true,gps_hdop_centi:85};
        if (path === '/api/config') result = { bms_type: 'ant', ant_channel: 1 };
        if (path.startsWith('/api/controls?')) result = { controls: path.endsWith('vehicle') ? [{ id: 'ratio', zh: '传动比 × 100', en: 'Gear ratio × 100', value: 100, min: 50, max: 1000, step: 1 }] : path.endsWith('display') ? [{ id: 'dashboard_style', zh: '仪表样式（0 宝马 / 1 控制器 / 2 本田）', en: 'Dashboard style', value: 0, min: 0, max: 2, step: 1 }, { id: 'dashboard', zh: '显示仪表首页', en: 'Show dashboard' }] : path.endsWith('system') ? [{ id: 'restore', zh: '恢复默认设置', en: 'Restore defaults', confirm: true }] : [] };
        if (path === '/api/bms/candidates') result = {candidates:[{mac:'AA:BB:CC:DD:EE:FF',name:'测试保护板',rssi:-48}]};
        if (method === 'POST') window.sent.push({path, body: JSON.parse(body || '{}')});
        window.__bmsNativeReply(id, JSON.stringify({ ok: true, body: result }));
      }) };
    });
    await page.goto(process.env.DASHBOARD_URL || 'http://127.0.0.1:8788/');
    await page.getByRole('button', { name: '刷新', exact: true }).click();
    await page.route('**/firmware/firmware.json', route => route.fulfill({json:{firmwares:[]}}));
    await page.getByRole('button', { name: '设置', exact: true }).click();
    await page.getByRole('button', { name: '固件更新 · 查看更新', exact: true }).click();
    const firmware = page.getByRole('dialog', {name:'固件更新',exact:true});
    await firmware.locator('input[type="file"]').waitFor({state:'visible'});
    assert.equal(await firmware.locator('.sheet-head button').count(),0);
    await page.keyboard.press('Escape');await firmware.waitFor({state:'detached'});
    await page.getByRole('button', { name: '功能', exact: true }).click();
    const controls = page.getByRole('region', { name: '面板功能控制' });
    assert.equal(await controls.getByRole('button', { name: '读取 / 刷新' }).count(), 0);
    await controls.locator('#control-brightness').waitFor({state:'attached'});
    assert.equal(await controls.locator('#control-restore').evaluate(row => row === row.parentElement.querySelector('.feature-row:last-child')), true, 'read device controls also place destructive operations last');
    await controls.locator('summary').filter({hasText:'GPS',exact:true}).click();
    for(const value of ['3D 定位，测速精准','GPS+BDS+GLONASS 三模','18 可见 / 9 有效，测速稳定','32 dBHz，越大越好','0.85，优秀']) await controls.getByText(value,{exact:true}).waitFor();
    await controls.locator('summary').filter({hasText:'控制器'}).click();
    await controls.locator('#control-ratio').click();
    const editor = page.getByRole('dialog');
    const ratio = editor.getByRole('spinbutton');
    await ratio.fill('250');
    await editor.getByRole('button', { name: '应用', exact: true }).click();
    await page.waitForFunction(() => window.sent.length === 1);
    assert.deepEqual(await page.evaluate(() => window.sent[0]), { path: '/api/controls', body: { action: 'ratio', value: 250 } });
    await ratio.fill('1001');
    await editor.getByRole('button', { name: '应用', exact: true }).click();
    assert.equal(await page.evaluate(() => window.sent.length), 1);
    await editor.getByRole('button',{name:'取消',exact:true}).click();
    await editor.waitFor({state:'detached'});
    await controls.locator('summary').filter({hasText:'系统'}).click();
    await controls.locator('#control-restore').click();
    assert.equal(await page.evaluate(() => window.sent.length), 1);
    await editor.getByRole('button', { name: '取消', exact: true }).click();
    await editor.waitFor({state:'detached'});
    await controls.locator('#control-restore').click();
    await editor.getByRole('button', { name: '确认执行', exact: true }).click();
    await page.waitForFunction(() => window.sent.length === 2);
    await editor.waitFor({state:'detached'});
    await controls.locator('summary').filter({hasText:'BMS 设置'}).click();
    await controls.locator('#control-ant_channel').click();
    await editor.getByRole('button',{name:'应用',exact:true}).click();
    await page.waitForFunction(() => window.sent.length === 3);
    assert.deepEqual(await page.evaluate(() => window.sent[2].body), { ant_channel: 1 });
    await page.keyboard.press('Escape');await editor.waitFor({state:'detached'});
    await controls.locator('summary').filter({hasText:'仪表'}).click();
    assert(!(await controls.locator('#control-dashboard_style').innerText()).includes('0 宝马'));
    await controls.locator('#control-dashboard_style').click();
    await editor.getByRole('radio',{name:'本田火刃',exact:true}).click();
    await editor.getByRole('button',{name:'应用',exact:true}).click();
    await page.waitForFunction(()=>window.sent.length===4);
    assert.deepEqual(await page.evaluate(()=>window.sent[3]),{path:'/api/controls',body:{action:'dashboard_style',value:2}});
    await page.keyboard.press('Escape');await editor.waitFor({state:'detached'});
    assert.equal(await controls.locator('#control-dashboard').count(),0,'firmware navigation action excluded');
    await controls.locator('#control-bms_type').click();
    await editor.getByRole('radio',{name:'极空 JK',exact:true}).check();
    await editor.getByRole('button',{name:'应用',exact:true}).click();
    await page.waitForFunction(()=>window.sent.length===5);
    assert.deepEqual(await page.evaluate(()=>window.sent[4]),{path:'/api/config',body:{bms_type:'jk'}});
    await page.keyboard.press('Escape');await editor.waitFor({state:'detached'});
    await controls.locator('#control-brightness').click();
    await editor.getByRole('spinbutton').fill('55');await editor.getByRole('button',{name:'应用',exact:true}).click();
    await page.waitForFunction(()=>window.sent.length===6);
    assert.deepEqual(await page.evaluate(()=>window.sent[5]),{path:'/api/config',body:{brightness:55}});
    await page.keyboard.press('Escape');await editor.waitFor({state:'detached'});
    await controls.locator('#control-bms_mac').click();
    await editor.getByRole('button',{name:'扫描设备',exact:true}).click();
    await editor.getByRole('button',{name:'刷新列表',exact:true}).click();
    await editor.getByRole('button',{name:/测试保护板/}).click();
    await editor.getByRole('button',{name:'绑定所选设备',exact:true}).click();
    await editor.getByRole('button',{name:'确认执行',exact:true}).click();
    await page.waitForFunction(()=>window.sent.length===8);
    assert.deepEqual(await page.evaluate(()=>window.sent[6]),{path:'/api/bms/scan',body:{}});
    assert.deepEqual(await page.evaluate(()=>window.sent[7]),{path:'/api/bms/bind',body:{mac:'AA:BB:CC:DD:EE:FF'}});
    await page.keyboard.press('Escape');await editor.waitFor({state:'detached'});
    await controls.scrollIntoViewIfNeeded();
    await page.screenshot({ path: 'preview/app-device-controls.png', fullPage: true });
    await page.setViewportSize({ width: 320, height: 568 });
    assert(await controls.evaluate(el => el.scrollWidth <= el.clientWidth));
    console.log('PASS: control readback, numeric payload/bounds, destructive confirmation/cancel, ANT config, narrow layout');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
