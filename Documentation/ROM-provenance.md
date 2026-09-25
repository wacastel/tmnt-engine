# Local ROM provenance

The supplied `rom/tmnt` directory and `rom/tmnt.zip` contain the same 16 files,
4,358,656 bytes in total. Their names, sizes and CRC32 values match the pinned
FBNeo `TmntRomDesc` for **Teenage Mutant Ninja Turtles (World 4 Players, version X)**.
SHA1 and SHA256 identities are recorded in `Verification/rom-identity.json`.

The main program consists of 963-x23.j17 / 963-x24.k17 followed by
963-x21.j15 / 963-x22.k15, interleaved as the original board driver specifies.
963e20.g13 is the Z80 sound program. The remaining files contain tile, sprite,
PROM and sample data. No ROM downloads or alternative game revisions are used.

Import, app staging and app startup each validate local data. Negative tests
reject a missing file, truncated file, appended byte and changed byte. Raw ROMs,
Assets, generated program entries, build products and captures are ignored by
Git. The generated local app alone bundles the media for standalone play.
