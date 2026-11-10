"""Exercise the real history reader and React SVG rendering without a device or extra dependencies."""
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DashboardHistoryTest(unittest.TestCase):
    def test_real_contract_pagination_races_retries_and_charts(self):
        result = subprocess.run(["node", "-e", r'''
const assert=require('node:assert/strict'), fs=require('node:fs'), vm=require('node:vm');
const {createRequire}=require('node:module');
const localRequire=createRequire(process.cwd()+'/vercel/package.json');
const ts=localRequire('typescript'),React=localRequire('react');
const {renderToStaticMarkup}=localRequire('react-dom/server');
const loaded={exports:{}};
vm.runInNewContext(ts.transpileModule(fs.readFileSync('vercel/src/RideHistory.tsx','utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,jsx:ts.JsxEmit.ReactJSX,target:ts.ScriptTarget.ES2022}}).outputText,
 {module:loaded,exports:loaded.exports,require:localRequire,URLSearchParams,console,Error});
const {createHistoryReader,HistoryPlot,default:RideHistory}=loaded.exports;
const sample=(t,elapsed=t,extra={})=>({t,elapsed_s:elapsed,flags:2,pack_voltage_mv:52400,current_deci_amps:-126,soc_percent:78,...extra});
const fault=(t,extra={})=>({t,session:7,active_mask:2,supported_mask:3,bms_type:1,...extra});
const session={id:7,start_s:100,end_s:200,elapsed_s:60,samples:10,calibrated:false,truncated:false};
const deferred=()=>{let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no});return {promise,resolve,reject};};
(async()=>{
 let state,calls=[],pending=[];
 const reader=createHistoryReader(path=>{calls.push(path);const request=deferred();pending.push(request);return request.promise;},next=>state=next);
 const first=reader.select(session);
 assert.equal(calls.length,2);assert(calls[0].includes('session=7'));assert(calls[0].includes('limit=8'));assert(calls[1].includes('limit=20'));
 pending[0].resolve({samples:[sample(10,0),sample(11,1)],next_cursor:11});pending[1].resolve({faults:[fault(10)],next_cursor:10});await first;
 assert.equal(state.samples.rows.length,2);assert.equal(state.faults.rows.length,1);
 const more=reader.samples(),duplicateClick=reader.samples();assert.equal(calls.length,3);assert(calls[2].includes('cursor=11'));
 pending[2].resolve({samples:[sample(11,1),sample(12,2)],next_cursor:12});await Promise.all([more,duplicateClick]);
 assert.equal(state.samples.rows.length,3);assert.equal(state.samples.cursor,12);
 const fail=reader.samples();pending[3].reject(new Error('HTTP 503'));await fail;assert.equal(state.samples.rows.length,3);assert.equal(state.samples.error,'HTTP 503');
 const retry=reader.samples();assert.equal(calls[4],calls[3]);pending[4].resolve({samples:[],next_cursor:null});await retry;
 assert.equal(state.samples.rows.length,3);assert.equal(state.samples.cursor,null);await reader.samples();assert.equal(calls.length,5);
 const faults=reader.faults();pending[5].resolve({faults:[fault(10),fault(11,{active_mask:0})],next_cursor:11});await faults;assert.equal(state.faults.rows.length,2);
 const repeating=reader.faults();pending[6].resolve({faults:[fault(11,{active_mask:0})],next_cursor:11});await repeating;assert.equal(state.faults.rows.length,2);assert.equal(state.faults.cursor,null);
 const old=reader.select(session);const newStart=pending.length;const current=reader.select({...session,id:8,calibrated:true});
 assert(calls[newStart+1].includes('from=100'));assert(calls[newStart+1].includes('to=200'));
 pending[newStart].resolve({samples:[sample(30)],next_cursor:30});pending[newStart+1].resolve({faults:[],next_cursor:null});await current;
 pending[newStart-2].resolve({samples:[sample(999)],next_cursor:999});pending[newStart-1].resolve({faults:[fault(999)],next_cursor:999});await old;
 assert.equal(state.session.id,8);assert.equal(state.samples.rows[0].t,30);assert.equal(state.faults.rows.length,0);
 const bad=reader.select(session);pending[pending.length-2].resolve({unrelated:[]});pending[pending.length-1].resolve({faults:[fault(50,{session:9})],next_cursor:50});await bad;
 assert.equal(state.samples.error,'invalid-history-response');assert.equal(state.faults.error,'invalid-history-response');
 const disposed=reader.select(session),last=state;reader.dispose();pending[pending.length-2].resolve({samples:[sample(777)],next_cursor:777});pending[pending.length-1].reject(new Error('late'));await disposed;assert.equal(state,last);
 const render=(component,props)=>renderToStaticMarkup(React.createElement(component,props));
 const samples=[sample(10,0),sample(11,10,{pack_voltage_mv:51900}),sample(12,20,{pack_voltage_mv:null}),sample(13,30,{flags:0,pack_voltage_mv:99999}),sample(14,40,{pack_voltage_mv:0,current_deci_amps:0,soc_percent:0})];
 const volts=render(HistoryPlot,{samples,field:'pack_voltage_mv',divisor:1000,label:'电池电压',unit:'V'});
 assert(volts.includes('52.40'));assert(volts.includes('0.00 V'));assert(!volts.includes('99.999'));assert(!volts.includes('NaN'));assert(volts.includes('<polyline'));assert(volts.includes('<circle'));assert(volts.includes('40s'));
 const currentChart=render(HistoryPlot,{samples:[sample(10)],field:'current_deci_amps',divisor:10,label:'电池电流',unit:'A'});assert(currentChart.includes('-12.6 A'));
 assert(render(RideHistory,{sessions:[],language:'zh',load:async()=>{}}).includes('暂无行驶历史会话'));
 const initial=render(RideHistory,{sessions:[session],language:'zh',load:async()=>{}});assert(initial.includes('未校准'));assert(!initial.includes('1970'));assert(!initial.includes('回放'));assert(!initial.includes('导出'));
 console.log('real endpoints and units, overlapping cursor pages, duplicate clicks, retries, stale sessions, disposal, missing responses and real SVG charts passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
'''], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)


if __name__ == "__main__":
    unittest.main()
