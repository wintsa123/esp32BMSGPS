// Deterministic DOM/event clock for the real swipe module; no browser installation required.
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const ts=require('../vercel/node_modules/typescript');
function environment(){
 let time=0,serial=0,reduce=false,allowed=true,tab='overview';
 const frames=new Map(),selected=[],progress=[],created=[];
 class Element {
  constructor(tag='DIV'){
   this.tagName=tag;this.style={transform:'',setProperty(k,v){this[k]=v},removeProperty(k){delete this[k]}};
   this.dataset={};this.attrs=new Map();this.listeners=new Map();this.children=[];
   this.clientWidth=360;this.scrollWidth=360;this.parentElement=null;this.overflowX='visible';
   const classes=new Set();this.classList={add:(...names)=>names.forEach(n=>classes.add(n)),remove:(...names)=>names.forEach(n=>classes.delete(n)),contains:n=>classes.has(n),toggle(n,on){const value=on??!classes.has(n);if(value)classes.add(n);else classes.delete(n)}};
  }
  addEventListener(type,fn,options){if(!this.listeners.has(type))this.listeners.set(type,new Set());this.listeners.get(type).add(fn);options?.signal?.addEventListener('abort',()=>this.removeEventListener(type,fn));}
  removeEventListener(type,fn){this.listeners.get(type)?.delete(fn);if(!this.listeners.get(type)?.size)this.listeners.delete(type);}
  dispatch(type,options={}){const e={type,target:this,cancelable:true,defaultPrevented:false,preventDefault(){this.defaultPrevented=true},stopImmediatePropagation(){this.stopped=true},...options};for(const fn of [...(this.listeners.get(type)||[])]){fn(e);if(e.stopped)break}return e;}
  closest(selector){return this.blocked&&selector.includes(this.blocked)?this:null;}
  cloneNode(){const node=new Element();created.push(node);return node;}
  removeAttribute(name){this.attrs.delete(name);}
  setAttribute(name,value){this.attrs.set(name,value);}
  hasAttribute(name){return this.attrs.has(name);}
  getAttribute(name){return this.attrs.get(name)||null;}
  querySelectorAll(selector){return selector==='[id]'?this.idChildren||[]:[];}
  querySelector(){return null;}
  set innerHTML(html){this.html=html;this.idChildren=[...html.matchAll(/id=["']([^"']+)/g)].map(match=>{const n=new Element();n.setAttribute('id',match[1]);return n});}
  get innerHTML(){return this.html||'';}
  append(node){node.parentElement=this;this.children.push(node);}
  remove(){this.removed=true;if(this.parentElement)this.parentElement.children=this.parentElement.children.filter(n=>n!==this);}
  setPointerCapture(id){this.captured=id;}
  releasePointerCapture(){this.captured=null;}
 }
 const root=new Element(),parent=new Element();parent.append(root);const loaded={exports:{}};
 const window=new Element();
 const sandbox={module:loaded,exports:loaded.exports,require:name=>name==='react-dom'?{flushSync:fn=>fn()}:require(name),
  window,document:{},HTMLElement:Element,Element,AbortController,performance:{now:()=>time},getComputedStyle:node=>({overflowX:node.overflowX}),
  matchMedia:()=>({matches:reduce}),requestAnimationFrame:fn=>{frames.set(++serial,fn);return serial},cancelAnimationFrame:id=>frames.delete(id),setTimeout,clearTimeout};
 vm.runInNewContext(ts.transpileModule(fs.readFileSync('vercel/src/vendor/dashboard-swipe.ts','utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText,sandbox);
 const bind=()=>loaded.exports.bindHorizontalPages(root,{current:()=>tab,items:['overview','rides','device'],enabled:()=>allowed,select:next=>{selected.push(next);tab=next},preview:next=>`<section id="copied-panel"><input id="copied-input">${next}</section>`,progress:value=>progress.push(value)});
 const point=(x,y=100)=>({clientX:x,clientY:y});
 const start=(x=220,y=100,options={})=>root.dispatch('touchstart',{touches:[point(x,y)],...options});
 const move=(x,y=100,options={})=>root.dispatch('touchmove',{touches:[point(x,y)],...options});
 const end=(cancel=false)=>root.dispatch(cancel?'touchcancel':'touchend',{touches:[]});
 const tick=delta=>{time+=delta;const callbacks=[...frames.values()];frames.clear();callbacks.forEach(fn=>fn(time))};
 const settle=()=>{for(let i=0;i<15&&frames.size;i++)tick(20);assert.equal(frames.size,0,'animation completed')};
 const clean=()=>{assert.equal(root.style.transform,'');assert(!root.classList.contains('is-swiping'));assert.equal(parent.children.length,1,'neighbor cleaned')};
 return {root,parent,window,Element,bind,start,move,end,tick,settle,clean,frames,selected,progress,created,point,
  get tab(){return tab},set tab(v){tab=v},set allowed(v){allowed=v},set reduce(v){reduce=v}};
}
const suites={
 axis(){const e=environment();e.bind();
  e.start();e.move(211);assert(!e.root.classList.contains('is-swiping'),'below 12px does not lock');e.end();e.clean();
  e.start();e.move(205,90);assert(!e.root.classList.contains('is-swiping'),'1.5 diagonal is undecided');e.move(200,90);assert(e.root.classList.contains('is-swiping'));e.end(true);e.settle();e.clean();
  e.start();const vertical=e.move(210,150);assert(!vertical.defaultPrevented,'vertical scrolling is untouched');e.move(50,160);e.end();e.settle();assert.equal(e.tab,'overview','vertical cannot become horizontal');e.clean();
 },
 preview(){const e=environment();e.bind();e.start();const moved=e.move(100);assert(moved.defaultPrevented);assert.equal(e.tab,'overview','dragging does not select');
  assert.equal(e.root.style.transform,'translateX(-120px)');assert.equal(e.parent.children.length,2);const neighbor=e.parent.children[1];assert(neighbor.inert);assert.equal(neighbor.getAttribute('aria-hidden'),'true');
  assert.equal(neighbor.dataset.page,'rides');assert(neighbor.innerHTML.includes('rides'));assert(!neighbor.hasAttribute('id'));assert(neighbor.querySelectorAll('[id]').every(n=>!n.hasAttribute('id')));
  assert(Math.abs(e.progress.at(-1)-1/3)<1e-9,'nav follows fractional page progress');e.end();assert.equal(e.tab,'overview','commit waits until settle');e.tick(90);assert.equal(e.tab,'overview');e.settle();assert.equal(e.tab,'rides');e.clean();assert.equal(e.progress.at(-1),null);
  assert(!e.root.dispatch('click',{detail:0}).defaultPrevented,'programmatic nav clicks are allowed');
  const click=e.root.dispatch('click',{detail:1});assert(click.defaultPrevented&&click.stopped,'drag suppresses accidental click');assert(!e.root.dispatch('click').defaultPrevented,'later normal clicks work');
 },
 cancellation(){const e=environment();e.bind();
  e.start();e.move(160);e.end();e.settle();assert.equal(e.tab,'overview','under 72px does not select');e.clean();
  e.start();e.move(100);e.end(true);e.settle();assert.equal(e.tab,'overview','touch cancellation does not commit');e.clean();
  e.start(100);e.move(220);assert.equal(e.root.style.transform,'translateX(30px)','left edge quarter resistance');assert.equal(e.parent.children.length,1);e.end();e.settle();assert.equal(e.tab,'overview');e.clean();
  e.tab='device';e.start();e.move(100);assert.equal(e.root.style.transform,'translateX(-30px)','right edge quarter resistance');e.end();e.settle();assert.equal(e.tab,'device');e.clean();
 },
 guards(){const e=environment();e.bind();e.allowed=false;e.start();e.move(100);e.end();e.clean();e.allowed=true;
  for(const selector of ['input','textarea','select','a','contenteditable','canvas','leaflet-container','action','cap-wrap']){
   const target=new e.Element();target.blocked=selector;target.parentElement=e.root;e.start(220,100,{target});e.move(100);e.end();e.clean();assert.equal(e.tab,'overview',selector+' blocks swipe');
  }
  const horizontal=new e.Element();horizontal.scrollWidth=800;horizontal.clientWidth=300;horizontal.overflowX='auto';horizontal.parentElement=e.root;
  e.start(220,100,{target:horizontal});e.move(100);e.end();e.clean();
  e.start(220,100,{touches:[e.point(220),e.point(260)]});e.move(100);e.end();e.clean();
  e.start();e.move(100);e.move(90,100,{touches:[e.point(90),e.point(130)]});e.end();e.settle();assert.equal(e.tab,'overview','second touch cancels active gesture');e.clean();
 },
 interruptions(){const e=environment();e.bind();e.start();e.move(100);e.tab='device';e.move(90);e.end();e.settle();assert.deepEqual(e.selected,[],'external tab change cancels gesture');e.clean();
  e.tab='overview';e.start();e.move(100);e.end();e.allowed=false;e.tick(20);assert.deepEqual(e.selected,[],'modal during settling cancels');e.clean();
  e.allowed=true;e.start();e.move(100);e.window.dispatch('blur');e.clean();assert.equal(e.frames.size,0);
  e.start();e.move(100);e.window.dispatch('resize');e.clean();
  e.start();e.move(100);e.end();e.start();e.move(90);e.end();e.settle();assert.deepEqual(e.selected,['rides'],'rapid gestures do not double commit');e.clean();
  e.root.dispatch('pointerdown',{pointerType:'mouse',button:0,pointerId:1,clientX:220,clientY:100});e.root.dispatch('pointermove',{pointerType:'mouse',pointerId:1,clientX:100,clientY:100});e.root.dispatch('pointercancel',{pointerId:1});e.settle();assert.equal(e.tab,'rides');e.clean();
 },
 cleanup(){const e=environment();let dispose=e.bind();assert.equal(typeof dispose,'function');e.reduce=true;e.start();e.move(100);e.end();assert.equal(e.tab,'rides','reduced motion commits immediately');assert.equal(e.frames.size,0);e.clean();
  e.reduce=false;e.start();e.move(100);e.end();assert(e.frames.size);dispose();assert.equal(e.frames.size,0);assert.equal(e.root.listeners.size,0,'all listeners removed');assert.equal(e.window.listeners.size,0,'window resize and blur listeners removed');e.clean();
  dispose=e.bind();e.start();e.move(100);assert.equal(e.parent.children.length,2);dispose();e.clean();assert.equal(e.root.listeners.size,0);
  e.start();e.move(100);e.end();assert.equal(e.frames.size,0,'unmounted instance cannot handle gestures');
 }
};
const names=process.argv[2]?[process.argv[2]]:Object.keys(suites);for(const name of names){suites[name]();console.log('PASS '+name)}
