import test from 'node:test';
import assert from 'node:assert/strict';
import {TelemetryStore,encodeStatusRequest,encodeStopRequest,decodeStopReply,decodeStatusReply,decodeControlWord} from '../dist/protocol.js';
import {syncStore,statusReply,stopReply,telemetry} from './fixtures.mjs';
const store=()=>syncStore(new TelemetryStore(),{at:700,uptime:700,state:3,testBuild:false});
function waiting(s,{state=3,testBuild=false}={}){
  s.beginStop(12,1100);
  assert.equal(s.ingest(statusReply({id:12,uptime:1100,state,testBuild}),'status',1150),true);
  return s.beginStopCommand(100,1160);
}
test('only current STATUS framing and consistent snapshot identity are accepted',()=>{
  assert.equal(decodeStatusReply(statusReply()).snapshot.sequence,12);
  assert.equal(decodeStatusReply(statusReply({age:null})).snapshot,null);
  assert.throws(()=>decodeStatusReply(new Uint8Array(26)));
  assert.throws(()=>decodeStatusReply(statusReply({frame:telemetry({session:7})})));
  assert.throws(()=>decodeStatusReply(statusReply({age:2001})));
});
test('full IDs and modulo deadlines survive command encoding',()=>{
  assert.equal(encodeStatusRequest(0xffffffff).length,5);
  const b=encodeStopRequest(0xffffffff,0xffffffff,0),v=new DataView(b.buffer);
  assert.equal(v.getUint32(1,true),0xffffffff);assert.equal(v.getUint32(9,true),0);
  assert.throws(()=>encodeStatusRequest(0));assert.throws(()=>encodeStopRequest(1,0,1));
});
test('all prepared controller states are bounded and latch invariants retained',()=>{
  for(let state=0;state<=13;state++)if(state!==6)assert.equal(decodeControlWord(state).controller_state_code,state);
  assert.throws(()=>decodeControlWord(14));assert.throws(()=>decodeControlWord(6));
  assert.throws(()=>decodeControlWord(16|8));assert.equal(decodeControlWord(16|6).stop_latched,true);
});
test('STOP requires a fresh handshake before constructing its one command',()=>{
  const s=store();s.beginStop(12,1100);
  assert.throws(()=>s.beginStopCommand(100,1101));
  s.ingest(statusReply({id:12,uptime:1100,state:3,testBuild:false}),'status',1150);
  const b=s.beginStopCommand(100,1160),v=new DataView(b.buffer);
  assert.equal(v.getUint32(9,true),3100);assert.equal(s.pendingStop.phase,'waiting');
  assert.throws(()=>s.beginStopCommand(101,1161));
});
test('expired preflight lease sends no STOP',()=>{
  const s=store();s.beginStop(12,1100);
  s.ingest(statusReply({id:12,uptime:1100,state:3,testBuild:false}),'status',1150);
  assert.throws(()=>s.beginStopCommand(100,3100),/lease|expired/i);
});
test('only matching nonretained timely controller reply confirms STOP',()=>{
  const s=store();waiting(s);
  assert.equal(s.ingest(stopReply({id:101}),'status',1170),false);
  assert.equal(s.ingest(stopReply(),'status',1171,true),false);
  assert.ok(s.pendingStop);
  assert.equal(s.ingest(stopReply(),'status',1172),true);
  assert.equal(s.stopReply.result,0);assert.equal(s.currentController(1172).stop_latched,true);
  assert.equal(s.ingest(stopReply(),'status',1173),false);
});
test('fault is not success and the ninth result bit is preserved',()=>{
  const reply=decodeStopReply(stopReply({result:5,word:23}));
  assert.equal(reply.result,5);assert.equal(reply.controller_state_code,7);
  assert.throws(()=>decodeStopReply(stopReply({result:0,word:23})));
  assert.throws(()=>decodeStopReply(stopReply({result:0,word:3})));
});
test('timeout means unknown outcome, and reconnect never automatically retries STOP',()=>{
  const s=store();waiting(s);s.expireCommands(5000);
  assert.equal(s.pendingStop,null);assert.match(s.stopNotice,/unknown/i);
  assert.equal(s.ingest(stopReply(),'status',5001),false);
  s.clearController();assert.equal(s.currentController(5002),null);assert.equal(s.pendingStop,null);
});
test('a new robot session cancels an in-flight command',()=>{
  const s=store();waiting(s);
  s.observeController({session_token:77,...decodeControlWord(3)},1200);
  assert.equal(s.pendingStop,null);assert.equal(s.ingest(stopReply(),'status',1201),false);
});
test('current controller expires; historical STATUS is never substituted for live stream',()=>{
  const s=store();assert.equal(s.currentController(3001),null);
  assert.equal(s.history.length,0);assert.equal(s.sample,null);assert.ok(s.statusReply.snapshot);
});
test('disabled build can acknowledge a logical latch without claiming physical standstill',()=>{
  const s=syncStore(new TelemetryStore(),{at:700,uptime:700});waiting(s,{state:1,testBuild:true});
  assert.equal(s.ingest(stopReply({word:49}),'status',1170),true);
  assert.match(s.stopNotice,/disabled/i);
});
