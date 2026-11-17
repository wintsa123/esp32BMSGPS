const assert=require('node:assert/strict');
let chromium;try{({chromium}=require('playwright'));}catch{({chromium}=require('/vol1/1000/.tools/battery-browser/node_modules/playwright'));}
const storageKey='bms.dashboard.home-layout.v1';
(async()=>{
 const browser=await chromium.launch({headless:true,args:['--no-sandbox']});
 try {
  const page=await browser.newPage({viewport:{width:390,height:844},serviceWorkers:'block'});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('http://127.0.0.1:8787/');
  const home=page.locator('.home-dashboard');
  assert.equal(await page.getByText('长按卡片或空白处编辑',{exact:true}).count(),0);
  assert.equal(await page.locator('#topbar button[aria-label="刷新"] + button[aria-label="编辑首页"]').count(),1);
  assert.equal(await home.locator('.home-toolbar').count(),0);
  assert.equal(await home.getByRole('button',{name:'热点 API',exact:true}).count(),0);
  assert.equal(await home.getByRole('button',{name:'蓝牙',exact:true}).count(),0);
  await home.getByRole('button',{name:/^保护板/}).click();
  await page.getByRole('dialog',{name:'保护板连接设置',exact:true}).waitFor();
  await page.getByRole('button',{name:'取消',exact:true}).click();
  await page.getByRole('dialog').waitFor({state:'detached'});
  // Hold a child button: enter editing without opening the image editor on release.
  const image=page.getByRole('button',{name:'更换车辆图片',exact:true});await image.scrollIntoViewIfNeeded();
  let box=await image.boundingBox();await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.waitForTimeout(650);await page.mouse.up();
  await page.getByRole('button',{name:'完成',exact:true}).waitFor();
  assert.equal(await page.getByRole('dialog').count(),0);
  const initial=await home.locator('[data-home-id]').evaluateAll(nodes=>nodes.map(node=>node.dataset.homeId));
  assert.equal(await page.locator('#topbar').isVisible(),false);
  assert.equal(await page.locator('#tabbar').isVisible(),false);
  assert.equal(await home.locator('.home-edit-hint,.home-size-actions,.home-resize,.home-drag').count(),0);
  for(const editor of await home.locator('.home-widget-editor').all()) assert.equal(await editor.getByRole('button').count(),1);
  // Holding the card lifts its actual content; the grid changes only on release.
  const from=home.locator('[data-home-id="gps"]');await from.scrollIntoViewIfNeeded();
  const target=home.locator('[data-home-id="vehicle"]');
  box=await from.boundingBox();const to=await target.boundingBox();
  await page.mouse.move(box.x+70,box.y+60);await page.mouse.down();await page.waitForTimeout(420);
  assert(await from.evaluate(el=>el.classList.contains('moving')));
  const floating=from.locator('.home-widget-shell'), before=await floating.boundingBox();
  await page.mouse.move(box.x+95,box.y+85);
  const after=await floating.boundingBox();assert(Math.abs(after.x-before.x-25)<3);assert(Math.abs(after.y-before.y-25)<3);
  assert.deepEqual(await home.locator('[data-home-id]').evaluateAll(nodes=>nodes.map(node=>node.dataset.homeId)),initial);
  await page.mouse.move(to.x+80,Math.max(180,to.y+80),{steps:8});await page.mouse.up();
  assert.equal(await home.locator('[data-home-id]').first().getAttribute('data-home-id'),'gps');
  assert(await home.locator('.home-widget-shell').evaluateAll(nodes=>nodes.some(node=>node.getAnimations().length>0)));
  await page.waitForTimeout(320);
  await home.getByRole('button',{name:'删除 保护板',exact:true}).click();
  assert.equal(await home.locator('[data-home-id="bms"]').count(),0);
  await page.getByRole('button',{name:'新增卡片',exact:true}).click();
  const library=page.getByRole('dialog',{name:'添加卡片',exact:true});
  await library.waitFor();
  const catalogue=require('../vercel/src/preview-controls.json').flatMap(g=>g.controls);
  for(const c of catalogue)assert.equal(await library.getByRole('button').filter({has:page.locator('strong',{hasText:c.zh})}).count(),1);
  await library.getByRole('button').filter({has:page.locator('strong',{hasText:'开启蓝牙广播'})}).click();
  await library.waitFor({state:'detached'});
  await page.getByRole('button',{name:'完成',exact:true}).click();
  const saved=await page.evaluate(key=>localStorage.getItem(key),storageKey);
  assert(saved.includes('control:bluetooth'));assert(!JSON.parse(saved).some(c=>c.id==='bms'));
  await page.reload();assert.equal(await home.locator('[data-home-id]').first().getAttribute('data-home-id'),'gps');
  // A later edit can be canceled without losing the saved layout.
  await page.getByRole('button',{name:'编辑首页',exact:true}).click();
  await home.getByRole('button',{name:'删除 最近骑行',exact:true}).click();
  await page.getByRole('button',{name:'取消',exact:true}).click();
  assert(await page.locator('#topbar').isVisible());assert(await page.locator('#tabbar').isVisible());
  assert.equal(await page.evaluate(key=>localStorage.getItem(key),storageKey),saved);
  await home.locator('[data-home-id="control:bluetooth"] button').click();
  await page.locator('#control-bluetooth[data-feature-focus="true"]').waitFor();
  assert.equal(await page.getByRole('dialog').count(),0, 'action shortcuts focus the inline action without executing it');
  await page.getByRole('button',{name:'爱车',exact:true}).click();
  // Blank background also enters editing.
  const background=home.getByLabel('首页空白区域',{exact:true});await background.scrollIntoViewIfNeeded();box=await background.boundingBox();
  await page.mouse.move(box.x+30,box.y+20);await page.mouse.down();await page.waitForTimeout(650);await page.mouse.up();
  assert.equal(await home.getAttribute('data-home-editing'),'true');
  await page.getByRole('button',{name:'取消',exact:true}).click();
  await page.setViewportSize({width:320,height:568});assert(await home.evaluate(el=>el.scrollWidth<=el.clientWidth));
  await page.setViewportSize({width:390,height:844});await page.locator('#scroll').evaluate(el=>el.scrollTop=0);await page.getByRole('button',{name:'编辑首页',exact:true}).click();
  await page.locator('#scroll').evaluate(el=>el.scrollTop=0);await page.screenshot({path:'preview/home-layout-editor.png',animations:'disabled'});
  assert.deepEqual(errors,[]);
  console.log('PASS: BMS entry, long-press suppresses child click, floating pointer reorder/animations, delete/add every remote card, persistence/cancel, shortcut destination, background hold, narrow viewport');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
