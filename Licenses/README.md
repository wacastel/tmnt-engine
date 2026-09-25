# Source and component notices

This project derives from [FBNeo commit 22e2aebcccf888ba9a041bf6023f3381a4fc86dd](https://github.com/finalburnneo/FBNeo/tree/22e2aebcccf888ba9a041bf6023f3381a4fc86dd).
The complete unchanged source archive is in `Tools/ReferenceLab`.
`FBNeo.txt` preserves its full license text, including Final Burn and MAME notices.
`Musashi.txt` and `Z80.txt` preserve the CPU notices. `Component-Notices.txt`
records the compiled board, graphics and audio component attributions; the full
original notices also remain in each source file. `provenance.json` binds these
notices and source files to the archive.

The app translates one 68000 program and one Z80 program offline. The arithmetic,
interrupt and memory semantics derive from Musashi and FBNeo's Z80 core. The
original interpreter cores are used only in the isolated development reference.
The unchanged Konami board driver provides original graphics, sound, timing and
I/O to both builds. The bridge supplies host services and selected-player input;
the offline translator adds the separate, optional player vulnerability gate.

The upstream terms include noncommercial restrictions, publication of source
changes, preservation of notices, and restrictions on ROM distribution. The full
text controls. A ROM-free engine source package is prepared separately from the
private game repository for publication review. It contains no ROMs, generated
program tables, artwork, app binaries, saves, captures or test goldens.

Only sources listed in the build manifest are compiled. The upstream archive
also contains optional components unused by this app. Apple frameworks,
CommonCrypto and system zlib are linked from macOS.
