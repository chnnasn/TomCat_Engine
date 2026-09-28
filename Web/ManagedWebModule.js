// Emscripten's GLFW glue reads Module.canvas directly. The .NET host's module
// builder does not forward arbitrary properties early enough for GLFW startup,
// so resolve the engine-owned canvas in native pre-JS.
Module.canvas ??= globalThis.TomCatWebCanvas ?? document.querySelector('#canvas');
if (!(Module.canvas instanceof HTMLCanvasElement))
    throw new Error('TomCat Web requires an HTMLCanvasElement in globalThis.TomCatWebCanvas or #canvas.');
