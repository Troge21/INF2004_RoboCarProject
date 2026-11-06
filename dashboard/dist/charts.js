const NS='http://www.w3.org/2000/svg';
function node(tag,attributes={},text=''){const el=document.createElementNS(NS,tag);for(const [k,v]of Object.entries(attributes))el.setAttribute(k,v);if(text)el.textContent=text;return el;}
export function lineChart(container,history,series,{seconds=60,now=Date.now(),min=0,max,unit='',empty='Waiting for sensor data',precision=1}={}){
  if(!container)return;
  const width=600,height=214,left=46,right=14,top=21,bottom=29,plotW=width-left-right,plotH=height-top-bottom;
  const data=history.filter(d=>d.at>=now-seconds*1000&&d.at<=now);
  const values=data.flatMap(d=>series.map(s=>d[s.key])).filter(Number.isFinite);
  const lower=Math.min(min,...values),upper=max??Math.max(unit==='m/s'?.4:1,...values)*1.18;
  const span=upper-lower||1,x=t=>left+((t-(now-seconds*1000))/(seconds*1000))*plotW,y=v=>top+plotH-(v-lower)/span*plotH;
  const svg=node('svg',{viewBox:`0 0 ${width} ${height}`,role:'img','aria-label':`${series.map(s=>s.label).join(' and ')} over the last ${seconds} seconds${values.length?'':'. '+empty}`});
  for(let i=0;i<5;i++){
    const val=lower+span*i/4,yy=y(val);
    svg.append(node('line',{x1:left,y1:yy,x2:width-right,y2:yy,stroke:'#edf0f4','stroke-width':1,'stroke-dasharray':'3 4'}));
    const label=upper>=100?Math.round(val):val.toFixed(precision);
    svg.append(node('text',{x:left-11,y:yy+3,'text-anchor':'end'},String(label)));
  }
  for(let i=0;i<=4;i++)svg.append(node('text',{x:left+plotW*i/4,y:height-7,'text-anchor':i===0?'start':i===4?'end':'middle'},i===4?'Now':`−${seconds*(1-i/4)}s`));
  if(unit)svg.append(node('text',{x:8,y:10},unit));
  for(const s of series){
    let path='',last=null;
    for(const d of data){
      const value=d[s.key];
      if(!Number.isFinite(value)){last=null;continue;}
      const px=x(d.at),py=y(value),gap=last&&d.at-last.at>Math.max(2500,seconds*1000/30);
      path+=`${!last||gap?'M':'L'}${px.toFixed(2)},${py.toFixed(2)} `;last=d;
    }
    svg.append(node('path',{d:path,fill:'none',stroke:s.color,'stroke-width':2,'stroke-linejoin':'round','stroke-linecap':'round'}));
    if(last){svg.append(node('circle',{cx:x(last.at),cy:y(last[s.key]),r:3.3,fill:s.color,stroke:'#fff','stroke-width':2}));}
  }
  container.replaceChildren(svg);
  if(!values.length){const emptyEl=document.createElement('div');emptyEl.className='chart-empty';const strong=document.createElement('strong');strong.textContent='No measurements yet';const small=document.createElement('span');small.textContent=empty;emptyEl.append(strong,small);container.append(emptyEl);}
  container.onpointermove=event=>{
    container.querySelector('.chart-tooltip')?.remove();
    if(!values.length)return;
    const rect=svg.getBoundingClientRect(),relative=(event.clientX-rect.left)/rect.width*width;
    const target=now-seconds*1000+((relative-left)/plotW)*seconds*1000;
    const closest=data.reduce((best,d)=>!best||Math.abs(d.at-target)<Math.abs(best.at-target)?d:best,null);
    if(!closest)return;
    const tip=document.createElement('div');tip.className='chart-tooltip';
    tip.textContent=new Date(closest.at).toLocaleTimeString([], {hour12:false})+'\n'+series.map(s=>`${s.label}: ${Number.isFinite(closest[s.key])?closest[s.key].toFixed(precision)+' '+unit:'Unavailable'}`).join('\n');
    tip.style.left=Math.max(6,Math.min(event.clientX-container.getBoundingClientRect().left+12,container.clientWidth-165))+'px';tip.style.top='35px';container.append(tip);
  };
  container.onpointerleave=()=>container.querySelector('.chart-tooltip')?.remove();
}
export function sparkline(container,values,color='#8b78df'){
  if(!container)return;const data=values.slice(-24);const good=data.filter(Number.isFinite);
  if(good.length<2){container.replaceChildren();return;}
  const min=Math.min(...good),max=Math.max(...good),span=max-min||1;
  let path='',open=false;
  data.forEach((v,i)=>{if(!Number.isFinite(v)){open=false;return;}path+=`${open?'L':'M'}${i/Math.max(1,data.length-1)*80},${24-(v-min)/span*20} `;open=true;});
  const svg=node('svg',{viewBox:'0 0 80 28','aria-hidden':'true'});svg.append(node('path',{d:path,fill:'none',stroke:color,'stroke-width':1.4}));container.replaceChildren(svg);
}
export function scanChart(container,scan){
  if(!container)return;const svg=node('svg',{viewBox:'0 0 600 214',role:'img','aria-label':scan?'Latest servo scan: angle and obstacle range':'Servo scan unavailable'});
  for(const r of [40,80,120,160])svg.append(node('path',{d:`M ${300-r} 185 A ${r} ${r} 0 0 1 ${300+r} 185`,fill:'none',stroke:'#e9edf3','stroke-dasharray':'3 4'}));
  for(const angle of [0,30,60,90,120,150,180]){const rad=angle*Math.PI/180;svg.append(node('line',{x1:300,y1:185,x2:300+160*Math.cos(rad),y2:185-160*Math.sin(rad),stroke:'#eef1f5'}));svg.append(node('text',{x:300+180*Math.cos(rad),y:187-180*Math.sin(rad),'text-anchor':'middle'},`${180-angle}°`));}
  if(scan){let path='',open=false;for(const p of [...scan].sort((a,b)=>a.angle-b.angle)){if(p.distance_mm===null){open=false;continue;}const rad=(180-p.angle)*Math.PI/180,r=Math.min(160,p.distance_mm/4000*160);path+=`${open?'L':'M'}${300+r*Math.cos(rad)},${185-r*Math.sin(rad)} `;open=true;}svg.append(node('path',{d:path,fill:'none',stroke:'#208f85','stroke-width':2}));}
  svg.append(node('circle',{cx:300,cy:185,r:5,fill:'#8ba764'}));container.replaceChildren(svg);
  if(!scan){const empty=document.createElement('div');empty.className='chart-empty';empty.textContent='Waiting for servo scan integration';container.append(empty);}
}
