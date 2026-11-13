// Current v4 observer. Read-only STATUS only; no STOP, reset or motor requests.
import {writeFileSync,readFileSync,readdirSync} from 'node:fs';
import {resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash,randomBytes} from 'node:crypto';
import {performance} from 'node:perf_hooks';
import {MqttConnection} from '../../dashboard/dist/mqtt.js';
import {decodeTelemetry,decodeHeartbeat,decodeStatusReply,decodeStopReply,
  decodeScan,decodeDiagnostics,encodeStatusRequest,hex} from '../../dashboard/dist/protocol.js';
export function observe({seconds=60,out,url='ws://127.0.0.1:9001'}={}) {
  if(!Number.isInteger(seconds)||seconds<5||seconds>300)throw new Error('Use --seconds 5..300');
  const output=resolve(out??'testing/subsystem1/logs/'+new Date().toISOString().replace(/[:.]/g,'-')+'-controller-observation.json');
  const topics={telemetryTopic:'mtk3smp-pico/telemetry',heartbeatTopic:'mtk3smp-pico/heartbeat',
    commandTopic:'mtk3smp-pico/command',statusTopic:'mtk3smp-pico/status'};
  const start=performance.now(),now=()=>Math.round(performance.now()-start);
  const report={started_utc:new Date().toISOString(),duration_seconds:seconds,format:'Packed v4',
    transport:'Local MQTT over WebSocket; includes observer/radio/broker delay',
    physical_motion:'Not measured; record a separate user observation',
    commands_sent:'Read-only STATUS only; maximum three attempts per observed robot session',
    expected_local_uf2:null,events:[],packets:[],omitted_packets:0};
  try {
    const dir=new URL('../../build_make/',import.meta.url),names=readdirSync(dir).filter(n=>n.endsWith('.uf2'));
    if(names.length===1){
      const path=new URL(names[0],dir),data=readFileSync(path);
      report.expected_local_uf2={path:fileURLToPath(path),bytes:data.length,
        sha256:createHash('sha256').update(data).digest('hex'),note:'Intended local file, not flash readback'};
    }
  }catch{}
  writeFileSync(output,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
  let done=false,pending=null,lastSend=-5000,attempts=0,confirmed=false;
  let deadline,requestTimer,saveTimer,dirty=false,lastWord=null,lastSession=null;
  function save(){writeFileSync(output,JSON.stringify(report,null,2)+'\n');dirty=false;}
  function note(message){if(report.events.length<150)report.events.push({at_ms:now(),message});dirty=true;console.log(message);}
  function finish(){
    if(done)return;done=true;clearTimeout(deadline);clearTimeout(requestTimer);clearInterval(saveTimer);
    connection.disconnect();process.removeListener('SIGINT',finish);
    process.stdin.removeListener('data',onInput);process.stdin.pause();
    report.finished_utc=new Date().toISOString();save();console.log('Saved '+output);
  }
  function onInput(text){if(String(text).trim()==='done')finish();}
  function controller(value){
    if(lastSession!==value.session_token){lastSession=value.session_token;attempts=0;confirmed=false;pending=null;clearTimeout(requestTimer);}
    const key=value.session_token+'/'+value.controller_word;
    if(key!==lastWord){lastWord=key;note('Controller: '+value.controller_state+'; latch='+value.stop_latched+'; TEST='+value.test_build);}
    if(confirmed||pending||attempts>=3||now()-lastSend<5000)return;
    let id=0;while(!id)id=randomBytes(4).readUInt32LE();
    pending={id,at_ms:now()};lastSend=now();attempts++;
    connection.publish(topics.commandTopic,encodeStatusRequest(id));note('Read-only STATUS sent: '+id);
    requestTimer=setTimeout(()=>{if(pending){pending=null;note('No matching STATUS within 3 seconds');}},3000);
  }
  const connection=new MqttConnection({url,...topics},{
    onStatus:(status,detail)=>{
      note(status+': '+detail);
      if(status!=='connected'){pending=null;confirmed=false;attempts=0;clearTimeout(requestTimer);}
    },
    onMessage:message=>{
      if(done)return;
      const item={at_ms:now(),topic:message.topic,length:message.payload.length,
        hex:hex(message.payload),retained:message.retained};
      try{
        const b=message.payload;
        if(message.retained)throw new Error('Retained packet ignored');
        if(message.topic===topics.heartbeatTopic)item.decoded={counter:decodeHeartbeat(b)};
        else if(message.topic===topics.telemetryTopic){
          if(b[0]===0xb4){item.decoded=decodeTelemetry(b).sample;controller(item.decoded);}
          else if(b[0]===0xb5)item.decoded=decodeScan(b);
          else if(b[0]===0xb6)item.decoded=decodeDiagnostics(b);
          else throw new Error('Unknown current telemetry marker');
        }else if(message.topic===topics.statusTopic&&b[0]===0xa1){
          item.decoded=decodeStatusReply(b);
          if(pending&&item.decoded.request_id===pending.id&&now()-pending.at_ms<=3000){
            item.matching_observer_STATUS=true;item.round_trip_ms=now()-pending.at_ms;
            pending=null;confirmed=true;clearTimeout(requestTimer);
            note('Matching STATUS: TEST='+item.decoded.test_build+', '+item.decoded.controller_state+', '+item.round_trip_ms+' ms');
          }
        }else if(message.topic===topics.statusTopic&&b[0]===0xa4){
          item.decoded=decodeStopReply(b);
          note('Observed STOP result: '+item.decoded.result_name+'; '+item.decoded.controller_state+'. Another client sent it.');
        }else throw new Error('Unknown current status marker');
      }catch(error){item.error=error.message;}
      if(report.packets.length<1500)report.packets.push(item);else report.omitted_packets++;
      dirty=true;
    }
  });
  process.once('SIGINT',finish);process.stdin.setEncoding('utf8');process.stdin.on('data',onInput);
  saveTimer=setInterval(()=>{if(dirty)save();},1000);
  deadline=setTimeout(finish,seconds*1000);
  try{connection.connect();}catch(error){note(error.message);finish();throw error;}
  return {finish,output};
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const o={};
  for(let i=2;i<process.argv.length;i+=2){
    const name=process.argv[i];
    if(!['--seconds','--out','--url'].includes(name)||process.argv[i+1]===undefined)
      throw new Error('Use --seconds 5..300 [--out NEW.json] [--url ws://127.0.0.1:9001]; type done to finish early');
    o[name.slice(2)]=name==='--seconds'?Number(process.argv[i+1]):process.argv[i+1];
  }
  observe(o);
}
