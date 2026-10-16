# stb_vorbis

- Version: 1.22
- Source: [nothings/stb](https://github.com/nothings/stb)
- Revision: `2c980bb59875b0d32144a71867fbdebb2f77cd20`

License: MIT or public domain, reproduced at the end of [stb_vorbis.c](stb_vorbis.c).

TomCat includes the decoder in [AudioClip.cpp](../../src/TomCat/Audio/AudioClip.cpp)
with file I/O and the push-data API disabled. Ogg Vorbis input is decoded from
memory to 16-bit PCM, with bounded source size, decoder workspace and decoded
output. The supported input is mono or stereo, up to 384 kHz.

This integration provides complete clip decoding; it does not enable compressed
Vorbis streaming in [AudioStream](../../src/TomCat/Audio/AudioStream.cpp).
The [audio regression fixture](../../../Tests/AudioRegression/Fixtures/README.md)
checks a real Vorbis clip and rejection of truncated data.
