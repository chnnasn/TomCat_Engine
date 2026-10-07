import { dotnet } from './_framework/dotnet.js';

const canvas = globalThis.TomCatWebCanvas ?? document.querySelector('#canvas');
if (!(canvas instanceof HTMLCanvasElement))
    throw new Error('TomCat Web requires an HTMLCanvasElement in globalThis.TomCatWebCanvas or #canvas.');

const runtime = await dotnet.withModuleConfig({ canvas }).create();
// The .NET host and Emscripten native module share this object, but the native
// factory may materialize it after the builder configuration has been merged.
runtime.Module.canvas = canvas;
// The bundled .NET 10 Emscripten MEMFS rename can leave the overwritten node
// in FS's lookup hash. A later rename/delete then exposes the old sidecar again.
// Invalidate that displaced node only after a successful atomic replacement.
const fs = runtime.Module.FS;
const rename = fs.rename;
fs.rename = function (source, destination) {
    let displaced;
    try { displaced = fs.lookupPath(destination).node; } catch { /* Destination may not exist. */ }
    const result = rename.call(fs, source, destination);
    if (displaced && fs.lookupPath(destination).node !== displaced)
        fs.destroyNode(displaced);
    return result;
};
const config = runtime.getConfig();
const exports = await runtime.getAssemblyExports(config.mainAssemblyName);
const exitCode = await runtime.runMain();
if (exitCode !== 0) {
    // Read the bootstrap reason through the export surface: a non-zero exit can also be a
    // runtime level termination, where the assembly exports are no longer reachable.
    let reason = '(no message)';
    try { reason = exports.TomCat.WebHost.BrowserBootstrap.Error() || reason; } catch { /* runtime already torn down */ }
    console.error(`TomCat managed Web bootstrap failed with exit code ${exitCode}:\n${reason}`);
    throw new Error(`TomCat managed Web bootstrap failed with exit code ${exitCode}:\n${reason}`);
}

function decodeBase64(value) {
    const binary = atob(value);
    const bytes = new Uint8Array(binary.length);
    for (let index = 0; index < binary.length; ++index)
        bytes[index] = binary.charCodeAt(index);
    return bytes;
}

function compileAndInstall(request) {
    const result = JSON.parse(exports.TomCat.WebHost.BrowserCompiler.Compile(
        typeof request === 'string' ? request : JSON.stringify(request)));
    if (result.succeeded) {
        const status = exports.TomCat.WebHost.BrowserExports.EditorSetManagedAssembly(
            decodeBase64(result.assembly), decodeBase64(result.pdb));
        if (status !== 0) {
            result.succeeded = false;
            result.diagnostics.push({ severity: 'error', code: 'TCWEB0002',
                message: exports.TomCat.WebHost.BrowserExports.EditorError(),
                file: null, line: 0, column: 0 });
        }
    }
    return result;
}

// The hosting page owns canvases, persistence and module recreation. C#
// compilation and both native engine entrypoint sets live in this one module.
globalThis.TomCatWeb = Object.freeze({
    compiler: exports.TomCat.WebHost.BrowserCompiler,
    compileAndInstall,
    engine: exports.TomCat.WebHost.BrowserExports,
    runtime
});
globalThis.dispatchEvent(new CustomEvent('tomcat-web-ready'));
