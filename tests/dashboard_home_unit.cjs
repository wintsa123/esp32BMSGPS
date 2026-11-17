const assert = require('node:assert/strict');
const {createRequire}=require('node:module');
const vm=require('node:vm');
const requireLocal=createRequire(process.cwd()+'/vercel/package.json');
const esbuild=requireLocal('esbuild');
function load(file) {
 const result=esbuild.buildSync({entryPoints:[file],bundle:true,write:false,platform:'node',format:'cjs',packages:'external',loader:{'.css':'empty'},define:{'import.meta.env.VITE_TIANDITU_BROWSER_TK':'""'}});
 const module={exports:{}};vm.runInNewContext(result.outputFiles[0].text,{module,exports:module.exports,require:requireLocal,URLSearchParams,console});return module.exports;
}
(async()=>{
 const {parseHomeLayout}=load('vercel/src/HomeDashboard.tsx');
 const defaults=[{id:'vehicle',width:4,height:3}];
 assert.deepEqual(JSON.parse(JSON.stringify(parseHomeLayout('{broken',defaults))),defaults);
 assert.equal(parseHomeLayout('[]',defaults).length,0);
 const valid=parseHomeLayout(JSON.stringify([{id:'gps',width:4,height:3},{id:'gps',width:2,height:1},{id:'invalid',width:99,height:-1},{id:'not-loaded-yet',width:2,height:2}]),defaults);
 assert.deepEqual(Array.from(valid,x=>x.id),['gps','not-loaded-yet']);
 const {readTrackPage}=load('vercel/src/GpsRideCard.tsx');
 let requestPath;
 const point={lat_e7:399084000,lon_e7:1163974000,flags:1};
 const first=await readTrackPage(async path=>{requestPath=path;return {samples:[point,{...point,flags:0},{...point,lat_e7:9999999999}],next_cursor:5}},7,null,false);
 assert(requestPath.includes('session=7') && requestPath.includes('limit=8'));
 assert.equal(first.points[0].lon_e7,1163974000);assert.equal(first.points[1],null);assert.equal(first.points[2],null);
 await assert.rejects(()=>readTrackPage(async()=>({samples:[point],next_cursor:5}),7,5,false),/invalid-track/);
 await assert.rejects(()=>readTrackPage(async()=>({}),undefined,null,false),/no-session/);
 const legacy=await readTrackPage(async path=>{assert.equal(path,'/api/gps/track');return {points:[point,null]};},undefined,null,true);
 assert.equal(legacy.points.length,2);assert.equal(legacy.cursor,null);
 console.log('PASS: layout validation/empty persistence, GPS flags/coordinates/gaps, BLE pagination and invalid cursor, HTTP legacy endpoint');
})().catch(e=>{console.error(e);process.exitCode=1});
