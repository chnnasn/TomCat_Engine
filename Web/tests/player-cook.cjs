// Cook the bundled sample with the real Player module, without a GL context.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const source = path.resolve(process.argv[2] || 'build/web');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'tomcat-player-cook-'));
fs.writeFileSync(path.join(directory, 'package.json'), '{"type":"commonjs"}');
(async () => {
  let module;
  try {
    for (const extension of ['js','wasm','data']) fs.copyFileSync(path.join(source,`tomcat_player.${extension}`),path.join(directory,`tomcat_player.${extension}`));
    module = await require(path.join(directory,'tomcat_player.js'))({locateFile:name=>path.join(directory,name)});
    assert.ok(module.FS.readFile('/Packages/Resources/Sprites/TomCat/Square.tga').length > 0);
    assert.equal(module.ccall('tc_web_player_cook_sample','number',[],[]),0,String(module.ccall('tc_web_player_error','string',[],[])));
    assert.ok(module.FS.readFile('/PhysicsPlayground.tcpak').length > 0);
    console.log('PASS: Player cooks bundled sample with packaged built-in sprites');
  } finally {
    module?.PThread.terminateAllThreads();
    if(path.dirname(directory)===os.tmpdir() && path.basename(directory).startsWith('tomcat-player-cook-')) fs.rmSync(directory,{recursive:true,force:true});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
