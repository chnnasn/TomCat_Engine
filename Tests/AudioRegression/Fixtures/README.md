# Audio regression fixtures

`sine-440-mono.ogg` is an original generated signal: mono, 8000 Hz, 8000 samples
(one second), 440 Hz sine at amplitude 0.2. It was encoded as Ogg Vorbis with
libsndfile through soundfile 0.14.0. CC0; no third-party recording.

`sine-440-mono.mp3` is the same generated signal (mono, 8000 Hz, one second,
440 Hz sine at amplitude 0.2), encoded as MP3 with libsndfile 1.2.2 through
soundfile 0.14.0. CC0; no third-party recording. [`TestMp3Fixture`](../src/AudioRegression.cpp)
checks the decoded format, a frame count between 8000 and 16000 (encoder delay
and padding), the signal level by RMS, and rejection of a truncated header and of
an ID3 tag with no audio.

[`TestVorbisFixture`](../src/AudioRegression.cpp) verifies the decoded channel
count, sample rate and frame count, then truncates the encoded data to 64 bytes
and checks that decoding fails. This fixture tests decoding, not audible output
or renderer behavior.

From the repository root, build and run it with the [complete regression suite](../../../README.md):

```powershell
$env:TC_RENDERER = 'opengl'
powershell -ExecutionPolicy Bypass -File Scripts/Run-Regressions.ps1 -Configuration Release -Log
```

After building, run only this executable with
`./Tests/bin/Release-windows-x86_64/AudioRegression/AudioRegression.exe`.
