// Offline binary test fixtures. Never sent to a live broker.
export function telemetry({sequence=12,distance=451,line=6,pitch=1051,height=12,max=34,state=2,flags=1}={}) {
  const bytes=new Uint8Array(11),view=new DataView(bytes.buffer);
  view.setUint32(0,sequence,true);view.setUint16(4,0x8000|distance|(line<<12),true);
  const word=BigInt(pitch)|(BigInt(height)<<11n)|(BigInt(max)<<18n)|(BigInt(state)<<25n)|(BigInt(flags)<<28n);
  for(let i=0;i<5;i++)bytes[6+i]=Number((word>>BigInt(i*8))&255n);
  return bytes;
}
export function heartbeat(counter=0) {
  const bytes=new Uint8Array([128,0,0,0,0]);new DataView(bytes.buffer).setUint32(1,counter,true);return bytes;
}
export function statusReply({id=12,uptime=1000,age=50,testBuild=true,frame=telemetry()}={}) {
  const bytes=new Uint8Array(age===null?11:22),view=new DataView(bytes.buffer);
  bytes[0]=0xa1;view.setUint32(1,id,true);view.setUint32(5,uptime,true);
  view.setUint16(9,(age===null?2047:age)|(testBuild?2048:0),true);
  if(age!==null)bytes.set(frame,11);return bytes;
}
