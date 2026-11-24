// One current Packed v4 contract. No v1/v2/v3/JSON compatibility path.
export const FRAME_BYTES=46, STATUS_HEADER_BYTES=16;
export const IMU_STATES=['Initializing','Calibrating','Monitoring','Ascending hump','Descending hump','Validating hump','Sensor fault'];
export const CONTROLLER_STATES=['Unavailable','Control disabled','Starting','Following line','Recovering line','Waiting for line','Stop latched','Controller fault','Ready','Turning left','Turning right','Scanning obstacle','Avoiding obstacle','Stopped'];
export const STOP_RESULTS=['Stop latched','Request expired','Robot session changed','Duplicate request','Controller busy','Controller fault'];
export const RESUME_RESULTS=['Stop latch released','Request expired','Robot session changed','Duplicate request','Controller busy','Controller fault','Resume conditions not ready','Resume authorization changed'];
export const BARCODE_LETTERS='ABCDEFGHIJKLMNOPQRSTUVWXYZ';
export const BARCODE_ACTIONS=['Left','Right','Forward','U-turn'];
const BARCODE_MAP_RESULTS=['Applied','Request expired','Robot session changed','Older or duplicate request','Controller busy','Invalid mapping','Pico must be STOP-latched and ready'];
export const FAULT_NAMES=['Encoder input fault','Motor timing fault','Left wheel stall','Right wheel stall','Left encoder direction','Right encoder direction','IMU sensor fault','Controller fault','Ultrasonic fault','Barcode input fault','Scan fault'];
const ALPHABET='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%';
const bytesOf=p=>p instanceof Uint8Array?p:new Uint8Array(p);
const viewOf=b=>new DataView(b.buffer,b.byteOffset,b.byteLength);
function bits(b,offset,width){let n=0;for(let i=0;i<width;i++)n+=((b[(offset+i)>>3]>>((offset+i)&7))&1)*2**i;return n;}
function signed(n){return n===0x80000000?null:n>=0x80000000?n-0x100000000:n;}
// Instantaneous mean of wheel-speed magnitudes, not a time average or centre velocity.
export function averageWheelSpeed(sample){
  return Number.isFinite(sample?.left_mps)&&Number.isFinite(sample?.right_mps)?
    (Math.abs(sample.left_mps)+Math.abs(sample.right_mps))/2:null;
}
export function decodeControlWord(word){
  const state=word&15,latch=Boolean(word&16);
  if(!Number.isInteger(word)||word<0||word>63||state>13||(state===6&&!latch)||(latch&&![1,6,7].includes(state)))throw new Error('Invalid controller state/flags');
  return {controller_word:word,controller_state_code:state,controller_state:CONTROLLER_STATES[state],controller_valid:state!==0,stop_latched:latch,test_build:Boolean(word&32)};
}
export function decodeTelemetry(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(b.length!==FRAME_BYTES||b[0]!==0xb4)throw new Error('Expected current 46-byte Packed v4 firmware');
  const session=v.getUint32(9,true),body=b.subarray(13),get=(o,n)=>bits(body,o,n);
  if(!session)throw new Error('Invalid telemetry session/reserved bits');
  const distance=get(0,12),line=get(12,3),left=get(49,14),right=get(63,14),bc=get(227,7);
  const known=get(205,11),fault=get(216,11),eventSequence=get(234,16),mode=get(256,2),reason=get(258,3),navigation=get(261,3);
  if((distance&&(distance<20||distance>4000))||(line&&line<4)||
    (left>10000&&left!==16383)||(right>10000&&right!==16383)||
    (bc&&((bc&63)<1||(bc&63)>43))||navigation>4||(!bc&&navigation)||(fault&~known)||(reason>5&&reason!==7))throw new Error('Invalid telemetry field range');
  const sample={sequence:v.getUint32(1,true),uptime_ms:v.getUint32(5,true),session_token:session,...decodeControlWord(get(250,6)),
    obstacle_valid:distance!==0,obstacle_mm:distance||null,line_bits:line,ir:line?[null,(line>>1)&1,line&1]:[null,null,null],
    left_mps:left===16383?null:(left-5000)/10000,right_mps:right===16383?null:(right-5000)/10000,
    left_encoder_count:signed(get(77,32)),right_encoder_count:signed(get(109,32)),
    net_distance_m:signed(get(141,32))===null?null:signed(get(141,32))/10000,
    distance_m:get(173,32)===0xffffffff?null:get(173,32)/1000,
    motor_mode:mode===3?null:['Stopped','Duty','RPM'][mode],motor_stop_reason:reason===7?null:['Requested','None','Command timeout','Command busy','Motor fault','Feedback wait'][reason],
    fault_known_mask:known,fault_mask:fault,faults:known?FAULT_NAMES.filter((_,i)=>fault&(1<<i)):null,
    barcode:bc?ALPHABET[(bc&63)-1]:null,barcode_navigation:navigation?[null,'Turn left','Turn right','Go straight','U-turn'][navigation]:null,barcode_reversed:bc?Boolean(bc&64):null,barcode_event_sequence:bc||eventSequence?eventSequence:null,
    pitch_deg:null,hump_mm:null,max_hump_mm:null,imu_state:null,imu_flags:null,imu_speed_source:null,scan:null};
  const pitch=get(15,11),height=get(26,7),max=get(33,7),state=get(40,3),flags=get(43,6);
  const heightOK=x=>x===0||(x>=6&&x<=80);
  if(pitch>1801||!heightOK(height)||!heightOK(max)||height>max)throw new Error('Invalid IMU field range');
  if(state===7){if(pitch||height||max||flags)throw new Error('Unavailable IMU must clear fields');}
  else {
    if(((flags&1)&&(flags&32))||(pitch&&(!(flags&1)||(flags&32)||state<2||state>5)))throw new Error('Invalid IMU validity/state');
    Object.assign(sample,{pitch_deg:pitch?(pitch-901)/10:null,hump_mm:height||null,max_hump_mm:max||null,imu_state:IMU_STATES[state],imu_flags:(flags&15)|((flags&16)<<1)|((flags&32)<<2)|(pitch?16:0),imu_speed_source:flags&16?'supplied':'nominal'});
  }
  sample.state=sample.controller_valid?sample.controller_state:null;
  sample.average_wheel_mps=averageWheelSpeed(sample);
  return {sample,format:'Packed v4'};
}
export function decodeScan(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(b.length<19||b.length>58||b[0]!==0xb5)throw new Error('Invalid scan chunk');
  const session=v.getUint32(1,true),total=b[13],start=b[14],count=b[15]&31;
  const hasWidth=Boolean(b[15]&128),pointEnd=16+Math.ceil(count*20/8);
  if(!session||!total||total>181||!count||count>16||start+count>total||(b[15]&96)||
     (hasWidth&&start!==0)||b.length!==pointEnd+(hasWidth?2:0))throw new Error('Invalid scan framing');
  if(count*20%8&&b[pointEnd-1]>>>(count*20%8))throw new Error('Reserved scan bits must be zero');
  const width=hasWidth?v.getUint16(pointEnd,true):null;
  if(hasWidth&&(!width||width>1023))throw new Error('Invalid scan width');
  const points=[];
  for(let i=0;i<count;i++){
    const angle=bits(b.subarray(16),20*i,8),distance=bits(b.subarray(16),20*i+8,12);
    if(angle>180||(i&&angle<=points[i-1].angle)||(distance&&(distance<20||distance>4000)))throw new Error('Invalid scan point');
    points.push({angle,distance_mm:distance||null});
  }
  return {session_token:session,scan_sequence:v.getUint32(5,true),uptime_ms:v.getUint32(9,true),total,start,points,width_mm:width};
}
export function decodeDiagnostics(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(b.length!==40||b[0]!==0xb6||!v.getUint32(1,true)||(b[23]&252))throw new Error('Invalid diagnostics packet');
  const result={session_token:v.getUint32(1,true),uptime_ms:v.getUint32(5,true)};
  ['static_ram_bytes','heap_free_bytes','largest_free_block_bytes','sampled_min_heap_free_bytes','task_stack_capacity_bytes','stack_high_water_bytes'].forEach((key,i)=>{
    const n=bits(b.subarray(9,24),19*i,19);if(n>270336&&n!==0x7ffff)throw new Error('Invalid resource value');result[key]=n===0x7ffff?null:n;
  });
  ['publish_errors','rejected_commands','mqtt_connections','snapshot_contention'].forEach((key,i)=>result[key]=v.getUint32(24+4*i,true));
  return result;
}
const MOTION_CLASSES=[null,'Stationary','Forward','Reverse','Accelerating','Slowing',
  'Turning left','Turning right','Climbing hump','Descending hump','Pickup','Sudden impact'];
