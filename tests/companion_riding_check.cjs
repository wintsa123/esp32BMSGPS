const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../android-cast/app/src/main/assets/companion-riding.html'), 'utf8');
const script = [...html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)].map(match => match[1]).join('\n');

function fixture({delayGeocode = false, routeError = false, loadError = false} = {}) {
  const calls = {loads: 0, geocodes: 0, rides: 0, replies: [], policy: null};
  const location = (lng, lat) => ({getLng: () => lng, getLat: () => lat});
  const config = {};
  const sdk = {
    getConfig: () => config,
    Geocoder: class {
      getLocation(query, callback) {
        calls.geocodes++;
        const complete = () => callback('complete', {geocodes: [{location: location(116.4, 39.85)}]});
        if (delayGeocode) calls.geocodeCallback = complete;
        else complete();
      }
    },
    Riding: class {
      constructor(options) { calls.policy = options.policy; }
      search(origin, destination, callback) {
        calls.rides++;
        calls.origin = origin;
        calls.destination = destination;
        if (routeError) callback('error', {info: 'denied:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'});
        else callback('complete', {routes: [{time: 120, rides: [
          {instruction: '直行', road: '测试路', path: [location(116.39, 39.84), location(116.395, 39.845)]},
          {instruction: '右转', road: '终点路', path: [location(116.395, 39.845), location(116.4, 39.85)]}
        ]}]});
      }
    }
  };
  const sandbox = {console, AMapLoader: {load(options) {
    calls.loads++;
    assert.equal(sandbox._AMapSecurityConfig.securityJsCode, 'security-test');
    assert.equal(options.key, 'key-test');
    assert.deepEqual(Array.from(options.plugins), ['AMap.Riding', 'AMap.Geocoder']);
    return loadError && calls.loads === 1 ? Promise.reject(new Error('offline')) : Promise.resolve(sdk);
  }}, BmsRiding: {complete(id, payload) { calls.replies.push({id, ...JSON.parse(payload)}); }}};
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  vm.runInContext(script, sandbox);
  return {calls, sandbox, config};
}
const request = (id, changes = {}) => ({id, key: 'key-test', securityCode: 'security-test', origin: [116.39, 39.84], name: '北京南站', ...changes});
async function main() {
  const {sandbox, calls, config} = fixture();
  await sandbox.planRiding(request('1'));
  assert.equal(calls.replies[0].status, '1');
  assert.equal(calls.geocodes, 1);
  assert.equal(calls.policy, 0);
  assert.equal(config.appname, 'amap-jsapi-skill');
  const result = calls.replies[0];
  assert.equal(result.route.paths[0].duration, 120);
  assert.equal(result.route.paths[0].steps[1].instruction, '右转');
  assert.equal(result.route.paths[0].steps[0].polyline, '116.39,39.84;116.395,39.845');
  await sandbox.planRiding(request('2', {destination: result.destination}));
  assert.equal(calls.loads, 1);
  assert.equal(calls.geocodes, 1);
  assert.equal(calls.rides, 2);
  await sandbox.planRiding(request('3', {name: '116.400000,39.850000'}));
  assert.equal(calls.geocodes, 1);
  assert.deepEqual(Array.from(calls.destination), [116.4, 39.85]);
  await sandbox.planRiding(request('4', {name: '181,39.85'}));
  assert.equal(calls.replies.at(-1).status, '0');
  assert.equal(calls.rides, 3);

  const cancelled = fixture({delayGeocode: true});
  const old = cancelled.sandbox.planRiding(request('old'));
  await new Promise(resolve => setImmediate(resolve));
  cancelled.sandbox.cancelRiding();
  cancelled.calls.geocodeCallback();
  await old;
  assert.equal(cancelled.calls.rides, 0);
  assert.equal(cancelled.calls.replies.length, 0);
  const stale = cancelled.sandbox.planRiding(request('stale'));
  await new Promise(resolve => setImmediate(resolve));
  const resumeOld = cancelled.calls.geocodeCallback;
  await cancelled.sandbox.planRiding(request('current', {destination: [116.4, 39.85]}));
  resumeOld();
  await stale;
  assert.deepEqual(cancelled.calls.replies.map(reply => reply.id), ['current']);

  const denied = fixture({routeError: true});
  await denied.sandbox.planRiding(request('denied'));
  assert.equal(denied.calls.replies[0].status, '0');
  assert.ok(!denied.calls.replies[0].info.includes('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'));
  const retry = fixture({loadError: true});
  await retry.sandbox.planRiding(request('offline'));
  assert.equal(retry.calls.replies[0].status, '0');
  await retry.sandbox.planRiding(request('retry'));
  assert.equal(retry.calls.replies[1].status, '1');
  console.log('PASS: JSAPI riding, geocoding, cached destination, coordinate bounds, cancellation, stale replies, credential redaction, and loader retry');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
