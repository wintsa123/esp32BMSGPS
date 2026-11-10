// Run with `node tests/dashboard_updates_check.cjs` (contracts) or `--browser` (real SW lifecycle).
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const vm = require('node:vm');
const http = require('node:http');
const ts = require('../vercel/node_modules/typescript');
const root = path.resolve(__dirname, '..');
const transpile = source => ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 } }).outputText;
function load(file, overrides = {}) {
  const module = { exports: {} };
  vm.runInNewContext(transpile(fs.readFileSync(path.join(root, file), 'utf8').replaceAll('import.meta.env', '({})')), { module, exports: module.exports, require, URL, window: {}, document: {baseURI:'https://example.test/fnconnect/'}, ...overrides });
  return module.exports;
}
const source = load('vercel/src/update-source.ts');
assert.equal(source.updateUrl('apk/latest.json'), 'https://example.test/fnconnect/apk/latest.json');
assert.equal(source.parseApkRelease({versionCode:4,versionName:'4.0',url:'app.apk'}, 'https://example.test/apk/latest.json').url,'https://example.test/apk/app.apk');
for (const value of [null,{}, {versionCode:0,versionName:'v',url:'x'}, {versionCode:1.5,versionName:'v',url:'x'}, {versionCode:2,versionName:'',url:'x'}, {versionCode:2,versionName:'v',url:'javascript:alert(1)'}, {versionCode:2,versionName:'v',url:'https://u:p@example.test/app.apk'}]) assert.throws(()=>source.parseApkRelease(value,'https://example.test/latest.json'));
console.log('PASS: APK manifest trust boundary and fnconnect relative catalog URL');
if (!process.argv.includes('--browser')) process.exit(0);
(async () => {
  const esbuild = require('../vercel/node_modules/esbuild');
  let playwright;
  try { playwright = require('playwright'); } catch { playwright = require('/vol1/1000/.tools/battery-browser/node_modules/playwright'); }
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(),'bms-update-check-'));
  const plugin = load('vercel/tools/offline-plugin.ts');
  let served, badAsset = false, failCatalog = false, failWorker = false;
  const builds = {};
  for (const version of ['v1','v2','v3']) {
    const buildRoot = path.join(tmp,version);fs.mkdirSync(path.join(buildRoot,'dist/assets'),{recursive:true});
    const result = await esbuild.build({stdin:{contents:`import ${JSON.stringify(path.join(root,'vercel/src/styles.css'))};import React from 'react';import{createRoot}from'react-dom/client';import Cards from ${JSON.stringify(path.join(root,'vercel/src/UpdateCards.tsx'))};createRoot(document.getElementById('root')).render(<Cards language={new URLSearchParams(location.search).has('en')?'en':'zh'} appInfo={{versionCode:1,versionName:'1.0'}}/>);window.testVersion=${JSON.stringify(version)};`,loader:'tsx',resolveDir:path.join(root,'vercel')},bundle:true,write:false,outfile:'ui.js',jsx:'automatic',define:{__WEB_VERSION__:JSON.stringify(version),'import.meta.env':'{}'}});
    for(const file of result.outputFiles)fs.writeFileSync(path.join(buildRoot,'dist',path.basename(file.path)),file.text);
    fs.writeFileSync(path.join(buildRoot,'dist/assets',`lazy-${version}.js`),version);
    fs.writeFileSync(path.join(buildRoot,'dist/index.html'),`<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="./ui.css"><div id="phone"><div id="scroll" class="scroll"><div id="root"></div></div></div><script type="module" src="./ui.js"></script>`);
    const old = process.cwd();process.chdir(buildRoot);try { plugin.offlinePlugin(version).closeBundle(); } finally { process.chdir(old); }
    builds[version]=path.join(buildRoot,'dist');
  }
  async function openUpdate(page, title) {
    if (await page.getByRole('dialog').count()) {
      await page.keyboard.press('Escape');
      await page.getByRole('dialog').waitFor({state:'detached'});
    }
    const en = title.includes('update');
    await page.getByRole('button', {name: `${title} · ${en ? 'View update' : '查看更新'}`, exact:true}).click();
    await page.getByRole('dialog', {name:title,exact:true}).waitFor();
  }
  served=builds.v1;
  const server=http.createServer((req,res)=>{
    let name=decodeURIComponent(new URL(req.url,'http://localhost').pathname).replace(/^\/fnconnect\//,'');
    if(!name)name='index.html';res.setHeader('Cache-Control','no-store');
    if(name==='sw.js'&&failWorker){res.writeHead(503).end();return;}
    if(name==='apk/latest.json'){res.setHeader('Content-Type','application/json');res.statusCode=failCatalog?503:200;res.end(JSON.stringify({versionCode:2,versionName:'2.0',url:'app.apk'}));return;}
    if(name.includes('..')){res.writeHead(400).end();return;}
    const file=path.join(served,name);if(!fs.existsSync(file)){res.writeHead(404).end();return;}
    res.setHeader('Content-Type',name.endsWith('.js')?'application/javascript':name.endsWith('.html')?'text/html':name.endsWith('.css')?'text/css':'application/json');
    res.end(badAsset&&name==='ui.js'?'bad asset':fs.readFileSync(file));
  });
  await new Promise(resolve=>server.listen(8898,'127.0.0.1',resolve));
  const browser=await playwright.chromium.launch({headless:true,args:['--no-sandbox']});
  try {
    const context=await browser.newContext();let page=await context.newPage();const base='http://127.0.0.1:8898/fnconnect/';
    await page.goto(base);await page.evaluate(async()=>{await navigator.serviceWorker.ready});
    await page.reload();await page.waitForFunction(()=>!!navigator.serviceWorker.controller);
    assert.equal(await page.getByRole('dialog').count(),0,'details stay hidden until requested');
    assert.equal(await page.getByRole('button',{name:'App 更新 · 查看更新',exact:true}).count(),1);
    assert.equal(await page.evaluate(()=>window.testVersion),'v1');
    await openUpdate(page,'App 更新');await page.getByRole('link',{name:'下载 App',exact:true}).waitFor();
    failCatalog=true;await openUpdate(page,'App 更新');await page.getByText('检查失败，点击重试',{exact:false}).first().waitFor();assert.equal(await page.getByRole('link',{name:'下载 App',exact:true}).count(),0);failCatalog=false;
    const sibling=await context.newPage();await sibling.goto(base);await sibling.waitForFunction(()=>window.testVersion==='v1');
    console.log('APK checks complete');served=builds.v2;await openUpdate(page,'Web 前端更新');await page.getByRole('button',{name:'重启并应用更新',exact:true}).waitFor();assert.equal(await page.evaluate(()=>window.testVersion),'v1','background update must not reload');
    await page.getByRole('button',{name:'重启并应用更新',exact:true}).click();await page.waitForFunction(()=>window.testVersion==='v2');
    const oldAsset=await sibling.evaluate(()=>fetch('./assets/lazy-v1.js').then(async r=>({status:r.status,body:await r.text()})));assert.deepEqual(oldAsset,{status:200,body:'v1'},'old tab must retain its matching lazy chunk after another tab restarts');await sibling.close();console.log('PASS: old tab still resolves v1 hash chunk after v2 activation');
    console.log('PASS: real subpath SW, background precache without reload, explicit restart to v2, APK failure is not latest');
    served=builds.v3;badAsset=true;await openUpdate(page,'Web 前端更新');await page.getByText('检查失败，点击重试',{exact:false}).first().waitFor();assert.equal(await page.evaluate(()=>window.testVersion),'v2');
    await context.setOffline(true);await page.reload();await page.waitForFunction(()=>window.testVersion==='v2');await openUpdate(page,'Web 前端更新');await page.getByText('检查失败，点击重试',{exact:false}).first().waitFor();console.log('PASS: bad asset rejects install; old complete UI survives offline reload');
    await context.setOffline(false);badAsset=false;await openUpdate(page,'Web 前端更新');await page.getByRole('button',{name:'重启并应用更新',exact:true}).waitFor();
    await page.close();page=await context.newPage();await page.goto(base);await page.waitForFunction(()=>window.testVersion==='v3');console.log('PASS: waiting update activates on next launch');
    await context.close();
    const retryContext=await browser.newContext();const retryPage=await retryContext.newPage();failWorker=true;await retryPage.goto(base);await openUpdate(retryPage,'Web 前端更新');await retryPage.getByText('检查失败，点击重试',{exact:false}).first().waitFor();failWorker=false;await retryPage.getByRole('button',{name:'再次检查',exact:true}).click();await retryPage.getByText('已是最新版本',{exact:true}).waitFor({timeout:7000});await retryContext.close();console.log('PASS: first registration 503 recovers on explicit retry');
    for(const modern of [false,true]){
      const c=await browser.newContext();await c.route('**/apk/latest.json',route=>route.fulfill({contentType:'application/json',body:JSON.stringify({versionCode:2,versionName:'2.0',url:'app.apk'})}));await c.addInitScript(modern=>{window.calls={check:0,restart:0,install:0};window.BmsNative={getAppInfo:()=>JSON.stringify(modern?{updateBaseUrl:'https://updates.example.test/fnconnect/'}:{}),openNative:()=>{},...(modern?{getWebUpdateState:()=>JSON.stringify({currentVersion:'native-v1',status:'ready',pendingVersion:'native-v2'}),checkWebUpdate:()=>window.calls.check++,restartWeb:()=>window.calls.restart++,installAppUpdate:()=>window.calls.install++}:{})};},modern);
      const p=await c.newPage();await p.goto(base+'?en');await openUpdate(p,'App update');await p.getByText('Published version 2.0').waitFor();assert.equal(await p.evaluate(()=>window.calls.install),0);
      await openUpdate(p,'Web frontend update');assert.equal(await p.evaluate(()=>window.calls.check),modern?1:0);assert.equal(await p.evaluate(()=>window.calls.restart),0);
      if(!modern)await p.getByText('Update source not configured',{exact:false}).first().waitFor();
      await c.close();
    }
    console.log('PASS: English, old/new native bridges, title clicks do not install/restart');
  } finally {await browser.close();await new Promise(resolve=>server.close(resolve));fs.rmSync(tmp,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1});
