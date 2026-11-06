import test from 'node:test';
import assert from 'node:assert/strict';
import {decodeTelemetry,decodeCompact,decodePackedV2,decodeHeartbeat,packetText,TelemetryStore,encoder} from '../dist/protocol.js';
const payload=(overrides={})=>encoder.encode(JSON.stringify({sequence:12,obstacle_mm:451,obstacle_valid:true,...overrides}));

test('current S1 firmware JSON decodes without inventing other sensor feeds',()=>{
  const {sample,format}=decodeTelemetry(payload());
  assert.equal(sample.obstacle_mm,451);assert.equal(sample.left_mps,null);assert.equal(sample.state,null);assert.equal(sample.faults,null);assert.match(format,/legacy firmware/);
});
test('no echo stays null and never becomes zero or clear space',()=>{
  assert.equal(decodeTelemetry(payload({obstacle_mm:null,obstacle_valid:false})).sample.obstacle_mm,null);
  assert.throws(()=>decodeTelemetry(payload({obstacle_mm:4000,obstacle_valid:false})),/must be null/);
});

test('packed IR decodes all four centre/right patterns without a left line reading',()=>{
  for (const [line_bits,expected] of [[4,[null,0,0]],[5,[null,0,1]],[6,[null,1,0]],[7,[null,1,1]]])
    assert.deepEqual(decodeTelemetry(payload({line_bits})).sample.ir,expected);
});

test('unavailable IR stays distinct from valid background and overrides older array data',()=>{
  const sample=decodeTelemetry(payload({line_bits:0,ir:[1,1,1]})).sample;
  assert.deepEqual(sample.ir,[null,null,null]);assert.equal(sample.obstacle_mm,451);
  assert.equal(decodeTelemetry(payload()).sample.ir,null);
  assert.deepEqual(decodeTelemetry(payload({ir:[1,0,1]})).sample.ir,[null,0,1]);
  assert.deepEqual(decodeTelemetry(payload({ir:[null,null,1]})).sample.ir,[null,null,1]);
});

test('reserved, noncanonical and nonnumeric IR fields are rejected',()=>{
  for (const line_bits of [-1,1,2,3,8,256,4.5,'6',null,true])
    assert.throws(()=>decodeTelemetry(payload({line_bits})),/line sensor bits/);
});

test('fresh IR disappears on expiry or disconnect while real history remains',()=>{
  const s=new TelemetryStore();s.ingest(payload({line_bits:6}),'telemetry',1000);
  assert.deepEqual(s.currentSample(3000,true).ir,[null,1,0]);
  assert.equal(s.currentSample(3001,true),null);assert.equal(s.currentSample(1001,false),null);
  assert.equal(s.currentSample(999,true),null);assert.equal(s.history.length,1);
  s.ingest(payload({sequence:13,line_bits:0}),'telemetry',3500);
  assert.deepEqual(s.currentSample(3500,true).ir,[null,null,null]);
});
test('malformed, out-of-range and ambiguous validity data are rejected',()=>{
  for(const overrides of [{obstacle_mm:19},{obstacle_mm:4001},{obstacle_valid:1},{sequence:-1},{sequence:4294967296},{sequence:1.5}])assert.throws(()=>decodeTelemetry(payload(overrides)));
  for(const data of [new Uint8Array(),encoder.encode('{bad'),encoder.encode('[]'),new Uint8Array([255,255])])assert.throws(()=>decodeTelemetry(data));
});
test('compact adapter matches documented six-byte golden vector',()=>{
  const bytes=new Uint8Array([0x0c,0,0,0,0xc3,0x31]);
  const sample=decodeCompact(bytes);assert.equal(sample.sequence,12);assert.equal(sample.obstacle_mm,451);assert.equal(sample.obstacle_valid,true);
  assert.equal(decodeTelemetry(bytes).sample.obstacle_mm,451);
  const subarray=new Uint8Array([99,...bytes,99]).subarray(1,7);assert.equal(decodeCompact(subarray).obstacle_mm,451);
});
test('compact adapter validates version, validity, length and maximum sequence',()=>{
  assert.equal(decodeCompact(new Uint8Array([255,255,255,255,0,0x20])).sequence,0xffffffff);
  assert.equal(decodeCompact(new Uint8Array([0,0,0,0,0,0x20])).obstacle_mm,null);
  for(const bytes of [[0,0,0,0,0,0],[0,0,0,0,1,0x20],[0,0,0,0,255,0x3f],[0,0,0]])assert.throws(()=>decodeCompact(new Uint8Array(bytes)));
});
test('heartbeats match the existing firmware format exactly',()=>{
  assert.equal(decodeHeartbeat(encoder.encode('hb 12')),12);
  for(const text of ['hb -1','heartbeat','hb 12\n','hb 123456789012'])assert.throws(()=>decodeHeartbeat(encoder.encode(text)));
});

test('actual packed v2 golden vector preserves binary zeros and line readings',()=>{
  const bytes=new Uint8Array([12,0,0,0,0xc3,0xe1]);
  const {sample,format}=decodeTelemetry(bytes);
  assert.match(format,/Packed v2/);assert.equal(sample.sequence,12);
  assert.equal(sample.line_bits,6);
  assert.equal(sample.obstacle_mm,451);assert.deepEqual(sample.ir,[null,1,0]);
  assert.equal(sample.left_mps,null);assert.equal(sample.state,null);
  const offset=new Uint8Array([99,...bytes,99]).subarray(1,7);
  assert.deepEqual(decodePackedV2(offset),sample);
});

