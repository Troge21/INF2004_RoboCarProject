// Real-car TCP observer. Never stops a broker, changes WiFi, flashes or drives.
import net from 'node:net';
import {createInterface} from 'node:readline';
import {writeFileSync,readFileSync,readdirSync} from 'node:fs';
import {createHash,randomBytes} from 'node:crypto';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {performance} from 'node:perf_hooks';
import {PacketReader,packet,concat,mqttString,parsePublish} from '../../dashboard/dist/mqtt.js';
import {decodeTelemetry,decodeHeartbeat,encodeStatusRequest,decodeStatusReply} from '../../dashboard/dist/protocol.js';

export function forwardSequence(previous,next) {
  const delta=(next-previous)>>>0;
  return delta>0&&delta<0x80000000;
}
export function baselineReady(now,telemetryAt,heartbeatAt,connected) {
  return connected&&telemetryAt!==null&&heartbeatAt!==null&&
    now-telemetryAt>=0&&now-telemetryAt<=2000&&now-heartbeatAt>=0&&now-heartbeatAt<=6000;
}
export function keepaliveAction(now,lastSendAt,pingAt) {
  if(pingAt!==null)return now-pingAt>5000?'close':'wait';
  return now-lastSendAt>=15000?'ping':'wait';
}
export function assessTrial(trial) {
  const reply=trial.status;
  const elapsed=reply?reply.at_ms-trial.baseline.at_ms:null;
  const uptimeDelta=reply?(reply.uptime_ms-trial.baseline.uptime_ms)>>>0:null;
  // Uptime is sampled BETWEEN request send and reply receive, for BOTH
  // exchanges. Baseline network delay must not masquerade as a reboot.
  const lower=reply?Math.max(0,elapsed-reply.round_trip_ms-2000):null;
  const upper=reply?elapsed+(trial.baseline.round_trip_ms??0)+2000:null;
  const clockContinuous=reply!==null&&uptimeDelta>=lower&&uptimeDelta<=upper;
  const markerBoot=reply?(reply.uptime_ms-(reply.at_ms-trial.restore_marker_ms))>>>0:null;
  const fresh=reply?trial.telemetry.filter(t=>t.session_token===reply.session_token&&
    (((t.uptime_ms-markerBoot)>>>0)<0x80000000)):[];
  const functional=fresh.length>=5&&trial.heartbeats.length>=2&&clockContinuous&&
    reply.sample_age_ms!==null&&reply.test_build===true;
  const recovery=fresh.length?fresh[0].at_ms-trial.restore_marker_ms:null;
  const intervals=items=>{
    const values=items.slice(1).map((x,i)=>x.at_ms-items[i].at_ms);
    return {count:values.length,min_ms:values.length?Math.min(...values):null,max_ms:values.length?Math.max(...values):null};
  };
  return {functional_recovery: Boolean(functional),uptime_continuous:clockContinuous,
    uptime_delta_ms:uptimeDelta,expected_uptime_delta_ms:{min:lower,max:upper},
    recovery_upper_bound_ms:recovery,
    fresh_telemetry_samples:fresh.length,queued_or_other_session_samples:trial.telemetry.length-fresh.length,
    telemetry_intervals:intervals(fresh),heartbeat_intervals:intervals(trial.heartbeats),
    within_conservative_15s_target:Boolean(functional&&recovery>=0&&recovery<=15000),
    timing_anchor:'Operator marker BEFORE restoring service; includes human, service-start and observer delays. Not isolated Pico WiFi time.'};
}

