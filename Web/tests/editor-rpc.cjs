// Executes the real Emscripten module in Node, without a graphics context.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const source = path.resolve(process.argv[2] || 'build/web');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'tomcat-editor-rpc-'));
fs.writeFileSync(path.join(directory, 'package.json'), '{"type":"commonjs"}');
for (const extension of ['js', 'wasm', 'data']) fs.copyFileSync(path.join(source, `tomcat_editor.${extension}`), path.join(directory, `tomcat_editor.${extension}`));
(async () => {
  let module;
  try {
    module = await require(path.join(directory, 'tomcat_editor.js'))({locateFile: name => path.join(directory, name)});
    let sequence = 0;
    function rpc(type, payload, expectedError) {
      const requestId = String(++sequence);
      const reply = JSON.parse(module.ccall('tc_web_editor_rpc', 'string', ['string'], [JSON.stringify({protocol:'tomcat.web.v1', requestId, type, payload})]));
      assert.equal(reply.requestId, requestId);
      assert.equal(reply.protocol, 'tomcat.web.v1');
      if (expectedError) { assert.equal(reply.ok, false); assert.equal(reply.error.code, expectedError, JSON.stringify(reply)); return reply; }
      assert.equal(reply.ok, true, JSON.stringify(reply));
      if (reply.result.sceneHandle) {
        const state = JSON.parse(module.ccall('tc_web_editor_state', 'string', [], []));
        for (const key of ['sceneHandle','revision','selectedEntityId','dirty','canUndo','canRedo'])
          assert.deepEqual(state[key], reply.result[key], `host status differs for ${key}`);
      }
      return reply.result;
    }
    assert.ok(rpc('system.capabilities', {}).capabilities.includes('scene.transact'));
    assert.equal(module.ccall('tc_web_editor_take_actions', 'number', [], []), 0);
    let snapshot = rpc('project.open', {projectPath:'/Samples/PhysicsPlayground/Project.tcproj'});
    const square = snapshot.entities.find(entity => entity.name === 'Square');
    const ground = snapshot.entities.find(entity => entity.name === 'Ground');
    assert.ok(square && ground);
    const transform = snapshot.schemas.find(schema => schema.name === 'TomCat.Transform');
    const sprite = snapshot.schemas.find(schema => schema.name === 'TomCat.SpriteRenderer');
    const health = snapshot.schemas.find(schema => schema.name === 'TomCat.HealthComponent');
    assert.ok(transform && sprite && health);
    assert.ok(BigInt(transform.id) > BigInt(Number.MAX_SAFE_INTEGER));
    const initial = snapshot;
    const selection = rpc('scene.select', {sceneHandle:snapshot.sceneHandle,entityId:square.id});
    assert.equal(selection.selectedEntityId,square.id); assert.equal(selection.revision,initial.revision);
    const tx = operations => ({sceneHandle:snapshot.sceneHandle, baseRevision:snapshot.revision, label:'RPC test', operations});
    const move = {op:'component.patch', entityId:square.id, componentId:transform.id, properties:{'1':[2, 3, 0]}};
    rpc('scene.transact', tx([move, {...move, properties:{'999999':1}}]), 'PROPERTY_READ_ONLY');
    assert.equal(rpc('scene.snapshot', {sceneHandle:snapshot.sceneHandle}).archive, initial.archive);
    snapshot = rpc('scene.transact', tx([move]));
    assert.deepEqual(snapshot.entities.find(e => e.id === square.id).components.find(c => c.id === transform.id).values['1'], [2,3,0]);
    assert.equal(snapshot.dirty, true); assert.equal(snapshot.canUndo, true);
    const moved = snapshot;
    rpc('scene.transact', {...tx([move]),baseRevision:initial.revision}, 'REVISION_CONFLICT');
    snapshot = rpc('history.undo', {sceneHandle:snapshot.sceneHandle,baseRevision:snapshot.revision});
    assert.equal(snapshot.archive, initial.archive); assert.equal(snapshot.dirty, false); assert.ok(snapshot.revision > moved.revision);
    snapshot = rpc('history.redo', {sceneHandle:snapshot.sceneHandle,baseRevision:snapshot.revision});
    assert.equal(snapshot.archive, moved.archive);
    rpc('scene.transact', tx([{...move,entityId:Number(square.id)}]), 'INVALID_REQUEST');
    rpc('scene.transact', tx([{...move,entityId:'18446744073709551616'}]), 'INVALID_REQUEST');
    rpc('scene.transact', tx([{op:'entity.set-parent',entityId:square.id,parentId:ground.id},{op:'entity.set-parent',entityId:ground.id,parentId:square.id}]), 'INVALID_REQUEST');
    assert.equal(rpc('scene.snapshot', {sceneHandle:snapshot.sceneHandle}).archive, moved.archive);
    const assets = rpc('asset.list', {}).assets;
    assert.ok(assets.some(a => a.type === 'Texture2D'));
    const builtIn = assets.find(a => a.pathHint.endsWith('/Circle.tga'));
    assert.ok(builtIn,'bundled sprites must be exposed without a project-local metadata sidecar');
    snapshot = rpc('scene.transact',tx([{op:'component.patch',entityId:square.id,componentId:sprite.id,properties:{'202':builtIn.handle}}]));
    assert.equal(snapshot.entities.find(e=>e.id===square.id).components.find(c=>c.id===sprite.id).values['202'],builtIn.handle);

    module.FS.mkdirTree('/Samples/PhysicsPlayground/Assets/WebImports');
    const tga = new Uint8Array(21); tga[2]=2; tga[12]=1; tga[14]=1; tga[16]=24; tga[18]=20; tga[19]=200; tga[20]=240;
    module.FS.writeFile('/Samples/PhysicsPlayground/Assets/WebImports/test.tga',tga);
    const imported = rpc('asset.import',{name:'test.tga'});
    assert.ok(rpc('asset.list',{}).assets.some(asset => asset.handle === imported.handle && asset.type === 'Texture2D'));
    assert.ok(module.FS.readFile('/Samples/PhysicsPlayground/Assets/WebImports/test.tga.tcmeta').length > 0);
    module.FS.writeFile('/Samples/PhysicsPlayground/Assets/WebImports/broken.png',new Uint8Array([1,2,3]));
    rpc('asset.import',{name:'broken.png'},'IMPORT_FAILED');
    assert.ok(!rpc('asset.list',{}).assets.some(asset => asset.pathHint.includes('broken.png')));
    rpc('scene.transact', tx([{op:'component.patch',entityId:square.id,componentId:sprite.id,properties:{'202':initial.sceneHandle}}]), 'INVALID_ASSET');
    const id = '18446744073709550000';
    const parentId = '18446744073709549999';
    snapshot = rpc('scene.transact', tx([{op:'entity.create',entityId:parentId,name:'Group'},{op:'entity.create',entityId:id,name:'Web entity',parentId},{op:'component.add',entityId:id,componentId:health.id}]));
    assert.equal(snapshot.entities.find(e => e.id === id).parentId, parentId);
    assert.ok(snapshot.entities.find(e => e.id === id).components.some(c => c.id === health.id));
    snapshot = rpc('scene.transact', tx([{op:'entity.rename',entityId:id,name:'Renamed entity'}]));
    assert.equal(snapshot.entities.find(e => e.id === id).name,'Renamed entity');
    snapshot = rpc('scene.transact', tx([{op:'component.remove',entityId:id,componentId:health.id}]));
    assert.ok(!snapshot.entities.find(e => e.id === id).components.some(c => c.id === health.id));
    snapshot = rpc('scene.transact', tx([{op:'entity.delete',entityId:parentId}]));
    assert.ok(!snapshot.entities.some(e => e.id === id || e.id === parentId));
    snapshot = rpc('history.undo', {sceneHandle:snapshot.sceneHandle,baseRevision:snapshot.revision});
    assert.ok(snapshot.entities.some(e => e.id === id));
    snapshot = rpc('scene.transact', tx([move]));
    // A no-op keeps history intact; a real divergent edit removes redo.
    snapshot = rpc('scene.transact', tx([{...move,properties:{'1':[4,3,0]}}]));
    assert.equal(snapshot.canRedo, false);
    rpc('history.redo', {sceneHandle:snapshot.sceneHandle,baseRevision:snapshot.revision}, 'HISTORY_EMPTY');
    snapshot = rpc('scene.markSaved', {sceneHandle:snapshot.sceneHandle,baseRevision:snapshot.revision});
    assert.equal(snapshot.dirty, false);
    rpc('scene.loadArchive', {sceneHandle:snapshot.sceneHandle,baseRevision:snapshot.revision,archive:'invalid'}, 'INVALID_SCENE');
    assert.equal(rpc('scene.snapshot', {sceneHandle:snapshot.sceneHandle}).archive, snapshot.archive);
    const authoring = rpc('scene.snapshot', {sceneHandle:snapshot.sceneHandle});
    rpc('preview.control',{command:'step'},'PREVIEW_STATE');
    for (let cycle = 0; cycle < 50; cycle++) {
      assert.equal(rpc('preview.control',{command:'play'}).mode,'play');
      rpc('preview.control',{command:'step'},'PREVIEW_STATE');
      rpc('scene.transact',tx([move]),'PREVIEW_ACTIVE');
      rpc('history.undo',{sceneHandle:snapshot.sceneHandle,baseRevision:snapshot.revision},'PREVIEW_ACTIVE');
      rpc('project.open',{projectPath:'/Samples/PhysicsPlayground/Project.tcproj'},'PREVIEW_ACTIVE');
      assert.equal(rpc('preview.control',{command:'pause'}).mode,'pause');
      const before = rpc('preview.snapshot',{});
      for (let step=0;step<10;step++) rpc('preview.control',{command:'step'});
      const after = rpc('preview.snapshot',{});
      assert.equal(after.frames,10);
      assert.notEqual(after.archive,before.archive,'physics must advance in the preview copy');
      assert.equal(rpc('preview.control',{command:'resume'}).mode,'play');
      assert.equal(rpc('preview.control',{command:'stop'}).mode,'edit');
      rpc('preview.snapshot',{},'PREVIEW_STATE');
      assert.deepEqual(rpc('scene.snapshot',{sceneHandle:snapshot.sceneHandle}),authoring,'stop must preserve authoring and history');
    }
    snapshot = rpc('project.new',{name:'Camera validation',template:'2D'});
    const camera = snapshot.entities.find(entity => entity.name === 'Main Camera');
    snapshot = rpc('scene.transact',tx([{op:'entity.delete',entityId:camera.id}]));
    rpc('preview.control',{command:'play'},'CAMERA_REQUIRED');
    assert.equal(JSON.parse(module.ccall('tc_web_editor_state','string',[],[])).mode,'edit');
    snapshot = rpc('history.undo',{sceneHandle:snapshot.sceneHandle,baseRevision:snapshot.revision});
    assert.equal(rpc('preview.control',{command:'play'}).mode,'play');
    rpc('preview.control',{command:'stop'});
    console.log('PASS: real WASM editor transactions, assets, undo/redo, isolated physics preview and 50 restarts');
  } finally {
    if (module) { try { module.ccall('tc_web_editor_shutdown', null, [], []); } finally { module.PThread.terminateAllThreads(); } }
    if (path.dirname(directory) === os.tmpdir() && path.basename(directory).startsWith('tomcat-editor-rpc-')) fs.rmSync(directory, {recursive:true,force:true});
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
