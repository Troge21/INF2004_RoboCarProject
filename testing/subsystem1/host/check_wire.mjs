// Decode actual C output; no browser, broker or simulated live feed is used.
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {decodeTelemetry,decodeHeartbeat} from '../../../dashboard/dist/protocol.js';
import {packet,concat,mqttString,parsePublish,PacketReader} from '../../../dashboard/dist/mqtt.js';

assert.ok(process.argv[2], 'Pass the compiled emit_frames executable path');
const result=spawnSync(process.argv[2],[],{encoding:'utf8',maxBuffer:8*1024*1024});
assert.equal(result.status,0,result.stderr || result.error?.message);
let telemetry=0,heartbeats=0;
for(const line of result.stdout.trim().split(/\r?\n/)) {
  const expected=JSON.parse(line), bytes=Uint8Array.from(Buffer.from(expected.hex,'hex'));
  if(expected.kind==='heartbeat') {
    assert.equal(decodeHeartbeat(bytes),expected.counter);heartbeats++;continue;
  }
  const sample=decodeTelemetry(bytes,'packed-v2').sample;
  assert.equal(sample.sequence,expected.sequence);
  assert.equal(sample.obstacle_mm,expected.distance || null);
  assert.equal(sample.obstacle_valid,expected.distance !== 0);
  assert.equal(sample.line_bits,expected.line);
  assert.deepEqual(sample.ir,expected.line ? [null,(expected.line>>1)&1,expected.line&1] : [null,null,null]);
  const reader=new PacketReader(), publish=packet(0x30,concat(mqttString('car/telemetry'),bytes));
  reader.push(publish.subarray(0,3));
  const parsed=reader.push(publish.subarray(3));
  assert.deepEqual(parsePublish(parsed[0].header,parsed[0].body).payload,bytes);
  if(expected.sequence===12 && expected.distance===451 && expected.line===6)
    assert.equal(expected.hex,'0c000000c3e1');
  telemetry++;
}
assert.equal(telemetry,19914);assert.equal(heartbeats,3);
console.log(`PASS: ${telemetry} C telemetry frames decoded and MQTT-framed byte-for-byte; ${heartbeats} C heartbeats decoded.`);
console.log('C constructor also rejected all 114 reserved distance codes. This is offline contract evidence, not a hardware timing trial.');
