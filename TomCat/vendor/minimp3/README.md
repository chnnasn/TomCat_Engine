# minimp3

- Source: [lieff/minimp3](https://github.com/lieff/minimp3)
- Revision: `afb604c06bc8beb145fecd42c0ceb5bda8795144`
- File: `minimp3.h` only (SHA-256 `5fb296a790734772b65a5514438cc06d14f24e539c13475ff2a7f737681c82c0`)

License: CC0 1.0, reproduced in [LICENSE](LICENSE).

TomCat includes the decoder in [AudioClip.cpp](../../src/TomCat/Audio/AudioClip.cpp)
and decodes MP3 (MPEG-1/2/2.5 Layer III) from memory to 16-bit PCM, with the
same source, channel, sample-rate and decoded-size limits as Ogg Vorbis.
`minimp3_ex.h` is not vendored: it adds file I/O and memory mapping the engine
does not need.

Like Vorbis, MP3 is decoded as a complete clip; compressed streaming through
[AudioStream](../../src/TomCat/Audio/AudioStream.cpp) is not provided.
