# DominoCheat

A ScriptHookRDR2 ASI mod advisor for RDR2's single-player dominoes
minigame (`dominoes_sp`). Built as a sibling of `../PokerCheat` and
`../BlackjackCheat`, reusing the exact same toolchain and conventions --
see those projects' `CLAUDE.md` files for the full backstory on why this
stack (ScriptHookRDR2 + native C++, not an injected mod-menu framework)
was chosen.

**Current status: hand/boneyard/seat chain, "my seat", and legal-move
advice are all live-confirmed. A full blocking/win-the-round advisor and
a real 3D world-space "PLAY THIS ONE" marker on the physical tile are
implemented but NOT yet live-tested (2026-09-13).**
`src/DominoCheat.cpp`'s file header comment documents every struct
offset this mod reads, each with its own confidence rating. Sessions so
far:

1. F12 -> Probe Table Struct/Probe Seat Hands/Probe Boneyard/Dump Full
   Stack JSONL against a real 3-player game confirmed the whole
   Table->Round->SeatsHolder->seats chain and the deck cursor, and caught
   two real bugs: `kSeatStride` was wrong (19, corrected to 44 -- found
   by locating each seat's own occupancy-marker signature in a raw stack
   dump) and the boneyard's packed-bitfield buffer turned out to have a
   header word after all (`kBoneyardSlot` 706 -> 707 -- found by
   brute-forcing which base slot reproduces a real, untouched seat's
   exact 7-tile hand at its known boneyard position). With both fixes,
   decoding all 28 boneyard slots gives 28 unique, valid tiles with zero
   duplicates.
2. A second session traced and confirmed "my seat" -- the same
   real-ped-handle-comparison mechanism Poker/Blackjack both use, not a
   guessed scalar flag (see `FindMySeatByPed()`). The first live attempt
   read all 4 seats' ped handles as 0; grepping a fresh stack dump for
   the exact live `PLAYER::PLAYER_PED_ID()` value found the per-seat ped
   array (`Table.f_1330.f_199`) ALSO has a header word the static trace
   had guessed against -- corrected, then immediately re-confirmed at the
   same table: `ProbeMySeat()` resolved `mySeat=0` with seat 0's ped
   handle reading exactly `PLAYER::PLAYER_PED_ID()` and the other 3 seats
   holding 3 other distinct, real ped handles. `DrawOverlay()` now marks
   your own seat `(you, ...)` and always shows it regardless of
   `ShowOpponentHands`.
3. A third pass (same day, static tracing only -- not yet live-tested)
   found whose-turn tracking (`Round.f_14` read as a bare scalar --
   confirmed by direct read/write call sites, not inference) and each
   seat's accumulated score toward the table's points target
   (`seat.f_2`). Also found hands are NOT capped at 7 tiles -- a seat
   that can't play draws from the boneyard until it can, up to 19 tiles
   -- so `DrawOverlay()`/`ProbeSeatHands()` were widened from the
   original 7-tile assumption to avoid silently truncating a grown hand.
   Also found that the actual tile-placement COMMIT goes through a native
   (`MINIGAME::_0x012027C28F421F46`), meaning the board's own layout
   (placed tiles, open-end pip values) isn't sitting in plain
   script-local memory the way hands/the boneyard are.
4. A fourth pass, prompted by the user going into IDA directly and
   labeling the placement-commit native as `DOMINOES_FUNCTION`, decompiled
   it (and its callees) via a headless `idat.exe` + IDAPython script. That
   confirmed the board really does live in a separate heap-allocated
   "game manager" object (a global pointer, resolvable in principle via
   an AOB scan the same way `GamePointers.cpp` resolves the script-thread
   pool) with up to 4 open-end child nodes -- NOT a PokerCheat-style dead
   end, just a bigger reverse-engineering surface than a script offset.
   But it turned out to be a detour: the same script function that calls
   the placement-commit native (func_168) first calls a SEPARATE,
   READ-ONLY native (`_0x3AE451860F03CA8A`, wrapped by the script's own
   func_347) specifically to find which hand tiles are currently
   playable -- and the script's own consumer functions (func_352/353)
   spell out that native's exact input/output buffer layout by how they
   read it back, with no native disassembly needed at all. **This is now
   implemented**: `MINIGAME::_FIND_PLAYABLE_HAND_TILES` (`ExtraNatives.h`)
   + `FindPlayableTiles()` (`DominoCheat.cpp`) call it for real, and
   `DrawOverlay()` marks your own currently-playable tiles with `*`. NOT
   yet live-tested -- `F12 -> Probe Legal Moves` exists specifically to
   check this against a real hand with known-legal and known-illegal
   tiles in it, the same live-confirm-or-correct loop every other offset
   in this file already went through.
