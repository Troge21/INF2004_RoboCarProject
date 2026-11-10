// Small MQTT 3.1.1 / WebSocket adapter: clean session, QoS 0 subscriptions,
// QoS 0 publishing; also acknowledges incoming QoS 1 deliveries.
const enc=new TextEncoder(), dec=new TextDecoder('utf-8',{fatal:true});
export function concat(...arrays) { const out=new Uint8Array(arrays.reduce((n,a)=>n+a.length,0));let i=0;for(const a of arrays){out.set(a,i);i+=a.length;}return out; }
export function mqttString(text) { const b=enc.encode(text);if(b.length>65535 || text.includes('\0'))throw new Error('Invalid MQTT string');return concat(new Uint8Array([b.length>>>8,b.length&255]),b); }
export function packet(header,body=new Uint8Array()) { let n=body.length,len=[];do{let b=n%128;n=Math.floor(n/128);len.push(b|(n?128:0));}while(n);return concat(new Uint8Array([header,...len]),body); }
export class PacketReader {
  constructor(){this.buffer=new Uint8Array();}
  push(chunk){
    this.buffer=concat(this.buffer,chunk);const packets=[];
    while(this.buffer.length>=2){
      let length=0,multiplier=1,index=1,complete=false;
      for(;index<this.buffer.length && index<=4;index++){
        const b=this.buffer[index];length+=(b&127)*multiplier;multiplier*=128;
        if(length>65536)throw new Error('MQTT packet exceeds dashboard limit');
        if(!(b&128)){index++;complete=true;break;}
        if(index===4)throw new Error('Malformed MQTT remaining length');
      }
      if(!complete || this.buffer.length<index+length)break;
      packets.push({header:this.buffer[0],body:this.buffer.slice(index,index+length)});
      this.buffer=this.buffer.slice(index+length);
    }
    return packets;
  }
}
export function parsePublish(header,body){
  if(body.length<2)throw new Error('Truncated publish');
  const length=(body[0]<<8)|body[1],qos=(header>>1)&3;
  if(qos>1 || body.length<2+length+(qos?2:0))throw new Error('Unsupported or truncated publish');
  const topic=dec.decode(body.slice(2,2+length));
  if(!topic || /[\0+#]/.test(topic))throw new Error('Invalid publish topic');
  const id=qos ? body.slice(2+length,4+length) : null;
  return {topic,payload:body.slice(2+length+(qos?2:0)),retained:Boolean(header&1),id};
}
export function validateTopics(options) {
  const topics=['telemetryTopic','heartbeatTopic','statusTopic','commandTopic'].map(key=>options[key]);
  if(topics.some(t=>typeof t!=='string'||!t||/[+#\0]/.test(t))||new Set(topics).size!==4)
    throw new Error('Use four different, exact telemetry/heartbeat/status/command topics');
  return topics.slice(0,3);
}
export class MqttConnection {
  constructor(options,{onStatus=()=>{},onMessage=()=>{},WebSocketClass=globalThis.WebSocket}={}){
    this.options=options;this.onStatus=onStatus;this.onMessage=onMessage;this.WS=WebSocketClass;this.closed=false;this.ready=false;this.retry=null;this.timer=null;this.deadline=null;
  }
  connect(){
    const o=this.options,url=new URL(o.url);
    if(!['ws:','wss:'].includes(url.protocol)||url.username||url.password)throw new Error('Use a ws:// or wss:// broker URL without embedded credentials');
    validateTopics(o);
    this.closed=false;this.ready=false;this.reader=new PacketReader();this.awaitingPing=false;
    this.onStatus('connecting','Opening MQTT WebSocket');
    const socket=new this.WS(o.url,['mqtt']);this.socket=socket;socket.binaryType='arraybuffer';
    const current=()=>this.socket===socket&&!this.closed;
    this.deadline=setTimeout(()=>{if(current())this.fail('Connection or subscription timed out');},10000);
    socket.onopen=()=>{
      if(!current())return;
      let flags=2,extra=[];
      if(o.username){flags|=128;extra.push(mqttString(o.username));if(o.password){flags|=64;extra.push(mqttString(o.password));}}
      const id=`rc-web-${Math.random().toString(16).slice(2,12)}`;
      this.send(packet(0x10,concat(mqttString('MQTT'),new Uint8Array([4,flags,0,30]),mqttString(id),...extra)));
    };
    socket.onmessage=e=>{
      if(!current())return;
      try {for(const p of this.reader.push(new Uint8Array(e.data)))this.handle(p);}catch(error){this.fail(error.message);}
    };
    socket.onerror=()=>{if(current())this.onStatus('error','Broker unavailable. Check the WebSocket listener, URL and browser network access.');};
    socket.onclose=()=>{
      if(!current())return;this.ready=false;this.clearTimers();
      this.onStatus('reconnecting','Connection lost · retrying in 3 seconds');
      this.retry=setTimeout(()=>{if(!this.closed)this.connect();},3000);
    };
  }
  send(bytes){if(this.socket?.readyState!==1)throw new Error('WebSocket is not open');this.socket.send(bytes);}
  handle({header,body}){
    const type=header>>4;
    if(type===2){
      if(body.length!==2||body[0]!==0||body[1]!==0)throw new Error(`MQTT connection rejected (${body[1]??'invalid reply'})`);
      const topics=concat(...validateTopics(this.options).flatMap(topic=>[mqttString(topic),new Uint8Array([0])]));
      this.send(packet(0x82,concat(new Uint8Array([0,1]),topics)));
    }else if(type===9){
      if(body.length!==5||body[0]!==0||body[1]!==1||body.slice(2).some(x=>x!==0))throw new Error('Broker did not accept QoS 0 subscriptions');
      clearTimeout(this.deadline);this.ready=true;this.onStatus('connected','Subscribed to telemetry, heartbeat and STATUS replies');
      this.timer=setInterval(()=>{try{if(this.awaitingPing){this.fail('MQTT heartbeat response timed out');return;}this.awaitingPing=true;this.send(packet(0xc0));}catch(error){this.fail(error.message);}},15000);
    }else if(type===3){
      const msg=parsePublish(header,body);
      if(msg.id)this.send(packet(0x40,msg.id));
      if(this.ready)this.onMessage(msg);
    }else if(type===13){if(body.length!==0)throw new Error('Malformed PINGRESP');this.awaitingPing=false;}
    else if(type===14)this.socket.close();
  }
  publish(topic,payload){if(!this.ready)throw new Error('MQTT is not subscribed');if(!topic||/[+#\0]/.test(topic))throw new Error('Invalid command topic');this.send(packet(0x30,concat(mqttString(topic),payload)));}
  fail(detail){this.onStatus('error',detail);this.socket?.close();}
  clearTimers(){clearInterval(this.timer);clearTimeout(this.deadline);clearTimeout(this.retry);}
  disconnect(){this.closed=true;this.ready=false;this.clearTimers();if(this.socket?.readyState===1){try{this.send(packet(0xe0));}catch{}}this.socket?.close();this.onStatus('disconnected','Disconnected');}
}
