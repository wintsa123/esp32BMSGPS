// HTTPS production build; the HTTP device and BLE peripheral below are explicit test doubles.
const assert = require('node:assert/strict');
const http = require('node:http');
const {chromium} = require('/vol1/1000/.tools/battery-browser/node_modules/playwright');
const url = process.env.DASHBOARD_LAN_URL || 'https://192.168.2.108:8789/';
const deviceHost = process.env.DEVICE_TEST_HOST || new URL(url).hostname;
const requests = [];
const responseFor = path => path === '/api/status' ? {version:'LAN-TEST', bms:'disconnected', controller_online:false}
  : path === '/api/settings/manifest' ? {protocol_version:1,sections:[]}
  : path.startsWith('/api/controls') ? {controls:[]}
  : {ready:true,sessions:[],records:[]};
const device = http.createServer((req, res) => {
  requests.push({method:req.method,url:req.url,origin:req.headers.origin});
  res.setHeader('Access-Control-Allow-Origin', new URL(url).origin);
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'content-type');
  res.setHeader('Access-Control-Allow-Private-Network', 'true');
  res.setHeader('Content-Type', 'application/json');
  res.end(JSON.stringify(responseFor(req.url)));
});

(async () => {
  await new Promise(resolve => device.listen(0, '0.0.0.0', resolve));
  const browser = await chromium.launch({headless:true,args:['--no-sandbox','--enable-experimental-web-platform-features']});
  try {
    const context = await browser.newContext({ignoreHTTPSErrors:true, serviceWorkers:'block',viewport:{width:390,height:740}});
    const page = await context.newPage();
    const errors=[];page.on('pageerror',error=>errors.push(error.message));
    // Grant only this test origin permission to contact the local fixture server.
    const cdp = await context.newCDPSession(page);
    await cdp.send('Browser.grantPermissions',{origin:new URL(url).origin,permissions:['localNetworkAccess']});
    await page.addInitScript(() => {
      window.bleRequests=[];window.bleOptions=[];window.cancelBle=false;
      const encoder=new TextEncoder(),decoder=new TextDecoder();
      const rx=new EventTarget();rx.startNotifications=async()=>rx;
      const peripheral=new EventTarget();peripheral.name='ESP32 BLE TEST';peripheral.id='test-peripheral';
      let pending=[];
      const tx={writeValueWithResponse:async chunk=>{
        if(chunk[0]&1)pending=[];
        pending.push(...chunk.slice(1));
        if(!(chunk[0]&2))return;
        const request=JSON.parse(decoder.decode(new Uint8Array(pending)));window.bleRequests.push(request);
        const body=request.path==='/api/status'?{version:'BLE-TEST',bms:'disconnected'}
          :request.path==='/api/settings/manifest'?{protocol_version:1,sections:[]}
          :request.path.startsWith('/api/controls')?{controls:[]}:{ready:true,sessions:[],records:[]};
        const bytes=encoder.encode(JSON.stringify({id:request.id,status:200,body}));
        const framed=new Uint8Array(bytes.length+1);framed[0]=3;framed.set(bytes,1);rx.value=new DataView(framed.buffer);
        queueMicrotask(()=>rx.dispatchEvent(new Event('characteristicvaluechanged')));
      }};
      peripheral.gatt={connected:false,connect:async()=>{
        peripheral.gatt.connected=true;
        return {getPrimaryService:async uuid=>{window.bleService=uuid;return {getCharacteristic:async id=>id.endsWith('a02')?tx:rx}}};
      },disconnect:()=>{peripheral.gatt.connected=false;peripheral.dispatchEvent(new Event('gattserverdisconnected'))}};
      window.disconnectBle=()=>peripheral.gatt.disconnect();
      Object.defineProperty(navigator,'bluetooth',{value:{requestDevice:async options=>{
        window.bleOptions.push(options);
        if(window.cancelBle)throw new DOMException('Canceled','NotFoundError');
        return peripheral;
      }}});
    });
    await page.goto(url);
    assert(await page.evaluate(()=>isSecureContext));
    await page.getByRole('button',{name:'设置',exact:true}).click();
    await page.getByLabel('设备地址',{exact:true}).fill(`http://${deviceHost}:${device.address().port}`);
    await page.getByRole('button',{name:'连接 Wi-Fi 设备',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('.hero-top .chip:last-child')?.classList.contains('accent'));
    assert(requests.some(req=>req.url==='/api/status'&&req.origin===new URL(url).origin),'real HTTPS-to-HTTP CORS/LAN request');
    assert.equal(await page.getByText('测试预览',{exact:true}).count(),0,'production build must not simulate controls');
    await page.getByRole('button',{name:'连接面板蓝牙',exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('.hero-top .chip:first-child')?.classList.contains('accent'));
    assert.equal(await page.evaluate(()=>window.bleService),'4f91e100-23b2-4ee7-9f9a-86d1093f0a01');
    assert((await page.evaluate(()=>window.bleRequests)).some(req=>req.path==='/api/status'));
    assert.deepEqual(await page.evaluate(()=>window.bleOptions),[{acceptAllDevices:true,optionalServices:['4f91e100-23b2-4ee7-9f9a-86d1093f0a01']}]);
    await page.evaluate(()=>window.disconnectBle());
    await page.waitForFunction(()=>!document.querySelector('.hero-top .chip:first-child').classList.contains('accent'));
    assert(await page.locator('.hero-top .chip').nth(1).evaluate(el=>el.classList.contains('accent')),'BLE disconnect preserves Wi-Fi link');
    await page.screenshot({path:'preview/lan-device-connections.png'});
    assert.deepEqual(errors,[]);
    await context.close();

    const insecure=await browser.newPage({serviceWorkers:'block'});
    await insecure.goto(`http://${deviceHost}:8787/`);
    assert.equal(await insecure.evaluate(()=>isSecureContext),false);
    await insecure.getByRole('button',{name:'设置',exact:true}).click();
    await insecure.getByRole('button',{name:'连接面板蓝牙',exact:true}).click();
    await insecure.getByText('请通过 HTTPS 或 localhost 打开页面，再使用本机蓝牙',{exact:true}).waitFor();
    console.log('PASS: HTTPS production UI, real LAN HTTP/CORS with local-network permission, framed BLE fixture, separate link status, insecure-origin guidance');
  } finally {
    await browser.close();
    await new Promise(resolve=>device.close(resolve));
  }
})().catch(error=>{console.error(error);device.close();process.exitCode=1;});
