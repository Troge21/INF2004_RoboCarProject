import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeTelemetry,decodeHeartbeat,decodeScan,decodeDiagnostics,decodeTerrain,TelemetryStore,packetText} from '../dist/protocol.js';
import {telemetry,heartbeat,statusReply,scanChunk,diagnostics,putBits,syncStore} from './fixtures.mjs';
test('v4 real motion preserves signed counts/net distance and separates path length',()=>{
  const s=decodeTelemetry(telemetry({left:1191,right:-1191,leftCount:2147483647,rightCount:-2147483647,net:-1234,travel:0})).sample;
  assert.equal(s.left_mps,.1191);assert.equal(s.right_mps,-.1191);
  assert.equal(s.left_encoder_count,2147483647);assert.equal(s.right_encoder_count,-2147483647);
  assert.equal(s.net_distance_m,-.1234);assert.equal(s.distance_m,0);
});
test('S4 terrain checks freshness, reports events once and preserves main-frame cadence',()=>{
  const store=syncStore(new TelemetryStore());
  const b=new Uint8Array(15),v=new DataView(b.buffer),body=b.subarray(9);
  b[0]=0xb7;v.setUint32(1,0x12345678,true);v.setUint32(5,1000,true);
  for(const [offset,width,n] of [[0,11,849],[11,9,256],[20,4,10],[24,2,3],[26,16,65535]])
    putBits(body,offset,width,n);
  assert.equal(decodeTerrain(b).roll_deg,-5.2);
  assert.equal(store.ingest(b,'telemetry',1000),true);
  assert.equal(store.currentTerrain(1000).turn_deg_s,0);
  assert.equal(store.currentTerrain(1000).motion_class,'Pickup');
  assert.equal(store.ingest(b,'telemetry',1200),true);
  assert.equal(store.events.filter(e=>e.title==='Pickup').length,1);
  assert.equal(store.events.filter(e=>e.title==='Hump measured').length,1);
  assert.equal(store.telemetryCount,0);
  assert.equal(store.currentTerrain(3001),null);
  putBits(body,42,1,1);
  assert.throws(()=>decodeTerrain(b),/terrain packet/);
});
test('zero measurements differ from unavailable sentinels',()=>{
  const missing=decodeTelemetry(telemetry()).sample;
  assert.equal(missing.left_mps,null);assert.equal(missing.distance_m,null);
  const zero=decodeTelemetry(telemetry({left:0,right:0,leftCount:0,rightCount:0,net:0,travel:0})).sample;
  assert.equal(zero.left_mps,0);assert.equal(zero.left_encoder_count,0);assert.equal(zero.distance_m,0);
});
test('independent canonical missing-data byte vector',()=>{
  const body=[0,0,0,0,0,7,254,255,255,31,0,0,0,16,0,0,0,16,0,0,0,240,255,255,255,31,0,0,0,0,0,0,31];
  const b=Uint8Array.from([180,1,0,0,0,0,0,0,0,17,0,0,0,...body]);
  const s=decodeTelemetry(b).sample;
  assert.equal(s.sequence,1);assert.equal(s.imu_state,null);assert.equal(s.left_mps,null);
  assert.equal(s.left_encoder_count,null);assert.equal(s.net_distance_m,null);assert.equal(s.distance_m,null);
});
test('only current v4 telemetry is accepted',()=>{
  for(const old of [new Uint8Array(6),new Uint8Array(11),new TextEncoder().encode('{"distance":20}')])assert.throws(()=>decodeTelemetry(old),/v4/);
  const b=telemetry();b[0]=0xb3;assert.throws(()=>decodeTelemetry(b));
});
test('all IR codes retain physical left/right roles',()=>{
  for(const line of [0,4,5,6,7]){
    const s=decodeTelemetry(telemetry({line})).sample;
    assert.deepEqual(s.ir,line?[null,(line>>1)&1,line&1]:[null,null,null]);
  }
  assert.throws(()=>decodeTelemetry(telemetry({line:3})));
});
test('malformed ranges, source flags and fault coverage are rejected',()=>{
  for(const options of [{distance:19},{distance:4001},{pitch:1802},{height:5},{height:80,max:6},{state:7},
    {known:1,fault:2},{barcode:44},{barcode:0,event:1},{barcode:0,navigation:1},{navigation:5}])
    assert.throws(()=>decodeTelemetry(telemetry(options)));
  const b=telemetry();putBits(b.subarray(13),49,14,10001);assert.throws(()=>decodeTelemetry(b));
});
test('Code39 character, reversal, event and owner navigation are distinct',()=>{
  const s=decodeTelemetry(telemetry({barcode:75,event:65535,navigation:1})).sample;
  assert.equal(s.barcode,'A');assert.equal(s.barcode_reversed,true);
  assert.equal(s.barcode_navigation,'Turn left');assert.equal(s.barcode_event_sequence,65535);
});
test('IMU faults and missing pitch never erase independent distance',()=>{
  const s=decodeTelemetry(telemetry({pitch:0,state:6,flags:32,known:64,fault:64})).sample;
  assert.equal(s.pitch_deg,null);assert.equal(s.imu_state,'Sensor fault');assert.equal(s.obstacle_mm,451);
  assert.deepEqual(s.faults,['IMU sensor fault']);
});
test('partial fault knowledge is preserved, not global all-clear',()=>{
  const s=decodeTelemetry(telemetry({known:128,fault:0})).sample;
  assert.equal(s.fault_known_mask,128);assert.deepEqual(s.faults,[]);
  assert.equal(decodeTelemetry(telemetry()).sample.faults,null);
});
test('clock sync is required before live measurements',()=>{
  const s=new TelemetryStore();assert.equal(s.ingest(telemetry(),'telemetry',1000),true);
  assert.equal(s.currentSample(1000),null);assert.equal(s.needsClockSync(1000),true);
  syncStore(s);assert.equal(s.ingest(telemetry(),'telemetry',1001),true);
  assert.equal(s.currentSample(1001).obstacle_mm,451);
});
test('delayed frames expire against fresh Pico uptime and live values expire',()=>{
  const s=syncStore(new TelemetryStore(),{uptime:5000});
  assert.equal(s.ingest(telemetry({uptime:1000}),'telemetry',1001),false);
  assert.equal(s.ingest(telemetry({uptime:5000}),'telemetry',1002),true);
  assert.equal(s.currentSample(3003),null);assert.equal(s.history.length,1);
});
test('sequence duplicate/backward rejection, gaps and wrap',()=>{
  const s=syncStore(new TelemetryStore());
  for(const sequence of [0xfffffffe,0xffffffff,0,2])assert.equal(s.ingest(telemetry({sequence}),'telemetry',1001),true);
  assert.equal(s.sequenceGaps,1);
  assert.equal(s.ingest(telemetry({sequence:2}),'telemetry',1002),false);
  assert.equal(s.ingest(telemetry({sequence:1}),'telemetry',1002),false);
});
test('heartbeat replay is rejected and wrap does not reset telemetry replay baseline',()=>{
  const s=syncStore(new TelemetryStore());s.ingest(telemetry({sequence:10}),'telemetry',1001);
  assert.equal(s.ingest(heartbeat(0xffffffff),'heartbeat',1001),true);
  assert.equal(s.ingest(heartbeat(0),'heartbeat',1002),true);
  assert.equal(s.ingest(heartbeat(0),'heartbeat',1003),false);
  assert.equal(s.ingest(telemetry({sequence:9}),'telemetry',1003),false);
  assert.equal(decodeHeartbeat(heartbeat(0xffffffff)),0xffffffff);
});
test('fresh new session resets live state, not history',()=>{
  const s=syncStore(new TelemetryStore());s.ingest(telemetry(),'telemetry',1001);
  s.beginStatus(2,2000);
  assert.equal(s.ingest(statusReply({id:2,uptime:10,session:99}),'status',2020),true);
  assert.equal(s.currentSample(2020),null);assert.equal(s.history.length,1);
  assert.equal(s.ingest(telemetry({sequence:0,session:99,uptime:10}),'telemetry',2021),true);
});
test('retained packets and lost connections never make measurements current',()=>{
  const s=syncStore(new TelemetryStore());
  assert.equal(s.ingest(telemetry(),'telemetry',1001,true),false);
  s.ingest(telemetry(),'telemetry',1002);s.clearController();
  assert.equal(s.currentSample(1003,true),null);
});
test('history, inspector and activity are bounded',()=>{
  const s=syncStore(new TelemetryStore());
  for(let i=0;i<800;i++)s.ingest(telemetry({sequence:i,uptime:1000+i}),'telemetry',1000+i);
  for(let i=0;i<100;i++)s.ingest(new Uint8Array(),'telemetry',1800);
  assert.equal(s.history.length,600);assert.equal(s.packets.length,30);assert.equal(s.events.length,80);
  assert.match(packetText(s.packets[0]),/Rejected/);
});
test('barcode events are not duplicated after a brief missing snapshot',()=>{
  const s=syncStore(new TelemetryStore());
  s.ingest(telemetry({sequence:1,barcode:11,event:5}),'telemetry',1001);
  s.ingest(telemetry({sequence:2}),'telemetry',1002);
  s.ingest(telemetry({sequence:3,barcode:11,event:5}),'telemetry',1003);
  assert.equal(s.events.filter(e=>e.title==='Barcode decoded').length,1);
});
test('181 point profile reassembles without treating chunks as telemetry samples',()=>{
  const s=syncStore(new TelemetryStore());
  for(let start=0;start<181;start+=16)assert.equal(s.ingest(scanChunk({start}),'telemetry',1001+start),true);
  assert.equal(s.scanProfile.points.length,181);assert.equal(s.scanProfile.points[180].angle,180);
  assert.equal(s.history.length,0);assert.equal(s.scanPending,null);
});
test('scan truncation, point ranges, order and reserved bits are rejected',()=>{
  assert.throws(()=>decodeScan(scanChunk().slice(0,-1)));
  assert.throws(()=>decodeScan(scanChunk({points:[{angle:181,distance_mm:451}]})));
  assert.throws(()=>decodeScan(scanChunk({points:[{angle:0,distance_mm:19}]})));
  assert.throws(()=>decodeScan(scanChunk({points:[{angle:1,distance_mm:451},{angle:0,distance_mm:451}]})));
  const b=scanChunk({total:1});b[b.length-1]|=128;assert.throws(()=>decodeScan(b));
});
test('width is accepted from the first chunk of its own complete scan only',()=>{
  const s=syncStore(new TelemetryStore());
  const first=scanChunk({total:25,width:126});
  assert.equal(decodeScan(first).width_mm,126);
  assert.equal(s.ingest(first,'telemetry',1001),true);
  assert.equal(s.scanProfile,null);
  assert.equal(s.ingest(scanChunk({total:25,width:200}),'telemetry',1002),false);
  assert.equal(s.ingest(scanChunk({total:25,start:16}),'telemetry',1003),true);
  assert.equal(s.scanProfile.width_mm,126);
  assert.equal(s.ingest(scanChunk({sequence:2,total:3}),'telemetry',1004),true);
  assert.equal(s.scanProfile.width_mm,null);
  assert.throws(()=>decodeScan(scanChunk({total:25,start:16,width:126})));
  assert.throws(()=>decodeScan(scanChunk({total:3,width:1024})));
  assert.throws(()=>decodeScan(scanChunk({total:3,width:0})));
  const invalid=scanChunk({total:3,width:80});invalid[23]|=128;
  assert.throws(()=>decodeScan(invalid));
});
test('resource unknown is distinct from valid zero; capacities are not peaks',()=>{
  const d=decodeDiagnostics(diagnostics({values:[82596,0,0,0,12288,null]}));
  assert.equal(d.heap_free_bytes,0);assert.equal(d.stack_high_water_bytes,null);
  assert.equal(d.task_stack_capacity_bytes,12288);
  assert.throws(()=>decodeDiagnostics(diagnostics({values:[270337,0,0,0,0,0]})));
});
test('diagnostic history is bounded and disconnected/stale values unavailable',()=>{
  const s=syncStore(new TelemetryStore());
  for(let i=0;i<125;i++)s.ingest(diagnostics({uptime:1000+i}),'telemetry',1000+i);
  assert.equal(s.diagnosticHistory.length,120);assert.equal(s.currentDiagnostics(8125),null);
  assert.equal(s.currentDiagnostics(1125,false),null);
});


test('average wheel speed uses both signed velocities and preserves unknown/zero',()=>{
  const mean=decodeTelemetry(telemetry({left:1191,right:-1191})).sample;
  assert.equal(mean.average_wheel_mps,0.1191);
  assert.equal(decodeTelemetry(telemetry({left:0,right:0})).sample.average_wheel_mps,0);
  assert.equal(decodeTelemetry(telemetry({left:null,right:0})).sample.average_wheel_mps,null);
  assert.equal(decodeTelemetry(telemetry({left:0,right:2000})).sample.average_wheel_mps,0.1);
});
