// Actual C output decoded by the current JavaScript and MQTT framing.
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import {decodeTelemetry,decodeScan,decodeDiagnostics,decodeStatusReply,decodeStopReply,decodeHeartbeat,decodeResumeArmReply,decodeResumeReply} from '../../../dashboard/dist/protocol.js';
import {packet,concat,mqttString,parsePublish,PacketReader} from '../../../dashboard/dist/mqtt.js';
assert.ok(process.argv[2],'Pass the compiled emit_frames executable');
const result=spawnSync(process.argv[2],[],{encoding:'utf8',maxBuffer:16*1024*1024});
assert.equal(result.status,0,result.stderr||result.error?.message);
const counts={};
for(const line of result.stdout.trim().split(/\r?\n/)){
  const e=JSON.parse(line),b=Uint8Array.from(Buffer.from(e.hex,'hex'));counts[e.kind]=(counts[e.kind]??0)+1;
  if(['empty','base','motion','barcode','imu'].includes(e.kind)){
    const s=decodeTelemetry(b).sample;
    if(e.kind==='empty'){assert.equal(s.left_mps,null);assert.equal(s.left_encoder_count,null);assert.equal(s.imu_state,null);}
    if(e.kind==='base'){assert.equal(s.sequence,e.sequence);assert.equal(s.obstacle_mm,e.distance||null);assert.equal(s.line_bits,e.line);}
    if(e.kind==='motion'){
      assert.equal(s.left_mps,Math.abs(e.speed)<=5000?e.speed/10000:null);
      assert.equal(s.left_encoder_count,Math.abs(e.count)<=2147483647?e.count:null);
      assert.equal(s.right_mps,0);assert.equal(s.net_distance_m,-.1234);
    }
    if(e.kind==='barcode'){assert.equal(s.barcode,e.character);assert.equal(s.barcode_reversed,Boolean(e.reversed));assert.equal(s.barcode_event_sequence,65535);}
    if(e.kind==='imu')assert.equal(s.pitch_deg,e.pitch/10);
  }else if(e.kind==='status'){const s=decodeStatusReply(b);assert.equal(s.request_id,0xffffffff);assert.equal(s.snapshot===null,Boolean(e.missing));}
  else if(e.kind==='resume_arm'){const r=decodeResumeArmReply(b);assert.equal(r.ticket,0xffffffff);assert.equal(r.snapshot===null,Boolean(e.missing));}
  else if(e.kind==='resume')assert.equal(decodeResumeReply(b).result,e.result);
  else if(e.kind==='stop')assert.equal(decodeStopReply(b).result,e.result);
  else if(e.kind==='scan'){
    const s=decodeScan(b);assert.equal(s.start,e.start);assert.equal(s.total,181);
    s.points.forEach(p=>assert.equal(p.distance_mm,p.angle%7?451:null));
  }else if(e.kind==='resources'){const d=decodeDiagnostics(b);assert.equal(d.heap_free_bytes,0);assert.equal(d.stack_high_water_bytes,null);assert.equal(d.rejected_commands,0xffffffff);}
  else if(e.kind==='heartbeat')assert.equal(decodeHeartbeat(b),0xffffffff);
  else throw new Error('Unknown C fixture kind');
  const reader=new PacketReader(),framed=packet(0x30,concat(mqttString('car/telemetry'),b));
  assert.equal(reader.push(framed.subarray(0,5)).length,0);
  const parsed=reader.push(framed.subarray(5))[0];
  assert.deepEqual(parsePublish(parsed.header,parsed.body).payload,b);
}
console.log('PASS: current C / JavaScript / MQTT vectors '+JSON.stringify(counts));
