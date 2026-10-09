const {chromium} = require('playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(process.argv[2] || 'build/web-managed');
const output = path.resolve(process.argv[3] || 'build/web-smoke-results');
const types = {'.html':'text/html','.js':'text/javascript','.json':'application/json','.wasm':'application/wasm','.dll':'application/octet-stream'};
(async () => {
 await fs.mkdir(output,{recursive:true});
 const logs=[]; const pageErrors=[];
 const server = http.createServer(async (req,res) => {
  try {
   const target=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://localhost').pathname));
   if(!target.startsWith(root+path.sep)) {res.writeHead(403).end();return;}
   res.setHeader('Cross-Origin-Opener-Policy','same-origin');
   res.setHeader('Cross-Origin-Embedder-Policy','require-corp');
   res.setHeader('Content-Type',types[path.extname(target)]||'application/octet-stream');
   res.end(await fs.readFile(target));
  } catch {res.writeHead(404).end();}
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 let browser, page;
 try {
  browser=await chromium.launch({headless:true,channel:process.env.TOMCAT_BROWSER_CHANNEL||undefined,args:['--enable-unsafe-swiftshader']});
  page=await browser.newPage({viewport:{width:960,height:720},deviceScaleFactor:Number(process.env.TOMCAT_BROWSER_DPR||1)});
  page.on('console',m=>logs.push(m.type()+': '+m.text()));page.on('pageerror',e=>pageErrors.push(String(e)));
  await page.goto(`http://127.0.0.1:${server.address().port}/managed-browser-smoke.html`);
  await page.waitForFunction(()=>{try{return typeof JSON.parse(document.querySelector('#status').textContent).ok==='boolean'}catch{return false}},{},{timeout:180000});
  const result=JSON.parse(await page.locator('#status').textContent());
  result.browser=browser.version();result.pageErrors=pageErrors;
  await fs.writeFile(path.join(output,'result.json'),JSON.stringify(result,null,2));
  await page.screenshot({path:path.join(output,'browser.png'),fullPage:true});
  assert.equal(result.ok,true,JSON.stringify(result));assert.deepEqual(pageErrors,[]);
  console.log('PASS managed browser lifecycle, fault isolation and preview controls');
  await page.goto(`http://127.0.0.1:${server.address().port}/managed-player-smoke.html`);
  await page.waitForFunction(()=>{try{return typeof JSON.parse(document.querySelector('#status').textContent).ok==='boolean'}catch{return false}},{},{timeout:60000});
  const player=JSON.parse(await page.locator('#status').textContent());
  await fs.writeFile(path.join(output,'player-result.json'),JSON.stringify(player,null,2));
  assert.equal(player.ok,true,JSON.stringify(player));assert.deepEqual(pageErrors,[]);
  console.log('PASS packaged player with multiple textures, frames and shutdown');
 } catch(error) {
  if(page) await page.screenshot({path:path.join(output,'failure.png'),fullPage:true}).catch(()=>{});
  await fs.writeFile(path.join(output,'failure.json'),JSON.stringify({error:String(error),pageErrors},null,2));
  throw error;
 } finally {
  await fs.writeFile(path.join(output,'console.log'),logs.join('\n'));
  if(browser)await browser.close();await new Promise(resolve=>server.close(resolve));
 }
})().catch(e=>{console.error(e);process.exitCode=1});
