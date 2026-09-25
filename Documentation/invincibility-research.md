# Original player protection

The supplied world four-player TMNT program consists of the interleaved `963-x23.j17` / `963-x24.k17` and `963-x21.j15` / `963-x22.k15` pairs. Their identity and generated translation are pinned by SHA-256. The [FBNeo cheat database](https://github.com/finalburnneo/FBNeo-cheats/blob/master/cheats/tmnt.ini) identifies the original protection countdown at player-one byte `06202D`, independently of the health and lives fields. The supplied program was disassembled locally to establish what that byte does.

The original player update begins at `01B69E`. It first clears bit one of the player's status byte at `(A0)`, then tests and decrements the signed protection countdown at `A0 + 2D`. Once original protection expires, the instruction at `01B6D8` restores status bit one. The following instruction at `01B6DC` sets display bit seven. Original enemy-to-player collision detection tests status bit one at `04734A`; an unset bit branches past the entire attack before changing health, hurt state, or collision bookkeeping.

Only the native translation entry at fixed PC `01B6D8` is specialized. It still consumes the original immediate and executes the original byte read/write and cycle count. When the native flag applies to the selected player's exact object address, its immediate source becomes zero; otherwise it retains its original value. The following original display instruction updates condition flags normally. This leaves the original vulnerability bit clear while preserving activity, attack, visibility, movement, and the original countdown. No timer, energy, life, scene, enemy, or collision-box freeze is installed. Switching the flag off allows the original update to restore vulnerability on its next eligible update.

The complete surrounding original window, `01B69E` through `01B6DF`, is pinned to SHA-256 `5225fd7898b9ece0b03bf9eb2fbc766db3ae9797c66a1e84c85f57f66581a882`. The native hook accepts only the selected object at `062000 + (player - 1) * 50` hexadecimal. Other players and nonplayer objects use the unmodified operation.

# Validation

`scripts/verify_invincibility.py` links a test-only register/bus helper against the precise object files recorded in each product library's build manifest. It rejects stale sources, headers, or product libraries. The writable test helper is not part of the shipped app.

Bounded fixtures cover original protection values zero, one, two, `80`, `81`, and `FF`, including signed countdown expiration. With the cheat disabled, native and original-interpreter execution must match all 18 observed 68000 registers, elapsed cycles, and the entire main RAM. The protected path preserves the original countdown and life/energy values.

An overlapping enemy attack then executes the original collision routine. Without protection it reduces health from `0A03` to `0A02` and installs hurt state `0400`; with protection it leaves both values unchanged. Each of the four selected turtles is tested, as are all three unselected turtles and a nonplayer object. A single-session toggle fixture checks immediate off/on behavior. Reset retains the user's chosen mode; a new session starts with invincibility off.

Input-only acceptance inserts a coin on frames 601–602 and joins on frames 661–662. Original scene introduction completes before the 1,800-frame observation begins. Naturally approaching enemies attack an idle player. Protected observations cover frames 1,800–4,800 with full initial energy, unchanged lives, and the original protection timer at zero. Invincibility is disabled after frame 4,800; subsequent natural damage and a life loss must occur. This route runs for each of the four turtle slots. An additional unprotected player-one run establishes ordinary vulnerability under the same initial inputs. The acceptance JSON contains the measured frames and values.

These are original protection and natural encounter checks. They do not claim a complete playthrough, every boss-specific or scripted death, or recovery after an already committed death.

# Read-only gameplay fields

Addresses below are logical 68000 addresses. Main RAM is word-swapped in the host allocation; the diagnostic reader accounts for that layout.

| Address / offset | Meaning | Evidence |
| --- | --- | --- |
| `062000 + 50 × slot` | Player object, slots zero through three | Original loop initializes `A0` at `01A434`, then advances by `50` at `01A54E` |
| Player `+00`, bit zero | Active player object | Original update loop tests it at `01A446` |
| Player `+00`, bit one | Vulnerable to original collision routines | Clear at `01B69E`, restore at `01B6D8`, enemy attack test at `04734A` |
| Player `+00`, bit two | Attacking collision object | Player-to-enemy collision entry tests it at `047224` |
| Player `+00`, bit seven | Display state | Original protection routine updates it at `01B6DC` |
| Player `+04` word | Player action/hurt state | Original enemy collision installs `0400` at `0473D6` |
| Player `+08` word | Original energy and residual damage counter | Original enemy damage calculation at `0473F0` through `04740A`; initial natural gameplay value `0A03` |
| Player `+0C` / `+0E` words | Object X/Y used by collision geometry | Original overlap calculations begin at `047354` and `04737C` |
| Player `+2D` byte | Original signed protection countdown | Original test/decrement/expiration at `01B6A2` through `01B6C0` |
| `060101 + 8 × slot` byte | Remaining reserve lives | Independent cheat identifies it; natural deaths decrement it |
| `060120` byte | Scene sequence index | Original scene advance increments it at `006BBA` |
| `060121` byte | Scene type selected from the scene table | Original table lookup stores it at `006306` |

Scene fields also exist during attract/intro sequences. They do not alone establish active gameplay; acceptance also checks a joined, active player, initialized energy, and subsequent natural enemy contact.
