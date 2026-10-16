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
    const texturedScene = scene.slice(0, split) + scene.slice(split).replaceAll('6071781742850736130', '6071781742850736129');
    const samples = 22050, rate = 44100;
    const wave = Buffer.alloc(44 + samples * 4);
    wave.write('RIFF'); wave.writeUInt32LE(wave.length - 8, 4); wave.write('WAVEfmt ', 8);
    wave.writeUInt32LE(16, 16); wave.writeUInt16LE(1, 20); wave.writeUInt16LE(2, 22);
    wave.writeUInt32LE(rate, 24); wave.writeUInt32LE(rate * 4, 28);
    wave.writeUInt16LE(4, 32); wave.writeUInt16LE(16, 34); wave.write('data', 36); wave.writeUInt32LE(samples * 4, 40);
    for (let frame = 0; frame < samples; ++frame) {
      wave.writeInt16LE(Math.round(Math.sin(frame * Math.PI / 50) * 12000), 44 + frame * 4);
      wave.writeInt16LE(Math.round(Math.sin(frame * Math.PI / 25) * 6000), 46 + frame * 4);
    }
    const audioPath = '/Samples/PhysicsPlayground/Assets/WebSmoke.wav';
    module.FS.writeFile(audioPath, wave);
    module.FS.writeFile(audioPath + '.tcmeta', 'SchemaVersion: 2\nAsset:\n  Handle: 7770000000000000007\n  Type: Audio\n  ImportSettings: {}\n  SubAssets: []\n');
    const properties = [
      [900, 'Enabled', true], [901, 'Clip', '7770000000000000007'], [902, 'PlayOnStart', true],
      [903, 'Loop', true], [904, 'Streaming', false], [905, 'Volume', .5], [906, 'Pitch', Math.fround(.02).toPrecision(9)],
      [907, 'SpatialBlend', 0], [908, 'MinDistance', 1], [909, 'MaxDistance', 20], [910, 'MixerGroup', 1], [911, 'Bus', 4294967295],
    ];
    const audio = '      - TypeId: ' + String(0x9f0000000000000an) + '\n        StableName: TomCat.AudioSource\n        SchemaVersion: 2\n        Properties:\n'
      + properties.map(([id, name, value]) => `          - PropertyId: ${id}\n            StableName: ${name}\n            Value: ${value}\n`).join('');
    const legacy = '    AudioSource:\n' + properties.map(([, name, value]) => `      ${name}: ${value}\n`).join('');
    module.FS.writeFile(scenePath, texturedScene.replace(/    Components:\r?\n/, legacy + '    Components:\n' + audio));
    assert.equal(module.ccall('tc_web_player_cook_sample', 'number', [], []), 0, module.ccall('tc_web_player_error', 'string', [], []));
    const bytes = Buffer.from(module.FS.readFile('/PhysicsPlayground.tcpak'));
    const count = Number(bytes.readBigUInt64LE(16)), header = bytes.readUInt32LE(12);
    let textures = 0, clips = 0;
    for (let i=0; i<count; i++) {
      if (bytes.readUInt16LE(header + i*64 + 8) === 2) textures++;
      if (bytes.readUInt16LE(header + i*64 + 8) === 4) clips++;
    }
    assert.ok(textures >= 2, `expected multiple textures, got ${textures}`);
    assert.equal(clips, 1, 'the cooked scene must retain its AudioSource clip dependency');
    fs.writeFileSync(path.join(output, 'multitexture.tcpak'), bytes);
  } finally { module.PThread?.terminateAllThreads?.(); }
})().catch(error => {console.error(error);process.exitCode=1;});