5. Live-confirmed: `FindPlayableTiles()` (playable tiles) and the
   overlay's "Turn: seat N" line both checked correct against real
   gameplay.
6. Built on top of the confirmed pieces, all NOT yet live-tested:
   `DetermineOpenEnds()` (finds the board's real open-end pip values by
   testing synthetic double tiles, in a LOCAL copy, against the same
   confirmed query native -- never touches real game memory) and
   `DetermineBestMove()` (a full blocking/win advisor: an immediate win
   if any legal tile empties your hand, otherwise whichever legal tile
   leaves the fewest opponent tiles able to respond -- exact, not a
   guess, because every session so far has dealt all 4 seats with an
   empty boneyard, meaning every one of the 28 tiles is visible with
   nothing hidden). Also found (and used the same way PokerCheat used
   its own community-card objects): `Scene.f_746[i]`, a REAL 3D prop per
   physical prop slot 0-27 -- `DrawWorldMarkerOnTile()` projects its live
   world position to screen and draws "PLAY THIS ONE"/"WINNING MOVE"
   directly over the physical tile.
7. Two live bugs found and fixed the same day: the recommendation was
   showing on other players' turns too (now gated on `turnSeat ==
   mySeat`), and the 3D marker landed on the wrong tile because
   `Scene.f_746[i]`'s index `i` is a fixed PHYSICAL PROP SLOT, not the
   tile's value -- `EncodeTile()`-as-index was simply wrong. CONFIRMED
   LIVE via a wide raw-field dump (`ProbeTilePropOwnership()`): each
   prop's bare value is `seat+2` when it's in that seat's hand or `6`
   when already played to the board (21 hand-owned + 7 board-owned = 28,
   exactly matching real hand-count deficits from 7), and `.f_3` holds
   the prop's own raw tile VALUE (0-27) -- but only populated for the
   LOCAL PLAYER's own props (a constant sentinel of 28 otherwise).
   `FindTilePropForTileValue()` now searches owner-matched props by
   `.f_3` instead of guessing an index. Also consolidated the F12 menu
   (11 items now, fits one page) by merging the now-fully-confirmed
   `ProbeTileProps` into `ProbeTilePropOwnership` and dropping
   `DumpLocalStackRange` (superseded by the JSONL dump).

Read `DominoCheat.cpp`'s header comment before touching any offset -- it
lays out the full derivation/citation trail (exact line numbers in the
decompile, plus both sessions' live evidence) the same way BlackjackCheat's
own header comment does. Notice the pattern: **every single SCR_ARRAY
this mod has touched so far turned out to have a header word** (seats
array, boneyard, ped array) -- if a new array read comes back all-zero or
garbage, guess "header present" first.

Unlike Poker/Blackjack, dominoes has no existing "hand strength" concept
to advise on -- what this mod does is the same core trick both those
mods started from: read hidden information the human player isn't
supposed to see. Specifically: every occupied opponent seat's real 7-tile
hand, and (when fewer than 4 seats are occupied) the undrawn boneyard
tiles in draw order -- fully deterministic to read ahead, since the whole
28-tile double-six set is shuffled once and fixed before a single tile is
dealt, the same "shuffled once, dealt sequentially" property PokerCheat's
predicted board and BlackjackCheat's deck-ahead prediction both depend
on. With all 4 seats occupied, dealing consumes the entire 28-tile set
(4 x 7 = 28) and leaves nothing in the boneyard to predict.

**Not yet done:** the legal-move advice implemented this session
(`FindPlayableTiles()`, see Status above) tells you WHICH of your tiles
are playable, not WHERE each one would go (which open end) -- the native
that answers that is the placement-COMMIT one
(`MINIGAME::_0x012027C28F421F46`), whose board-layout internals were
decompiled (see `DominoCheat.cpp`'s "Session 3"/IDA writeup) but not
fully mapped to specific field meanings. Also not ported: func_352/353's
own move-preference heuristics (prefer a scoring-bonus tile, else the
highest-pip tile) -- this file currently just marks every legal tile
equally.

## Coding conventions

Same as PokerCheat/BlackjackCheat's own (see either project's `CLAUDE.md`
for the full rationale): no C-style casts, no C-style strings/buffers
(`std::string`/`std::ostringstream` only, except at the literal call-site
boundary into a ScriptHookRDR2 native that requires `char*` --
`const_cast<char*>(str.c_str())` right at that call, never a fixed-size
buffer upstream of it). Logging goes through `Log::Write` (spdlog,
fmt-style `{}` placeholders, compile-time checked -- never printf
`%`-style).

## Build & deploy

```
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" DominoCheat.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal
```

The project's `PostBuildEvent` copies the built `.asi` straight into the
game folder (`E:\SteamLibrary\steamapps\common\Red Dead Redemption 2`).
**RDR2.exe must be closed first** or the copy fails with a file-in-use
error -- check `tasklist //FI "IMAGENAME eq RDR2.exe"` before every build.

