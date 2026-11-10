import {telemetry} from './fixtures.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {PacketReader,packet,mqttString,parsePublish,concat,MqttConnection} from '../dist/mqtt.js';
import {TelemetryStore} from '../dist/protocol.js';
const enc=new TextEncoder();

test('packet reader reassembles fragmented and coalesced MQTT messages',()=>{
  const a=packet(0x30,new Uint8Array(180).fill(17)),b=packet(0xd0),bytes=concat(a,b),reader=new PacketReader();
  assert.equal(reader.push(bytes.slice(0,2)).length,0);assert.equal(reader.push(bytes.slice(2,50)).length,0);
  const out=reader.push(bytes.slice(50));assert.equal(out.length,2);assert.equal(out[0].body.length,180);assert.equal(out[1].header,0xd0);
});
test('packet reader bounds untrusted lengths and rejects malformed varints',()=>{
  assert.throws(()=>new PacketReader().push(new Uint8Array([0x30,0x80,0x80,0x80,0x80])),/limit|Malformed/);
  assert.throws(()=>new PacketReader().push(new Uint8Array([0x30,0xff,0xff,0x7f])),/limit/);
});
test('publish parser preserves binary bytes, retain flag and packet ID',()=>{
  const body=concat(mqttString('car/telemetry'),new Uint8Array([0,7]),new Uint8Array([0,255,128,65]));
  const result=parsePublish(0x33,body);assert.equal(result.topic,'car/telemetry');assert.equal(result.retained,true);assert.deepEqual([...result.id],[0,7]);assert.deepEqual([...result.payload],[0,255,128,65]);
  assert.throws(()=>parsePublish(0x30,new Uint8Array([0,8,65])));assert.throws(()=>parsePublish(0x34,body));
});
class FakeSocket{
  static instances=[];
  constructor(url,protocols){this.url=url;this.protocols=protocols;this.readyState=0;this.sent=[];FakeSocket.instances.push(this);}
  open(){this.readyState=1;this.onopen();}
  send(bytes){this.sent.push(bytes);}
  receive(bytes){this.onmessage({data:bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength)});}
  close(){this.readyState=3;this.onclose?.();}
}
const options={url:'ws://localhost:9001',telemetryTopic:'car/telemetry',heartbeatTopic:'car/heartbeat'};
test('live connection waits for both SUBACKs before declaring readiness',()=>{
  const statuses=[],messages=[];
  const c=new MqttConnection(options,{WebSocketClass:FakeSocket,onStatus:s=>statuses.push(s),onMessage:m=>messages.push(m)});
  try{
    c.connect();const ws=FakeSocket.instances.at(-1);ws.open();assert.equal(ws.sent[0][0],0x10);
    ws.receive(packet(0x20,new Uint8Array([0,0])));assert.equal(c.ready,false);assert.equal(ws.sent[1][0],0x82);assert.ok(!statuses.includes('connected'));
    ws.receive(packet(0x90,new Uint8Array([0,1,0,0])));assert.equal(c.ready,true);assert.equal(statuses.at(-1),'connected');
    ws.receive(packet(0x31,concat(mqttString('car/telemetry'),new Uint8Array([0,255]))));assert.equal(messages[0].retained,true);assert.deepEqual([...messages[0].payload],[0,255]);
    c.publish('car/command',enc.encode('STATUS'));assert.equal(ws.sent.at(-1)[0],0x30);
  }finally{c.disconnect();}
});
test('subscription failure never enables commands and disconnect cancels retries',()=>{
  const statuses=[];const c=new MqttConnection(options,{WebSocketClass:FakeSocket,onStatus:s=>statuses.push(s)});
  try{c.connect();const ws=FakeSocket.instances.at(-1);ws.open();ws.receive(packet(0x20,new Uint8Array([0,0])));ws.receive(packet(0x90,new Uint8Array([0,1,0,128])));assert.equal(c.ready,false);assert.ok(statuses.includes('error'));assert.throws(()=>c.publish('car/command',new Uint8Array()),/not subscribed/);}finally{c.disconnect();}
  assert.equal(c.closed,true);
});
test('unsafe or ambiguous broker/topic settings are rejected before opening a socket',()=>{
  for(const bad of [{url:'http://localhost'},{url:'ws://user:pass@localhost'},{heartbeatTopic:'car/telemetry'},{telemetryTopic:'car/#'}])assert.throws(()=>new MqttConnection({...options,...bad},{WebSocketClass:FakeSocket}).connect());
});

test('a subscribed dashboard stays empty until fresh telemetry arrives',()=>{
  const store=new TelemetryStore();
  const c=new MqttConnection(options,{WebSocketClass:FakeSocket,onMessage:message=>store.ingest(message.payload,'telemetry',1000,message.retained,message.topic)});
  try{
    c.connect();const ws=FakeSocket.instances.at(-1);ws.open();
    ws.receive(packet(0x20,new Uint8Array([0,0])));
    ws.receive(packet(0x90,new Uint8Array([0,1,0,0])));
    assert.equal(c.ready,true);assert.equal(store.sample,null);assert.equal(store.history.length,0);
    const body=concat(mqttString('car/telemetry'),telemetry({sequence:1}));
    ws.receive(packet(0x31,body));
    assert.equal(store.sample,null);assert.equal(store.history.length,0);
    ws.receive(packet(0x30,body));
    assert.equal(store.sample.obstacle_mm,451);assert.equal(store.sample.left_mps,null);assert.equal(store.history.length,1);
    assert.deepEqual(store.sample.ir,[null,1,0]);
    const unavailable=concat(mqttString('car/telemetry'),telemetry({sequence:2,line:0}));
    ws.receive(packet(0x30,unavailable));assert.deepEqual(store.sample.ir,[null,null,null]);
    c.disconnect();ws.receive(packet(0x30,body));assert.equal(store.history.length,2);
  }finally{c.disconnect();}
});

test('current 11-byte telemetry survives MQTT framing and reach the telemetry store',()=>{
  const store=new TelemetryStore();
  const c=new MqttConnection(options,{WebSocketClass:FakeSocket,onMessage:m=>store.ingest(m.payload,'telemetry',1000,m.retained,m.topic)});
  try {
    c.connect();const ws=FakeSocket.instances.at(-1);ws.open();
    ws.receive(packet(0x20,new Uint8Array([0,0])));ws.receive(packet(0x90,new Uint8Array([0,1,0,0])));
    const bytes=telemetry();
    const frame=packet(0x30,concat(mqttString(options.telemetryTopic),bytes));
    ws.receive(frame.subarray(0,5));assert.equal(store.sample,null);
    ws.receive(frame.subarray(5));
    assert.equal(store.sample.sequence,12);assert.equal(store.sample.obstacle_mm,451);
    assert.deepEqual(store.sample.ir,[null,1,0]);assert.equal(store.lastPayloadBytes,11);
    assert.deepEqual(store.packets[0].bytes,bytes);
  } finally { c.disconnect(); }
});
