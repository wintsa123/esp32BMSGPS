"""Compare the ported theme algorithm with the upstream bundle, including live application."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DashboardThemeTest(unittest.TestCase):
    def test_real_engine_presets_custom_colors_and_system_appearance(self):
        result = subprocess.run(["node", "--input-type=module", "-e", r'''
import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import * as engine from './vercel/src/vendor/dashboard-theme-engine.js';
const reference = JSON.parse(fs.readFileSync('tests/fixtures/dashboard_theme_reference.json','utf8'));
const digest = value => crypto.createHash('sha256').update(JSON.stringify(Object.fromEntries(Object.entries(value).sort()))).digest('hex');
const normalize = value => engine.normalize({ ...value, themeV:2,accentV:2 });
assert.equal(engine.presets.length,8);
for(const accent of engine.presets.map(p=>p.v))for(const dark of [false,true]) {
  const preferences=normalize({accent});
  assert.equal(digest(engine.colors(preferences,dark)),reference.presets[accent][Number(dark)]);
  assert.match(engine.colors(preferences,dark).primary,/^#[a-f\d]{6}$/);
}
assert.equal(engine.hex('#AABBCC'),'#aabbcc');assert.equal(engine.hex('red'),null);
assert.equal(engine.normalize({theme:'dark',accent:'red'}).theme,'system');
assert.equal(engine.normalize({theme:'dark',accent:'red'}).accent,'purple');
for(const style of engine.androidStyles) {
  const preferences=normalize({accent:'custom',customColors:{primary:'#12ab34',secondary:'#123456',tertiary:null,style:style.id}});
  for(const dark of [false,true])assert.equal(digest(engine.colors(preferences,dark)),reference.custom[style.id][Number(dark)]);
}
const random=engine.randomCustom({style:'tonal-spot'},()=>0.5);assert.notEqual(random.style,'tonal-spot');assert(engine.hex(random.primary));
let light=false;const style={setProperty(name,value){this[name]=value;}};const meta={removeAttribute(){},setAttribute(name,value){this[name]=value;}};
globalThis.matchMedia=()=>({matches:light});globalThis.document={documentElement:{dataset:{},style},querySelectorAll:()=>[meta]};
const preferences=normalize({theme:'system',accent:'blue'});
engine.apply(preferences);assert.equal(document.documentElement.dataset.theme,'dark');
const darkPrimary=style['--md-sys-color-primary'];light=true;engine.apply(preferences);
assert.equal(document.documentElement.dataset.theme,'light');assert.notEqual(style['--md-sys-color-primary'],darkPrimary);
assert.equal(meta.content,engine.colors(preferences,false)['surface-container']);
engine.apply({...preferences,theme:'dark'});assert.equal(document.documentElement.dataset.theme,'dark');
console.log('8 presets, 7 custom algorithms, normalized storage version and live system appearance match upstream');
'''], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)


if __name__ == "__main__":
    unittest.main()
