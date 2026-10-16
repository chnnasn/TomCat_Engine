// Multiple textures and font chains expose inline Web jobs waiting for their
// own prepared queues to be drained. Keep these as real cooked dependencies.
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
    let fixture = texturedScene.replace(/    Components:\r?\n/, legacy + '    Components:\n' + audio);
    for (let index = 0; index < 2; ++index) {
      const fontName = index ? 'OpenSans-Bold.ttf' : 'OpenSans-Regular.ttf';
      const handle = index ? '7770000000000000011' : String(0x54434d464e540001n);
      const fontPath = (index ? '/Samples/PhysicsPlayground/Assets/' : '/Packages/fonts/opensans/') + fontName;
      module.FS.mkdirTree(path.posix.dirname(fontPath));
      module.FS.writeFile(fontPath, fs.readFileSync(path.join(__dirname, '../../Editor/TomCatInut/Packages/fonts/opensans', fontName)));
      if(index)module.FS.writeFile(fontPath + '.tcmeta', `SchemaVersion: 2\nAsset:\n  Handle: ${handle}\n  Type: Font\n  ImportSettings: {}\n  SubAssets: []\n`);
      const fields = [[1,'Enabled',true],[2,'Font',handle],[3,'Text',index ? 'BOLD' : 'REGULAR'],
        [4,'FontSize',.5],[5,'Color',index ? '[0, 1, 1, 1]' : '[1, 0, 1, 1]'],
        [6,'Alignment',0],[7,'MaxWidth',0],[8,'LineSpacing',1],[9,'FallbackFont',0],[10,'EmojiFont',0]];
      const component = `      - TypeId: ${0x9f01000000000001n}\n        StableName: TomCat.TextRenderer\n        SchemaVersion: 1\n        Properties:\n`
        + fields.map(([id,name,value]) => `          - PropertyId: ${0x9f01100000000000n + BigInt(id)}\n            StableName: ${name}\n            Value: ${value}\n`).join('');
      // Attach to the two visible physics entities. Their distinct text colors
      // let the browser assert both font atlases reached rendered pixels.
      let entityIndex = 0;
      fixture = fixture.replace(/    Components:\r?\n/g, match => ++entityIndex === index + 2 ? match + component : match);
    }
    module.FS.writeFile(scenePath, fixture);
    assert.equal(module.ccall('tc_web_player_cook_sample', 'number', [], []), 0, module.ccall('tc_web_player_error', 'string', [], []));
    const bytes = Buffer.from(module.FS.readFile('/PhysicsPlayground.tcpak'));
    const count = Number(bytes.readBigUInt64LE(16)), header = bytes.readUInt32LE(12);
    let textures = 0, clips = 0, fonts = 0;
    for (let i=0; i<count; i++) {
      if (bytes.readUInt16LE(header + i*64 + 8) === 2) textures++;
      if (bytes.readUInt16LE(header + i*64 + 8) === 4) clips++;
      if (bytes.readUInt16LE(header + i*64 + 8) === 5) fonts++;
    }
    assert.ok(textures >= 2, `expected multiple textures, got ${textures}`);
    assert.equal(clips, 1, 'the cooked scene must retain its AudioSource clip dependency');
    assert.equal(fonts, 2, 'the cooked scene must retain both independent font dependencies');
    fs.writeFileSync(path.join(output, 'multitexture.tcpak'), bytes);
  } finally { module.PThread?.terminateAllThreads?.(); }
})().catch(error => {console.error(error);process.exitCode=1;});
