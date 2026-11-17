let chromium;try{({chromium}=require('playwright'));}catch{({chromium}=require('/vol1/1000/.tools/battery-browser/node_modules/playwright'));}
const assert=require('node:assert/strict');
(async()=>{
 const b=await chromium.launch({headless:true,args:['--no-sandbox']});
 try {
  const p=await b.newPage({viewport:{width:390,height:844},isMobile:true,hasTouch:true,serviceWorkers:'block'});
  const errors=[];p.on('pageerror',e=>errors.push(e.message));
  await p.goto('http://127.0.0.1:8787/');
  const c=await p.context().newCDPSession(p),card=p.locator('[data-home-id="vehicle"]');
  const touch=(type,touchPoints)=>c.send('Input.dispatchTouchEvent',{type,touchPoints});
  let box=await card.boundingBox(), x=box.x+80,y=box.y+100;
  await touch('touchStart',[{id:1,x,y}]);await p.waitForTimeout(650);
  assert.equal(await p.locator('.home-dashboard').getAttribute('data-home-editing'),'true');
  assert(await card.evaluate(el=>el.classList.contains('moving')));
  const before=await card.locator('.home-widget-shell').boundingBox();
  await touch('touchMove',[{id:1,x:x+30,y:y+30}]);
  const after=await card.locator('.home-widget-shell').boundingBox();
  assert(Math.abs(after.x-before.x-30)<3);assert(Math.abs(after.y-before.y-30)<3);
  await touch('touchEnd',[]);await p.waitForTimeout(320);
  const pinch=async(scale,cancel=false)=>{
   await card.scrollIntoViewIfNeeded();box=await card.boundingBox();
   const cx=box.x+box.width/2,cy=box.y+box.height/2;
   await touch('touchStart',[{id:1,x:cx-65,y:cy-45},{id:2,x:cx+65,y:cy+45}]);
   await touch('touchMove',[{id:1,x:cx-65*scale,y:cy-45*scale},{id:2,x:cx+65*scale,y:cy+45*scale}]);
   assert(await card.evaluate(el=>el.classList.contains('resizing')));
   await touch(cancel?'touchCancel':'touchEnd',[]);await p.waitForTimeout(320);
  };
  await pinch(.5);
  assert.equal(await card.evaluate(el=>el.style.gridColumn),'span 2');
  assert.equal(await card.evaluate(el=>el.style.gridRow),'span 2');
  await pinch(1.8,true);assert.equal(await card.evaluate(el=>el.style.gridColumn),'span 2');
  await pinch(1.8);assert.equal(await card.evaluate(el=>el.style.gridColumn),'span 4');
  assert.equal(await card.evaluate(el=>el.style.gridRow),'span 4');
  // Gestures still work after cancellation; short vertical swipes scroll in edit mode.
  await card.scrollIntoViewIfNeeded();box=await card.boundingBox();
  const top=await p.locator('#scroll').evaluate(el=>el.scrollTop);
  x=box.x+50;y=Math.max(180,box.y+box.height-60);
  await touch('touchStart',[{id:1,x,y}]);await touch('touchMove',[{id:1,x,y:y-80}]);await touch('touchEnd',[]);
  assert((await p.locator('#scroll').evaluate(el=>el.scrollTop))>top);
  assert.equal(await card.evaluate(el=>el.classList.contains('moving')),false);
  await p.getByRole('button',{name:'完成',exact:true}).tap();
  await p.locator('.home-dashboard[data-home-editing="false"]').waitFor();
  await p.reload();assert.equal(await card.evaluate(el=>el.style.gridColumn),'span 4');assert.equal(await card.evaluate(el=>el.style.gridRow),'span 4');
  assert.deepEqual(errors,[]);
  console.log('PASS: real touch long-hold drag, two-finger shrink/expand, pointer cancellation, edit scrolling, save/reload');
 }finally{await b.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
