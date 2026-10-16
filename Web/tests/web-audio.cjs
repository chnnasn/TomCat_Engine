// Exercises the production pre-JS in Chromium's real audio graph, including
// offline rendered samples. A Null device or merely advancing timer cannot pass.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const http = require('node:http');
const path = require('node:path');

(async () => {
    const source = await fs.readFile(path.join(__dirname, '..', 'WebAudio.js'), 'utf8');
    const errors = [];
    const server = http.createServer((request, response) => {
        response.setHeader('Content-Type', 'text/html');
        response.end('<!doctype html><button>Enable sound</button><script>var Module = {};</script>');
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let browser;
    try {
        browser = await chromium.launch({ headless: true, channel: process.env.TOMCAT_BROWSER_CHANNEL || undefined,
            args: ['--autoplay-policy=user-gesture-required'] });
        const page = await browser.newPage();
        console.log('WebAudio regression: browser launched');
        page.on('pageerror', error => errors.push(String(error)));
        const url = `http://127.0.0.1:${server.address().port}`;
        await page.goto(url);
        // OfflineAudioContext renders genuine browser sample output, making
        // channel conversion, stereo isolation and gain/pan independently observable.
        await page.evaluate(() => {
            window.audioContexts = [];
            window.AudioContext = function () {
                const context = new OfflineAudioContext(2, 8192, 48000);
                audioContexts.push(context); return context;
            };
        });
        await page.addScriptTag({ content: source });
        const rendered = await page.evaluate(async () => {
            const audio = Module.tomcatAudio;
            const results = [];
            for (const bits of [8, 16, 24, 32, 'float']) {
                const id = audio.initialize();
                const width = bits === 'float' ? 4 : bits / 8;
                const heap = new Uint8Array(128 * 2 * width);
                const view = new DataView(heap.buffer);
                for (let frame = 0; frame < 128; ++frame) {
                    for (let channel = 0; channel < 2; ++channel) {
                        const value = channel === 0 ? -.5 : .25;
                        const offset = (frame * 2 + channel) * width;
                        if (bits === 'float') view.setFloat32(offset, value, true);
                        else if (bits === 8) view.setUint8(offset, value * 128 + 128);
                        else if (bits === 16) view.setInt16(offset, value * 32768, true);
                        else if (bits === 24) {
                            const packed = value * 8388608;
                            view.setUint8(offset, packed); view.setUint8(offset + 1, packed >> 8); view.setUint8(offset + 2, packed >> 16);
                        } else view.setInt32(offset, value * 2147483648, true);
                    }
                }
                const voice = audio.create(id, heap, 0, heap.length, 2, 8000, bits === 'float' ? 32 : bits,
                    bits === 'float', true, .5, 1);
                if (!voice || !audio.spatial(id, voice, 1, .5) || !audio.play(id, voice)) throw new Error('PCM voice failed');
                heap.fill(0); // The graph must own a copy, not the old WASM view.
                const result = await audioContexts.at(-1).startRendering();
                results.push({ bits, left: result.getChannelData(0)[500], right: result.getChannelData(1)[500] });
                audio.shutdown(id);
            }
            const id = audio.initialize();
            const heap = new Uint8Array(256);
            const data = new DataView(heap.buffer);
            for (let i = 0; i < 128; ++i) data.setInt16(i * 2, 16384, true);
            const mono = audio.create(id, heap, 0, heap.length, 1, 8000, 16, false, true, 1, 1);
            audio.spatial(id, mono, 1, .25); audio.play(id, mono);
            const result = await audioContexts.at(-1).startRendering();
            results.push({ bits: 'mono hard right', left: result.getChannelData(0)[500], right: result.getChannelData(1)[500] });
            audio.shutdown(id);
            return results;
        });
        for (const sample of rendered) {
            assert.ok(Math.abs(sample.left - (sample.bits === 'mono hard right' ? 0 : -.125)) < 1e-5, JSON.stringify(sample));
            assert.ok(Math.abs(sample.right - (sample.bits === 'mono hard right' ? .125 : .0625)) < 1e-5, JSON.stringify(sample));
        }
        console.log('WebAudio regression: PCM rendering passed');
        await page.goto(url);
        await page.evaluate(() => {
            const NativeAudioContext = AudioContext;
            window.audioContexts = [];
            window.audioGains = [];
            window.AudioContext = class extends NativeAudioContext {
                constructor(options) { super(options); audioContexts.push(this); }
                createGain() { const node = super.createGain(); audioGains.push(node); return node; }
            };
        });
        await page.addScriptTag({ content: source });
        const initialized = await page.evaluate(() => {
            const audio = Module.tomcatAudio;
            window.deviceId = audio.initialize();
            const heap = new Uint8Array(4800 * 2);
            const data = new DataView(heap.buffer);
            for (let i = 0; i < 4800; ++i) data.setInt16(i * 2, Math.sin(i * Math.PI / 48) * 16000, true);
            window.pcm = heap;
            window.voiceId = audio.create(deviceId, heap, 0, heap.length, 1, 48000, 16, false, true, 1, 1);
            window.analyser = audioContexts[0].createAnalyser();
            audioGains[0].connect(analyser);
            return { deviceId, voiceId, playing: audio.play(deviceId, voiceId), state: audioContexts[0].state };
        });
        assert.ok(initialized.deviceId && initialized.voiceId && initialized.playing);
        // User gesture resumes the actual context, even after async engine loading.
        await page.getByRole('button', { name: 'Enable sound' }).click();
        await page.waitForFunction(() => audioContexts[0].state === 'running');
        await page.waitForFunction(() => {
            const samples = new Float32Array(analyser.fftSize); analyser.getFloatTimeDomainData(samples);
            return samples.some(value => Math.abs(value) > .05);
        });
        const lifecycle = await page.evaluate(async () => {
            const audio = Module.tomcatAudio;
            const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
            const checks = [];
            checks.push(audio.pitch(deviceId, voiceId, .02));
            checks.push(audio.pitch(deviceId, voiceId, Math.fround(.01)));
            checks.push(!audio.pitch(deviceId, voiceId, .009), !audio.pitch(deviceId, voiceId, NaN));
            checks.push(audio.pause(deviceId, voiceId));
            const paused = audio.position(deviceId, voiceId);
            await delay(80);
            checks.push(audio.state(deviceId, voiceId) === 2, audio.position(deviceId, voiceId) === paused);
            checks.push(audio.play(deviceId, voiceId));
            await delay(80);
            const advanced = audio.position(deviceId, voiceId) - paused;
            checks.push(advanced > 0 && advanced < .01);
            checks.push(audio.stop(deviceId, voiceId), audio.position(deviceId, voiceId) === 0);
            const samples = new Float32Array(analyser.fftSize);
            await delay(100); analyser.getFloatTimeDomainData(samples);
            checks.push(samples.every(value => Math.abs(value) < 1e-5));
            checks.push(audio.destroy(deviceId, voiceId), !audio.play(deviceId, voiceId));
            const oneShot = audio.create(deviceId, pcm, 0, pcm.length, 1, 48000, 16, false, false, 1, 4);
            checks.push(oneShot !== voiceId, audio.play(deviceId, oneShot));
            await delay(100);
            checks.push(audio.state(deviceId, oneShot) === 0, Math.abs(audio.position(deviceId, oneShot) - .1) < 1e-5);
            // Malformed input fails before creating an audio graph.
            checks.push(audio.create(deviceId, pcm, 1, pcm.length, 1, 48000, 16, false, false, 1, 1) === 0);
            checks.push(audio.create(deviceId, pcm, 0, pcm.length, 1, 48000, 12, false, false, 1, 1) === 0);
            audio.shutdown(deviceId); audio.shutdown(deviceId);
            checks.push(!audio.operational(deviceId), !audio.play(deviceId, oneShot));
            for (let i = 0; i < 3; ++i) {
                const id = audio.initialize();
                const voice = audio.create(id, pcm, 0, pcm.length, 1, 48000, 16, false, true, 1, 1);
                checks.push(audio.play(id, voice)); audio.shutdown(id);
            }
            const finalDevice = audio.initialize();
            dispatchEvent(new Event('pagehide'));
            checks.push(!audio.operational(finalDevice));
            await delay(30);
            checks.push(audioContexts.every(context => context.state === 'closed'));
            return checks;
        });
        lifecycle.forEach((passed, index) => assert.equal(passed, true, `lifecycle check ${index}`));
        assert.deepEqual(errors, []);
        console.log('PASS WebAudio: rendered PCM 8/16/24/32/float, stereo/pan/gain, gesture, audible signal, pitch, pause/resume, loop/end, stop and cleanup');
    } finally {
        if (browser) await browser.close();
        await new Promise(resolve => server.close(resolve));
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
