import { dotnet } from './_framework/dotnet.js';

const canvas = globalThis.TomCatWebCanvas ?? document.querySelector('#canvas');
if (!(canvas instanceof HTMLCanvasElement))
    throw new Error('TomCat Web requires an HTMLCanvasElement in globalThis.TomCatWebCanvas or #canvas.');

const runtime = await dotnet.withModuleConfig({ canvas }).create();
// The .NET host and Emscripten native module share this object, but the native
// factory may materialize it after the builder configuration has been merged.
runtime.Module.canvas = canvas;
const config = runtime.getConfig();
const exports = await runtime.getAssemblyExports(config.mainAssemblyName);
const exitCode = await runtime.runMain();
if (exitCode !== 0)
    throw new Error(`TomCat managed Web bootstrap failed with exit code ${exitCode}:\n${
        exports.TomCat.WebHost.BrowserBootstrap.Error()}`);

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
