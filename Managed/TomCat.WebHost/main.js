import { dotnet } from './_framework/dotnet.js';

const runtime = await dotnet.create();
const config = runtime.getConfig();
const exports = await runtime.getAssemblyExports(config.mainAssemblyName);
await dotnet.run();

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
