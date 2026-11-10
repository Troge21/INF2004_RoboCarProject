import { TelemetryStore,hex,packetText } from './protocol.js';
import { MqttConnection,validateTopics } from './mqtt.js';
import { lineChart,sparkline,scanChart } from './charts.js';

const $=id=>document.getElementById(id);
const icons={
  car:'<path d="m5 12 3-7h8l3 7v7H5z"/><path d="M5 12h14M8 16h1m6 0h1M7 19v2m10-2v2"/>',
  grid:'<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
  wave:'<path d="M2 12h4l3-8 5 16 3-8h5"/>', radio:'<circle cx="12" cy="12" r="2"/><path d="M7 7a7 7 0 0 0 0 10m10-10a7 7 0 0 1 0 10M4 4a11 11 0 0 0 0 16M20 4a11 11 0 0 1 0 16"/>',
  chip:'<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m-4 6h4m12-6h4m-4 6h4"/>',
  plug:'<path d="M8 3v5m8-5v5M6 8h12v3a6 6 0 0 1-12 0zM12 17v5"/>',
  download:'<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
  pause:'<path d="M9 5v14m6-14v14"/>',play:'<path d="m8 4 12 8-12 8z"/>',
  speed:'<path d="M4 18a9 9 0 1 1 16 0M12 13l5-6"/><circle cx="12" cy="13" r="2"/>',
  distance:'<circle cx="5" cy="18" r="2"/><circle cx="19" cy="6" r="2"/><path d="M7 18h7a4 4 0 0 0 0-8h-4a4 4 0 0 1 0-8h7"/>',
  radar:'<path d="M12 3a9 9 0 1 0 9 9M12 7a5 5 0 1 0 5 5m-5 0 8-8"/><circle cx="12" cy="12" r="1"/>',
  mountain:'<path d="m2 20 7-14 5 8 3-5 5 11H2zM6.5 11l3 2 2-2"/>',
  check:'<path d="m5 12 4 4L19 6"/>',close:'<path d="m6 6 12 12M6 18 18 6"/>',
  info:'<circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10v.5"/>',
  shield:'<path d="m12 3 8 3v6c0 5-8 9-8 9s-8-4-8-9V6zM8 12l3 3 5-6"/>',
  barcode:'<path d="M3 4v16M6 4v16m4-16v16m3-16v16m3-16v16m4-16v16"/>',
  clock:'<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  packet:'<path d="m12 3 9 5-9 5-9-5zM3 8v9l9 5 9-5V8m-9 5v9"/>',
  stop:'<rect x="5" y="5" width="14" height="14" rx="2"/>'
};
const icon=name=>`<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${icons[name]||icons.info}</svg>`;
function fillIcons(root=document){root.querySelectorAll('[data-icon]').forEach(el=>el.innerHTML=icon(el.dataset.icon));}
function escape(text){return String(text??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function value(name,text){document.querySelectorAll(`[data-value="${name}"]`).forEach(el=>el.textContent=text);}
const format=(v,places=2)=>Number.isFinite(v)?v.toFixed(places):'—';
const metric=(name,label,unit,sub,iconName)=>`<article class="metric"><div class="metric-top">${icon(iconName)}<span>${label}</span><span class="metric-mark" title="${escape(sub)}">i</span></div><div class="metric-reading"><strong class="metric-value" data-value="${name}">—</strong><span class="metric-unit">${unit}</span></div><div class="metric-bottom" data-value="${name}-note">${sub}</div><div class="sparkline" id="spark-${name}"></div></article>`;
const card=(title,description,content,extra='',className='')=>`<section class="card ${className}"><div class="card-heading"><div><h2>${title}</h2><p>${description}</p></div>${extra}</div>${content}</section>`;
const chartCard=(title,description,id,legend,foot)=>card(title,description,`<div class="chart" id="${id}"></div><div class="chart-foot">${foot}</div>`,legend);
const speedLegend='<div class="legend"><span><i class="teal"></i>Left wheel</span><span><i class="purple"></i>Right wheel</span></div>';
const obstacleLegend='<div class="legend"><span><i class="teal"></i>Ultrasonic range</span></div>';
const speedFoot='<span>Measured wheel speed <strong>· m/s</strong></span><span>Latest <strong data-value="speed-pair">— / —</strong></span>';
const obstacleFoot='<span>Invalid readings appear as gaps</span><span data-value="range-valid">Waiting for data</span>';

const store=new TelemetryStore();
let tab='overview',paused=false,seconds=60,connection=null,status='disconnected',statusDetail='',packetView='decoded',lastOptions=null,lastConnectionStarted=null,hadConnection=false,reconnects=[];
const vehicleSvg=`<svg viewBox="0 0 280 190" role="img" aria-label="Top view of the robot with ultrasonic field of view"><defs><linearGradient id="range-fade" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#b4cb94" stop-opacity=".2"/><stop offset="1" stop-color="#b4cb94" stop-opacity="0"/></linearGradient></defs><path d="M140 90 76 12Q140-18 204 12Z" fill="url(#range-fade)"/><path d="M91 26Q140 4 189 26M105 43Q140 26 175 43M119 59Q140 49 161 59" fill="none" stroke="#c6d6b3" stroke-width="1" stroke-dasharray="3 4"/><path d="M140 12v156" stroke="#d8ddcf" stroke-dasharray="3 5"/><ellipse cx="140" cy="132" rx="42" ry="10" fill="#3039480a"/><rect x="106" y="81" width="13" height="26" rx="4" fill="#343d47"/><rect x="161" y="81" width="13" height="26" rx="4" fill="#343d47"/><rect x="106" y="116" width="13" height="24" rx="4" fill="#343d47"/><rect x="161" y="116" width="13" height="24" rx="4" fill="#343d47"/><rect x="117" y="72" width="46" height="78" rx="13" fill="#c8d2ba" stroke="#acba9b"/><path d="M122 83q18-12 36 0v19h-36Z" fill="#e9eee2"/><rect x="123" y="110" width="34" height="27" rx="5" fill="#323d46"/><rect x="128" y="115" width="24" height="17" rx="2" fill="#546355"/><path d="M133 119h14m-14 4h14m-14 4h8" stroke="#97b274" stroke-width="1.2"/><rect x="127" y="67" width="26" height="10" rx="3" fill="#667560"/><circle cx="133" cy="72" r="3" fill="#dce6d1"/><circle cx="147" cy="72" r="3" fill="#dce6d1"/><circle cx="140" cy="140" r="2" fill="#a2c66c"/><path d="M98 94H87m96 0h10" stroke="#c5cdbd"/><text x="74" y="97" font-size="8" fill="#a5af9c" font-family="inherit">L</text><text x="200" y="97" font-size="8" fill="#a5af9c" font-family="inherit">R</text></svg>`;
function overview(){
  return `<div class="metrics">${metric('speed','Average wheel speed','m/s','From encoder feedback','speed')}${metric('distance','Distance travelled','m','Current run','distance')}${metric('obstacle','Obstacle distance','mm','Latest valid range','radar')}${metric('hump','Highest hump estimate','mm','Maximum for this run','mountain')}</div>
  <div class="main-grid">${chartCard('Wheel velocity','Left and right encoder feedback','speed-chart',speedLegend,speedFoot)}${card('Vehicle status','Current operating state',`<div class="vehicle-visual">${vehicleSvg}<span class="state-pill" data-value="state">Unavailable</span></div><div class="vehicle-details"><div class="vehicle-detail"><span>Line sensors · L / R</span><span class="ir-dots" id="overview-ir"><i></i><i></i></span></div><div class="vehicle-detail"><span>Latest barcode</span><strong data-value="barcode">Unavailable</strong></div><div class="vehicle-detail"><span>Fault status</span><strong data-value="faults">Unavailable</strong></div></div><div class="command-actions"><button class="button button-white" data-command="STATUS" disabled aria-describedby="command-integration-note">${icon('info')}Request status</button><button class="button button-stop" data-command="STOP" disabled aria-describedby="command-integration-note">${icon('stop')}Stop</button></div><p class="command-note" id="command-integration-note" data-value="command-note">STATUS is read-only. STOP awaits controller integration.</p><div class="card-bottom-note" data-value="status-result">No STATUS reply received yet.</div>`,`<span class="card-label" data-value="vehicle-source">NO TELEMETRY</span>`,'vehicle-card')}</div>
  <div class="main-grid">${chartCard('Obstacle proximity','Distance ahead of the ultrasonic sensor','obstacle-chart',obstacleLegend,obstacleFoot)}${card('Activity feed','The latest events from your vehicle','<div class="events" id="events"></div><div class="card-bottom-note" data-value="events-note">Showing the latest 4 events</div>','<span class="activity-count">RECENT</span>')}</div>
  <div class="health-strip"><div class="health-intro"><span>${icon('shield')}</span><div><strong data-value="health-title">Awaiting connection</strong><small data-value="health-description">Connect your RoboCar to receive live measurements</small></div></div><div class="health-facts"><div><strong data-value="heartbeat-age">—</strong>Heartbeat age</div><div><strong data-value="interval">—</strong>Telemetry interval</div><div><strong data-value="packet-size">—</strong>Last payload</div><div><strong data-value="sequence">—</strong>Sequence</div></div></div>`;
}
function sensors(){
  return `<div class="metrics">${metric('speed','Average wheel speed','m/s','From encoder feedback','speed')}${metric('distance','Distance travelled','m','Current run','distance')}${metric('obstacle','Obstacle distance','mm','Latest valid range','radar')}${metric('hump','Highest hump estimate','mm','Maximum for this run','mountain')}</div>
  <div class="two-columns">${chartCard('Wheel velocity','Compare the two encoder speed estimates','speed-chart',speedLegend,speedFoot)}${chartCard('Obstacle proximity','Live ultrasonic range measurements','obstacle-chart',obstacleLegend,obstacleFoot)}</div>
  <div class="two-columns">${chartCard('Vehicle pitch','Live IMU angle; front-up is positive','pitch-chart','<div class="legend"><span><i class="teal"></i>Pitch</span></div>','<span>Sensor angle <strong>· degrees</strong></span><span>Calibrating or stale samples appear as gaps</span>')}${card('IMU & height status','Real sensor status and estimator assumptions','<div class="vehicle-details"><div class="vehicle-detail"><span>Pitch</span><strong data-value="pitch">Unavailable</strong></div><div class="vehicle-detail"><span>IMU state</span><strong data-value="imu-state">Unavailable</strong></div><div class="vehicle-detail"><span>Last accepted hump</span><strong data-value="last-hump">Unavailable</strong></div><div class="vehicle-detail"><span>Run maximum estimate</span><strong data-value="run-hump">Unavailable</strong></div></div><div class="card-bottom-note" data-value="imu-note">Waiting for IMU telemetry</div>')}</div>
  <div class="two-columns">${chartCard('Terrain & hump height','Current estimate and highest hump of the run','hump-chart','<div class="legend"><span><i class="teal"></i>Current</span><span><i class="purple"></i>Run maximum</span></div>','<span>Estimated height <strong>· mm</strong></span><span data-value="height-source">Waiting for IMU telemetry</span>')}${chartCard('Distance travelled','Cumulative encoder estimate for the current run','distance-chart','<div class="legend"><span><i class="teal"></i>Distance</span></div>','<span>Measured distance <strong>· m</strong></span><span>Reset by the vehicle controller</span>')}</div>
  <div class="two-columns">${card('Line & navigation','Left/right line readings and dedicated barcode input','<div class="sensor-status" id="sensor-status"></div><div class="barcode-row">'+icon('barcode')+'Latest barcode<strong data-value="barcode">Unavailable</strong></div><div class="card-bottom-note">Left/right: filled = black line · empty = background · ? = unavailable</div>')}${card('Obstacle scan profile','Latest servo sweep · angle vs measured range','<div class="chart scan-chart" id="scan-chart"></div><div class="chart-foot"><span>0°–180° · outer ring = 4000 mm</span><span>Missing echoes are gaps</span></div>','<span class="card-label">LATEST SCAN</span>')}</div>`;
}
function communications(){
  return `<div class="metrics">${metric('broker','Broker connection','','MQTT WebSocket session','radio')}${metric('messages','Received messages','','Includes rejected messages','packet')}${metric('interval-metric','Telemetry interval','ms','Target ≤ 2000 ms','clock')}${metric('payload','Latest payload','bytes','Application payload only','chip')}</div>
  <div class="two-columns">${chartCard('Telemetry delivery','Time between accepted telemetry messages','interval-chart','<div class="legend"><span><i class="teal"></i>Receive interval</span></div>','<span>Receiver timing <strong>· ms</strong></span><span>Not a round-trip latency measurement</span>')}${card('Communication checks','Live observations against your proposed targets','<div class="requirements" id="requirements"></div>','<span class="card-label" data-value="checks-source">OBSERVATIONS</span>')}</div>
  <div class="two-columns">${card('Message inspector','Most recent MQTT message','<div class="packet-toolbar"><button class="seg-button active" data-packet-view="decoded">Decoded</button><button class="seg-button" data-packet-view="hex">Raw bytes</button></div><pre id="packet-content">Waiting for a message…</pre><div class="card-bottom-note" data-value="packet-topic">No topic yet</div>')}${card('Session diagnostics','Bounded browser history · export the current session','<div class="stats-row"><div><strong data-value="accepted">0</strong>Telemetry accepted</div><div><strong data-value="rejected">0</strong>Messages rejected</div><div><strong data-value="gaps">0</strong>Sequence gaps</div></div><div class="requirements"><div class="requirement"><div>Received payload data<small>Excludes MQTT / TCP / WiFi overhead</small></div><span data-value="total-bytes">0 B</span></div><div class="requirement"><div>Last reconnection<small>Browser connection through subscription readiness</small></div><span data-value="reconnect-time">Not measured</span></div><div class="requirement"><div>Active decoder<small>Packed v3 adds pitch and hump estimates</small></div><span data-value="decoder">—</span></div></div>')}</div>
  <div class="two-columns">${card('Change requirement evidence','Record measured firmware results for the final submission','<div class="measurement-grid"><div class="measurement">Runtime RAM · before<strong>Not measured</strong><small>Measure baseline heap and stack use</small></div><div class="measurement">Runtime RAM · after<strong>Not measured</strong><small>Measure packed heap and stack use</small></div><div class="measurement">Latest telemetry payload<strong data-value="observed-payload">Not received</strong><small>Size of an accepted MQTT telemetry message</small></div><div class="measurement">Average telemetry payload<strong data-value="average-payload">Not received</strong><small>Average across the collected telemetry history</small></div></div><div class="card-bottom-note">Payload sizes come from received telemetry. Runtime RAM comparisons require on-board heap and stack measurements.</div>')}${card('Supervisory commands','Read-only communication status; STOP awaits controller integration','<div class="command-actions"><button class="button button-white" data-command="STATUS" disabled aria-describedby="command-integration-note">'+icon('info')+'Request status</button><button class="button button-stop" data-command="STOP" disabled aria-describedby="command-integration-note">'+icon('stop')+'Stop</button></div><p class="command-note" id="command-integration-note" data-value="command-note">STATUS is read-only. STOP awaits controller integration.</p><div class="card-bottom-note" data-value="status-result">No STATUS reply received yet.</div><div class="events" id="events"></div>')}</div>
  ${card('Recent messages','Latest six deliveries · rejected and retained messages remain visible','<div class="table-wrap"><table class="data-table"><thead><tr><th>RECEIVED</th><th>TOPIC</th><th>SIZE</th><th>RESULT</th></tr></thead><tbody id="packet-table"></tbody></table></div>')}`;
}

function switchTab(){
  tab=['overview','sensors','communications'].includes(location.hash.slice(1))?location.hash.slice(1):'overview';
  const titles={overview:['Mission overview','Live vehicle status and the latest measurements.','Overview'],sensors:['Sensors & motion','Inspect movement, terrain and the world around your robot.','Sensors & motion'],communications:['Communications','Inspect delivery, connectivity and the messages in between.','Communications']};
  $('page-title').innerHTML=escape(titles[tab][0])+'<span class="heading-dot">.</span>';$('page-description').textContent=titles[tab][1];$('breadcrumb').textContent=titles[tab][2];
  document.querySelectorAll('[data-tab]').forEach(a=>{a.classList.toggle('active',a.dataset.tab===tab);if(a.dataset.tab===tab)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current');});
  $('dashboard-view').innerHTML=tab==='sensors'?sensors():tab==='communications'?communications():overview();
  document.querySelectorAll('[data-packet-view]').forEach(b=>b.addEventListener('click',()=>{packetView=b.dataset.packetView;updateInspector();}));
  update(true);
}
function toast(message){$('toast').textContent=message;$('toast').classList.add('visible');clearTimeout(toast.timer);toast.timer=setTimeout(()=>$('toast').classList.remove('visible'),4500);}
function age(timestamp,now=Date.now()){return timestamp===null?'Not received':`${((now-timestamp)/1000).toFixed(1)} s`;}
function disconnectBroker(){
  if(!connection)return;
  connection.disconnect();connection=null;status='disconnected';statusDetail='Disconnected from the broker.';
  lastConnectionStarted=null;hadConnection=false;
  store.event('Broker disconnected','Live data collection stopped');
  closeDialog();update(true);toast('Disconnected from the broker');
}
function updateHeader(now){
  $('clock').textContent=new Date(now).toLocaleString([], {hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false});
  const connected=status==='connected',fresh=store.lastTelemetry!==null&&now-store.lastTelemetry<=2000;
  $('mode-badge').className='mode-badge'+(connected?' live':' disconnected');$('mode-badge').innerHTML='<i></i>'+escape(connected?'Live MQTT':status==='connecting'?'Connecting':status==='reconnecting'?'Reconnecting':'Not connected');
  $('connect-button').innerHTML=icon('plug')+(connection?'Connection':'Connect robot');
  $('run-label').textContent=connected?'Live session':'Awaiting connection';
  $('source-notice').classList.toggle('live-notice',connected);
  const notice=connected?(store.lastTelemetry===null?'Waiting for telemetry from your RoboCar.':fresh?'Showing measurements received from your RoboCar.':'Telemetry is stale. Waiting for new measurements.'):statusDetail||'Connect to your broker to receive live measurements.';
  $('source-notice').querySelector('p').innerHTML=(connected?'<strong>Live MQTT connection.</strong> ':'<strong>Connect your RoboCar.</strong> ')+escape(notice);
  $('notice-connect').innerHTML='Connection settings<span aria-hidden="true"> ↗</span>';
  $('feed-title').textContent=connected?(fresh?'Live telemetry':store.lastTelemetry===null?'Waiting for first telemetry':'Telemetry stale'):status==='connecting'?'Connecting to broker':status==='reconnecting'?'Reconnecting to broker':'Not connected';
  $('live-dot').classList.toggle('offline',!connected||!fresh);
  $('last-update').textContent=paused?'Display paused · still collecting':store.lastTelemetry===null?'No telemetry received':`Last update ${age(store.lastTelemetry,now)} ago`;
  $('footer-source').textContent=connected?`${store.messageCount} messages · ${store.format}`:'Awaiting RoboCar connection';
  $('disconnect-button').disabled=!connection;
  $('export-button').disabled=store.history.length===0;
  $('vehicle-connection-dot').classList.toggle('offline',!connected||!fresh);
  document.querySelector('.run-label').classList.toggle('offline',!connected);
  $('pause-button').innerHTML=icon(paused?'play':'pause');$('pause-button').setAttribute('aria-label',paused?'Resume chart display':'Pause chart display');$('pause-button').title=paused?'Resume chart display':'Pause chart display';
}
function update(force=false){
  const now=Date.now();updateHeader(now);updateCommands(now);if(paused&&!force)return;
  const sample=store.sample,current=store.currentSample(now,status==='connected'),stale=current===null;
  const mean=current&&Number.isFinite(current.left_mps)&&Number.isFinite(current.right_mps)?(Math.abs(current.left_mps)+Math.abs(current.right_mps))/2:null;
  value('speed',format(mean));value('distance',format(current?.distance_m));value('obstacle',format(current?.obstacle_mm,0));value('hump',format(current?.max_hump_mm,0));
  value('speed-note',mean===null?'Encoder feed unavailable':'Left / right encoder average');value('distance-note',current?.distance_m===null||!current?'Waiting for encoder feed':'Current run · encoder estimate');
  value('obstacle-note',stale?'Waiting for fresh telemetry':sample?.obstacle_valid?'Measurement valid':sample?'No valid echo':'Waiting for sensor data');value('hump-note',!current?.imu_state?'Waiting for IMU feed':current.max_hump_mm==null?'No accepted hump received':current.imu_speed_source==='nominal'?'Nominal speed · accuracy unverified':'Maximum estimate for this run');
  value('pitch',current?.pitch_deg==null?'Unavailable':`${format(current.pitch_deg,1)}°`);value('imu-state',current?.imu_state??'Unavailable');
  value('last-hump',!current?.imu_state?'Unavailable':current.hump_mm==null?'No accepted hump received':`${current.hump_mm} mm`);value('run-hump',!current?.imu_state?'Unavailable':current.max_hump_mm==null?'No accepted hump received':`${current.max_hump_mm} mm`);
  const imuNote=!current?.imu_state?'Waiting for fresh IMU telemetry':current.imu_speed_source==='nominal'?'Nominal speed: 150 mm/s. Height accuracy is unverified; encoder speed is not connected.':'External speed supplied. Validate estimated height on the course.';
  value('imu-note',imuNote);value('height-source',imuNote);
  value('state',current?.state??(stale?'Telemetry unavailable':'State unavailable'));value('barcode',current?.barcode??'Unavailable');
  value('faults',current?.faults===null||!current?'Unavailable':current.faults.length?current.faults.join(', '):'None reported');
  value('vehicle-source',sample===null?'NO TELEMETRY':stale?'STALE DATA':'LIVE DATA');
  document.querySelector('.state-pill')?.classList.toggle('unavailable',stale||!current?.state);
  value('speed-pair',`${format(current?.left_mps)} / ${format(current?.right_mps)}`);
  value('range-valid',stale?'Stale / unavailable':sample?.obstacle_valid?'Latest measurement valid':'No valid measurement');
  value('health-title',status==='connected'?(stale?'Waiting for fresh telemetry':'Receiving live telemetry'):'Awaiting connection');
  value('health-description','Robot presence is inferred from fresh messages, not broker connection alone.');
  value('heartbeat-age',age(store.lastHeartbeat,now));value('interval',store.lastInterval===null?'—':`${Math.round(store.lastInterval)} ms`);value('packet-size',`${store.lastPayloadBytes||'—'} B`);value('sequence',sample?.sequence??'—');
  value('broker',status==='connected'?'Connected':status==='connecting'?'Connecting':status==='reconnecting'?'Reconnecting':'Offline');value('messages',store.messageCount.toLocaleString());value('interval-metric',store.lastInterval===null?'—':Math.round(store.lastInterval));value('payload',store.lastPayloadBytes||'—');
  value('broker-note',status==='connected'?'All three subscriptions ready':'Awaiting MQTT subscriptions');
  value('messages-note','Includes rejected messages');value('payload-note',store.format);
  document.querySelector('[data-value="broker"]')?.classList.add('small');
  value('accepted',store.telemetryCount.toLocaleString());value('rejected',store.rejected.toLocaleString());value('gaps',store.sequenceGaps.toLocaleString());value('total-bytes',`${store.totalBytes.toLocaleString()} B`);value('reconnect-time',reconnects.length?`${(reconnects.at(-1)/1000).toFixed(2)} s`:'Not measured');value('decoder',store.format);value('checks-source','OBSERVATIONS');
  value('observed-payload',store.lastPayloadBytes?`${store.lastPayloadBytes} bytes`:'Not received');
  value('average-payload',store.history.length?`${Math.round(store.history.reduce((total,row)=>total+row.bytes,0)/store.history.length)} bytes`:'Not received');
  const lineLabels=['Left','Right'];
  const lineState=i=>current?.ir?.[i]===1?'Black line':current?.ir?.[i]===0?'Background':'Unavailable';
  const ir=$('overview-ir');if(ir){ir.innerHTML=[1,2].map((i,j)=>`<i class="${current?.ir?.[i]===1?'on':current?.ir?.[i]===0?'':'unknown'}" title="${lineLabels[j]}: ${lineState(i)}" aria-label="${lineLabels[j]}: ${lineState(i)}">${lineState(i)==='Unavailable'?'?':''}</i>`).join('');ir.setAttribute('aria-label',`Line sensors: left ${lineState(1)}, right ${lineState(2)}. Barcode IR is a separate input.`);}
  if($('sensor-status'))$('sensor-status').innerHTML='<div class="sensor-tile"><span class="sensor-indicator reserved" aria-hidden="true">B</span><strong>Barcode IR</strong><small>Dedicated barcode input</small></div>'+[1,2].map((i,j)=>`<div class="sensor-tile"><span class="sensor-indicator ${current?.ir?.[i]===1?'on':current?.ir?.[i]===0?'':'unknown'}" aria-hidden="true">${lineState(i)==='Unavailable'?'?':''}</span><strong>${lineLabels[j]} IR</strong><small>${lineState(i)}</small></div>`).join('');

  for(const [name,key,color]of [['speed','left_mps','#208f85'],['distance','distance_m','#8b78df'],['obstacle','obstacle_mm','#208f85'],['hump','max_hump_mm','#acbc89']])sparkline($(`spark-${name}`),store.history.map(d=>d[key]),color);
  const opts={seconds,now};
  lineChart($('speed-chart'),store.history,[{key:'left_mps',label:'Left wheel',color:'#208f85'},{key:'right_mps',label:'Right wheel',color:'#8b78df'}],{...opts,unit:'m/s',precision:2,empty:'Wheel speed requires the encoder telemetry feed.'});
  lineChart($('obstacle-chart'),store.history,[{key:'obstacle_mm',label:'Distance',color:'#208f85'}],{...opts,unit:'mm',precision:0,empty:'Connect the robot to receive ultrasonic measurements.'});
  lineChart($('pitch-chart'),store.history,[{key:'pitch_deg',label:'Pitch',color:'#208f85'}],{...opts,unit:'°',precision:1,empty:'Keep the car still during calibration, then gently tilt it to observe pitch.'});
  lineChart($('hump-chart'),store.history,[{key:'hump_mm',label:'Current height',color:'#208f85'},{key:'max_hump_mm',label:'Run maximum',color:'#8b78df'}],{...opts,unit:'mm',precision:0,empty:'Hump estimates require the IMU telemetry feed.'});
  lineChart($('distance-chart'),store.history,[{key:'distance_m',label:'Distance',color:'#208f85'}],{...opts,unit:'m',precision:1,empty:'Distance requires the encoder telemetry feed.'});
  lineChart($('interval-chart'),store.history,[{key:'interval',label:'Receive interval',color:'#208f85'}],{...opts,unit:'ms',precision:0,min:0,empty:'Receive at least two fresh telemetry messages.'});
  scanChart($('scan-chart'),current?.scan);
  if($('events'))$('events').innerHTML=store.events.slice(0,tab==='communications'?3:4).map(e=>`<div class="event-row"><span class="event-symbol ${escape(e.tone)}">${icon(e.tone==='success'?'check':e.tone==='warning'?'info':'wave')}</span><div class="event-copy"><strong>${escape(e.title)}</strong><small>${escape(e.detail)}</small></div><time>${new Date(e.at).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false})}</time></div>`).join('')||'<div class="empty-events">Your vehicle events will appear here.<br>Connect to receive telemetry.</div>';
  if($('requirements')){
    const interval=store.lastInterval!==null&&!stale&&store.lastInterval<=2000,hb=store.lastHeartbeat!==null&&now-store.lastHeartbeat<=5000,rt=reconnects.at(-1);
    $('requirements').innerHTML=[['Telemetry update','Proposed target: at most 2 seconds',stale?'No fresh data':store.lastInterval===null?'Waiting':`${Math.round(store.lastInterval)} ms`,interval],['Heartbeat','Proposed target: at most 5 seconds',store.lastHeartbeat===null?'Not received':age(store.lastHeartbeat,now),hb],['Reconnection trials','Team target: ≤ 15 s in 4 of 5 trials',rt===undefined?'Not tested':`${reconnects.length} observation(s)`,false]].map(([name,note,result,good])=>`<div class="requirement"><div>${name}<small>${note}</small></div><span class="${good?'good':''}">${escape(result)}</span></div>`).join('');
  }
  updateInspector();
}
function updateCommands(now) {
  store.expireStatus(now);
  const cooling=store.statusLastSentAt!==null&&now-store.statusLastSentAt<250;
  document.querySelectorAll('[data-command="STATUS"]').forEach(button=>{
    button.disabled=status!=='connected'||Boolean(store.pendingStatus)||cooling;
    button.title='Read-only status request; does not move the car';
  });
  value('command-note',status==='connected'?store.statusNotice:'Connect to the broker to request read-only STATUS. STOP awaits controller integration.');
  const reply=store.statusReply;
  value('status-result',reply?`Last reply: ${new Date(reply.received_at).toLocaleTimeString()} | Pico uptime ${(reply.uptime_ms/1000).toFixed(1)} s | ${reply.test_build?'Stationary test build':'Normal build'} | ${reply.sample_age_ms===null?'No fresh telemetry sample':`Sample age ${reply.sample_age_ms} ms`}`:'No STATUS reply received yet.');
}
function requestStatus(){
  if(status!=='connected'||!connection?.ready)return;
  try {
    const nonce=new Uint32Array(1);
    do { crypto.getRandomValues(nonce); } while(!nonce[0]||nonce[0]===store.lastStatusId);
    const bytes=store.beginStatus(nonce[0]);
    try { connection.publish(lastOptions.commandTopic,bytes); }
    catch(error){store.cancelStatus(`STATUS send failed: ${error.message}`);throw error;}
  } catch(error){toast(error.message);}
  update(true);
}
function updateInspector(){
  if(!$('packet-content'))return;const p=store.packets[0];
  $('packet-content').textContent=p&&packetView==='hex'?hex(p.bytes):packetText(p);
  value('packet-topic',p?`${p.topic} · ${p.bytes.length} bytes · ${p.detail}`:'No topic yet');
  document.querySelectorAll('[data-packet-view]').forEach(b=>b.classList.toggle('active',b.dataset.packetView===packetView));
  if($('packet-table'))$('packet-table').innerHTML=store.packets.slice(0,6).map(p=>`<tr><td>${new Date(p.at).toLocaleTimeString([], {hour12:false})}</td><td title="${escape(p.topic)}">${escape(p.topic)}</td><td>${p.bytes.length} B</td><td class="${p.accepted?'accepted':'rejected'}" title="${escape(p.detail)}">${p.accepted?'Accepted':'Rejected'}</td></tr>`).join('')||'<tr><td colspan="4">No messages received yet.</td></tr>';
}
function openDialog(){
  $('connection-error').textContent='';if(lastOptions)for(const key of ['url','telemetryTopic','heartbeatTopic','statusTopic','commandTopic'])$('connection-form').elements[key].value=lastOptions[key];$('connection-dialog').showModal();
}
function closeDialog(){ $('connection-form').elements.password.value='';$('connection-form').elements.username.value='';$('connection-dialog').close(); }
function connectLive(event){
  event.preventDefault();const form=$('connection-form'),options=Object.fromEntries(new FormData(form));
  try{
    const url=new URL(options.url);
    if(location.protocol==='https:'&&url.protocol==='ws:')throw new Error('For ws:// on your local network, run this dashboard locally. This HTTPS page requires a reachable wss:// broker.');
    const next=new MqttConnection(options,{
      onStatus:(nextStatus,detail)=>{
        if(connection!==next)return;
        if(nextStatus!=='connected')store.cancelStatus();
        const previous=status;status=nextStatus;statusDetail=detail;
        if(nextStatus==='connecting'){if(lastConnectionStarted===null)lastConnectionStarted=Date.now();store.lastSequence=null;}
        if(nextStatus==='reconnecting'&&lastConnectionStarted===null)lastConnectionStarted=Date.now();
        if(nextStatus==='connected'){if(hadConnection){store.event('Connection restored','All three MQTT subscriptions are ready','success');if(lastConnectionStarted!==null)reconnects.push(Date.now()-lastConnectionStarted);}else store.event('Broker connected','All three MQTT subscriptions are ready','success');hadConnection=true;lastConnectionStarted=null;}
        else if(nextStatus==='error'||(nextStatus==='reconnecting'&&previous!=='reconnecting'))store.event('Connection update',detail,'warning');
        update(true);
      },
      onMessage:msg=>{if(connection!==next)return;const kind=msg.topic===options.heartbeatTopic?'heartbeat':msg.topic===options.telemetryTopic?'telemetry':msg.topic===options.statusTopic?'status':null;if(kind)store.ingest(msg.payload,kind,Date.now(),msg.retained,msg.topic);}
    });
    // Validate settings before replacing the current connection.
    if(!['ws:','wss:'].includes(url.protocol)||url.username||url.password)throw new Error('Enter a ws:// or wss:// broker URL without embedded credentials.');
    if(options.password&&!options.username)throw new Error('Enter a username when using a password.');
    validateTopics(options);
    if(connection)connection.disconnect();
    paused=false;store.reset();reconnects=[];hadConnection=false;lastConnectionStarted=Date.now();connection=next;
    lastOptions={url:options.url,telemetryTopic:options.telemetryTopic,heartbeatTopic:options.heartbeatTopic,statusTopic:options.statusTopic,commandTopic:options.commandTopic};
    next.connect();closeDialog();update(true);toast('Connecting to your RoboCar broker');
  }catch(error){$('connection-error').textContent=error.message;}
}
function exportData(){
  if(!store.history.length){toast('No telemetry to export yet.');return;}
  const columns=['received_at','source','sequence','state','left_mps','right_mps','distance_m','obstacle_mm','obstacle_valid','hump_mm','max_hump_mm','ir_left','ir_right','pitch_deg','imu_state','imu_flags','imu_speed_source'];
  const csv=columns.join(',')+'\r\n'+store.history.map(d=>[new Date(d.at).toISOString(),'mqtt',d.sequence,d.state,d.left_mps,d.right_mps,d.distance_m,d.obstacle_mm,d.obstacle_valid,d.hump_mm,d.max_hump_mm,d.ir?.[1],d.ir?.[2],d.pitch_deg,d.imu_state,d.imu_flags,d.imu_speed_source].map(v=>v??'').join(',')).join('\r\n');
  const url=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8;'})),link=document.createElement('a');link.href=url;link.download=`robocar-mqtt-${new Date().toISOString().replace(/[:.]/g,'-')}.csv`;link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);toast(`Exported ${store.history.length} received telemetry samples`);
}
document.addEventListener('click',event=>{const button=event.target.closest('[data-command="STATUS"]');if(button&&!button.disabled)requestStatus();});
fillIcons();switchTab();
window.addEventListener('hashchange',switchTab);
$('time-range').addEventListener('change',event=>{seconds=Number(event.target.value);update(true);});
$('pause-button').addEventListener('click',()=>{paused=!paused;update(true);});
$('connect-button').addEventListener('click',openDialog);$('notice-connect').addEventListener('click',openDialog);
$('close-dialog').addEventListener('click',closeDialog);$('disconnect-button').addEventListener('click',disconnectBroker);
$('connection-dialog').addEventListener('cancel',()=>{ $('connection-form').elements.password.value='';$('connection-form').elements.username.value=''; });
$('connection-form').addEventListener('submit',connectLive);$('export-button').addEventListener('click',exportData);
setInterval(()=>update(),500);
window.addEventListener('beforeunload',()=>connection?.disconnect());
