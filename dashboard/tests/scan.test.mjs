import assert from 'node:assert/strict';
import test from 'node:test';
import {renderObstacleRadar,renderScanReadings} from '../dist/charts.js';

function render(points){
  const directions={}, angles={}, button={};
  const more={querySelector:()=>angles};
  const elements={
    '.scan-directions':directions, '.scan-more':more, '[data-export="scan"]':button
  };
  renderScanReadings({querySelector:selector=>elements[selector]},points);
  return {html:directions.innerHTML,more,angles,button};
}

test('scan readings use bearings and distinguish close, far and missing echoes',()=>{
  const {html,more,button}=render([
    {angle:30,distance_mm:null},
    {angle:90,distance_mm:250},
    {angle:150,distance_mm:251}
  ]);
  const [at150,at90,at30]=html.split(/<div class="scan-reading \w+">/).slice(1);
  assert.match(at150,/150°<\/dt>[\s\S]*251 <small>mm<\/small>[\s\S]*Beyond 250 mm/);
  assert.match(at90,/90°<\/dt>[\s\S]*250 <small>mm<\/small>[\s\S]*Close obstacle/);
  assert.match(at30,/30°<\/dt>[\s\S]*No echo/);
  assert.equal(more.hidden,true);
  assert.equal(button.disabled,false);
});

test('empty and wider scans retain their available readings and export state',()=>{
  const empty=render();
  assert.doesNotMatch(empty.html,/No echo|Close obstacle|Beyond 250 mm/);
  assert.equal(empty.button.disabled,true);
  const wider=render([{angle:60,distance_mm:400},{angle:120,distance_mm:null}]);
  assert.match(wider.html,/Not scanned/);
  assert.equal(wider.more.hidden,false);
  assert.match(wider.angles.innerHTML,/120°<\/dt><dd>No echo[\s\S]*60°<\/dt><dd>400 mm/);
  assert.equal(wider.button.disabled,false);
});

test('radar displays five degree guides and plots off-guide echoes at their own bearings',()=>{
  const previousDocument=globalThis.document;
  globalThis.document={createElementNS:(_,tag)=>({
    tag,attributes:{},children:[],
    setAttribute(key,value){this.attributes[key]=value;},
    append(...children){this.children.push(...children);}
  })};
  try{
    const container={replaceChildren(svg){this.svg=svg;}};
    const profile=[
      {angle:30,distance_mm:420},
      {angle:47,distance_mm:310},
      {angle:90,distance_mm:134},
      {angle:120,distance_mm:null},
      {angle:150,distance_mm:75}
    ];
    renderObstacleRadar(container,profile);
    const svg=container.svg;
    const guides=svg.children.filter(el=>el.tag==='text'&&el.textContent.endsWith('°'));
    assert.deepEqual(guides.map(el=>el.textContent),['150°','120°','90°','60°','30°']);
    const dots=svg.children.filter(el=>el.tag==='circle');
    assert.deepEqual(dots.map(dot=>dot.children[0].textContent),[
      '30° · 420 mm','47° · 310 mm','90° · 134 mm','150° · 75 mm'
    ]);
    const {x1,y1}=svg.children.find(el=>el.tag==='line').attributes;
    const {cx,cy}=dots[1].attributes;
    const plottedAngle=Math.atan2(y1-cy,cx-x1)*180/Math.PI;
    assert.ok(Math.abs(plottedAngle-47)<0.001);
    assert.ok(dots[0].attributes.cx>dots[2].attributes.cx);
    assert.ok(dots[3].attributes.cx<dots[2].attributes.cx);
    assert.ok(svg.children.some(el=>el.tag==='text'&&el.textContent==='500 mm'));
    renderObstacleRadar(container,profile);
    assert.equal(container.svg,svg);
    renderObstacleRadar(container);
    assert.equal(container.svg.children.filter(el=>el.tag==='circle').length,0);
  }finally{
    globalThis.document=previousDocument;
  }
});
