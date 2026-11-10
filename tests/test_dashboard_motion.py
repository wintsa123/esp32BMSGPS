"""Run the actual vendored motion module against a deterministic frame clock."""
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class DashboardMotionTest(unittest.TestCase):
    def test_frames_tabs_scroll_reduced_motion_and_cleanup(self):
        result = subprocess.run(["node", "-e", r'''
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('./vercel/node_modules/typescript');
let now = 100, serial = 0, reduce = false;
const frames = new Map();
const animations = [];
const observers = [];
class Element {
  constructor() {
    this.dataset = {}; this.textContent = ''; this.attrs = new Map(); this.listeners = new Map();
    const classes = new Set();
    this.classList = { add: (...names) => names.forEach(n => classes.add(n)), remove: (...names) => names.forEach(n => classes.delete(n)),
      contains: n => classes.has(n), toggle(n,on) { const next = on ?? !classes.has(n); if(next) classes.add(n); else classes.delete(n); } };
    this.style = { setProperty(k,v) { this[k] = v; } }; this.nodes = {}; this.lists = {};
  }
  querySelector(s) { return this.nodes[s] || null; }
  querySelectorAll(s) { return this.lists[s] || []; }
  hasAttribute(s) { return this.attrs.has(s); }
  setAttribute(k,v) { this.attrs.set(k,v); }
  getAttribute(k) { return this.attrs.get(k); }
  removeAttribute(k) { this.attrs.delete(k); }
  toggleAttribute(k,on) { if(on) this.attrs.set(k,''); else this.attrs.delete(k); }
  addEventListener(k,v) { this.listeners.set(k,v); }
  removeEventListener(k) { this.listeners.delete(k); }
  matches() { return false; }
  closest() { return null; }
  remove() { this.removed = true; }
  cloneNode() { return new Element(); }
  append() {}
  animate(keys,options) { const a = { keys, options, finished: new Promise(()=>{}), cancel() { this.cancelled=true; } }; animations.push(a); return a; }
}
const phone = new Element(), scroll = new Element(), top = new Element(), tabs = new Element(), indicator = new Element();
const controls = new Element(), title = new Element(), load = new Element(), page = new Element(), value = new Element();
const buttonA = new Element(), buttonB = new Element(); buttonA.dataset.tab='overview'; buttonB.dataset.tab='rides';
phone.nodes = {'#scroll':scroll,'#topbar':top,'#tabbar':tabs,'#loadbar':load,'.tab.sel .tab-label':{textContent:'首页'},'#scroll > .panel.active':page};
phone.lists['#tabbar .tab[data-tab]']=[buttonA,buttonB];
tabs.nodes['.tab-ind']=indicator; tabs.lists['.tab[data-tab]']=[buttonA,buttonB];
top.nodes={'.topbar-controls':controls,'.topbar-page-title':title};
top.firstElementChild=controls;top.offsetHeight=100;
scroll.scrollTop=0;scroll.scrollHeight=1000;scroll.clientHeight=500;
page.lists['[data-num]']=[value];page.parentElement=scroll;
value.dataset={num:'80.0',key:'soc'};value.textContent='80.0';
const loaded={exports:{}};
const context={module:loaded,exports:loaded.exports,window:new Element(),performance:{now:()=>now},matchMedia:()=>({matches:reduce}),
  ResizeObserver:class { constructor(fn) { this.fn=fn;observers.push(this); } observe(el) { this.el=el; } disconnect() { this.disconnected=true; } },
  requestAnimationFrame:fn=>{frames.set(++serial,fn);return serial;},cancelAnimationFrame:id=>frames.delete(id),setTimeout:()=>++serial,clearTimeout() {}};
vm.runInNewContext(ts.transpileModule(fs.readFileSync('vercel/src/vendor/dashboard-motion.ts','utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText,context);
const tick=t=>{now=t;const callbacks=[...frames.values()];frames.clear();callbacks.forEach(fn=>fn(t));};
const {bindDashboardMotion,updateDashboardMotion}=loaded.exports;
let clean=bindDashboardMotion(phone);
assert.equal(phone.style['--topbar-h'],'100px');top.offsetHeight=96;observers[0].fn();assert.equal(phone.style['--topbar-h'],'96px');
updateDashboardMotion(phone,'overview',{busy:true});
tick(100);assert.equal(value.textContent,'0.0');
tick(480);assert.equal(value.textContent,'70.0');
tick(860);assert.equal(value.textContent,'80.0');assert.equal(load.classList.contains('on'),true);
assert.equal(indicator.style.transform,'translateX(calc(0 * (100% + var(--tab-gap))))');
now=3000;value.dataset.num='90.0';updateDashboardMotion(phone,'overview',{busy:false});
tick(3240);assert.equal(value.textContent,'88.8');tick(3480);assert.equal(value.textContent,'90.0');
scroll.scrollTop=100;scroll.listeners.get('scroll')();tick(3500);
assert.equal(top.classList.contains('is-compact'),true);assert.equal(controls.hasAttribute('inert'),true);
assert.equal(title.textContent,'首页');assert.equal(phone.style['--tabbar-veil'],'1');
scroll.scrollTop=500;scroll.listeners.get('scroll')();tick(3510);assert.equal(phone.style['--tabbar-veil'],'0');
const second=new Element();second.parentElement=scroll;phone.nodes['#scroll > .panel.active']=second;
updateDashboardMotion(phone,'rides',{busy:false});
assert.equal(indicator.style.transform,'translateX(calc(1 * (100% + var(--tab-gap))))');
assert.equal(second.classList.contains('page-fwd'),true);assert.equal(animations.length,0);
const press=new Element();press.closest=s=>s.startsWith('button')?press:null;
phone.listeners.get('pointerdown')({target:press});assert.equal(animations.length,1);assert.equal(animations[0].options.duration,200);
clean();assert.equal(frames.size,0);assert.equal(scroll.listeners.size,0);assert(animations.every(a=>a.cancelled));assert(observers[0].disconnected);
reduce=true;phone.nodes['#scroll > .panel.active']=page;clean=bindDashboardMotion(phone);
updateDashboardMotion(phone,'overview',{busy:false});assert.equal(value.textContent,'90.0');assert.equal(frames.size,0);
bindDashboardMotion(phone);assert.equal(scroll.listeners.size,1);clean();
console.log('motion frames, source easing, navigation, scroll, reduced motion and cleanup passed');
'''], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)


if __name__ == "__main__":
    unittest.main()
