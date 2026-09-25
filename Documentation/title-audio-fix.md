# Title-song audio correction

The opening theme uses a separate prerecorded sample in `963a25.d5`. The
inherited decoder shifted its signed mantissa by `exponent - 3`. That shift
count is negative for quiet samples, which is undefined in C++. The supplied
song contains 47,416 such samples out of 262,144.

With the Apple Silicon compiler and optimization settings measured in
[title-audio-acceptance.json](title-audio-acceptance.json), the original routine
corrupts all 47,416 samples. Its vectorized loop narrows an intermediate value
under the assumption that the shift count is valid; negative counts then expose
unwanted high bits. The decoded waveform's RMS rises from 2,048.95 to 9,986.01.
This explains why the theme produces loud static while other music and sound
effects can work: those sounds do not use this decoder.

The correction keeps the intermediate mantissa nonnegative and computes the
same intended value using bounded multiplication and division:

```text
scale = 2^exponent
sample = floor(unsigned_mantissa * scale / 8) - 64 * scale
```

This preserves the inherited quarter-scale amplitude, existing mixer gain,
sample timing, and playback routing. It does not change the 68000 or Z80
translation, other sound devices, or `RenderTitleSample`.

## Independent reference

At MAME revision `70642b82cb651a3e6a1b3a470b5637123fb63ab8`, the
[TMNT driver](https://github.com/mamedev/mame/blob/70642b82cb651a3e6a1b3a470b5637123fb63ab8/src/mame/konami/tmnt.cpp)
identifies the title data as Yamaha floating-point PCM, decodes each little-endian
word with `ymfm::decode_fp(word >> 3)`, and plays it at 20,000 Hz. Aaron Giles's
[YMFM representation and decoder](https://github.com/mamedev/mame/blob/70642b82cb651a3e6a1b3a470b5637123fb63ab8/3rdparty/ymfm/src/ymfm.h)
provide the independent reference: invert the sign and exponent, narrow the
shifted value to signed 16-bit, then apply the resulting arithmetic right shift.
The test expresses these bit operations in Python and divides the result by four
with floor rounding to retain this engine's volume.

The unchanged negative control comes from the local FBNeo source archive at
revision `22e2aebcccf888ba9a041bf6023f3381a4fc86dd`. Source and fixture hashes,
compiler identity, flags, ROM identity, and waveform statistics are recorded in
the acceptance report.

## Reproduction and measured results

Run on an Apple Silicon Mac with the locally supplied media:

```sh
python3 scripts/verify_title_audio.py
```

The test extracts the actual production function instead of maintaining a
duplicate implementation. It compiles optimized arm64 and UndefinedBehaviorSanitizer
fixtures using the relevant production flags. Both fixtures match the independent
reference for all 65,536 possible encoded words and every one of the supplied
song's 262,144 samples. The sanitizer emits no diagnostics for the corrected
routine. Compiling the original routine reproduces the corruption, and its
sanitized negative control fails on a negative shift exponent.

The corrected source waveform has 13.1072 seconds of mono PCM at 20,000 Hz,
range -8,192 to 8,176, and SHA-256
`3c07f757a5f84f8386083e143ccd5b7bba9d663a372986a231716d21e918943b`.
These measurements cover the decoder; they do not establish acoustic output or
full-system correctness. Shared native/reference CPU comparisons alone could
not detect this bug because both builds used the same faulty hardware routine.

The separate [engine integration report](title-audio-integration.json) checks
the actual initialized title sample array against the same independent formula.
Across 10,800 title and gameplay frames, an instrumented probe matches every
shipping-library output, and the pre-fix library matches CPU state, RAM and
pixels. All 8,437 audio frames outside title playback and the existing 200ms
limiter response match the previous release exactly. The private project's
`scripts/verify_title_integration.py --baseline /path/to/previous/libtmnt.dylib`
reproduces that comparison when the original binary is retained.

Only source, hashes, and measurements are tracked. The test keeps generated
fixtures under ignored `build/verification/title-audio`; it does not write ROM
or decoded PCM files.
