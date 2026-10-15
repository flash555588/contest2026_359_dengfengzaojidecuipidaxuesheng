const {chromium}=require('C:/Users/11937/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({channel:'msedge',headless:true});
 const page=await browser.newPage({viewport:{width:1120,height:900}}),errors=[];
 page.on('pageerror',e=>errors.push(e.message));page.on('response',r=>{if(r.status()>=400)errors.push(r.url()+': '+r.status())});
 await page.goto('http://127.0.0.1:18766/home-pixel.html',{waitUntil:'networkidle'});
 const v=await(await page.request.get('http://127.0.0.1:18766/home-pixel/validation.json')).json();
 assert.equal(v.desktop.style,'original-pixel');assert.equal(v.desktop.body_cell,1);assert.equal(v.desktop.eye_cell,1);assert.equal(v.desktop.grout,null);
 assert.equal(v.desktop.diameter,96);assert.deepEqual(v.desktop.center,[76,65]);assert.equal(v.desktop.nominal_hidden_bottom,24);
 assert.equal(v.reference_equivalence.compared_frames,384);assert.equal(v.expressions,32);assert.equal(v.flashed,false);
 assert.equal(v.desktop.viewpoint_override,true);assert.equal(v.desktop.face_direction,'right');assert.equal(v.desktop.engine_pose_mutation,false);assert.equal(v.reference_equivalence.black_geometry_frames,384);
 assert.equal(v.reference_equivalence.spin_animation_and_settle,true);
 assert.equal(v.screens,32);assert.equal(v.animations,24);assert.equal(v.motion_checks.length,12);
 assert.ok(v.motion_checks.every(row=>row.moving&&row.unique_card_frames>1));
 for(const theme of ['light','dark']){
  await page.locator(`[data-theme="${theme}"]`).click();
  for(const state of ['idle','thinking','replying','busy','done','error','stopped','listening']){
   await page.selectOption('#state',state);await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth>0));
   assert.equal(await page.locator('#home').evaluate(i=>i.naturalWidth),1024);
   assert.deepEqual(await page.locator('.card').evaluateAll(els=>els.map(i=>[i.naturalWidth,i.naturalHeight])),[[285,178],[285,178]]);
   if(['idle','thinking','replying'].includes(state)){
    assert.ok(await page.locator('#home').evaluate(i=>i.src.endsWith('-home.gif')));
    assert.ok(await page.locator('#alternate').evaluate(i=>i.src.endsWith('-card.gif')));
    assert.ok((await page.locator('#alternate').getAttribute('src')).includes(`-${state}-card.gif`));
   }
  }
 }
 await page.locator('[data-theme="light"]').click();await page.selectOption('#state','thinking');
 await page.waitForFunction(()=>[...document.images].every(i=>i.complete&&i.naturalWidth>0));
 await page.locator('#alternate').scrollIntoViewIfNeeded();
 // Sample the actual browser GIF; existence alone would miss a still preview.
 const blackSignature=()=>page.locator('#alternate').screenshot({animations:'allow'});
 const first=await blackSignature();let changed=false;
 for(let i=0;i<6;i++){await page.waitForTimeout(250);if(!(await blackSignature()).equals(first)){changed=true;break;}}
 assert.ok(changed,'black comparison GIF must visibly animate in the browser');
 await page.screenshot({path:'output/home-pixel/review-page.png',fullPage:true});
 await page.setViewportSize({width:414,height:871});assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
 await page.goto('http://127.0.0.1:18766/home-mosaic.html');assert.equal(await page.title(),'首页 · 原版像素小球');
 assert.deepEqual(errors,[]);await browser.close();console.log('HOME_PIXEL_UI_OK: black comparison visibly animates; full homepage GIFs; same-state palettes; 32 native screens; both themes; no overflow or errors');
})().catch(e=>{console.error(e);process.exit(1)});
