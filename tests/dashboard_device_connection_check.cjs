const assert = require('node:assert/strict');
let chromium;
try { ({ chromium } = require('playwright')); }
catch { ({ chromium } = require('/vol1/1000/.tools/battery-browser/node_modules/playwright')); }
const settings = require('../vercel/src/preview-settings.json');
(async () => {
 const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
 try {
  const page = await browser.newPage({ viewport: { width: 390, height: 844 }, serviceWorkers: 'block' });
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(items => {
   window.sent = []; window.deviceStatus = { bms: 'disconnected', controller_online: false }; window.failConfig = false;
   window.deviceConfig = { bms_type: 'ant', controller_type: 'fardriver', ant_channel: 0 };
   localStorage.setItem('bms.dashboard.home-layout.v1', JSON.stringify([{id:'vehicle',width:4,height:3},{id:'metrics',width:4,height:2},{id:'bms',width:2,height:1},{id:'controller',width:2,height:1},{id:'gps',width:4,height:3}]));
   window.BmsNative = { getAppInfo: () => '{}', bleConnected: () => false, openNative: () => {}, request(id, method, path, body) {
    let result = {}, ok = true;
    const value = body ? JSON.parse(body) : {};
    if (method === 'POST') window.sent.push({ path, body: value });
    if (path === '/api/config') {
     if (method === 'POST') { if(window.failConfig) ok = false; else Object.assign(window.deviceConfig, value); }
     result = window.deviceConfig;
    }
    if (path === '/api/status') result = window.deviceStatus;
    if (path === '/api/settings/manifest') result = { protocol_version: 1, sections: [{ id: 'device', items }, { id: 'bms', items: [{id:'bms_mac',kind:'choice',label:{zh:'蓝牙连接',en:'Bluetooth'}}] }, {id:'controller',items:[{id:'controller_mac',kind:'choice',label:{zh:'蓝牙连接',en:'Bluetooth'}}]}] };
    if (path.startsWith('/api/controls')) result = {controls:[]};
    if (path === '/api/history/sessions') result = {ready:true,sessions:[]};
    if (path.endsWith('/candidates')) result = {candidates:[{mac:'AA:BB:CC:DD:EE:FF',name:'测试设备',rssi:-48},{mac:'invalid',name:'invalid'}]};
    if (path.endsWith('/bind')) { if(path.includes('/bms/')) window.deviceStatus.bms = 'connected'; else window.deviceStatus.controller_online = true; }
    queueMicrotask(() => window.__bmsNativeReply(id, JSON.stringify(ok ? {ok:true,body:result} : {ok:false,error:'save failed'})));
   }};
  }, settings);
  await page.goto(process.env.DASHBOARD_URL || 'http://127.0.0.1:8787/');
  const home = page.locator('.home-dashboard');
  assert.equal(await home.locator('[data-home-id="metrics"]').count(),0, 'obsolete metrics card disappears from saved layouts');
  await home.locator('[data-home-id="bms"] button').click();
  let modal = page.getByRole('dialog',{name:'保护板连接设置',exact:true}); await modal.waitFor();
  assert.equal(await modal.locator('.route-page').count(),0);
  assert(await modal.getByRole('button',{name:'保存设置',exact:true}).isDisabled());
  await modal.getByRole('button',{name:'取消',exact:true}).click(); await modal.waitFor({state:'detached'});
  await page.getByRole('button',{name:'刷新',exact:true}).click();
  await page.waitForFunction(()=>document.querySelector('.hero-top .chip:last-child')?.classList.contains('accent'));
  await home.locator('[data-home-id="bms"] button').click(); await modal.waitFor();
  assert.equal(await modal.getByLabel('蓝宝配对码',{exact:true}).count(),0);
  await modal.getByRole('button',{name:/^设备类型:/}).click();
  await modal.getByRole('button',{name:'蓝宝电量模块',exact:true}).click();
  assert.equal(await modal.getByRole('button',{name:/^通信通道:/}).count(),0);
  await modal.getByLabel('蓝宝配对码',{exact:true}).fill('123456');
  await modal.getByRole('button',{name:'扫描附近设备',exact:true}).click();
  await modal.getByRole('button',{name:'查看扫描结果',exact:true}).click();
  assert.equal(await modal.locator('.connection-candidate').count(),1);
  await modal.locator('.connection-candidate').click();
  await page.locator('.toast').waitFor({state:'detached'});
  const saveBox=await modal.getByRole('button',{name:'保存并连接',exact:true}).boundingBox();const modalBox=await modal.boundingBox();assert(saveBox.y+saveBox.height<=modalBox.y+modalBox.height,'save action stays inside the modal');
  await modal.screenshot({path:'preview/app-bms-connection.png'});
  for(const viewport of [{width:320,height:568},{width:390,height:600}]){
   await page.setViewportSize(viewport);
   assert(await modal.locator('.device-connection').evaluate(el=>el.scrollWidth<=el.clientWidth+1));
   await modal.getByRole('button',{name:'保存并连接',exact:true}).scrollIntoViewIfNeeded();
   const box=await modal.getByRole('button',{name:'保存并连接',exact:true}).boundingBox();assert(box&&box.y>=0&&box.y+box.height<=viewport.height);
  }
  await page.setViewportSize({width:390,height:844});
  await modal.getByRole('button',{name:'保存并连接',exact:true}).click(); await modal.waitFor({state:'detached'});
  let sent=await page.evaluate(()=>window.sent);
  assert.deepEqual(sent.filter(item=>item.path==='/api/config').at(-1).body,{bms_type:'bluebabe',bluebabe_code:'123456'});
  assert.deepEqual(sent.filter(item=>item.path==='/api/bms/bind').at(-1).body,{mac:'AA:BB:CC:DD:EE:FF'});
  await home.locator('[data-home-id="bms"] button').click();
  const status=page.getByRole('dialog',{name:'保护板',exact:true});await status.waitFor();
  assert(await status.getByRole('heading',{name:'保护板状态',exact:true}).isVisible());
  await status.getByRole('button',{name:'返回',exact:true}).click();await status.waitFor({state:'detached'});
  await home.locator('[data-home-id="controller"] button').click();
  modal=page.getByRole('dialog',{name:'控制器连接设置',exact:true});await modal.waitFor();
  assert.equal(await modal.getByLabel('蓝宝配对码',{exact:true}).count(),0);
  await modal.getByRole('button',{name:/^设备类型:/}).click();
  await modal.getByRole('button',{name:'远驱 FarDriver',exact:true}).click();
  await modal.getByLabel('蓝牙地址',{exact:true}).fill('AA:BB:CC:DD:EE:FF');
  await page.locator('.toast').waitFor({state:'detached'});
  await modal.screenshot({path:'preview/app-controller-connection.png'});
  await page.evaluate(()=>window.failConfig=true);
  const before=await page.evaluate(()=>window.sent.filter(item=>item.path.endsWith('/bind')).length);
  await modal.getByRole('button',{name:'保存并连接',exact:true}).click();
  await page.locator('.toast').filter({hasText:'save failed'}).waitFor();
  assert.equal(await page.evaluate(()=>window.sent.filter(item=>item.path.endsWith('/bind')).length),before,'failed settings must never bind with the wrong type');
  assert(await modal.isVisible());
  await page.evaluate(()=>window.failConfig=false);
  await modal.getByRole('button',{name:'保存并连接',exact:true}).click();await modal.waitFor({state:'detached'});
  sent=await page.evaluate(()=>window.sent);
  assert.deepEqual(sent.filter(item=>item.path==='/api/controller/bind').at(-1).body,{mac:'AA:BB:CC:DD:EE:FF'});
  await home.locator('[data-home-id="controller"] button').click();
  const controller=page.getByRole('dialog',{name:'控制器',exact:true});await controller.waitFor();
  assert(await controller.getByRole('heading',{name:'控制器状态',exact:true}).isVisible());
  await controller.getByRole('button',{name:'返回',exact:true}).click();await controller.waitFor({state:'detached'});
  await page.getByRole('button',{name:'编辑首页',exact:true}).click();await page.getByRole('button',{name:'新增卡片',exact:true}).click();
  assert.equal(await page.getByRole('dialog').getByText('实时数据',{exact:true}).count(),0);
  assert.deepEqual(errors,[]);
  const web = await browser.newPage({viewport:{width:390,height:844},serviceWorkers:'block'});
  await web.addInitScript(()=>{
   window.localConnections=0;window.localDisconnects=0;
   Object.defineProperty(navigator,'bluetooth',{configurable:true,value:{requestDevice:async()=>{
    const device=new EventTarget();device.name='电脑蓝牙测试设备';device.gatt={connect:async()=>{window.localConnections++;},disconnect:()=>{window.localDisconnects++;}};return device;
   }}});
  });
  await web.goto(process.env.DASHBOARD_PREVIEW_URL || 'http://127.0.0.1:8787/');
  await web.locator('[data-home-id="controller"] button').click();
  const local=web.getByRole('dialog',{name:'控制器连接设置',exact:true});await local.waitFor();
  await local.getByRole('button',{name:'本机蓝牙',exact:true}).click();
  await local.getByRole('button',{name:'选择电脑蓝牙设备',exact:true}).click();
  await local.getByText('电脑蓝牙测试设备',{exact:true}).waitFor();
  assert.equal(await web.evaluate(()=>window.localConnections),1);
  assert(await local.getByRole('button',{name:'保存设置',exact:true}).isDisabled(), 'local BLE does not enable panel settings without a connected panel');
  await local.getByRole('button',{name:'取消',exact:true}).click();await local.waitFor({state:'detached'});
  assert.equal(await web.evaluate(()=>window.localDisconnects),1);
  await web.locator('[data-home-id="controller"] button').click();await local.waitFor();
  assert.equal(await web.getByRole('dialog',{name:'控制器',exact:true}).count(),0,'local browser BLE must not pretend the ESP peripheral is online');
  await web.close();
  console.log('PASS: offline connection forms, online status routing, old metrics removal, type/pairing fields, ESP scan/bind/save ordering, failed save protection and narrow/short layouts');
 } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1;});
