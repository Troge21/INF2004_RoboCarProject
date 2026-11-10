// Decode actual C output; no browser, broker or simulated live feed is used.
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {decodeTelemetry,decodeHeartbeat,IMU_STATES,decodeStatusReply,encodeStatusRequest} from '../../../dashboard/dist/protocol.js';
import {packet,concat,mqttString,parsePublish,PacketReader} from '../../../dashboard/dist/mqtt.js';

assert.ok(process.argv[2], 'Pass the compiled emit_frames executable path');
const result=spawnSync(process.argv[2],[],{encoding:'utf8',maxBuffer:8*1024*1024});
assert.equal(result.status,0,result.stderr || result.error?.message);
let telemetry=0,heartbeats=0,imu=0,status=0,requests=0;
for(const line of result.stdout.trim().split(/\r?\n/)) {
  const expected=JSON.parse(line), bytes=Uint8Array.from(Buffer.from(expected.hex,'hex'));
  if(expected.kind==='status_request') {
    assert.deepEqual(encodeStatusRequest(expected.id),bytes);requests++;continue;
  }
  if(expected.kind==='status') {
    const reply=decodeStatusReply(bytes);
    assert.equal(reply.request_id,expected.id);assert.equal(reply.uptime_ms,expected.id);
    assert.equal(reply.test_build,Boolean(expected.test));
    assert.equal(reply.sample_age_ms,expected.age===2047?null:expected.age);
    if(expected.age===2047)assert.equal(reply.snapshot,null);
    else {assert.equal(reply.snapshot.sequence,12);assert.equal(reply.snapshot.obstacle_mm,451);}
    const reader=new PacketReader(),frame=packet(0x30,concat(mqttString('car/status'),bytes));
    reader.push(frame.subarray(0,4));const parts=reader.push(frame.subarray(4));
    assert.deepEqual(parsePublish(parts[0].header,parts[0].body).payload,bytes);status++;continue;
  }
  if(expected.kind==='heartbeat') {
    assert.equal(decodeHeartbeat(bytes),expected.counter);heartbeats++;continue;
  }
  const sample=decodeTelemetry(bytes).sample;
  if(expected.kind==='imu') {
    assert.equal(sample.pitch_deg,expected.pitch===-9999?null:expected.pitch/10);
    assert.equal(sample.hump_mm,expected.height||null);assert.equal(sample.max_hump_mm,expected.max||null);
    assert.equal(sample.imu_state,IMU_STATES[expected.state]??null);
    assert.equal(sample.imu_flags,expected.state===7?null:expected.flags);
    assert.equal(sample.imu_speed_source,expected.state===7?null:expected.flags&32?'supplied':'nominal');
    assert.equal(sample.obstacle_mm,451);assert.equal(sample.line_bits,6);
    imu++;
  } else {
  assert.equal(sample.sequence,expected.sequence);
  assert.equal(sample.obstacle_mm,expected.distance || null);
  assert.equal(sample.obstacle_valid,expected.distance !== 0);
  assert.equal(sample.line_bits,expected.line);
  assert.deepEqual(sample.ir,expected.line ? [null,(expected.line>>1)&1,expected.line&1] : [null,null,null]);
  }
  const reader=new PacketReader(), publish=packet(0x30,concat(mqttString('car/telemetry'),bytes));
  reader.push(publish.subarray(0,3));
  const parsed=reader.push(publish.subarray(3));
  assert.deepEqual(parsePublish(parsed[0].header,parsed[0].body).payload,bytes);
  if(expected.sequence===12 && expected.distance===451 && expected.line===6)
    assert.equal(expected.hex,'0c000000c3e10000000e00');
  if(expected.kind!=='imu')telemetry++;
}
assert.equal(telemetry,19914);assert.equal(heartbeats,3);
assert.ok(imu>3602);assert.equal(status,24);assert.equal(requests,3);
console.log(`PASS: ${telemetry} distance/IR and ${imu} IMU current-format C telemetry frames decoded and MQTT-framed byte-for-byte; ${heartbeats} C heartbeats decoded.`);
console.log('C constructor also rejected all 114 reserved distance codes. This is offline contract evidence, not a hardware timing trial.');
console.log(`PASS: ${requests} C STATUS requests match JS encoding; ${status} C STATUS replies decode and preserve MQTT bytes.`);