A `Debug|x64` configuration also exists (`/p:Configuration=Debug` in the
same command) -- `/MTd` static debug CRT, optimizations disabled, PDB
deployed alongside the `.asi`. This is also the configuration with the
F12 test menu and the `Probe*`/`Dump*` diagnostics (see below) -- Release
enables the advisor unconditionally with no menu at all, same convention
as PokerCheat/BlackjackCheat.

**F12** opens the test menu (NUMPAD 8/2 move, NUMPAD 5 select, NUMPAD
0/Backspace/F12 back) -- chosen specifically so PokerCheat (F10),
BlackjackCheat (F11), and this mod (F12) can all be loaded into the game
at once without a key collision.

Runtime log: `<game folder>\DominoCheat.log`, written by `Log::Write`.

## Tests

`tests/DominoHandEvalTests.vcxproj` unit-tests `src/DominoHandEval.h`
(the tile decode table + pip scoring) in complete isolation from the
game -- plain console app, no ScriptHookRDR2/game dependency, links
against the exact same header the mod itself includes:

```
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" tests\DominoHandEvalTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal
bin\Debug\DominoHandEvalTests.exe
```

Exits 0 and prints `ALL PASS` if every case passes; nonzero with a
`[FAIL]` line per failing case otherwise.

## Source layout

- `src/main.cpp` -- `DllMain`, registers `ScriptMain`. Vendored from
  PokerCheat/BlackjackCheat with only the identifiers renamed.
- `src/script.h` / `script.cpp` -- entry point (`ScriptMain`) and the F12
  menu shell.
- `src/DominoCheat.h` / `.cpp` -- the actual cheat module. **Its file
  header comment in `.cpp` is the single most important thing to read
  before touching struct offsets** -- full derivation/confidence trail
  for every field, same discipline as BlackjackCheat's own.
