import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeTelemetry,decodeHeartbeat,packetText,TelemetryStore,encodeStatusRequest,decodeStatusReply} from '../dist/protocol.js';
import {telemetry,heartbeat,statusReply} from './fixtures.mjs';

test('current C golden vector supplies distance, IR and live IMU without invented feeds',()=>{
  const bytes=new Uint8Array([12,0,0,0,0xc3,0xe1,0x1b,0x64,0x88,0x14,0]);
  const {sample,format}=decodeTelemetry(bytes);
  assert.match(format,/Packed v3/);assert.equal(sample.sequence,12);assert.equal(sample.obstacle_mm,451);
  assert.deepEqual(sample.ir,[null,1,0]);assert.equal(sample.pitch_deg,15);
  assert.equal(sample.hump_mm,12);assert.equal(sample.max_hump_mm,34);assert.equal(sample.imu_state,'Monitoring');
  assert.equal(sample.imu_speed_source,'nominal');assert.equal(sample.imu_flags,0x11);
  for(const key of ['state','left_mps','right_mps','distance_m','barcode','faults','scan'])assert.equal(sample[key],null);
});
test('offset byte views decode without using unrelated backing bytes',()=>{
  const bytes=telemetry(),offset=new Uint8Array([99,...bytes,99]).subarray(1,12);
  assert.deepEqual(decodeTelemetry(offset),decodeTelemetry(bytes));
});
test('obsolete formats are rejected with a clear current-firmware instruction',()=>{
  const enc=new TextEncoder();
  for(const bytes of [enc.encode('{"sequence":12}'),new Uint8Array([12,0,0,0,0xc3,0xe1]),
    new Uint8Array([12,0,0,0,0xc3,0x31]),new Uint8Array(),new Uint8Array(10),new Uint8Array(12)])
    assert.throws(()=>decodeTelemetry(bytes),/11-byte telemetry/);
});
test('line readings preserve all patterns and unavailable independently of no echo',()=>{
  for(const [line,expected] of [[0,[null,null,null]],[4,[null,0,0]],[5,[null,0,1]],[6,[null,1,0]],[7,[null,1,1]]]) {
    const sample=decodeTelemetry(telemetry({line,distance:0})).sample;
    assert.deepEqual(sample.ir,expected);assert.equal(sample.line_bits,line);
    assert.equal(sample.obstacle_valid,false);assert.equal(sample.obstacle_mm,null);
  }
  for(const line of [1,2,3])assert.throws(()=>decodeTelemetry(telemetry({line})),/line sensor bits/);
});
test('distance boundary and reserved codes cannot appear as plausible measurements',()=>{
  for(const distance of [20,4000])assert.equal(decodeTelemetry(telemetry({distance})).sample.obstacle_mm,distance);
  for(const distance of [1,19,4001,4095])assert.throws(()=>decodeTelemetry(telemetry({distance})),/Obstacle distance/);
});
test('full signed pitch range and unavailable angle are distinct',()=>{
  for(const [pitch,expected] of [[0,null],[1,-90],[901,0],[1801,90]])
    assert.equal(decodeTelemetry(telemetry({pitch})).sample.pitch_deg,expected);
  assert.throws(()=>decodeTelemetry(telemetry({pitch:1802})));
});
test('accepted heights have a bounded range and no-event is not a measured zero',()=>{
  const empty=decodeTelemetry(telemetry({height:0,max:0})).sample;
  assert.equal(empty.hump_mm,null);assert.equal(empty.max_hump_mm,null);
  assert.equal(decodeTelemetry(telemetry({height:80,max:80})).sample.max_hump_mm,80);
  for(const fields of [{height:5},{height:81,max:81},{height:35,max:34}])
    assert.throws(()=>decodeTelemetry(telemetry(fields)));
});
test('a missing IMU leaves real distance and IR usable',()=>{
  const sample=decodeTelemetry(telemetry({pitch:0,height:0,max:0,state:7,flags:0})).sample;
  assert.equal(sample.imu_state,null);assert.equal(sample.pitch_deg,null);assert.equal(sample.max_hump_mm,null);
  assert.equal(sample.obstacle_mm,451);assert.deepEqual(sample.ir,[null,1,0]);
});
test('IMU fault invalidates angle while retaining accepted heights',()=>{
  const sample=decodeTelemetry(telemetry({pitch:0,state:6,flags:32})).sample;
  assert.equal(sample.imu_state,'Sensor fault');assert.equal(sample.pitch_deg,null);
  assert.equal(sample.max_hump_mm,34);assert.equal(sample.imu_flags,128);assert.equal(sample.obstacle_mm,451);
});
test('calibration and speed-source flags are explicit',()=>{
  assert.equal(decodeTelemetry(telemetry({pitch:0,state:1})).sample.imu_state,'Calibrating');
  assert.equal(decodeTelemetry(telemetry({flags:17})).sample.imu_speed_source,'supplied');
  for(const fields of [{state:7},{state:1},{state:6,flags:33},{flags:0}])
    assert.throws(()=>decodeTelemetry(telemetry(fields)));
});
test('marker, padding and unavailable-state canonical bits are checked',()=>{
  const wrong=telemetry();wrong[5]&=127;assert.throws(()=>decodeTelemetry(wrong),/marker/);
  const reserved=telemetry();reserved[10]|=128;assert.throws(()=>decodeTelemetry(reserved),/reserved/);
  assert.throws(()=>decodeTelemetry(telemetry({pitch:0,state:7})),/Unavailable/);
});
test('binary heartbeats preserve full counters and reject old text',()=>{
  for(const count of [0,12,0x30206268,0xffffffff])assert.equal(decodeHeartbeat(heartbeat(count)),count);
  for(const bytes of [new TextEncoder().encode('hb 0'),new TextEncoder().encode('hb 12'),new Uint8Array([128]),
    new Uint8Array([128,0,0,0,0,0]),new Uint8Array([0,0,0,0,0])])
    assert.throws(()=>decodeHeartbeat(bytes),/5-byte binary heartbeat/);
});
test('current values expire and disappear on disconnect without losing history',()=>{
  const store=new TelemetryStore();store.ingest(telemetry(),'telemetry',1000);
  assert.equal(store.currentSample(3000,true).pitch_deg,15);
  assert.equal(store.currentSample(3001,true),null);assert.equal(store.currentSample(1001,false),null);
  assert.equal(store.currentSample(999,true),null);assert.equal(store.history[0].pitch_deg,15);
});
test('retained and rejected data never refresh live values',()=>{
  const store=new TelemetryStore();store.ingest(telemetry(),'telemetry',1000);
  assert.equal(store.ingest(telemetry({sequence:13}),'telemetry',2000,true),false);
  assert.equal(store.ingest(new Uint8Array(6),'telemetry',2200),false);
  assert.equal(store.lastTelemetry,1000);assert.equal(store.sample.sequence,12);
  store.ingest(heartbeat(1),'heartbeat',3000,true);assert.equal(store.lastHeartbeat,null);
  assert.equal(store.rejected,3);
});
test('sequence wrap, gaps and replay checks apply to the single current format',()=>{
  const store=new TelemetryStore();store.ingest(telemetry({sequence:0xffffffff}),'telemetry',1000);
  assert.equal(store.ingest(telemetry({sequence:0}),'telemetry',1500),true);
  assert.equal(store.ingest(telemetry({sequence:3}),'telemetry',2000),true);assert.equal(store.sequenceGaps,2);
  assert.equal(store.ingest(telemetry({sequence:3}),'telemetry',2500),false);
  assert.equal(store.ingest(telemetry({sequence:2}),'telemetry',3000),false);assert.equal(store.lastTelemetry,2000);
});
test('a robot heartbeat restart resets the sequence baseline',()=>{
  const store=new TelemetryStore();store.ingest(telemetry({sequence:100}),'telemetry',1000);
  store.ingest(heartbeat(8),'heartbeat',1500);store.ingest(heartbeat(0),'heartbeat',2000);
  assert.equal(store.ingest(telemetry({sequence:0}),'telemetry',2500),true);
});
test('inspector uses each packet decoded value and readable hex on rejection',()=>{
  const store=new TelemetryStore();store.ingest(telemetry(),'telemetry',1000,false,'car/telemetry');
  const first=store.packets[0];store.ingest(heartbeat(0),'heartbeat',1100);
  assert.deepEqual(JSON.parse(packetText(store.packets[0])),{counter:0});
  store.ingest(telemetry({sequence:13,distance:20}),'telemetry',1500);
  assert.equal(JSON.parse(packetText(first)).obstacle_mm,451);assert.equal(first.topic,'car/telemetry');
  store.ingest(new Uint8Array([128,0]),'heartbeat',2000);
  assert.match(packetText(store.packets[0]),/Rejected/);assert.match(packetText(store.packets[0]),/80 00/);
});
test('browser history and diagnostics remain bounded',()=>{
  const store=new TelemetryStore();assert.equal(store.currentSample(1000),null);
  for(let i=0;i<1000;i++){store.ingest(telemetry({sequence:i}),'telemetry',i*500);store.event('test');}
  assert.equal(store.history.length,600);assert.equal(store.packets.length,30);assert.equal(store.events.length,80);
  assert.equal(store.telemetryCount,1000);
});