const HUMP_KINDS=[null,'Full-width hump','Left-wheel hump','Right-wheel hump'];
export function decodeTerrain(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(b.length!==15||b[0]!==0xb7||!v.getUint32(1,true)||(b[14]&252))throw new Error('Invalid S4 terrain packet');
  const body=b.subarray(9),roll=bits(body,0,11),turn=bits(body,11,9),motion=bits(body,20,4);
  if(roll>1801||motion>=MOTION_CLASSES.length||(!roll&&(turn||motion)))throw new Error('Invalid S4 terrain values');
  return {session_token:v.getUint32(1,true),uptime_ms:v.getUint32(5,true),
    roll_deg:roll?(roll-901)/10:null,turn_deg_s:turn?turn-256:null,
    motion_class:MOTION_CLASSES[motion],hump_kind:HUMP_KINDS[bits(body,24,2)],hump_event_sequence:bits(body,26,16)};
}
export function decodeBarcodeSignal(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(b.length<17||b.length>47||b[0]!==0xb8||!v.getUint32(1,true))throw new Error('Invalid barcode sensor packet');
  const word=v.getUint16(9,true),valid=Boolean(word&4096);
  if((word&49152)||(!valid&&word))throw new Error('Invalid barcode sensor sample');
  return {session_token:v.getUint32(1,true),uptime_ms:v.getUint32(5,true),
    adc:valid?word&4095:null,black:valid?Boolean(word&8192):null,
    edges:v.getUint16(11,true),rejected:v.getUint16(13,true),gaps:v.getUint16(15,true),
    runs_ms:Array.from(b.subarray(17),ticks=>ticks*2)};
}
export function decodeHeartbeat(payload){const b=bytesOf(payload);if(b.length!==5||b[0]!==0x80)throw new Error('Expected 5-byte binary heartbeat');return viewOf(b).getUint32(1,true);}
export function encodeStatusRequest(id){
  if(!Number.isInteger(id)||id<1||id>0xffffffff)throw new Error('Invalid STATUS request ID');
  const b=new Uint8Array(5);b[0]=0xa0;viewOf(b).setUint32(1,id,true);return b;
}
export function decodeStatusReply(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(![16,62].includes(b.length)||b[0]!==0xa1)throw new Error('Expected current Packed v4 STATUS firmware (16/62 bytes)');
  const id=v.getUint32(1,true),uptime=v.getUint32(5,true),meta=b[9]|(b[10]<<8)|(b[11]<<16),age=meta&2047,session=v.getUint32(12,true),control=decodeControlWord(meta>>>11);
  if(!id||!session||(meta>>>17)||(age>2000&&age!==2047)||b.length!==(age===2047?16:62))throw new Error('Invalid STATUS fields');
  const snapshot=age===2047?null:decodeTelemetry(b.subarray(16)).sample;
  if(snapshot&&(snapshot.session_token!==session||snapshot.uptime_ms!==uptime||snapshot.controller_word!==control.controller_word))throw new Error('STATUS snapshot/session mismatch');
  return {request_id:id,uptime_ms:uptime,session_token:session,...control,sample_age_ms:age===2047?null:age,snapshot};
}
export function encodeStopRequest(id,session,deadline){
  for(const [value,min] of [[id,1],[session,1],[deadline,0]])if(!Number.isInteger(value)||value<min||value>0xffffffff)throw new Error('Invalid STOP request field');
  const b=new Uint8Array(13),v=viewOf(b);b[0]=0xa2;v.setUint32(1,id,true);v.setUint32(5,session,true);v.setUint32(9,deadline,true);return b;
}
export function decodeStopReply(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(b.length!==11||b[0]!==0xa4)throw new Error('Invalid STOP reply');
  const id=v.getUint32(1,true),session=v.getUint32(5,true),word=v.getUint16(9,true),result=word>>>6,control=decodeControlWord(word&63);
  if(!id||!session||(word>>>9)||result>5||(result===0&&(!control.stop_latched||control.controller_state_code===7)))throw new Error('Invalid STOP result');
  return {request_id:id,session_token:session,result,result_name:STOP_RESULTS[result],...control};
}
export function encodeResumeRequest(id,session,deadline,commit=false){
  const b=encodeStopRequest(id,session,deadline);b[0]=commit?0xa7:0xa5;return b;
}
export function decodeResumeArmReply(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(![20,66].includes(b.length)||b[0]!==0xa6)throw new Error('Invalid Resume authorization reply');
  const ticket=v.getUint32(16,true);
  const status=new Uint8Array(b.length-4);status.set(b.subarray(0,16));status[0]=0xa1;
  if(b.length>20)status.set(b.subarray(20),16);
  const reply=decodeStatusReply(status);
  if(!ticket||reply.test_build||!reply.stop_latched||reply.controller_state_code!==6)throw new Error('Invalid Resume authorization');
  return {...reply,ticket};
}
export function decodeResumeReply(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(b.length!==11||b[0]!==0xa8)throw new Error('Invalid Resume result');
  const id=v.getUint32(1,true),session=v.getUint32(5,true),word=v.getUint16(9,true),result=word>>>6,control=decodeControlWord(word&63);
  if(!id||!session||(word>>>9)||result>7||(!result&&(control.stop_latched||control.test_build||[0,1,7].includes(control.controller_state_code))))throw new Error('Invalid Resume result fields');
  return {request_id:id,session_token:session,result,result_name:RESUME_RESULTS[result],...control};
}