- `src/DominoHandEval.h` -- self-contained tile decode table (the 28-entry
  double-six index-to-{low,high} mapping, transcribed exactly from the
  decompile's own func_151) + pip-total scoring. Zero game dependency,
  shared by the mod and `tests/DominoHandEvalTests.cpp`. No legal-move/
  best-play logic yet -- see its own header comment.
- `src/ScriptLocal.h` -- a small chainable script-local field/array
  accessor, ported from HorseMenu's own `game/rdr/ScriptLocal.hpp`/
  `ScriptGlobal.hpp` (`..\HorseMenu\src\game\rdr\`) at the user's own
  suggestion (2026-09-13), after the original hand-flattened-constexpr-
  offset approach produced two of this project's three live-confirmed
  bugs (guessing wrong about whether a given array had a header word).
  `DominoCheat.cpp` now builds every struct access as a chain of `.At()`
  calls mirroring the decompile's own field-access syntax one step at a
  time: `.At(fieldOffset)` for a plain nested `.f_N` field, `.At(index,
  elementStride)` for bracket-indexed `[i]` access (which automatically
  applies the "+1 for the array's header word" every such array in this
  file has turned out to need). See its own header comment for exactly
  what this does and doesn't solve -- it does NOT know a struct's total
  size, or the header-word question for a field only ever accessed
  through a raw pointer/native call rather than the VM's own `[i]`
  opcode (the boneyard bitfield); both of those still need live
  confirmation regardless of accessor style.
- `src/scriptmenu.h/.cpp`, `src/keyboard.h/.cpp` -- vendored unchanged
  from PokerCheat/BlackjackCheat (itself adapted from the ScriptHookRDR2
  SDK's NativeTrainer sample), except the F12 toggle key.
- `src/Log.h` -- file logger (`DominoCheat.log`) backed by spdlog
  (`external/spdlog`, header-only), vendored unchanged.
- `src/GamePointers.h/.cpp`, `src/PatternScan.h/.cpp` -- generic
  scrThread-pool resolution / AOB pattern scanning, vendored unchanged
  from PokerCheat/BlackjackCheat (nothing dominoes-specific in either
  file).
- `src/Config.h/.cpp` -- INI-backed HUD toggles (`DominoCheat.ini`), same
  inipp-based approach as PokerCheat/BlackjackCheat's own, trimmed to
  this mod's much smaller toggle set (no board/turn advice to gate a
  toggle on yet).
- `src/ExtraNatives.h` -- `UIDEBUG::_BG_DISPLAY_TEXT`/`_BG_SET_TEXT_COLOR`,
  vendored unchanged. Not currently used by `DominoCheat.cpp` -- its
  `DrawLine()` uses plain `UI::DRAW_TEXT`/`SET_TEXT_COLOR_RGBA` instead,
  same as PokerCheat/BlackjackCheat's own Debug text-panel `DrawLine()`
  (both projects' F10/F11 menus themselves also render through plain
  `UI::DRAW_TEXT` via `scriptmenu.cpp`, which is confirmed working every
  session) -- the UIDEBUG pair is only needed for a custom RDR2 font or
  rich-text `<FONT FACE=...>` tags (see PokerCheat's `DrawFontTest()`
  header comment), neither of which `DrawLine()` here uses.

## External resources

- `D:\Backup\Stuff\RDR2 Shit\Scripts\rdr2-scripts-decompiled\1491.50\script_rel\dominoes_sp.ysc.c`
  -- the actual target, already decompiled for our exact game build
  (1491.50), ~34.7k lines. `act_gen_dominoes.ysc.c` (~41.9k lines) is
  likely the shared generic card/tile-game engine (same
  `act_gen_*`-shared-activity-logic pattern poker_sp/act_gen_poker.ysc.c
  already established) -- check here if turn-order/legal-move logic isn't
  in `dominoes_sp.ysc.c` itself, which is where the board/turn-state
  struct trace should start. `dominoes_launch_sp.ysc.c` is the launcher/
  wrapper; `act_camp_dominoes_light.ysc.c` and `dominoesintro_invite.ysc.c`
  are unrelated camp/invite scenes, not traced.
- `..\ScriptHookSDK\` -- local copy of Alexander Blade's ScriptHookRDR2
  SDK, same shared copy PokerCheat/BlackjackCheat/CollectorOffline use.
  The `ScriptHookRDR2.dll` runtime itself (not redistributed here) must
  be downloaded from http://www.dev-c.com/rdr2/scripthookrdr2/ matching
  game build 1491.50 and dropped into the game folder alongside the
  `.asi`.
- `external\RDR-Classes\`, `external\inipp\`, `external\spdlog\` --
  vendored PLAIN COPIES (not git submodules, unlike PokerCheat's own
  setup) of the same content PokerCheat/BlackjackCheat use, copied
  directly into this project rather than shared -- simpler to set up
  fresh (no `git submodule update --init` needed), at the cost of not
  automatically picking up upstream updates the way a submodule would.
- `..\PokerCheat\docs\JOURNAL.md`, `..\BlackjackCheat\docs\JOURNAL.md` --
  read either for the actual live-probing methodology (trace statically,
  run a `Probe*` menu item in-game, compare the log against the real
  screen, re-derive when wrong) -- this project's own first pass through
  it (2026-09-13) already corrected two offsets; expect more of the same
  once turn/board tracing starts.
- `..\CollectorOffline\CLAUDE.md` -- documents
  `D:\Backup\Stuff\RDR2 Shit\EXEs\1491.50\RDR2_Dumped.exe.i64`, an
  already-analyzed IDA database for this exact build, useful if a struct
  offset ever needs confirming below the script-source level (shouldn't
  be needed here -- everything so far is plain script-local slot
  arithmetic, same as Poker/Blackjack).

## Next concrete step

Both "Probe Legal Moves" and the "Turn: seat N" overlay line are now
CONFIRMED LIVE (2026-09-13) -- see Status above. What's left:

1. **Watch a hand grow past 7 tiles** (draw from the boneyard because no
   hand tile was playable) and confirm `ProbeSeatHands()`'s widened
   (up to 19) tile dump shows the real extra tile(s) rather than garbage.
2. **Trace kSeatActiveFlagOffset's real meaning** (reads 100 when
   occupied, 0 when empty) -- low priority, not blocking anything.
3. The natural next feature is WHICH end to play a legal tile on, and/or
   porting func_352/353's own preference order (scoring-bonus tile
   first, else highest-pip tile) -- both need the placement-commit
   native's board-layout internals (partially decompiled, not fully
   mapped -- see "Session 3"'s IDA writeup) or a second read-only query
   native we haven't looked for yet.

Expect more corrections on anything still marked untraced -- four fixes
so far (`kSeatStride`, the boneyard header word, the ped-array header
word, and the legal-move buffer layout, all confirmed correct on the
first live test) are proof "static tracing agrees with itself" isn't the
same bar as "matches a live memory read," even when it keeps turning out
right.