test('binary STATUS request is five bytes with an independent golden vector',()=>{
  assert.deepEqual([...encodeStatusRequest(12)],[160,12,0,0,0]);
  for(const id of [0,-1,1.5,0x100000000,'12'])assert.throws(()=>encodeStatusRequest(id));
});
test('STATUS no-sample golden vector preserves ID, uptime and test-build flag',()=>{
  const reply=decodeStatusReply(new Uint8Array([161,12,0,0,0,4,3,2,1,255,15]));
  assert.deepEqual(reply,{request_id:12,uptime_ms:0x01020304,test_build:true,sample_age_ms:null,snapshot:null});
  const max=decodeStatusReply(statusReply({id:0xffffffff,uptime:0xffffffff,age:2000,testBuild:false}));
  assert.equal(max.request_id,0xffffffff);assert.equal(max.uptime_ms,0xffffffff);assert.equal(max.test_build,false);
  assert.equal(max.sample_age_ms,2000);assert.equal(max.snapshot.pitch_deg,15);
});
test('STATUS decoder rejects malformed metadata, presence, reserved bits and snapshots',()=>{
  for(const bytes of [new Uint8Array(),statusReply().subarray(0,21),statusReply({id:0}),statusReply({age:2001})])
    assert.throws(()=>decodeStatusReply(bytes));
  const reserved=statusReply();reserved[10]|=128;assert.throws(()=>decodeStatusReply(reserved));
  const missing=statusReply();missing[9]=255;missing[10]=15;assert.throws(()=>decodeStatusReply(missing));
  const bad=statusReply();bad[16]&=127;assert.throws(()=>decodeStatusReply(bad));
});
test('STATUS requires a matching current ID and ignores retained/unsolicited replies',()=>{
  const store=new TelemetryStore();store.beginStatus(12,1000);
  assert.equal(store.ingest(statusReply({id:13}),'status',1100),false);assert.ok(store.pendingStatus);
  assert.equal(store.ingest(statusReply(),'status',1150,true),false);assert.ok(store.pendingStatus);
  assert.equal(store.ingest(statusReply(),'status',1200),true);assert.equal(store.pendingStatus,null);
  assert.equal(store.statusReply.round_trip_ms,200);
  assert.equal(store.ingest(statusReply(),'status',1250),false);
});
test('STATUS timeout and disconnect never retry or accept a delayed reply',()=>{
  const store=new TelemetryStore();store.beginStatus(12,1000);
  assert.equal(store.expireStatus(3999),false);assert.equal(store.expireStatus(4000),true);
  assert.equal(store.expireStatus(4500),false);assert.equal(store.ingest(statusReply(),'status',4500),false);
  store.beginStatus(13,5000);store.cancelStatus();
  assert.equal(store.ingest(statusReply({id:13}),'status',5100),false);assert.equal(store.statusReply,null);
});
test('STATUS has one outstanding request, fresh IDs and browser rate limiting',()=>{
  const store=new TelemetryStore();store.beginStatus(12,1000);
  assert.throws(()=>store.beginStatus(13,1100),/pending/);store.cancelStatus();
  assert.throws(()=>store.beginStatus(13,1100),/Wait briefly/);
  assert.throws(()=>store.beginStatus(12,1500),/fresh/);store.beginStatus(13,1500);
  assert.equal(store.pendingStatus.id,13);
});
test('STATUS snapshots stay separate from the live stream and inspector keeps each value',()=>{
  const store=new TelemetryStore();store.ingest(telemetry({sequence:99}),'telemetry',900);
  store.beginStatus(12,1000);store.ingest(statusReply(),'status',1100,false,'car/status');
  assert.equal(store.statusReply.snapshot.sequence,12);assert.equal(store.sample.sequence,99);
  assert.equal(store.history.length,1);assert.equal(store.lastTelemetry,900);
  assert.equal(JSON.parse(packetText(store.packets[0])).request_id,12);
  assert.equal(store.packets[0].topic,'car/status');
});