function options(args) {
  const o={kind:null,host:'172.20.10.2',port:1883,trials:5,out:null};
  for(let i=0;i<args.length;i++) {
    const key=args[i].replace(/^--/,'');
    if(!['kind','host','port','trials','out'].includes(key)||args[i+1]===undefined)
      throw new Error('Use --kind broker|wifi [--host IP] [--port 1883] [--trials 1..5] [--out NEW.json]');
    o[key]=args[++i];
  }
  o.port=Number(o.port);o.trials=Number(o.trials);
  if(!['broker','wifi'].includes(o.kind)||!net.isIP(o.host)||!Number.isInteger(o.port)||o.port<1||o.port>65535||
    !Number.isInteger(o.trials)||o.trials<1||o.trials>5)throw new Error('Invalid kind, IP, port or trial count');
  o.out=resolve(o.out??`testing/subsystem1/logs/${new Date().toISOString().replace(/[:.]/g,'-')}-${o.kind}-recovery.json`);
  return o;
}

export function runProbe(o) {
  const topics={telemetry:'mtk3smp-pico/telemetry',heartbeat:'mtk3smp-pico/heartbeat',
    command:'mtk3smp-pico/command',status:'mtk3smp-pico/status'};
  const started=performance.now(),now=()=>Math.round(performance.now()-started);
  const report={started_utc:new Date().toISOString(),kind:o.kind,host:o.host,port:o.port,
    requested_trials:o.trials,topics,local_uf2_sha256:null,
    probe_sha256:createHash('sha256').update(readFileSync(fileURLToPath(import.meta.url))).digest('hex'),
    image_note:'Local file fingerprint only; operator must verify the flashed image.',trials:[],events:[]};
  try { const dir=new URL('../../build_make/',import.meta.url),names=readdirSync(dir).filter(n=>n.endsWith('.uf2'));if(names.length===1)report.local_uf2_sha256=createHash('sha256').update(readFileSync(new URL(names[0],dir))).digest('hex'); } catch {}
  writeFileSync(o.out,JSON.stringify(report,null,2),{flag:'wx'}); // Never overwrite earlier evidence.
  let socket,reader,ready=false,stopped=false,retry,watchdog,phase='idle',pending=null,trial=null;
  let telemetryAt=null,heartbeatAt=null,lastSequence=null,lastSession=null,lastTelemetrySample=null,lastSendAt=0,pingAt=null;
  const input=createInterface({input:process.stdin,output:process.stdout});
  function save() { writeFileSync(o.out,JSON.stringify(report,null,2)+'\n'); }
  function note(message) {
    console.log(`[${now()} ms] ${message}`);
    report.events.push({at_ms:now(),message});if(report.events.length>500)report.events.shift();
  }
  function send(bytes) {
    if(!socket||socket.destroyed)throw new Error('Observer socket unavailable');
    socket.write(bytes);lastSendAt=now();
  }
  function requestStatus(purpose) {
    if(!ready||pending)return;
    let id=0;while(!id)id=randomBytes(4).readUInt32LE();
    pending={id,at_ms:now(),purpose};
    send(packet(0x30,concat(mqttString(topics.command),encodeStatusRequest(id))));
  }
  function finishTrial(reason=null) {
    if(!trial)return;
    trial.end_ms=now();trial.reason=reason;trial.result=assessTrial(trial);
    if(reason)trial.result.within_conservative_15s_target=false;
    report.trials.push(trial);
    note(`Trial ${report.trials.length}: ${reason??JSON.stringify(trial.result)}`);
    trial=null;pending=null;phase='idle';save();
    if(report.trials.length>=o.trials)finish();
    else note('Next trial: wait for fresh telemetry/heartbeat, then type loss.');
  }
  function finish() {
    if(stopped)return;
    stopped=true;
    if(trial)finishTrial('Interrupted/incomplete; no acceptance pass');
    clearTimeout(retry);clearInterval(watchdog);socket?.destroy();input.close();
    process.removeListener('SIGINT',interrupt);
    report.finished_utc=new Date().toISOString();
    report.recorded_attempts=report.trials.length;
    report.complete_functional_trials=report.trials.filter(t=>!t.reason&&t.result.functional_recovery).length;
    report.within_conservative_target=report.trials.filter(t=>t.result.within_conservative_15s_target).length;
    save();console.log(`Saved ${o.out}. Live results apply only to this capture; review timing limitations.`);
  }
  function interrupt(){finish();}
  function receive({header,body}) {
    const type=header>>4;
    if(type===2) {
      if(body.length!==2||body[0]!==0||body[1]!==0)throw new Error('MQTT CONNECT rejected');
      send(packet(0x82,concat(new Uint8Array([0,1]),...['telemetry','heartbeat','status'].flatMap(k=>[mqttString(topics[k]),new Uint8Array([0])]))));
    } else if(type===9) {
      if(body.length!==5||body[0]!==0||body[1]!==1||body.slice(2).some(x=>x!==0))throw new Error('Subscriptions rejected');
      ready=true;pingAt=null;socket.setTimeout(0);note('TCP observer subscribed. This is broker readiness, not Pico recovery.');
      if(trial&&phase==='restoring'&&trial.broker_observer_ready_ms===null)trial.broker_observer_ready_ms=now();
    } else if(type===13) { pingAt=null;
    } else if(type===3&&ready) {
      const msg=parsePublish(header,body);
      if(msg.id)send(packet(0x40,msg.id));
      if(msg.retained){note('Ignored retained packet');return;}
      const at=now();
      if(msg.topic===topics.telemetry) {
        if(msg.payload[0]!==0xb4)return; // Scan/resource messages are not cadence samples.
        const sample=decodeTelemetry(msg.payload).sample;
        if(lastSession!==sample.session_token){lastSession=sample.session_token;lastSequence=null;}
        lastTelemetrySample=sample;
        if(lastSequence!==null&&!forwardSequence(lastSequence,sample.sequence))return;
        lastSequence=sample.sequence;telemetryAt=at;
        if(phase==='restoring'&&trial.telemetry.length<600)trial.telemetry.push({at_ms:at,sequence:sample.sequence,uptime_ms:sample.uptime_ms,session_token:sample.session_token});
      } else if(msg.topic===topics.heartbeat) {
        const counter=decodeHeartbeat(msg.payload);heartbeatAt=at;
        if(phase==='restoring'&&trial.heartbeats.length<60)trial.heartbeats.push({at_ms:at,counter});
      } else if(msg.topic===topics.status) {
        if(msg.payload[0]!==0xa1)return;
        const reply=decodeStatusReply(msg.payload);
        if(!pending||reply.request_id!==pending.id||at-pending.at_ms>3000)return;
        const purpose=pending.purpose;
        const status={at_ms:at,round_trip_ms:at-pending.at_ms,uptime_ms:reply.uptime_ms,
          sample_age_ms:reply.sample_age_ms,test_build:reply.test_build,session_token:reply.session_token};
        pending=null;
        if(purpose==='baseline') {
          if(!baselineReady(at,telemetryAt,heartbeatAt,ready)||reply.sample_age_ms===null||!reply.test_build||!lastTelemetrySample||lastTelemetrySample.session_token!==reply.session_token||((reply.uptime_ms-lastTelemetrySample.uptime_ms)>>>0)>2000) {
            phase='idle';note('Baseline rejected: need fresh real data and stationary TEST=1 firmware.');return;
          }
          trial={number:report.trials.length+1,baseline:status,loss_marker_ms:at,
            outage_observed_ms:null,restore_marker_ms:null,broker_observer_ready_ms:null,
            telemetry:[],heartbeats:[],status:null};
          phase='outage';note('Baseline confirmed. NOW stop the custom broker or turn off hotspot, keeping Pico powered.');save();
        } else if(phase==='restoring')trial.status=status;
      }
    }
  }
  function connect() {
    if(stopped)return;
    ready=false;pingAt=null;reader=new PacketReader();
    const current=net.createConnection({host:o.host,port:o.port});socket=current;
    current.setTimeout(3000,()=>current.destroy());
    current.on('connect',()=>send(packet(0x10,concat(mqttString('MQTT'),new Uint8Array([4,2,0,30]),mqttString(`s1-recovery-${randomBytes(5).toString('hex')}`)))));
    current.on('data',bytes=>{
      try{for(const message of reader.push(bytes))receive(message);}catch(error){note(error.message);current.destroy();}
    });
    current.on('error',()=>{});
    current.on('close',()=>{
      const wasReady=ready;ready=false;
      if(pending?.purpose==='baseline'){phase='idle';pending=null;note('Baseline cancelled on connection loss; no test started.');}
      else pending=null;
      if(wasReady)note('TCP observer disconnected');
      if(!stopped)retry=setTimeout(connect,250);
    });
  }
  input.on('line',line=>{
    const cmd=line.trim().toLowerCase(),at=now();
    if(cmd==='finish')return finish();
    if(cmd==='abort'){if(trial)finishTrial('Operator aborted; no acceptance pass');return;}
    if(cmd==='status'){note(`phase=${phase}, broker=${ready}, telemetry age=${telemetryAt===null?'unavailable':at-telemetryAt} ms`);return;}
    if(cmd==='loss'&&phase==='idle') {
      if(!baselineReady(at,telemetryAt,heartbeatAt,ready)){note('Cannot start: need fresh telemetry AND heartbeat from the real car.');return;}
      phase='baseline';requestStatus('baseline');note('Checking correlated Pico STATUS before allowing the outage...');return;
    }
    if(cmd==='restore'&&phase==='outage') {
      if(trial.outage_observed_ms===null||at-telemetryAt<=2000||(o.kind==='broker'&&ready)) {
        note('Cannot mark restoration: actual outage has not been observed, or data already resumed.');return;
      }
      if(at-trial.outage_observed_ms<10000){note('Keep the outage for 10 seconds after the observation message before restoring.');return;}
      trial.restore_marker_ms=at;phase='restoring';
      trial.broker_was_ready_at_marker=ready;
      note('Restoration marker set. NOW restart the custom broker or enable hotspot. Do not reboot Pico.');save();return;
    }
    note('Commands: loss (fresh baseline first), restore (BEFORE restoring), status, abort, finish.');
  });
  watchdog=setInterval(()=>{
    const at=now();
    if(pending&&at-pending.at_ms>3000) {
      if(pending.purpose==='baseline'){phase='idle';note('STATUS baseline timed out; no test started.');}
      pending=null;
    }
    if(ready) {
      const action=keepaliveAction(at,lastSendAt,pingAt);
      if(action==='close')socket.destroy();
      else if(action==='ping'&&!socket.destroyed){pingAt=at;send(packet(0xc0));}
    }
    if(phase==='outage'&&trial.outage_observed_ms===null&&at-telemetryAt>2000&&(o.kind==='wifi'||!ready)) {
      trial.outage_observed_ms=at;note('Outage observed. Keep service off at least 10 s; then type restore immediately BEFORE restoring it.');
    }
    if(phase==='restoring') {
      if(trial.telemetry.length&&!trial.status&&!pending)requestStatus('recovery');
      if(trial.telemetry.length>=5&&trial.heartbeats.length>=2&&trial.status)finishTrial();
      else if(at-trial.restore_marker_ms>30000)finishTrial('Incomplete within 30 s observation window');
    }
  },100);
  process.on('SIGINT',interrupt);
  note(`Observe ${o.kind} recovery; ${o.trials} trials. No service changes are automated. Type loss only when the car is ready.`);
  connect();
  return {finish};
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  if(process.argv.includes('--help'))console.log('From repo root: node testing/subsystem1/recovery_probe.mjs --kind broker|wifi [--host 172.20.10.2] [--trials 5] [--out NEW.json]\nRead testing/subsystem1/RECOVERY_TESTS.md first. No fake feed or automatic broker/WiFi changes.');
  else try{runProbe(options(process.argv.slice(2)));}catch(error){console.error(error.message);process.exitCode=1;}
}
