// Shared pre-JS for the standalone Emscripten and managed browser-wasm players.
// State belongs to this module, never to a page-global engine singleton.
Module['tomcatAudio'] = (() => {
    const devices = new Map();
    let nextDevice = 1;
    const minimumPitch = Math.fround(.01); // Match the native float boundary.
    const valid = (value, min, max) => Number.isFinite(value) && value >= min && value <= max;
    const stopNode = voice => {
        const node = voice.source;
        voice.source = null;
        if (node) { node.onended = null; node.stop(); node.disconnect(); }
    };
    const position = (device, voice) => {
        const elapsed = voice.cursor + (voice.state === 1
            ? Math.max(0, device.context.currentTime - voice.startedAt) * voice.pitch : 0);
        return voice.loop ? elapsed % voice.buffer.duration : Math.min(elapsed, voice.buffer.duration);
    };
    const refresh = (device, voice) => {
        if (voice.state === 1 && !voice.loop && position(device, voice) >= voice.buffer.duration) {
            stopNode(voice); voice.state = 0; voice.cursor = voice.buffer.duration;
        }
    };
    const release = voice => {
        stopNode(voice);
        voice.splitter.disconnect(); voice.merger.disconnect(); voice.volume.disconnect();
        for (const gain of voice.gains) gain.disconnect();
    };
    const spatial = (voice, pan, distance) => {
        voice.pan = pan; voice.distance = distance;
        // Match BuildAudioOutputMatrix: equal-power mono, discrete stereo and
        // modulo folding for additional channels. Never collapse stereo to mono.
        for (let channel = 0; channel < voice.gains.length; ++channel) {
            voice.gains[channel].gain.value = voice.buffer.numberOfChannels === 1 && voice.outputs === 2
                ? distance * (channel === 0 ? Math.cos((pan + 1) * Math.PI / 4) : Math.sin((pan + 1) * Math.PI / 4))
                : distance;
        }
    };
    const lookup = (deviceId, voiceId) => {
        const device = devices.get(deviceId);
        const voice = device?.voices.get(voiceId);
        return device && voice && device.context.state !== 'closed' ? [device, voice] : null;
    };
    const api = {
        initialize() {
            let context;
            try {
                const AudioContextClass = globalThis.AudioContext || globalThis.webkitAudioContext;
                if (!AudioContextClass || nextDevice > 0x7fffffff) return 0;
                context = new AudioContextClass({ latencyHint: 'interactive' });
                const id = nextDevice++;
                const resume = () => {
                    if (context.state === 'suspended' || context.state === 'interrupted')
                        context.resume().catch(() => {}); // Another gesture may retry.
                };
                const hide = () => api.shutdown(id);
                devices.set(id, { context, voices: new Map(), nextVoice: 1, resume, hide });
                // Loading the engine is asynchronous, so the launch button's
                // activation may have expired. The first canvas/key gesture
                // unlocks queued playback; suspended contexts are not failures.
                globalThis.addEventListener('pointerdown', resume, true);
                globalThis.addEventListener('keydown', resume, true);
                globalThis.addEventListener('touchend', resume, true);
                globalThis.addEventListener('pagehide', hide);
                resume();
                return id;
            } catch {
                if (context && context.state !== 'closed') context.close().catch(() => {});
                return 0;
            }
        },
        operational(id) { const device = devices.get(id); return !!device && device.context.state !== 'closed'; },
        shutdown(id) {
            const device = devices.get(id);
            if (!device) return;
            devices.delete(id);
            globalThis.removeEventListener('pointerdown', device.resume, true);
            globalThis.removeEventListener('keydown', device.resume, true);
            globalThis.removeEventListener('touchend', device.resume, true);
            globalThis.removeEventListener('pagehide', device.hide);
            for (const voice of device.voices.values()) release(voice);
            device.voices.clear();
            if (device.context.state !== 'closed') device.context.close().catch(() => {});
        },
        create(id, heap, pointer, byteLength, channels, sampleRate, bits, floating, loop, volume, pitch) {
            const device = devices.get(id);
            const width = bits / 8;
            if (!device || !api.operational(id) || device.nextVoice > 0x7fffffff
                || !valid(volume, 0, 4) || !valid(pitch, minimumPitch, 4)
                || !Number.isInteger(channels) || channels < 1 || channels > 8
                || !Number.isInteger(sampleRate) || sampleRate <= 0
                || !(floating ? bits === 32 : [8, 16, 24, 32].includes(bits))
                || !Number.isInteger(pointer) || pointer < 0 || !Number.isInteger(byteLength) || byteLength <= 0
                || pointer + byteLength > heap.byteLength || byteLength % (channels * width)) return 0;
            const nodes = [];
            try {
                const context = device.context;
                const frames = byteLength / (channels * width);
                const buffer = context.createBuffer(channels, frames, sampleRate);
                // Copy now: WASM memory may grow or the native clip may be freed.
                const source = new DataView(heap.buffer, heap.byteOffset + pointer, byteLength);
                for (let channel = 0; channel < channels; ++channel) {
                    const target = buffer.getChannelData(channel);
                    for (let frame = 0; frame < frames; ++frame) {
                        const offset = (frame * channels + channel) * width;
                        let value;
                        if (floating) value = source.getFloat32(offset, true);
                        else if (bits === 8) value = (source.getUint8(offset) - 128) / 128;
                        else if (bits === 16) value = source.getInt16(offset, true) / 32768;
                        else if (bits === 24) {
                            const packed = source.getUint8(offset) | (source.getUint8(offset + 1) << 8) | (source.getUint8(offset + 2) << 16);
                            value = (packed << 8 >> 8) / 8388608;
                        } else value = source.getInt32(offset, true) / 2147483648;
                        target[frame] = Number.isFinite(value) ? value : 0;
                    }
                }
                const outputs = Math.min(2, context.destination.maxChannelCount || 2);
                const splitter = context.createChannelSplitter(channels); nodes.push(splitter);
                const merger = context.createChannelMerger(outputs); nodes.push(merger);
                const gain = context.createGain(); nodes.push(gain);
                const gains = [];
                for (let channel = 0; channel < (channels === 1 ? outputs : channels); ++channel) {
                    const route = context.createGain(); nodes.push(route); gains.push(route);
                    splitter.connect(route, channels === 1 ? 0 : channel);
                    route.connect(merger, 0, channel % outputs);
                }
                gain.gain.value = volume;
                merger.connect(gain); gain.connect(context.destination);
                const voice = { buffer, outputs, splitter, merger, volume: gain, gains,
                    loop: !!loop, pitch, state: 0, cursor: 0, startedAt: 0, source: null, pan: 0, distance: 1 };
                spatial(voice, 0, 1);
                const handle = device.nextVoice++;
                device.voices.set(handle, voice);
                return handle;
            } catch {
                for (const node of nodes) node.disconnect();
                return 0;
            }
        },
        destroy(id, handle) {
            const found = lookup(id, handle);
            if (!found) return false;
            const [device, voice] = found;
            release(voice); device.voices.delete(handle); return true;
        },
        play(id, handle) {
            const found = lookup(id, handle);
            if (!found) return false;
            const [device, voice] = found;
            refresh(device, voice);
            if (voice.state === 1) return true;
            if (voice.state === 0) voice.cursor = 0;
            let source;
            try {
                source = device.context.createBufferSource();
                source.buffer = voice.buffer; source.loop = voice.loop; source.playbackRate.value = voice.pitch;
                source.connect(voice.splitter);
                source.onended = () => {
                    if (voice.source !== source) return;
                    source.disconnect(); voice.source = null; voice.state = 0; voice.cursor = voice.buffer.duration;
                };
                source.start(0, voice.cursor);
                voice.source = source; voice.startedAt = device.context.currentTime; voice.state = 1;
                return true;
            } catch { if (source) source.disconnect(); return false; }
        },
        pause(id, handle) {
            const found = lookup(id, handle);
            if (!found) return false;
            const [device, voice] = found;
            refresh(device, voice);
            if (voice.state !== 1) return false;
            voice.cursor = position(device, voice); stopNode(voice); voice.state = 2; return true;
        },
        stop(id, handle) {
            const found = lookup(id, handle);
            if (!found) return false;
            const voice = found[1]; stopNode(voice); voice.state = 0; voice.cursor = 0; return true;
        },
        loop(id, handle, value) {
            const found = lookup(id, handle);
            if (!found) return false;
            const [device, voice] = found;
            if (voice.loop === !!value) return true;
            voice.cursor = position(device, voice); voice.startedAt = device.context.currentTime;
            voice.loop = !!value;
            if (voice.source) voice.source.loop = voice.loop;
            return true;
        },
        volume(id, handle, value) {
            const found = lookup(id, handle);
            if (!found || !valid(value, 0, 4)) return false;
            found[1].volume.gain.value = value; return true;
        },
        pitch(id, handle, value) {
            const found = lookup(id, handle);
            if (!found || !valid(value, minimumPitch, 4)) return false;
            const [device, voice] = found;
            if (voice.pitch === value) return true;
            voice.cursor = position(device, voice); voice.startedAt = device.context.currentTime;
            voice.pitch = value;
            if (voice.source) voice.source.playbackRate.value = value;
            return true;
        },
        spatial(id, handle, pan, distance) {
            const found = lookup(id, handle);
            if (!found || !valid(pan, -1, 1) || !valid(distance, 0, 1)) return false;
            const voice = found[1];
            if (voice.pan !== pan || voice.distance !== distance) spatial(voice, pan, distance);
            return true;
        },
        state(id, handle) {
            const found = lookup(id, handle);
            if (!found) return 0;
            refresh(...found); return found[1].state;
        },
        position(id, handle) { const found = lookup(id, handle); return found ? position(...found) : 0; },
    };
    return api;
})();