// Letters ordered by action, one five-bit A–Z index per action.
export function encodeBarcodeMap(id,session,deadline,letters){
  if(!Number.isInteger(id)||id<1||id>0xffff||!Number.isInteger(session)||session<1||session>0xffffffff||
     !Number.isInteger(deadline)||deadline<0||deadline>0xffffffff)throw new Error('Invalid mapping request field');
  if(!Array.isArray(letters)||letters.length!==4||letters.some(letter=>typeof letter!=='string'||!/^[A-Z]$/.test(letter))||
     new Set(letters).size!==4)throw new Error('Select a different letter for each action');
  const b=new Uint8Array(14),v=viewOf(b);
  b[0]=0xa9;v.setUint16(1,id,true);v.setUint32(3,session,true);v.setUint32(7,deadline,true);
  const packed=letters.reduce((word,letter,i)=>word|(BARCODE_LETTERS.indexOf(letter)<<(5*i)),0);
  b[11]=packed;b[12]=packed>>>8;b[13]=packed>>>16;
  return b;
}

export function decodeBarcodeMapReply(payload){
  const b=bytesOf(payload),v=viewOf(b);
  if(b.length!==8||b[0]!==0xaa)throw new Error('Invalid barcode mapping reply');
  const id=v.getUint16(1,true),session=v.getUint32(3,true),result=b[7];
  if(!id||!session||result>=BARCODE_MAP_RESULTS.length)throw new Error('Invalid barcode mapping result');
  return {request_id:id,session_token:session,result,result_name:BARCODE_MAP_RESULTS[result]};
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
  needsClockSync(at=Date.now()) {
    return this.seenSession!==null&&!this.pendingStatus&&!this.pendingStop&&!this.pendingResume&&!this.pendingMap&&
      (!this.clockAnchor||this.clockAnchor.session!==this.seenSession||at<this.clockAnchor.at||at-this.clockAnchor.at>=10000);
  }
  packetAge(value,at,limit=2000) {
    const a=this.clockAnchor;
    if(!a||a.session!==value.session_token||at<a.at||at-a.at>30000)return null;
    const estimated=(a.boot+at-a.at)>>>0,delta=(estimated-value.uptime_ms)>>>0;
    if(delta>0x7fffffff){if(((value.uptime_ms-estimated)>>>0)>a.rtt+250)throw new Error('Robot clock changed; awaiting fresh STATUS');return 0;}
    if(delta>limit)throw new Error('Delayed telemetry expired against Pico STATUS clock');
    return delta;
  }
  currentDiagnostics(at=Date.now(),connected=true) {
    return connected&&this.clockAnchor&&this.diagnostics?.session_token===this.clockAnchor.session&&at>=this.diagnostics.at&&at-this.diagnostics.at<=7000?this.diagnostics:null;
  }
  currentTerrain(at=Date.now(),connected=true) {
    const terrain=this.terrain;
    return connected&&terrain&&terrain.session_token===this.clockAnchor?.session&&
      at>=terrain.at&&at-terrain.at+terrain.age<=2000?terrain:null;
  }
  currentBarcodeSignal(at=Date.now(),connected=true) {
    const signal=this.barcodeSignal;
    return connected&&signal&&signal.session_token===this.clockAnchor?.session&&
      at>=signal.at&&at-signal.at+signal.age<=2000?signal:null;
  }
  ingestAux(bytes,at,packet) {
    const marker=bytes[0];
    const value=marker===0xb5?decodeScan(bytes):marker===0xb6?decodeDiagnostics(bytes):
      marker===0xb7?decodeTerrain(bytes):decodeBarcodeSignal(bytes);
    this.seenSession=value.session_token;
    const age=this.packetAge(value,at,marker===0xb5?10000:marker===0xb6?7000:2000);
    packet.accepted=true;packet.decoded=value;
    if(age===null){packet.detail='Waiting for fresh Pico STATUS clock sync';return true;}
    if(marker===0xb8){
      packet.detail='GP28 barcode sensor';
      this.barcodeSignal={...value,at,age};
      return true;
    }
    if(marker===0xb7){
      packet.detail='S4 motion and terrain';
      const before=this.terrain?.session_token===value.session_token?this.terrain:null;
      if(before&&((value.uptime_ms-before.uptime_ms)>>>0)>0x7fffffff){packet.detail='Older terrain sample ignored';return true;}
      if(value.motion_class&&value.motion_class!==before?.motion_class){
        const urgent=['Pickup','Sudden impact'].includes(value.motion_class);
        this.event(value.motion_class,'S4 motion classification',urgent?'warning':'neutral',at);
      }
      const humpKey=value.hump_kind?value.session_token+'/'+value.hump_event_sequence:null;
      if(humpKey&&humpKey!==this.lastHumpEvent){this.lastHumpEvent=humpKey;this.event('Hump measured',value.hump_kind,'success',at);}
      this.terrain={...value,at,age};
      return true;
    }
    if(bytes[0]===0xb6){this.diagnostics={...value,at};this.diagnosticHistory.push(this.diagnostics);if(this.diagnosticHistory.length>120)this.diagnosticHistory.shift();packet.detail='Runtime diagnostics';return true;}
    if(this.scanProfile?.session_token===value.session_token&&this.scanProfile.sequence===value.scan_sequence){packet.detail='Completed scan duplicate ignored';return true;}
    let p=this.scanPending;
    const key=value.session_token+'/'+value.scan_sequence;
    if(!p||p.key!==key||at-p.at>10000){
      if(value.start!==0){packet.detail='Waiting for scan start chunk';return true;}
      p=this.scanPending={key,total:value.total,source:value.uptime_ms,width_mm:value.width_mm,points:new Array(value.total),received:0,at};
    }
    if(p.total!==value.total||p.source!==value.uptime_ms||(value.start===0&&p.width_mm!==value.width_mm))throw new Error('Scan metadata changed mid-profile');
    value.points.forEach((point,i)=>{
      const old=p.points[value.start+i];
      if(old&&(old.angle!==point.angle||old.distance_mm!==point.distance_mm))throw new Error('Conflicting scan chunk');
    });
    value.points.forEach((point,i)=>{const index=value.start+i;if(!p.points[index]){p.points[index]=point;p.received++;}});
    if(p.received===p.total){
      if(p.points.some((point,i)=>i&&point.angle<=p.points[i-1].angle))throw new Error('Scan angles are not increasing');
      this.scanProfile={sequence:value.scan_sequence,session_token:value.session_token,uptime_ms:value.uptime_ms,width_mm:p.width_mm,points:p.points,at};this.scanPending=null;
      this.event('Scan profile received',p.total+' measured angles','neutral',at);
    }
    packet.detail='Scan chunk '+(value.start+1)+'–'+(value.start+value.points.length)+' / '+value.total;return true;
  }
  currentSample(now = Date.now(), connected = true) {
    const age = this.lastTelemetry === null ? Infinity : now - this.lastTelemetry;
    return connected && this.clockAnchor && this.sample?.session_token===this.clockAnchor.session && age >= 0 && age + (this.sample?.transport_age_ms??0) <= 2000 ? this.sample : null;
  }
  reset() {
    this.history=[]; this.events=[]; this.packets=[]; this.sample=null;
    this.lastTelemetry=null; this.lastHeartbeat=null; this.lastSequence=null; this.lastHeartbeatCounter=null;
    this.messageCount=0; this.telemetryCount=0; this.rejected=0; this.sequenceGaps=0;
    this.totalBytes=0; this.lastPayloadBytes=0; this.lastInterval=null; this.format='—';
    this.clockAnchor=null;this.seenSession=null;this.scanProfile=null;this.scanPending=null;this.diagnostics=null;this.diagnosticHistory=[];this.heartbeatIntervals=[];this.lastBarcodeEvent=null;this.lastBarcode=null;this.barcodeSignal=null;
    this.terrain=null;this.lastHumpEvent=null;
    this.pendingStatus=null;this.statusReply=null;this.lastStatusId=0;this.statusLastSentAt=null;
    this.pendingResume=null;this.resumeReply=null;this.lastResumeId=0;this.resumeNotice='Resume releases STOP only after fresh controller checks.';
    this.pendingMap=null;this.confirmedMap=null;this.mapNotice='';
    this.statusNotice='Controller status is read-only. Resume may restart autonomous movement.';
    this.controller=null;this.pendingStop=null;this.stopReply=null;this.stopNotice='Waiting for current controller status.';
  }
  beginStatus(id,at=Date.now(),forStop=false,purpose='user') {
    if((this.pendingStop&&!forStop)||this.pendingResume)throw new Error('A controller request is pending');
    if(this.pendingStatus)throw new Error('A STATUS request is already pending');
    if(this.statusLastSentAt!==null&&at-this.statusLastSentAt<250)throw new Error('Wait briefly before requesting STATUS again');
    const bytes=encodeStatusRequest(id);
    if(id===this.lastStatusId)throw new Error('Use a fresh STATUS request ID');
    this.pendingStatus={id,at,deadline:at+3000,purpose};this.lastStatusId=id;this.statusLastSentAt=at;
    this.statusNotice='Waiting for the Pico STATUS reply...';return bytes;
  }
  cancelStatus(reason='Connection changed; STATUS request cancelled.') {
    if(this.pendingStatus){this.pendingStatus=null;this.statusNotice=reason;}
    if(this.pendingStop){this.pendingStop=null;this.stopNotice=reason;}
    if(this.pendingResume){this.pendingResume=null;this.resumeNotice=reason;}
    if(this.pendingMap){this.pendingMap=null;this.mapNotice='Mapping confirmation was interrupted. Send the mapping again.';}
  }
  clearController(reason='Connection changed; pending commands cancelled.') {
    if(!this.pendingMap)this.mapNotice='';
    this.cancelStatus(reason);this.confirmedMap=null;this.controller=null;this.clockAnchor=null;this.seenSession=null;this.scanPending=null;this.lastSequence=null;this.lastHeartbeatCounter=null;this.lastHeartbeat=null;this.terrain=null;
  }
  currentController(now=Date.now(),connected=true) {
    return connected&&this.controller?.controller_valid&&now>=this.controller.at&&now-this.controller.at<=2000?this.controller:null;
  }
  observeController(value,at) {
    const old=this.controller;
    if(old&&old.session_token!==value.session_token) {
      this.confirmedMap=null;this.mapNotice='';
      this.cancelStatus('Robot session changed; pending commands cancelled.');this.lastSequence=null;this.sample=null;this.lastTelemetry=null;this.scanPending=null;this.diagnostics=null;this.lastHeartbeatCounter=null;
      this.event('Controller session changed','Fresh requests are required','neutral',at);
    }
    this.controller={...value,at};
    if(!old||old.controller_word!==value.controller_word)
      this.event(value.controller_state,value.stop_latched?'Controller stop inhibit is latched':'Controller publication',value.controller_state_code===7?'warning':'neutral',at);
  }
  beginStop(id,at=Date.now()) {
    if(this.pendingStop)throw new Error('A stop request is already pending');
    if(this.pendingStatus){
      const p=this.pendingStatus;
      this.pendingStop={phase:'checking',status_id:p.id,at:p.at,deadline:p.deadline};
      this.stopNotice='Using the pending fresh STATUS reply before STOP...';
      return null;
    }
    const cooling=this.statusLastSentAt!==null&&at-this.statusLastSentAt<250;
    if(this.pendingResume||cooling){
      const session=this.pendingResume?.session??this.clockAnchor?.session;
      let deadline;try{deadline=this.commandDeadline(at,session);}catch{}
      const bytes=deadline===undefined?null:encodeStopRequest(id,session,deadline);
      if(this.pendingResume){this.pendingResume=null;this.resumeNotice='Resume cancelled by STOP.';}
      if(bytes){
        this.pendingStop={phase:'waiting',command_id:id,session,sent_at:at,deadline:at+3000};
        this.stopNotice='STOP submitted; waiting for the controller result...';
        return bytes;
      }
    }
    /* STOP is harmless while latched and must remain possible after a lost
     * Resume result. Refresh the session/clock instead of assuming its state. */
    const bytes=this.beginStatus(id,at,true);
    this.pendingStop={phase:'checking',status_id:id,at,deadline:at+3000};
    this.stopNotice='Checking fresh Pico status before sending STOP...';return bytes;
  }
  beginStopCommand(id,at=Date.now()) {
    const p=this.pendingStop,c=this.currentController(at);
    if(!p||p.phase!=='ready'||!c||c.session_token!==p.session||at>=p.valid_until)
      throw new Error('STOP lease expired or session changed; request again');
    if(id===p.status_id)throw new Error('Use a distinct STOP correlation ID');
    const bytes=encodeStopRequest(id,p.session,p.boot_deadline);
    Object.assign(p,{phase:'waiting',command_id:id,sent_at:at,deadline:at+3000});
    this.stopNotice='STOP submitted; waiting for the controller result...';return bytes;
  }
  commandDeadline(at,session){
    const a=this.clockAnchor;
    if(!a||a.session!==session||at<a.at||at-a.at>30000)throw new Error('Wait for fresh Pico clock synchronization');
    return (a.boot+at-a.at+2000)>>>0;
  }
  beginBarcodeMap(id,letters,at=Date.now()){
    const c=this.currentController(at);
    if(!c||!c.stop_latched||c.controller_state_code!==6)throw new Error('Latch STOP before sending the mapping');
    if(this.pendingMap||this.pendingStatus||this.pendingStop||this.pendingResume)throw new Error('A controller request is pending');
    const session=c.session_token;
    const b=encodeBarcodeMap(id,session,this.commandDeadline(at,session),letters);
    this.pendingMap={id,session,at,letters:[...letters]};this.mapNotice='Waiting for Pico confirmation…';
    return b;
  }
  beginResume(id,at=Date.now()){
    const c=this.currentController(at);
    if(!c||c.test_build||!c.stop_latched||c.controller_state_code!==6)throw new Error('Resume requires a current normal-mode stop latch');
    if(this.pendingStatus||this.pendingStop||this.pendingResume||this.pendingMap)throw new Error('A controller request is pending');
    const b=encodeResumeRequest(id,c.session_token,this.commandDeadline(at,c.session_token));
    if(id===this.lastResumeId)throw new Error('Use a fresh Resume request ID');
    this.pendingResume={phase:'arming',id,session:c.session_token,at,deadline:at+3000};this.lastResumeId=id;
    this.resumeNotice='Requesting one-use Resume authorization...';return b;
  }
  beginResumeCommand(at=Date.now()){
    const p=this.pendingResume,c=this.currentController(at);
    if(!p||p.phase!=='ready'||!c||c.session_token!==p.session||at>=p.valid_until)throw new Error('Resume authorization expired or session changed');
    const b=encodeResumeRequest(p.ticket,p.session,p.boot_deadline,true);
    Object.assign(p,{phase:'waiting',sent_at:at,deadline:at+3000});
    this.resumeNotice='Resume submitted; waiting for the controller result...';return b;
  }
  expireCommands(at=Date.now()) {
    this.expireStatus(at);
    if(this.pendingMap&&at-this.pendingMap.at>=3000){
      this.pendingMap=null;this.confirmedMap=null;this.mapNotice='No mapping reply. Send again before starting the run.';
    }
    const resume=this.pendingResume;
    if(resume&&((resume.phase==='ready'&&at>=resume.valid_until)||at>=resume.deadline)){
      this.pendingResume=null;
      this.resumeNotice=resume.phase==='waiting'?'No matching Resume result; outcome unknown. Check live controller state.':'No Resume sent; authorization expired.';
      this.event('Resume expired',this.resumeNotice,'warning',at);
    }
    const p=this.pendingStop;if(!p)return;
    if((p.phase==='ready'&&at>=p.valid_until)||at>=p.deadline) {
      this.pendingStop=null;
      this.stopNotice=p.phase==='waiting'?'No matching STOP reply; outcome unknown. Check controller state.':'No STOP sent; fresh-status check expired.';
      this.event('STOP request expired',this.stopNotice,'warning',at);
    }
  }
  expireStatus(at=Date.now()) {
    if(!this.pendingStatus||at<this.pendingStatus.deadline)return false;
    this.pendingStatus=null;this.statusNotice='No STATUS reply within 3 seconds. Check Pico firmware and command/status topics.';
    if(this.pendingStop?.phase==='checking'){this.pendingStop=null;this.stopNotice='No STOP sent; status handshake timed out.';}
    this.event('STATUS timed out','No automatic retry; request again after checking the robot','warning',at);return true;
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
      if(kind==='telemetry'&&[0xb5,0xb6,0xb7,0xb8].includes(bytes[0]))return this.ingestAux(bytes,at,packet);
      if(kind==='status') {
        if(bytes[0]===0xaa){
          this.expireCommands(at);
          const reply=decodeBarcodeMapReply(bytes),p=this.pendingMap;
          if(!p||at<p.at||reply.request_id!==p.id||(reply.session_token!==p.session&&reply.result!==2))
            throw new Error('Unmatched or expired barcode mapping reply');
          this.pendingMap=null;
          if(reply.result===0)this.confirmedMap=p.letters;
          if(reply.result===2)this.confirmedMap=null;
          this.mapNotice=reply.result===0?'Pico confirmed the mapping.':`Mapping rejected. ${reply.result_name}.`;
          packet.accepted=true;packet.decoded=reply;packet.detail='Barcode mapping result';return true;
        }
        if(bytes[0]===0xa6){
          const reply=decodeResumeArmReply(bytes),p=this.pendingResume;this.expireCommands(at);
          if(!p||p!==this.pendingResume||p.phase!=='arming'||reply.request_id!==p.id||at<p.at)throw new Error('Unmatched or expired Resume authorization');
          const elapsed=at-p.at;
          if(reply.session_token!==p.session||elapsed>=2000){this.pendingResume=null;throw new Error('Resume session changed or authorization too old');}
          this.clockAnchor={session:reply.session_token,boot:reply.uptime_ms,at,rtt:elapsed};
          this.observeController(reply,at);
          Object.assign(p,{phase:'ready',ticket:reply.ticket,boot_deadline:(reply.uptime_ms+2000)>>>0,valid_until:at+2000-elapsed});
          this.resumeNotice='Fresh Resume authorization received.';
          packet.accepted=true;packet.decoded=reply;packet.detail='Resume authorization';return true;
        }
        if(bytes[0]===0xa8){
          const reply=decodeResumeReply(bytes),p=this.pendingResume;this.expireCommands(at);
          const id=p?.phase==='arming'?p.id:p?.ticket;
          if(!p||p!==this.pendingResume||!['arming','waiting'].includes(p.phase)||reply.request_id!==id||at<(p.phase==='arming'?p.at:p.sent_at)||
             (p.phase==='arming'&&reply.result===0)||(reply.session_token!==p.session&&reply.result!==2))throw new Error('Unmatched or expired Resume result');
          this.pendingResume=null;this.resumeReply={...reply,received_at:at};this.observeController(reply,at);
          this.resumeNotice=reply.result===0?'STOP latch released; autonomous control may move the car.':`Resume not applied: ${reply.result_name}.`;
          this.event(reply.result===0?'Resume confirmed':'Resume blocked',this.resumeNotice,reply.result===0?'success':'warning',at);
          packet.accepted=true;packet.decoded=reply;packet.detail='Controller Resume result';return true;
        }
        if(bytes[0]===0xa4) {
          const reply=decodeStopReply(bytes),p=this.pendingStop;
          this.expireCommands(at);
          if(!p||this.pendingStop!==p||p.phase!=='waiting'||reply.request_id!==p.command_id||at<p.sent_at||
             (reply.session_token!==p.session&&reply.result!==2))throw new Error('Unmatched or expired STOP reply ignored');
          this.pendingStop=null;this.stopReply={...reply,received_at:at,round_trip_ms:at-p.sent_at};
          this.observeController(reply,at);
          this.stopNotice=reply.result===0?(reply.controller_state_code===1?'Controller stop latch confirmed; motor control is disabled in this build.':'Controller stop latch confirmed; zero-duty request submitted. Physical standstill is not measured.'):`STOP not confirmed: ${reply.result_name}.`;
          this.event(reply.result===0?'Controller stop confirmed':'STOP rejected',this.stopNotice,reply.result===0?'success':'warning',at);
          packet.accepted=true;packet.decoded=reply;packet.detail='Controller STOP result';return true;
        }
        const reply=decodeStatusReply(bytes);
        this.expireStatus(at);
        if(!this.pendingStatus||reply.request_id!==this.pendingStatus.id||at<this.pendingStatus.at)
          throw new Error('Unmatched or expired STATUS reply ignored');
        const elapsed=at-this.pendingStatus.at,purpose=this.pendingStatus.purpose;
        this.clockAnchor={session:reply.session_token,boot:reply.uptime_ms,at,rtt:elapsed};this.seenSession=reply.session_token;
        this.pendingStatus=null;this.statusReply={...reply,received_at:at,round_trip_ms:elapsed};
        this.observeController(reply,at);
        this.statusNotice=`Pico STATUS reply confirmed in ${elapsed} ms.`;
        if(this.pendingStop?.phase==='checking'&&this.pendingStop.status_id===reply.request_id) {
          if(!reply.controller_valid||elapsed>=2000) {
            this.pendingStop=null;this.stopNotice='No STOP sent; controller unavailable or status reply too old.';
          } else {
            Object.assign(this.pendingStop,{phase:'ready',session:reply.session_token,
              boot_deadline:(reply.uptime_ms+2000)>>>0,valid_until:at+2000-elapsed});
          }
        }
        if(purpose!=='clock')this.event('STATUS reply confirmed',`Pico uptime ${Math.floor(reply.uptime_ms/1000)} s`,'success',at);
        packet.accepted=true;packet.decoded=reply;packet.detail='STATUS reply';return true;
      }
      if (kind==='heartbeat') {
        const count=decodeHeartbeat(bytes);
        if(this.lastHeartbeatCounter!==null){
          const delta=(count-this.lastHeartbeatCounter)>>>0;
          if(delta===0||delta>0x7fffffff)throw new Error('Duplicate or backward heartbeat counter');
        }
        if(this.lastHeartbeat!==null&&at>=this.lastHeartbeat){this.heartbeatIntervals.push(at-this.lastHeartbeat);if(this.heartbeatIntervals.length>120)this.heartbeatIntervals.shift();}
        this.lastHeartbeatCounter=count; this.lastHeartbeat=at; packet.accepted=true;
        packet.decoded={counter:count};packet.detail='Binary heartbeat';return true;
      }
      const decoded=decodeTelemetry(bytes), sample=decoded.sample;
      this.seenSession=sample.session_token;
      const transportAge=this.packetAge(sample,at);
      if(transportAge===null){packet.accepted=true;packet.decoded=sample;packet.detail='Waiting for fresh Pico STATUS clock sync';return true;}
      sample.transport_age_ms=transportAge;
      this.observeController(sample,at);
      if (this.lastSequence!==null) {
        const delta=(sample.sequence-this.lastSequence)>>>0;
        if (delta===0 || delta>0x7fffffff) throw new Error('Duplicate or out-of-order sequence');
        this.sequenceGaps+=delta-1;
      }
      this.lastSequence=sample.sequence;
      this.lastInterval=this.lastTelemetry===null ? null : at-this.lastTelemetry;
      if (sample.state && sample.state!==this.sample?.state) this.event(sample.state,'Vehicle state changed','neutral',at);
      const barcodeKey=sample.barcode_event_sequence!==null?sample.session_token+'/'+sample.barcode_event_sequence+'/'+sample.barcode:null;
      if(barcodeKey&&barcodeKey!==this.lastBarcodeEvent){
        this.lastBarcodeEvent=barcodeKey;
        this.lastBarcode={session_token:sample.session_token,sequence:sample.barcode_event_sequence,
          barcode:sample.barcode,barcode_navigation:sample.barcode_navigation};
        if(sample.barcode)this.event('Barcode decoded',sample.barcode,'success',at);
        else this.event('Barcode detected',`Scan #${sample.barcode_event_sequence}`,'success',at);
      }
      if(sample.imu_state && sample.imu_state!==this.sample?.imu_state)
        this.event(`IMU · ${sample.imu_state}`,'S4 sensor state changed',sample.imu_state==='Sensor fault'?'warning':'neutral',at);
      if (!sample.obstacle_valid && this.sample?.obstacle_valid!==false) this.event('Range unavailable','No valid ultrasonic measurement','warning',at);
      this.sample=sample; this.lastTelemetry=at; this.telemetryCount++; this.lastPayloadBytes=bytes.length; this.format=decoded.format;
      this.history.push({...sample,at,interval:this.lastInterval,bytes:bytes.length});
      if(this.history.length>600) this.history.shift();
      packet.accepted=true; packet.decoded=sample; packet.detail=decoded.format; return true;
    } catch (error) { this.rejected++; packet.detail=error.message; this.event('Message rejected',error.message,'warning',at); return false; }
  }
}
