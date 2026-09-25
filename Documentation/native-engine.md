# Fixed native execution

This app follows the prior After Burner II implementation's workflow: verified
local ROM import, offline 68000/Z80 translation, native Apple host, isolated
reference core, differential tests, source audit, standalone bundle, private
source repository with ROM-free publication review for engine changes.

The pinned input is World four-player version X. Its interleaved main ROM is
0x60000 bytes; its Z80 sound ROM is 0x8000 bytes. The offline generators enumerate
fixed program addresses and choose operation semantics and register selectors
at generation time. Immediate operands and memory remain live data; fixed
operation bytes and indexed extensions are verified before execution. Unadmitted
RAM execution or changed operations stop with a diagnostic. The app does not
use Rosetta, a runtime CPU opcode decoder, a JIT, or an interpreter fallback.

Musashi retains its original bus/prefetch, interrupt, register and cycle ABI.
The Z80 translation retains original interrupt, prefix, register and memory
semantics. The generated source is compiled directly by clang to arm64. Generated
program-specific entries stay in ignored files and are recreated from pinned
local media. Generation metadata is under Documentation.

The unchanged FBNeo d_tmnt.cpp driver is included in the native bridge's
translation unit to encapsulate its static RAM and input state. Its original
Konami K052109/K051960 graphics, YM2151, K007232, UPD7759 and title sample rendering
are shared by native and reference builds. Other Konami support functions used
by the common chip lifecycle are retained; only TMNT is registered as a game.

The board driver runs a 60 Hz schedule (8 MHz 68000 and 3.579545 MHz Z80) with
735 stereo sample frames per step. This is the pinned model's timing; no physical
board timing equivalence is claimed. SpriteKit displays the 304×224 result at
4:3. The bridge converts the board's packed 0x00RRGGBB output to opaque
RGBA8888; SpriteKit renders that texture with color blending disabled.
AVAudioEngine drains a single mixed 44.1 kHz stereo stream. The host bounds
catch-up, pauses on focus/sleep/disconnect, and neutralizes input on resume.

A singleton native session owns the board. Reset destroys/recreates the board,
starts from a cold shared Musashi context, restores the chosen turtle and cheat
mode, and clears inputs. A fresh app session starts with invincibility disabled.
Preferences use local.william.tmnt and no earlier game's preferences or saves.

The original-interpreter baseline is built only with `--reference` into
build/reference. Tests use that baseline, but app linking uses only the native
static archive. `scripts/audit_native.py` verifies source identities and checks
reference isolation. These checks supplement measured differential execution;
they are not a formal proof of the complete game.
