"""Verify the editor's shared preview/export geometry against the real TS helper."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DashboardCropTest(unittest.TestCase):
    def test_fit_zoom_visibility_invalid_values_and_preview_coordinates(self):
        result = subprocess.run(["node", "-e", r'''
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const ts=require('./vercel/node_modules/typescript');
const loaded={exports:{}};
vm.runInNewContext(ts.transpileModule(fs.readFileSync('vercel/src/vehicle-image-crop.ts','utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText,{module:loaded,exports:loaded.exports});
const {CROP_WIDTH:W,CROP_HEIGHT:H,cropGeometry,clampCrop}=loaded.exports;
const close=(actual,expected)=>assert(Math.abs(actual-expected)<1e-9,`${actual} != ${expected}`);
const rect=(w,h,crop,x,y,rw,rh)=>{const r=cropGeometry(w,h,crop);[r.x,r.y,r.width,r.height].forEach((v,i)=>close(v,[x,y,rw,rh][i]));};
const center={x:0,y:0,zoom:1};assert.equal(W,900);assert.equal(H,600);
rect(900,600,center,0,0,900,600);rect(600,900,center,250,0,400,600);
rect(1800,600,center,0,150,900,300);rect(600,600,center,150,0,600,600);
rect(900,600,{x:0,y:0,zoom:4},-1350,-900,3600,2400);
rect(900,600,{x:.1,y:-.2,zoom:1},90,-120,900,600);
for(const [w,h] of [[900,600],[600,900],[1800,600],[600,600],[0,0],[NaN,Infinity],[-1,0],[Number.MAX_VALUE,Number.MIN_VALUE],[Number.MIN_VALUE,Number.MAX_VALUE],[Number.MIN_VALUE,Number.MIN_VALUE]]){
 for(const zoom of [-4,0,1,2,4,9,NaN,Infinity])for(const x of [-1e10,0,1e10,NaN,Infinity]){
  const input={x,y:-x,zoom},safe=clampCrop(w,h,input),r=cropGeometry(w,h,input),twice=clampCrop(w,h,safe);
  Object.values(r).forEach(v=>assert(Number.isFinite(v)));Object.values(safe).forEach(v=>assert(Number.isFinite(v)));
  assert(safe.zoom>=1&&safe.zoom<=4);['x','y','zoom'].forEach(k=>close(safe[k],twice[k]));
  const horizontal=Math.max(0,Math.min(W,r.x+r.width)-Math.max(0,r.x));
  const vertical=Math.max(0,Math.min(H,r.y+r.height)-Math.max(0,r.y));
  assert(horizontal+1e-9>=Math.min(W,r.width)*.25);assert(vertical+1e-9>=Math.min(H,r.height)*.25);
  const already=cropGeometry(w,h,safe);['x','y','width','height'].forEach(k=>close(r[k],already[k]));
 }
}
const extreme=cropGeometry(900,600,{x:1e10,y:1e10,zoom:1});close(extreme.x,675);close(extreme.y,450);
const original={x:.13,y:-.07,zoom:2.2};const exported=cropGeometry(1800,900,original);
// A CSS-scaled canvas or SVG preview uses exactly this rectangle scaled to its frame.
const previewScale=.4,preview={x:exported.x*previewScale,y:exported.y*previewScale,width:exported.width*previewScale,height:exported.height*previewScale};
const drag={x:36,y:-24};const moved=cropGeometry(1800,900,{...original,x:original.x+drag.x/(W*previewScale),y:original.y+drag.y/(H*previewScale)});
close((moved.x-exported.x)*previewScale,drag.x);close((moved.y-exported.y)*previewScale,drag.y);
close(preview.width/exported.width,previewScale);close(preview.height/exported.height,previewScale);
console.log('contain fit, finite invalid inputs, zoom limits, minimum visible overlap, idempotence and preview/export coordinate contract passed');
'''], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
