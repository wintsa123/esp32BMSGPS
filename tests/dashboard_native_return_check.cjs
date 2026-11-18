const assert = require('node:assert/strict');
const {chromium} = require('/vol1/1000/.tools/battery-browser/node_modules/playwright');
(async () => {
  const browser = await chromium.launch({headless:true,args:['--no-sandbox']});
  try {
    const page = await browser.newPage({viewport:{width:390,height:844},serviceWorkers:'block'});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.addInitScript(()=>{
      window.nativeCalls=[];window.bleState={state:'DISCONNECTED'};
      window.BmsNative={getAppInfo:()=>'{"versionCode":6,"versionName":"0.4.2"}',bleConnected:()=>false,
        getBleState:()=>JSON.stringify(window.bleState),
        openConnectionSettings:kind=>window.nativeCalls.push(['settings',kind]),
        connectBle:()=>{window.nativeCalls.push(['ble']);window.bleState={state:'ERROR',error:'面板蓝牙连接失败'};},
        request:(id,method,path)=>{window.nativeCalls.push(['request',path]);queueMicrotask(()=>window.__bmsNativeReply?.(id,JSON.stringify({ok:false,error:'未连接面板热点'})));}};
    });
    await page.goto('http://127.0.0.1:8787/');
    await page.getByRole('button',{name:'设置',exact:true}).click();
    const wifi=page.getByRole('button',{name:'连接 Wi-Fi 设备',exact:true});
    const ble=page.getByRole('button',{name:'连接面板蓝牙',exact:true});
    await wifi.click();
    assert.deepEqual(await page.evaluate(()=>window.nativeCalls),[['settings','http']],'do not query while the user is in system settings');
    await page.evaluate(()=>{window.pageMarker=crypto.randomUUID();window.bmsConnectionSettingsClosed?.();window.bmsConnectionSettingsClosed?.();});
    await page.getByText('未连接面板热点',{exact:true}).waitFor();
    assert(await wifi.isEnabled());
    assert.deepEqual(await page.evaluate(()=>window.nativeCalls),[['settings','http'],['request','/api/status']],'one failed probe, no manifest/history retry loop');
    await ble.click();await page.evaluate(()=>window.bmsConnectionSettingsClosed?.());
    await page.getByText('面板蓝牙连接失败',{exact:true}).waitFor();
    assert(await ble.isEnabled());
    assert.equal(await page.evaluate(()=>!!window.pageMarker),true,'return must preserve the page');
    assert.equal(await page.locator('.topbar .loading-ring').count(),0,'connection errors must stop refresh UI');
    await page.evaluate(()=>{window.BmsNative.connectBle=()=>window.nativeCalls.push(['ble-timeout']);window.bleState={state:'CONNECTING'};});
    await page.clock.install();
    await ble.click();await page.evaluate(()=>window.bmsConnectionSettingsClosed?.());
    await page.clock.fastForward(21000);
    await page.getByText('未找到面板蓝牙，请确认面板已开机后重试',{exact:true}).waitFor();
    assert(await ble.isEnabled(),'even a missing native terminal event must end waiting');
    assert.deepEqual(errors,[]);
    console.log('PASS: settings return once; failed Wi-Fi probe stops; BLE errors and deadline end waiting; page state retained');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
