// Offline specification fixtures only. Never publish these to a live broker.
export function putBits(bytes,offset,width,value){
  const n=BigInt(value>>>0);
  for(let i=0;i<width;i++){
    const index=(offset+i)>>3,mask=1<<((offset+i)&7);
    bytes[index]=(bytes[index]&~mask)|(Number((n>>BigInt(i))&1n)<<((offset+i)&7));
  }
}
export function telemetry({sequence=12,distance=451,line=6,pitch=1051,height=12,max=34,
  state=2,flags=1,session=0x12345678,uptime=1000,control=33,left=null,right=null,
  leftCount=null,rightCount=null,net=null,travel=null,known=0,fault=0,
  barcode=0,event=0,navigation=0,mode=3,reason=7}={}){
  const b=new Uint8Array(46),v=new DataView(b.buffer),body=b.subarray(13);
  b[0]=0xb4;v.setUint32(1,sequence,true);v.setUint32(5,uptime,true);v.setUint32(9,session,true);
  for(const [offset,width,value]of [[0,12,distance],[12,3,line],[15,11,pitch],[26,7,height],
    [33,7,max],[40,3,state],[43,6,flags],[49,14,left===null?16383:left+5000],
    [63,14,right===null?16383:right+5000],[77,32,leftCount===null?0x80000000:leftCount],
    [109,32,rightCount===null?0x80000000:rightCount],[141,32,net===null?0x80000000:net],
    [173,32,travel===null?0xffffffff:travel],[205,11,known],[216,11,fault],
    [227,7,barcode],[234,16,event],[250,6,control],[256,2,mode],[258,3,reason],[261,3,navigation]])
    putBits(body,offset,width,value);
  return b;
}
export function heartbeat(counter=0){const b=new Uint8Array([128,0,0,0,0]);new DataView(b.buffer).setUint32(1,counter,true);return b;}
export function statusReply({id=12,uptime=1000,age=50,testBuild=true,frame,
  session=0x12345678,state=1,latched=false}={}){
  const b=new Uint8Array(age===null?16:62),v=new DataView(b.buffer);
  const control=state|(latched?16:0)|(testBuild?32:0),meta=(age===null?2047:age)|(control<<11);
  b[0]=0xa1;v.setUint32(1,id,true);v.setUint32(5,uptime,true);
  b[9]=meta;b[10]=meta>>8;b[11]=meta>>16;v.setUint32(12,session,true);
  if(age!==null)b.set(frame??telemetry({uptime,session,control}),16);
  return b;
}
export function stopReply({id=100,session=0x12345678,result=0,word=22}={}){
  const b=new Uint8Array(11),v=new DataView(b.buffer);b[0]=0xa4;
  v.setUint32(1,id,true);v.setUint32(5,session,true);v.setUint16(9,word|(result<<6),true);return b;
}
export function syncStore(store,{at=1000,uptime=1000,session=0x12345678,state=1,testBuild=true,id=1,latched=false}={}){
  store.beginStatus(id,at-20);
  if(!store.ingest(statusReply({id,uptime,session,state,testBuild,latched}),'status',at))throw new Error('Fixture clock sync failed');
  return store;
}
export function scanChunk({session=0x12345678,sequence=1,uptime=900,total=181,start=0,
  points=Array.from({length:Math.min(16,total-start)},(_,i)=>({angle:start+i,distance_mm:451})),width=null}={}){
  const pointEnd=16+Math.ceil(points.length*20/8);
  const b=new Uint8Array(pointEnd+(width===null?0:2)),v=new DataView(b.buffer);
  b[0]=0xb5;v.setUint32(1,session,true);v.setUint32(5,sequence,true);v.setUint32(9,uptime,true);
  b[13]=total;b[14]=start;b[15]=points.length;
  points.forEach((p,i)=>{putBits(b.subarray(16),20*i,8,p.angle);putBits(b.subarray(16),20*i+8,12,p.distance_mm??0);});
  if(width!==null){b[15]|=128;v.setUint16(pointEnd,width,true);}
  return b;
}
export function diagnostics({session=0x12345678,uptime=1000,values=[82596,5000,4000,4500,12288,null],counters=[1,2,3,4]}={}){
  const b=new Uint8Array(40),v=new DataView(b.buffer);b[0]=0xb6;
  v.setUint32(1,session,true);v.setUint32(5,uptime,true);
  values.forEach((n,i)=>putBits(b.subarray(9,24),19*i,19,n??0x7ffff));
  counters.forEach((n,i)=>v.setUint32(24+4*i,n,true));return b;
}

export function resumeArmReply(options={}){
  const {ticket=77,...rest}=options;
  const base=statusReply({state:6,latched:true,testBuild:false,...rest});
  const b=new Uint8Array(base.length+4);b.set(base.subarray(0,16));b[0]=0xa6;
  new DataView(b.buffer).setUint32(16,ticket,true);if(base.length>16)b.set(base.subarray(16),20);return b;
}
export function resumeReply(options={}){const b=stopReply({id:77,word:3,...options});b[0]=0xa8;return b;}
