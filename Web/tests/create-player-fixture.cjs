// Cook two distinct built-in textures: one texture cannot expose the inline
// Web executor waiting for its own prepared-texture queue to be drained.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const build = path.resolve(process.argv[2]);
const output = path.resolve(process.argv[3]);
(async () => {
  const module = await require(path.join(build, 'tomcat_player.js'))({locateFile: name => path.join(build, name)});
  try {
    const scenePath = '/Samples/PhysicsPlayground/Assets/Scene/sample.tomcat';
    const scene = module.FS.readFile(scenePath, {encoding:'utf8'});
    const split = scene.lastIndexOf('  - Entity:');
    assert.ok(split > 0);
    module.FS.writeFile(scenePath, scene.slice(0, split) + scene.slice(split).replaceAll('6071781742850736130', '6071781742850736129'));
    assert.equal(module.ccall('tc_web_player_cook_sample', 'number', [], []), 0, module.ccall('tc_web_player_error', 'string', [], []));
    const bytes = Buffer.from(module.FS.readFile('/PhysicsPlayground.tcpak'));
    const count = Number(bytes.readBigUInt64LE(16)), header = bytes.readUInt32LE(12);
    let textures = 0;
    for (let i=0; i<count; i++) if (bytes.readUInt16LE(header + i*64 + 8) === 2) textures++;
    assert.ok(textures >= 2, `expected multiple textures, got ${textures}`);
    fs.writeFileSync(path.join(output, 'multitexture.tcpak'), bytes);
  } finally { module.PThread?.terminateAllThreads?.(); }
})().catch(error => {console.error(error);process.exitCode=1;});
