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
const scanSummaryAngles=[150,90,30];
const radarGuideAngles=[150,120,90,60,30];
const closeRangeMm=250; // Matches S5_OBSTACLE_MM on the Pico.
const radarRangesMm=[250,500,1000,2000,4000];
const renderedRadarProfiles=new WeakMap();

export function renderObstacleRadar(container,points){
  if(!container)return;
  const profile=points??null;
  if(renderedRadarProfiles.get(container)===profile)return;

  const echoes=(profile??[]).filter(point=>Number.isFinite(point.distance_mm));
  const furthest=Math.max(closeRangeMm,...echoes.map(point=>point.distance_mm));
  const range=radarRangesMm.find(max=>max>=furthest)??furthest;
  const originX=200,originY=174,radius=146;
  const at=(angle,r)=>{
    const radians=angle*Math.PI/180;
    return {x:originX+r*Math.cos(radians),y:originY-r*Math.sin(radians)};
  };
  const arc=r=>`M ${originX-r} ${originY} A ${r} ${r} 0 0 1 ${originX+r} ${originY}`;
  const svg=node('svg',{viewBox:'0 0 400 211',role:'img',class:'obstacle-radar',
    'aria-label':`Ultrasonic scan viewed from behind the car. ${echoes.length} measured echoes. Range up to ${range} millimetres.`});
  svg.append(node('path',{class:'radar-field',d:`${arc(radius)} Z`}));
  for(const fraction of [.25,.5,.75,1]){
    svg.append(node('path',{class:'radar-ring',d:arc(radius*fraction)}));
  }
  for(const angle of radarGuideAngles){
    const edge=at(angle,radius),caption=at(angle,radius+16);
    svg.append(node('line',{class:'radar-ray',x1:originX,y1:originY,x2:edge.x,y2:edge.y}));
    svg.append(node('text',{class:'radar-label',x:caption.x,y:caption.y+4,'text-anchor':'middle'},`${angle}°`));
  }
  for(const fraction of [.5,1]){
    svg.append(node('text',{class:'radar-scale',x:originX+8,y:originY-radius*fraction+13},`${range*fraction} mm`));
  }
  svg.append(node('path',{class:'radar-car',d:`M ${originX} ${originY+2} l 6 10 h -12 Z`}));
  svg.append(node('text',{class:'radar-label',x:originX,y:originY+29,'text-anchor':'middle'},'Car'));

  for(const {angle,distance_mm:mm} of echoes){
    // Use the measured bearing directly, independently of the guides above.
    const {x,y}=at(angle,radius*mm/range);
    const dot=node('circle',{class:`radar-dot ${mm<=closeRangeMm?'near':'far'}`,cx:x,cy:y,r:4.6});
    dot.append(node('title',{},`${angle}° · ${mm} mm`));
    svg.append(dot);
  }
  container.replaceChildren(svg);
  renderedRadarProfiles.set(container,profile);
}

export function renderScanReadings(panel,points){
  if(!panel)return;
  const readings=new Map(points?.map(({angle,distance_mm})=>[angle,distance_mm]));
  panel.querySelector('.scan-directions').innerHTML=scanSummaryAngles.map(angle=>{
    const mm=readings.get(angle),measured=Number.isFinite(mm);
    readings.delete(angle);
    const tone=measured?(mm<=closeRangeMm?'near':'far'):'unknown';
    const status=measured?(tone==='near'?'Close obstacle':`Beyond ${closeRangeMm} mm`):
      (mm===null?'No echo':points?'Not scanned':'');
    return `<div class="scan-reading ${tone}">
      <dt>${angle}°</dt>
      <dd class="scan-distance">${measured?`${mm} <small>mm</small>`:'—'}</dd>
      <dd class="scan-status">${status}</dd>
    </div>`;
  }).join('');

  const more=panel.querySelector('.scan-more');
  more.hidden=readings.size===0;
  more.querySelector('dl').innerHTML=[...readings].sort((a,b)=>b[0]-a[0])
    .map(([angle,mm])=>`<div><dt>${angle}°</dt><dd>${mm===null?'No echo':`${mm} mm`}</dd></div>`).join('');
  panel.querySelector('[data-export="scan"]').disabled=!points;
}