test('packed v2 preserves no echo and all canonical line states independently',()=>{
  for(const [bits,ir] of [[0,[null,null,null]],[4,[null,0,0]],[5,[null,0,1]],[6,[null,1,0]],[7,[null,1,1]]]) {
    const bytes=new Uint8Array([255,255,255,255,0,0x80|(bits<<4)]);
    const sample=decodePackedV2(bytes);
    assert.equal(sample.sequence,0xffffffff);assert.equal(sample.obstacle_mm,null);
    assert.equal(sample.obstacle_valid,false);assert.deepEqual(sample.ir,ir);
  }
  assert.equal(decodePackedV2(new Uint8Array([0,0,0,0,20,0x80])).obstacle_mm,20);
  assert.equal(decodePackedV2(new Uint8Array([0,0,0,0,0xa0,0xff])).obstacle_mm,4000);
});

test('packed v2 rejects invalid lengths, tags, reserved ranges and line encodings',()=>{
  for(const length of [0,5,7])assert.throws(()=>decodePackedV2(new Uint8Array(length)));
  assert.throws(()=>decodePackedV2(new Uint8Array([0,0,0,0,20,0])),/marker/);
  for(const distance of [1,19,4001,4095])
    assert.throws(()=>decodePackedV2(new Uint8Array([0,0,0,0,distance&255,0x80|(distance>>>8)])));
  for(const bits of [1,2,3])
    assert.throws(()=>decodePackedV2(new Uint8Array([0,0,0,0,20,0x80|(bits<<4)])),/line sensor bits/);
});

test('packed heartbeats preserve full counter and remain unambiguous with legacy text',()=>{
  for(const count of [0,12,0x30206268,0xffffffff]) {
    const bytes=new Uint8Array(5);bytes[0]=0x80;new DataView(bytes.buffer).setUint32(1,count,true);
    assert.equal(decodeHeartbeat(bytes),count);
  }
  assert.equal(decodeHeartbeat(encoder.encode('hb 0')),0);
  assert.equal(decodeHeartbeat(encoder.encode('hb 4294967295')),0xffffffff);
  for(const bytes of [new Uint8Array([0x80]),new Uint8Array([0x80,0,0,0]),new Uint8Array([0x80,0,0,0,0,0])])
    assert.throws(()=>decodeHeartbeat(bytes),/5 bytes/);
  assert.throws(()=>decodeHeartbeat(encoder.encode('hb 4294967296')),/uint32/);
});

test('packed telemetry uses the same retention, freshness and sequence protections',()=>{
  const s=new TelemetryStore(),bytes=new Uint8Array([12,0,0,0,0xc3,0xe1]);
  assert.equal(s.ingest(bytes,'telemetry',1000,true),false);
  assert.equal(s.ingest(bytes,'telemetry',1000,false,'packed-v2'),true);
  assert.equal(s.lastPayloadBytes,6);assert.deepEqual(s.currentSample(2000).ir,[null,1,0]);
  assert.equal(s.ingest(bytes,'telemetry',1500),false);
  assert.equal(s.currentSample(3001),null);assert.equal(s.history.length,1);
});

test('packet inspector shows decoded binary counters and preserves each packet value',()=>{
  const s=new TelemetryStore();
  s.ingest(new Uint8Array([12,0,0,0,0xc3,0xe1]),'telemetry',1000);
  const first=s.packets[0];
  s.ingest(new Uint8Array([0x80,0,0,0,0]),'heartbeat',1100);
  assert.deepEqual(JSON.parse(packetText(s.packets[0])),{counter:0});
  s.ingest(new Uint8Array([13,0,0,0,20,0xc0]),'telemetry',1500);
  assert.equal(JSON.parse(packetText(first)).obstacle_mm,451);
  s.ingest(new Uint8Array([0x80,0]),'heartbeat',2000);
  assert.match(packetText(s.packets[0]),/Rejected/);assert.match(packetText(s.packets[0]),/80 00/);
  assert.doesNotMatch(packetText(s.packets[0]),/\uFFFD/);
});
test('retained and rejected packets never refresh live telemetry or heartbeat',()=>{
  const s=new TelemetryStore();s.ingest(payload(),'telemetry',1000);
  assert.equal(s.ingest(payload({sequence:13}),'telemetry',2000,true),false);
  assert.equal(s.lastTelemetry,1000);assert.equal(s.sample.sequence,12);
  s.ingest(encoder.encode('hb 1'),'heartbeat',3000,true);assert.equal(s.lastHeartbeat,null);
  assert.equal(s.rejected,2);assert.equal(s.packets[0].accepted,false);
});
test('sequence wrap, gaps, replay rejection and reboot observations',()=>{
  const s=new TelemetryStore();s.ingest(payload({sequence:0xffffffff}),'telemetry',1000);
  assert.equal(s.ingest(payload({sequence:0}),'telemetry',1500),true);
  assert.equal(s.ingest(payload({sequence:3}),'telemetry',2000),true);assert.equal(s.sequenceGaps,2);
  assert.equal(s.ingest(payload({sequence:3}),'telemetry',2500),false);assert.equal(s.ingest(payload({sequence:2}),'telemetry',3000),false);assert.equal(s.lastTelemetry,2000);
  s.ingest(encoder.encode('hb 8'),'heartbeat',3500);s.ingest(encoder.encode('hb 0'),'heartbeat',4000);
  assert.equal(s.ingest(payload({sequence:0}),'telemetry',4500),true);
});
test('history, packet and event buffers remain bounded',()=>{
  const s=new TelemetryStore();for(let i=0;i<1000;i++){s.ingest(payload({sequence:i}),'telemetry',i*500);s.event('test');}
  assert.equal(s.history.length,600);assert.equal(s.packets.length,30);assert.equal(s.events.length,80);assert.equal(s.telemetryCount,1000);
});
