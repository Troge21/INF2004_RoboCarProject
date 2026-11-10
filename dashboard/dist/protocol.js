// Current firmware contract only: 11-byte telemetry and 5-byte heartbeat.
export const IMU_STATES = ['Initializing','Calibrating','Monitoring','Ascending hump','Descending hump','Validating hump','Sensor fault'];

export function decodeTelemetry(payload) {
  const bytes=payload instanceof Uint8Array?payload:new Uint8Array(payload);
  if(bytes.length!==11) throw new Error('Expected 11-byte telemetry (Packed v3); flash the current UF2');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const base=view.getUint16(4,true),distance=base&4095,line=(base>>>12)&7;
  if(!(base&0x8000)) throw new Error('Unsupported telemetry format marker');
  if(distance!==0&&(distance<20||distance>4000)) throw new Error('Obstacle distance outside 20-4000 mm');
  if(line!==0&&(line<4||line>7)) throw new Error('Invalid line sensor bits');
  const sample={sequence:view.getUint32(0,true),obstacle_valid:distance!==0,obstacle_mm:distance||null,
    line_bits:line,ir:line?[null,(line>>1)&1,line&1]:[null,null,null],
    pitch_deg:null,hump_mm:null,max_hump_mm:null,imu_state:null,imu_flags:null,imu_speed_source:null,
    state:null,left_mps:null,right_mps:null,distance_m:null,barcode:null,faults:null,scan:null};
  let word=0n;
  for(let i=0;i<5;i++) word|=BigInt(bytes[6+i])<<BigInt(i*8);
  const pitch=Number(word&2047n),height=Number((word>>11n)&127n),max=Number((word>>18n)&127n);
  const state=Number((word>>25n)&7n),flags=Number((word>>28n)&63n);
  const heightOK=x=>x===0||(x>=6&&x<=80);
  if((word>>34n)!==0n||pitch>1801||!heightOK(height)||!heightOK(max)||height>max)
    throw new Error('Invalid packed IMU field range or reserved bits');
  if(state===7) {
    if(word!==(7n<<25n)) throw new Error('Unavailable IMU must clear measurement fields');
  } else {
    if(((flags&1)&&(flags&32))||(pitch&&(!(flags&1)||(flags&32)||state<2||state>5)))
      throw new Error('Invalid packed IMU validity/state');
    Object.assign(sample,{pitch_deg:pitch?(pitch-901)/10:null,hump_mm:height||null,max_hump_mm:max||null,
      imu_state:IMU_STATES[state],imu_flags:(flags&15)|((flags&16)<<1)|((flags&32)<<2)|(pitch?16:0),
      imu_speed_source:flags&16?'supplied':'nominal'});
  }
  return {sample,format:'Packed v3 - distance + line IR + IMU'};
}
export function decodeHeartbeat(payload) {
  const bytes=payload instanceof Uint8Array?payload:new Uint8Array(payload);
  if(bytes.length!==5||bytes[0]!==0x80) throw new Error('Expected 5-byte binary heartbeat');
  return new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(1,true);
}
export function hex(bytes) { return [...bytes].map(x => x.toString(16).padStart(2,'0')).join(' '); }
export function packetText(packet) {
  if (!packet) return 'Waiting for a message…';
  if (packet.accepted && packet.decoded !== null)
    return JSON.stringify(packet.decoded,null,2);
  return `${packet.accepted?'Accepted':'Rejected'}: ${packet.detail}\n\n${hex(packet.bytes)}`;
}

export class TelemetryStore {
  constructor() { this.reset(); }
  currentSample(now = Date.now(), connected = true) {
    const age = this.lastTelemetry === null ? Infinity : now - this.lastTelemetry;
    return connected && age >= 0 && age <= 2000 ? this.sample : null;
  }
  reset() {
    this.history=[]; this.events=[]; this.packets=[]; this.sample=null;
    this.lastTelemetry=null; this.lastHeartbeat=null; this.lastSequence=null; this.lastHeartbeatCounter=null;
    this.messageCount=0; this.telemetryCount=0; this.rejected=0; this.sequenceGaps=0;
    this.totalBytes=0; this.lastPayloadBytes=0; this.lastInterval=null; this.format='—';
  }
  event(title, detail='', tone='neutral', at=Date.now()) {
    this.events.unshift({title,detail,tone,at}); this.events.length=Math.min(this.events.length,80);
  }
  ingest(bytes, kind, at=Date.now(), retained=false, topic='') {
    this.messageCount++; this.totalBytes+=bytes.length;
    const packet={at,topic,bytes:new Uint8Array(bytes),kind,accepted:false,detail:'',decoded:null};
    this.packets.unshift(packet); this.packets.length=Math.min(this.packets.length,30);
    try {
      if (retained) throw new Error('Retained message ignored; waiting for a fresh sample');
      if (kind==='heartbeat') {
        const count=decodeHeartbeat(bytes);
        if(this.lastHeartbeatCounter!==null && count<this.lastHeartbeatCounter) {
          this.lastSequence=null;
          this.event('Heartbeat counter restarted','Sequence baseline reset; the robot may have rebooted','neutral',at);
        }
        this.lastHeartbeatCounter=count; this.lastHeartbeat=at; packet.accepted=true;
        packet.decoded={counter:count};packet.detail='Binary heartbeat';return true;
      }
      const decoded=decodeTelemetry(bytes), sample=decoded.sample;
      if (this.lastSequence!==null) {
        const delta=(sample.sequence-this.lastSequence)>>>0;
        if (delta===0 || delta>0x7fffffff) throw new Error('Duplicate or out-of-order sequence');
        this.sequenceGaps+=delta-1;
      }
      this.lastSequence=sample.sequence;
      this.lastInterval=this.lastTelemetry===null ? null : at-this.lastTelemetry;
      if (sample.state && sample.state!==this.sample?.state) this.event(sample.state,'Vehicle state changed','neutral',at);
      if (sample.barcode && sample.barcode!==this.sample?.barcode) this.event('Barcode decoded',sample.barcode,'success',at);
      if(sample.imu_state && sample.imu_state!==this.sample?.imu_state)
        this.event(`IMU · ${sample.imu_state}`,sample.imu_speed_source==='nominal'?'Height uses nominal speed; accuracy not verified':'External speed supplied; validate height on the course',sample.imu_state==='Sensor fault'?'warning':'neutral',at);
      if (!sample.obstacle_valid && this.sample?.obstacle_valid!==false) this.event('Range unavailable','No valid ultrasonic measurement','warning',at);
      this.sample=sample; this.lastTelemetry=at; this.telemetryCount++; this.lastPayloadBytes=bytes.length; this.format=decoded.format;
      this.history.push({...sample,at,interval:this.lastInterval,bytes:bytes.length});
      if(this.history.length>600) this.history.shift();
      packet.accepted=true; packet.decoded=sample; packet.detail=decoded.format; return true;
    } catch (error) { this.rejected++; packet.detail=error.message; this.event('Message rejected',error.message,'warning',at); return false; }
  }
}
