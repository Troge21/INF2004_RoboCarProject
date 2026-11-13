import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdtempSync,readFileSync,writeFileSync,unlinkSync,rmdirSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
import {forwardSequence,baselineReady,assessTrial,keepaliveAction} from '../recovery_probe.mjs';
function trial() {
  return {baseline:{at_ms:100,uptime_ms:100000,round_trip_ms:20},restore_marker_ms:20000,
    telemetry:Array.from({length:5},(_,i)=>({at_ms:21000+i*500,uptime_ms:120900+i*500,session_token:17})),heartbeats:[{at_ms:22000},{at_ms:27000}],
    status:{at_ms:22000,uptime_ms:121900,round_trip_ms:20,sample_age_ms:100,test_build:true,session_token:17}};
}
test('recovery baseline requires fresh real telemetry, heartbeat and subscribed connection',()=>{
  assert.ok(baselineReady(10000,9000,5000,true));
  for(const args of [[10000,null,5000,true],[10000,7000,5000,true],[10000,9000,null,true],
    [10000,9000,3000,true],[10000,9000,5000,false],[10000,10001,5000,true]])assert.equal(baselineReady(...args),false);
});
test('observer accepts forward/wrapped sequence and rejects duplicate/backward half-range',()=>{
  assert.ok(forwardSequence(0xffffffff,0));assert.ok(forwardSequence(5,6));
  assert.equal(forwardSequence(5,5),false);assert.equal(forwardSequence(5,4),false);
  assert.equal(forwardSequence(0,0x80000000),false);
});
test('complete recovery has a conservative marker-based upper bound',()=>{
  const r=assessTrial(trial());assert.ok(r.functional_recovery);assert.ok(r.within_conservative_15s_target);
  assert.equal(r.recovery_upper_bound_ms,1000);
  assert.equal(r.telemetry_intervals.max_ms,500);assert.equal(r.heartbeat_intervals.max_ms,5000);
});
test('Pico reboot, stale status and incomplete samples cannot count as recovery passes',()=>{
  for(const change of [t=>t.status.uptime_ms=500,t=>t.status.sample_age_ms=null,
    t=>t.status.test_build=false,t=>t.telemetry=[],t=>t.heartbeats=[],t=>t.status=null]) {
    const t=trial();change(t);assert.equal(assessTrial(t).within_conservative_15s_target,false);
  }
});
test('uptime wrap remains continuous; over-target result retains functional evidence',()=>{
  const t=trial();t.baseline.uptime_ms=0xfffffff0;t.status.uptime_ms=(0xfffffff0+21900)>>>0;
  assert.ok(assessTrial(t).uptime_continuous);
  t.telemetry[0].at_ms=36000;
  assert.ok(assessTrial(t).functional_recovery);assert.equal(assessTrial(t).within_conservative_15s_target,false);
});
test('uptime continuity includes network delay in the baseline AND recovery exchanges',()=>{
  // Actual second live trial: baseline request/reply took 2348 ms.
  const t=trial();t.baseline={at_ms:300366,uptime_ms:394450,round_trip_ms:2348};
  t.status={at_ms:345961,uptime_ms:442230,round_trip_ms:57,sample_age_ms:130,test_build:true};
  assert.ok(assessTrial(t).uptime_continuous);
  t.status.uptime_ms=500;assert.equal(assessTrial(t).uptime_continuous,false);
  t.status.uptime_ms=450000;assert.equal(assessTrial(t).uptime_continuous,false);
});
test('incoming telemetry cannot postpone required outbound MQTT keepalive',()=>{
  assert.equal(keepaliveAction(14999,0,null),'wait');
  assert.equal(keepaliveAction(15000,0,null),'ping');
  // Last outbound packet, rather than incoming traffic, controls the deadline.
  assert.equal(keepaliveAction(30000,15000,null),'ping');
  assert.equal(keepaliveAction(20000,15000,15000),'wait');
  assert.equal(keepaliveAction(20001,15000,15000),'close');
});
test('CLI refuses an outage without live baseline and preserves existing evidence',()=>{
  const dir=mkdtempSync(join(tmpdir(),'robocar-recovery-probe-')),out=join(dir,'capture.json');
  const script=fileURLToPath(new URL('../recovery_probe.mjs',import.meta.url));
  const args=[script,'--kind','broker','--host','127.0.0.1','--port','1','--out',out];
  try {
    const child=spawnSync(process.execPath,args,{input:'loss\nfinish\n',encoding:'utf8',timeout:5000});
    assert.equal(child.status,0,child.stderr);assert.match(child.stdout,/Cannot start: need fresh telemetry AND heartbeat/);
    const report=JSON.parse(readFileSync(out,'utf8'));
    assert.equal(report.recorded_attempts,0);assert.equal(report.complete_functional_trials,0);
    assert.equal(report.within_conservative_target,0);
    writeFileSync(out,'preserve evidence');
    const repeat=spawnSync(process.execPath,args,{encoding:'utf8',timeout:5000});
    assert.equal(repeat.status,1);assert.equal(readFileSync(out,'utf8'),'preserve evidence');
  } finally {unlinkSync(out);rmdirSync(dir);}
});
