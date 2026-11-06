const decoder = new TextDecoder('utf-8', { fatal: true });
export const encoder = new TextEncoder();
export const STATES = ['Ready', 'Following line', 'Turning left', 'Turning right', 'Scanning obstacle', 'Avoiding obstacle', 'Recovering line', 'Stopped', 'Fault'];

function finite(value, min = 0, max = Infinity) {
  return typeof value === 'number' && Number.isFinite(value) && value >= min && value <= max ? value : null;
}
function decodeLineSensors(data) {
  if (Object.hasOwn(data, 'line_bits')) {
    const bits = data.line_bits;
    if (!Number.isInteger(bits) || bits < 0 || bits > 7 || (bits !== 0 && !(bits & 4)))
      throw new Error('Invalid packed line sensor bits');
    return bits & 4 ? [null, (bits >> 1) & 1, bits & 1] : [null, null, null];
  }
  // Older JSON may supply an array. Left is reserved for barcode capture.
  return Array.isArray(data.ir) && data.ir.length === 3 && data.ir.every(x => x === null || x === 0 || x === 1)
    ? [null, data.ir[1], data.ir[2]] : null;
}
export function validateTelemetry(data) {
  if (!data || typeof data !== 'object' || Array.isArray(data)) throw new Error('Telemetry must be an object');
  if (!Number.isInteger(data.sequence) || data.sequence < 0 || data.sequence > 0xffffffff) throw new Error('Invalid sequence');
  if (typeof data.obstacle_valid !== 'boolean') throw new Error('Missing measurement validity');
  if (data.obstacle_valid && (!Number.isInteger(data.obstacle_mm) || data.obstacle_mm < 20 || data.obstacle_mm > 4000)) throw new Error('Obstacle distance outside 20–4000 mm');
  if (!data.obstacle_valid && data.obstacle_mm !== null) throw new Error('Invalid distance must be null');
  return {
    sequence: data.sequence, obstacle_valid: data.obstacle_valid,
    obstacle_mm: data.obstacle_valid ? data.obstacle_mm : null,
    state: STATES.includes(data.state) ? data.state : null,
    left_mps: finite(data.left_mps, -5, 5), right_mps: finite(data.right_mps, -5, 5),
    distance_m: finite(data.distance_m), hump_mm: finite(data.hump_mm, 0, 1000), max_hump_mm: finite(data.max_hump_mm, 0, 1000),
    ir: decodeLineSensors(data),
    barcode: ['Turn left', 'Turn right', 'Go straight', 'U-turn'].includes(data.barcode) ? data.barcode : null,
    faults: Array.isArray(data.faults) && data.faults.length <= 16 && data.faults.every(x => typeof x === 'string' && x.length <= 60) ? [...data.faults] : null,
    scan: Array.isArray(data.scan) && data.scan.length <= 181 && data.scan.every(x => x && finite(x.angle, 0, 180) !== null && (x.distance_mm === null || finite(x.distance_mm, 20, 4000) !== null)) ? data.scan.map(x => ({angle:x.angle, distance_mm:x.distance_mm})) : null
  };
}

// Proposed distance-only adapter. It is not yet implemented or agreed in firmware.
// 32-bit sequence (LE), followed by distance:12, valid:1, version:3 (LE).
export function decodeCompact(bytes) {
  if (bytes.length !== 6) throw new Error('Compact distance frame must contain 6 bytes');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const packed = view.getUint16(4, true);
  if (packed >>> 13 !== 1) throw new Error('Unsupported compact frame version');
  const valid = Boolean(packed & 0x1000);
  if (!valid && (packed & 0xfff) !== 0) throw new Error('Invalid frame must clear distance bits');
  return validateTelemetry({ sequence:view.getUint32(0,true), obstacle_valid:valid, obstacle_mm:valid ? packed & 0xfff : null });
}
export function decodeTelemetry(payload, format = 'auto') {
  const bytes = payload instanceof Uint8Array ? payload : new Uint8Array(payload);
  if (bytes.length === 0 || bytes.length > 16384) throw new Error('Invalid telemetry length');
  const compact = format === 'compact-v1' || (format === 'auto' && bytes.length === 6 && (bytes[5] >>> 5) === 1);
  return compact ? {sample:decodeCompact(bytes), format:'Compact v1 · proposed'} : {sample:validateTelemetry(JSON.parse(decoder.decode(bytes))), format:'JSON · current firmware'};
}
export function decodeHeartbeat(payload) {
  const text = decoder.decode(payload);
  if (!/^hb [0-9]{1,10}$/.test(text)) throw new Error('Unrecognised heartbeat');
  return Number(text.slice(3));
}
export function hex(bytes) { return [...bytes].map(x => x.toString(16).padStart(2,'0')).join(' '); }

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
  ingest(bytes, kind, at=Date.now(), retained=false, format='auto', topic='') {
    this.messageCount++; this.totalBytes+=bytes.length;
    const packet={at,topic,bytes:new Uint8Array(bytes),kind,accepted:false,detail:''};
    this.packets.unshift(packet); this.packets.length=Math.min(this.packets.length,30);
    try {
      if (retained) throw new Error('Retained message ignored; waiting for a fresh sample');
      if (kind==='heartbeat') {
        const count=decodeHeartbeat(bytes);
        if(this.lastHeartbeatCounter!==null && count<this.lastHeartbeatCounter) {
          this.lastSequence=null;
          this.event('Heartbeat counter restarted','Sequence baseline reset; the robot may have rebooted','neutral',at);
        }
        this.lastHeartbeatCounter=count; this.lastHeartbeat=at; packet.accepted=true; packet.detail='Heartbeat'; return true;
      }
      const decoded=decodeTelemetry(bytes,format), sample=decoded.sample;
      if (this.lastSequence!==null) {
        const delta=(sample.sequence-this.lastSequence)>>>0;
        if (delta===0 || delta>0x7fffffff) throw new Error('Duplicate or out-of-order sequence');
        this.sequenceGaps+=delta-1;
      }
      this.lastSequence=sample.sequence;
      this.lastInterval=this.lastTelemetry===null ? null : at-this.lastTelemetry;
      if (sample.state && sample.state!==this.sample?.state) this.event(sample.state,'Vehicle state changed','neutral',at);
      if (sample.barcode && sample.barcode!==this.sample?.barcode) this.event('Barcode decoded',sample.barcode,'success',at);
      if (!sample.obstacle_valid && this.sample?.obstacle_valid!==false) this.event('Range unavailable','No valid ultrasonic measurement','warning',at);
      this.sample=sample; this.lastTelemetry=at; this.telemetryCount++; this.lastPayloadBytes=bytes.length; this.format=decoded.format;
      this.history.push({...sample,at,interval:this.lastInterval,bytes:bytes.length});
      if(this.history.length>600) this.history.shift();
      packet.accepted=true; packet.detail=decoded.format; return true;
    } catch (error) { this.rejected++; packet.detail=error.message; this.event('Message rejected',error.message,'warning',at); return false; }
  }
}
