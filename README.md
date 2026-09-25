# TMNT fixed native engine source

This source companion builds an arm64 macOS library for the supplied World
four-player version X of Teenage Mutant Ninja Turtles. It contains the modified
engine sources, offline translators, original upstream source archive, media
identity manifest, component notices and measured acceptance reports.

It contains no game ROM files, game graphics or audio, generated fixed-program
tables, app bundle, Swift host, build products, saves or gameplay captures.
The pinned original FBNeo source archive remains unchanged: it includes its
upstream frontend icons/images and an unused Windows SDK dependency archive.
Those upstream resources are not the TMNT game's artwork and are not compiled
into this arm64 engine library.

## Build on an Apple Silicon Mac

Install Xcode Command Line Tools and Python 3.12 or newer. Supply your own exact
16-file local ROM set; Verification/rom-identity.json records the accepted sizes
and SHA-256 identities. This project does not download ROMs.

```sh
python3 scripts/import_assets.py --source /absolute/path/to/tmnt
python3 scripts/build_native.py
```

The importer verifies every file before placing it in ignored Assets. The build
extracts the pinned upstream source into ignored build/reference-source, derives
fixed 68000 and Z80 entries offline from Assets, and compiles arm64 artifacts:

- build/native/libtmnt.a
- build/native/libtmnt.dylib
- build/native/build-manifest.json

Generated program-specific sources are ignored and must remain local. Their
generation is not a claim of complete executed-game coverage. Historical
acceptance reports under Documentation describe the exact measured routes and
their limits. The standalone source companion includes the ROM-free title-song
decoder regression (`python3 scripts/verify_title_audio.py`, after importing
local media); other private test harnesses and the macOS UI are omitted.

For development only, `python3 scripts/build_native.py --reference` builds the
original CPU interpreters into build/reference. The normal native library has
no runtime opcode decoder, JIT, Rosetta dependency or interpreter fallback.

## Embedding

Sources/Native/tmnt_bridge.h defines the C interface. Create one session with
`tmnt_create(absolute_assets_directory, NULL)`, step it at 60 Hz, and consume its
304x224 RGBA pixels and 735 stereo signed 16-bit PCM frames per step at 44.1 kHz.
The board state is a singleton, so only one session may be active per library.
All calls must be serialized by the host. Copy output before the next step if a
renderer or audio callback consumes it asynchronously.

The x/y control range is -1 to +1, with positive y meaning up; the unused third
axis must be zero. Button bits are coin=1, join=2, attack=4 and jump=8. The
four-player board joins on the attack input. `tmnt_set_player` accepts slots 1-4;
the original slots correspond to Leonardo, Michelangelo, Donatello and Raphael.
`tmnt_set_invincible` toggles the narrow original vulnerability-bit gate for
the selected turtle. It does not change health, lives or the protection timer.
The host supplies controller mapping and UI. Every failed step should surface
`tmnt_error`; this engine fails closed on unadmitted or changed instructions.

## Source and license

Read Licenses/README.md and the complete component terms before redistribution.
The pinned upstream source is FBNeo commit
22e2aebcccf888ba9a041bf6023f3381a4fc86dd. Its terms include noncommercial and
source-publication conditions. No alternative blanket license is applied here.
SourceInventory.json lists every packaged payload file's size and SHA-256.
This is a local source package prepared for review; its existence does not imply
that a repository has been created or published.
