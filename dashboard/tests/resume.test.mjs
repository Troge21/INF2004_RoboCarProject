import test from 'node:test';
import assert from 'node:assert/strict';
import {TelemetryStore,encodeResumeRequest,decodeResumeArmReply,decodeResumeReply,decodeStopReply} from '../dist/protocol.js';
import {syncStore,statusReply,resumeArmReply,resumeReply} from './fixtures.mjs';
const store=()=>syncStore(new TelemetryStore(),{at:700,uptime:700,state:6,latched:true,testBuild:false});
function ready(s){s.beginResume(12,1100);assert.equal(s.ingest(resumeArmReply({id:12,uptime:1100}),'status',1150),true);}
function waiting(s){ready(s);return s.beginResumeCommand(1160);}

test('Resume requests retain full IDs, session and modulo boot deadlines',()=>{
  for(const commit of [false,true]){
    const b=encodeResumeRequest(0xffffffff,0xffffffff,0,commit),v=new DataView(b.buffer);
    assert.equal(b.length,13);assert.equal(b[0],commit?0xa7:0xa5);
    assert.equal(v.getUint32(1,true),0xffffffff);assert.equal(v.getUint32(9,true),0);
  }
  assert.throws(()=>encodeResumeRequest(0,1,2));
});
test('authorization includes a nonzero server ticket and normal latched state',()=>{
  assert.equal(decodeResumeArmReply(resumeArmReply()).ticket,77);
  assert.equal(decodeResumeArmReply(resumeArmReply({age:null})).snapshot,null);
  for(const options of [{ticket:0},{testBuild:true},{latched:false,state:3},{state:7}])
    assert.throws(()=>decodeResumeArmReply(resumeArmReply(options)));
  assert.throws(()=>decodeResumeArmReply(new Uint8Array(16)));
});
test('successful Resume result cannot claim a latch, disabled build or fault',()=>{
  assert.equal(decodeResumeReply(resumeReply()).result,0);
  for(const word of [22,23,33,0])assert.throws(()=>decodeResumeReply(resumeReply({word})));
  assert.equal(decodeResumeReply(resumeReply({result:7,word:22})).result,7);
  assert.throws(()=>decodeStopReply(resumeReply()));
});
test('Resume is available only for a fresh normal-mode latch',()=>{
  const s=store();assert.equal(s.beginResume(12,1100)[0],0xa5);
  assert.throws(()=>s.beginResumeCommand(1101));
  assert.throws(()=>store().beginResume(12,3000));
  for(const options of [{state:1,testBuild:true,latched:true},{state:3,testBuild:false},{state:7,testBuild:false,latched:true}]){
    const other=syncStore(new TelemetryStore(),{at:1000,uptime:1000,...options});
    assert.throws(()=>other.beginResume(12,1100));
  }
});
test('one explicit Resume click arms then commits the server ticket once',()=>{
  const s=store();const b=waiting(s),v=new DataView(b.buffer);
  assert.equal(b[0],0xa7);assert.equal(v.getUint32(1,true),77);assert.equal(v.getUint32(9,true),3100);
  assert.throws(()=>s.beginResumeCommand(1161));
  assert.equal(s.ingest(resumeReply(),'status',1170),true);
  assert.equal(s.pendingResume,null);assert.equal(s.currentController(1170).stop_latched,false);
  assert.equal(s.history.length,0);assert.match(s.resumeNotice,/may move/i);
});
test('only matching nonretained authorization can enable a commit',()=>{
  const s=store();s.beginResume(12,1100);
  assert.equal(s.ingest(resumeArmReply({id:13}),'status',1150),false);
  assert.equal(s.ingest(resumeArmReply({id:12}),'status',1150,true),false);
  assert.throws(()=>s.beginResumeCommand(1160));
  assert.equal(s.ingest(resumeArmReply({id:12,session:7}),'status',1160),false);
  assert.equal(s.pendingResume,null);
});
test('delayed authorization and expired commit lease never produce Resume',()=>{
  const s=store();s.beginResume(12,1100);
  assert.equal(s.ingest(resumeArmReply({id:12}),'status',3100),false);assert.equal(s.pendingResume,null);
  const other=store();ready(other);assert.throws(()=>other.beginResumeCommand(3100));
});
test('replies are correlated to server ticket, not the earlier client arm ID',()=>{
  const s=store();waiting(s);
  assert.equal(s.ingest(resumeReply({id:12}),'status',1170),false);
  assert.equal(s.ingest(resumeReply(),'status',1170,true),false);
  assert.equal(s.ingest(resumeReply({result:6,word:22}),'status',1171),true);
  assert.equal(s.currentController(1172).stop_latched,true);assert.match(s.resumeNotice,/not ready/i);
});
test('connection/session changes cancel Resume without automatic restart',()=>{
  for(const phase of ['arming','waiting']){
    const s=store();if(phase==='arming')s.beginResume(12,1100);else waiting(s);
    s.clearController();assert.equal(s.pendingResume,null);
    assert.equal(s.ingest(resumeReply(),'status',1170),false);
  }
});
test('missing commit reply reports unknown outcome without automatic retry',()=>{
  const s=store();waiting(s);s.expireCommands(5000);
  assert.equal(s.pendingResume,null);assert.match(s.resumeNotice,/unknown/i);
  assert.equal(s.ingest(resumeReply(),'status',5001),false);
  assert.equal(s.beginStop(101,5010)[0],0xa0,'STOP can refresh status after Resume timed out and controller data expired');
});
test('STOP remains valid even when the latest state already says latched',()=>{
  const s=store();assert.equal(s.beginStop(12,1100)[0],0xa0);
  s.ingest(resumeArmReply({id:12}),'status',1150); // Different reply type cannot satisfy STATUS.
  assert.equal(s.pendingStop.phase,'checking');
  assert.equal(s.ingest(statusReply({id:12,state:6,latched:true,testBuild:false,uptime:1100}),'status',1151),true);
  assert.equal(s.beginStopCommand(100,1160)[0],0xa2);
});
test('STOP supersedes both an arming and an already submitted Resume',()=>{
  for(const phase of ['arming','waiting']){
    const s=store();if(phase==='arming')s.beginResume(12,1100);else waiting(s);
    const b=s.beginStop(100,1170);assert.equal(b[0],0xa2);
    assert.equal(s.pendingResume,null);assert.equal(s.pendingStop.phase,'waiting');
    assert.equal(s.ingest(resumeReply(),'status',1171),false);
  }
});
test('Resume serializes other requests and rejects reuse of the last arm ID',()=>{
  const s=store();s.beginResume(12,1100);assert.throws(()=>s.beginStatus(13,1101));
  s.cancelStatus();assert.throws(()=>s.beginResume(12,1150),/fresh/);
});
test('STOP can promote an in-flight read-only STATUS instead of waiting for another button opportunity',()=>{
  const s=store();s.beginStatus(13,1100);
  assert.equal(s.beginStop(100,1101),null);
  assert.equal(s.pendingStop.status_id,13);
  s.ingest(statusReply({id:13,uptime:1100,state:6,latched:true,testBuild:false}),'status',1150);
  assert.equal(s.beginStopCommand(101,1160)[0],0xa2);
});
test('a just-completed STATUS cooldown cannot suppress STOP',()=>{
  const s=store();assert.equal(s.beginStop(12,710)[0],0xa2);
  assert.equal(s.pendingStop.phase,'waiting');
});
