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
implemented but NOT yet live-tested (2026-09-13). A same-day release-prep
pass (Session 8 below) rebuilt the entire HUD on the same working
`$Font5`/`UIDEBUG` font pipeline Poker/BlackjackCheat use, gave opponent
hands their own real-font per-seat blocks, gave the blocking/win advisor
a standalone articulated readout, and added full 13-language
localization -- also NOT yet live-tested. A user report the same evening
found the blocking/win advisor's recommendations were legal but still
lost more than expected -- Session 9 below replaced its 1-ply heuristic
with a depth-limited minimax over the fully-known hands. That FIRST
version froze the game outright the same day (per-tick recomputation +
no cost bound, see Session 9's own addendum) -- fixed in two escalating
passes, first a synchronous memoization cache + a hard node budget +
fixed-capacity arrays, then (per user request for a more robust fix) a
full move to a background worker thread (`AsyncMoveAdvisor.h`) so
`DetermineBestMove()` never blocks the game thread on the search at
all. NOT yet live-tested. Two more same-evening changes ARE live-tested
and confirmed (Session 10 below): the boneyard row now draws real tile
icons instead of text, and a new `ProbeDominoSkin()` diagnostic
confirmed `Scene.f_6` really is the table's `dominos_set_N` skin
index. A later pass (2026-09-17, see `DominoCheat.cpp`'s own header
comment's "Session 12" entries for the full detail) replaced the
observational `BoardTracker` mechanism for the "PLAY HERE!" world-space
board marker with a deterministic formula ported from the game's own
ghost-preview code, then found and fixed two live bugs in it: an overly
strict candidate-match filter that suppressed the marker entirely, and a
Z-height bug where the underlying Scene coordinate turned out to be a
floor/anchor reference rather than table height (fixed by reusing a real
tile entity's own height instead). CONFIRMED LIVE now -- both
"PLAY THIS ONE!" and "PLAY HERE!" land correctly on the physical table.
The same pass also removed six F12 diagnostics that had been disabled
since Session 8 for menu space and were no longer needed. The same day
(item 11 in the numbered Session list below), a full 1v1 game against
the scripted NPC opponent --
heavy on boneyard draws on both sides -- gave Session 11's boneyard-draw-
aware deep search its first live test: CONFIRMED LIVE. The search ran
every single decision (no fallback-heuristic label ever appeared in the
log), reported search depths in the 20s consistent with
`DrawUntilPlayable()` correctly collapsing forced-draw plies for both
seats, every recommended move matched what was actually played, and the
round was won.**
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
8. Release-prep pass (2026-09-13, same day) -- four presentation changes,
   none touching a struct offset: (a) switched every Release-facing draw
   call from `UI::DRAW_TEXT`/`SET_TEXT_COLOR_RGBA` (nullsub on this game
   build, the same finding Poker/BlackjackCheat already documented) to
   the `UIDEBUG::_BG_DISPLAY_TEXT`/`$Font5` pipeline via new
   `BgText()`/`DrawBgText()` helpers; (b) replaced the single-panel
   opponent-hand dump with a real-font, Release+Debug per-seat block;
   (c) added a standalone move-advice readout
   (`DrawMoveAdviceStatus()`/`DrawMoveSafetyStatus()`) that articulates
   `DetermineBestMove()`'s recommendation plus a SAFE/RISKY/VERY RISKY
   qualifier (suppressed whenever the boneyard isn't empty, since the
   blocking count is only exact then); (d) added `Localization.h/.cpp`,
   ported from Poker/BlackjackCheat's own, for every string those three
   changes draw. The FIRST version of (b), `DrawSeatHandStatus()`,
   stacked one line per seat in a fixed screen corner keyed by raw seat
   index -- a live user report the same day called this "nonsense" once
   actually seen on screen. Replaced with `DrawOpponentHandStatus()` +
   `ComputeDenseRowForSeat()`, porting PokerCheat's own dense-relative-
   seat-offset technique so each opponent's tiles sit "next to" that
   seat's on-screen name/stack panel instead of a corner list (never
   your own seat, matching PokerCheat's own opponent-only scope -- see
   `DominoCheat.cpp`'s file header comment's "Session 8" entry for the
   full detail on both versions). Revised a THIRD time the same day: the
   per-tile TEXT itself (`FormatTile()`'s `"[low|high]"`) is now real 2D
   tile-face icons, `GRAPHICS::DRAW_SPRITE`'d against the game's own
   `"dominos_set_N"` texture dictionary -- the same asset family
   PokerCheat's `card_set_N` card icons use. CONFIRMED to exist for
   dominoes via `dominoes_sp.ysc.c`'s own `func_267` (`"dominos_set_"+N`)
   and `func_862` (`"DOMINO_<low>_<high>"` per-tile names, matching
   `DominoHandEval::DecodeTile()`'s own numbering exactly) PLUS the
   user's own read of the game's `ui_minigames.txt` asset manifest
   listing `dominos_set_1..6` alongside poker's `card_set_1..9` -- see
   `BuildDominoTileTextureName()`/`FindLoadedDominoSetDict()`'s own
   header comments in `DominoCheat.cpp` for the full citation trail.
   **The opponent-hand tile icons ARE now live-confirmed** (same day,
   2026-09-13) -- the user tuned `OpponentTileIconWidth/Height/SpacingX/
   LabelOffsetX` live via Reload Config against a real table (final
   values: `0.015/0.045/0.015/0.035`, notably smaller/narrower than
   PokerCheat's portrait-card starting guess), confirming both the
   `"dominos_set_N"`/`"DOMINO_<low>_<high>"` asset pair actually renders
   AND that the dense-row rotation direction (`ComputeDenseRowForSeat()`)
   puts opponents in sensible positions for at least the table
   configuration tested. `DrawWorldMarkerOnTile()`'s "PLAY THIS ONE"/
   "WINNING MOVE" world-space text is ALSO live-confirmed now, same
   day -- a live report found the original -0.06f/0 offset sitting over
   the tile's LEFT side instead of centered; `WorldMarkerOffsetX=-0.03`
   (Y unchanged) centers it correctly. Also fixed the same day: the
   advice readout and the world marker were both showing during EVERY
   sub-state of mySeat's own turn (1-6), not just the real decision
   window -- both are now gated on `turnSubState==4` specifically
   (CONFIRMED LIVE), which supersedes `kTurnSubStateFieldOffset`'s
   original "4/5 both mean committing the move" guess. And per a
   further user request, the advice readout and the world marker are
   now two independent `Config` toggles -- `ShowAdvice` and
   `ShowPlayableDomino` -- instead of always showing together. **Still NOT
   live-confirmed**: whether the dense-row rotation direction holds for
   every seat arrangement (only tested from one specific seat so far).
9. Same evening, user report: the blocking/win advisor's recommendations
   were legal but still lost more than expected -- not a bug, a weak
   heuristic. `DetermineBestMove()`'s old logic only ever looked ONE ply
   ahead (minimize `CountPipAcrossOpponents()` against the single
   resulting open end, tie-broken by playing the highest-pip tile
   first), which can hand an opponent a great position moves later
   without ever seeing it coming. Replaced with a new pure-logic header,
   `src/DominoSearch.h` (same "zero game-dependency, unit tested in
   isolation" convention as `DominoHandEval.h`): a depth-limited
   PARANOID minimax (alpha-beta pruned; treats all three opponents as
   one adversary that always plays whichever of their own legal replies
   is worst for you) over the fully-known hands, sound specifically
   because a full 4-seat, boneyard-empty game has zero hidden
   information left to guess at -- the only real uncertainty is what an
   opponent chooses to play, which the paranoid assumption resolves as a
   worst-case-safe guarantee rather than an average-case guess. Also,
   per a user question mid-session: `DetermineBestMove()` no longer
   calls `FindPlayableTiles()` (the native legal-move query) at all --
   once `DetermineOpenEnds()` gives the open pip set, "does this hand
   tile match an open pip" is plain local logic, exactly the same check
   the fallback path already did for `endCount==0`, so the native call
   was pure redundancy for this one function (its OTHER call sites --
   the HUD's own `*` marker, `ProbeLegalMoves()` -- are untouched and
   still use it). The deep search only runs when every hand is fully
   known (all 4 seats dealt, boneyard empty -- the same scope
   `CountPipAcrossOpponents()`'s own header comment already draws); a
   real boneyard or the very first move of a round (no open ends yet)
   falls back to the original 1-ply heuristic, now with its own
   redundant native call similarly removed. Added
   `tests/DominoHandEvalTests.cpp` cases for the new header, including
   one built directly from the failure mode that motivated this session:
   two candidate replies that look EQUALLY safe one ply out, where the
   old heuristic's pip-total tie-break picked the one that (two plies
   later) hands the opponent their double and an outright win -- the new
   minimax correctly avoids it. Builds clean (Debug + Release) and all
   unit tests pass; the search itself is NOT yet live-tested against a
   real table, and its default 8-ply search depth is a starting guess,
   not tuned against real frame-time.
   **Live bug, same day: running "Probe Best Move" froze the game.**
   Root cause was two-fold, both now fixed: (1) `DrawOverlay()` calls
   `DetermineBestMove()` every single TICK for the entire real-world
   decision window (however long the player looks at the screen, gated
   on `turnSubState==4`), not once per turn -- so the very first
   version was rebuilding a fresh depth-8 search 30-60 times a second,
   which alone is enough to grind the game to a halt; a plain 1-ply
   heuristic had been cheap enough that nobody had ever noticed this
   per-tick recomputation was happening at all. Fixed by memoizing
   `DetermineBestMove()` against the last hand/open-ends/boneyard state
   it computed for (a function-local `static` cache) -- the search now
   only actually runs once per real decision. (2) `DominoSearch.h` had
   no hard bound on total search cost -- `std::vector` everywhere meant
   every node heap-allocated (a `GameState` copy of 4 vectors plus a
   fresh move-list vector, per node), and depth 8 had no cap on
   branching, so a branchy board had no worst-case guarantee at all.
   Rewrote the whole header around fixed-capacity arrays (zero heap
   allocation anywhere in the search loop) and added a hard
   `nodeBudget` (default 100k node visits, `kDefaultNodeBudget`) that
   `Search()` decrements globally across the whole call and short-
   circuits on exhaustion -- bounds worst-case wall-clock regardless of
   how bad branching gets, independent of the memoization fix. Existing
   unit tests needed only mechanical updates (fixed-array state
   construction instead of `std::vector`); all still pass. Builds clean
   (Debug + Release), redeployed -- **still NOT live-tested against a
   real table** (the freeze WAS the live test; this is the fix, not yet
   itself confirmed not to freeze).
   **Same day, user request for a more robust fix than tuning the
   bound**: moved the deep search off the game thread entirely onto a
   dedicated background worker, `src/AsyncMoveAdvisor.h` (new, generic,
   templated on a caller-supplied `Key` type). The natural thread
   boundary is exactly the one `DominoSearch.h` already had by design --
   `GameState` has zero game-memory dependency, so it's exactly as safe
   to hand to another thread as any other plain value; only the LIVE
   READ into that snapshot has to stay on the game thread (no documented
   locking exists for touching `scrThread`/script-local memory from a
   second thread while the game keeps ticking). `DetermineBestMove()`'s
   deep-search branch now: builds a `DecisionKey` (a cheap, comparable
   snapshot of "what real-world decision is this for" -- seat, hand,
   open ends, deck cursor) and the `GameState` snapshot as before, checks
   `AsyncMoveAdvisor::GetLatest()` (non-blocking) for a published result
   whose key matches, and either returns it immediately or calls
   `SubmitJob()` (also non-blocking) and reports no recommendation for
   THIS tick -- deliberately never a stale one for a hand that's already
   moved on. `DetermineBestMove()` itself now never blocks on the search
   at all; the worker typically publishes within a few milliseconds
   (bounded by `DominoSearch::kDefaultNodeBudget` regardless of board
   complexity), an unnoticeable gap against a human decision. Superseded
   the previous synchronous memoization cache entirely (the async
   advisor's own published-result cache does the same job). Added a
   concurrency sanity test (`TestAsyncAdvisorPublishesMatchingResult`,
   using a real background thread and a 2-second poll timeout) since the
   real in-game plumbing can't be exercised from the test project --
   confirms the worker actually runs, publishes under its own mutex
   correctly, and agrees with what `FindBestMove()` gives synchronously
   for the identical input. Builds clean (Debug + Release), all tests
   pass, redeployed. **Still NOT live-tested against a real table** --
   this is the third iteration of the same fix in one day and deserves a
   real live check before being trusted.
10. Same evening, two more small user-requested changes, both CONFIRMED
    LIVE (2026-09-13): (a) `DrawBoneyardStatus()` was rewritten to draw
    the same real 2D `"dominos_set_N"`/`"DOMINO_<low>_<high>"` tile-face
    icons `DrawOpponentHandStatus()` already draws, replacing its old
    plain `FormatTile()` text list -- new `Config` fields
    `BoneyardTileIconLabelOffsetX/SpacingX/Width/Height`, seeded from
    `OpponentTileIcon*`'s own confirmed values as a starting point.
    `LabelOffsetX` needed its own retune (0.035 sat the icon strip too
    close to the "Boneyard (N):" label; 0.055 clears it), user-tuned
    live via Reload Config and now the default; `SpacingX/Width/Height`
    reused the opponent row's values as-is, unretuned but visually fine
    against the same sprite asset. (b) A user question -- "is it
    possible to figure out which dominos_set_ the current table uses"
    -- led to tracing `Scene.f_6` (`kSceneDominoSkinFieldOffset`,
    `DominoCheat.cpp`) via `func_267`/`func_60`/`func_59`/`func_2`, plus
    a new `ProbeDominoSkin()` F12 diagnostic built specifically to
    settle a static-trace ambiguity the derivation couldn't resolve on
    its own: the one real call site of `func_59` appears to omit its
    trailing `iParam5` argument, which (if RAGE script's own default-
    to-0 convention applies) would make `Scene.f_6` always read 0
    regardless of the real table. Live result: `Scene.f_6=5` against a
    real `"dominos_set_6"` table, matching `FindLoadedDominoSetDict()`'s
    own independently-streamed answer exactly -- MATCH, both confirming
    `Scene.f_6` really is the skin index AND disproving the "always 0"
    theory (the real per-location wiring reaches this field some way
    the static trace didn't find). `kSceneDominoSkinFieldOffset` and
    `ProbeDominoSkin()`'s own header comments in `DominoCheat.cpp`/`.h`
    are both updated to CONFIRMED LIVE.
11. Same day (2026-09-17), the first live test against a full 1v1 game
    (you vs. the scripted NPC opponent, seat 1) with a heavily-drawn
    boneyard -- both seats repeatedly ran out of legal moves and had to
    draw multiple tiles mid-turn. This was specifically Session 11's
    boneyard-draw modeling's first live exercise (previously "modeled,
    NOT yet live-tested" per that section). CONFIRMED LIVE: the deep
    search (`DominoSearch.h`'s `DrawUntilPlayable()`) ran on every single
    decision the entire game -- no fallback-heuristic label ever appeared
    in `DominoCheat.log` -- and the advice readout's reported search
    depth climbed into the 20s (`@depth 24` down to `@depth 12` as tiles
    were consumed), only explainable by the tree correctly collapsing
    through the known, forced draw sequence for BOTH seats, not just
    mySeat's. Every recommended move matched what was actually played
    (`prediction MATCH`, every turn, no exceptions), and the round ended
    in a win (`WINNING MOVE [6|6]`, hand emptied). Still open from this
    pass: the points-target/score reads and the scoring-mode (All Fives/
    Threes) opponent model were NOT exercised (this table was Draw
    rules) -- see "Decision engine (Session 11)" and "Next concrete
    step" below.

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
own move-preference heuristics (prefer a scoring-bonus tile) -- the
pip-total tiebreak IS now ported (`DominoSearch.h`'s own tie-break), but
"prefer a scoring-bonus tile" specifically needs board-layout fields
nobody has mapped.

## Coding conventions

Same as PokerCheat/BlackjackCheat's own (see either project's `CLAUDE.md`
for the full rationale): no C-style casts, no C-style strings/buffers
(`std::string`/`std::ostringstream` only, except at the literal call-site
boundary into a ScriptHookRDR2 native that requires `char*` --
`const_cast<char*>(str.c_str())` right at that call, never a fixed-size
buffer upstream of it). Logging goes through `Log::Write` (spdlog,
fmt-style `{}` placeholders, compile-time checked -- never printf
`%`-style).

**Strings: `std::string_view` for read-only text.** `Localization`'s
tables are `constexpr std::string_view` and its getters return
`std::string_view`. Per-frame Release HUD text must not allocate: build it
into a reused `static std::string` (`BgText`, `AppendInt`, `AppendTile`) rather than
concatenating temporaries or using `ostringstream`. Debug-only text can keep
`std::string`/`ostringstream`.

## Build & deploy

```
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" DominoCheat.vcxproj /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal
```

The project's `PostBuildEvent` auto-locates the RDR2 install directory
(`BuildTools\Find-RDR2GameDir.ps1` -- vendored identically into every
sibling project, since each is its own separate git repo) and copies the
built `.asi` straight into it, on every build regardless
of whether the build itself was up to date. `DisableFastUpToDateCheck` is
set in the tracked `.vcxproj` so this also holds for Visual Studio IDE
builds, not just command-line MSBuild. **RDR2.exe must be closed first**
or the copy fails with a file-in-use error -- check
`tasklist //FI "IMAGENAME eq RDR2.exe"` before every build.

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

Runtime log: `<game folder>\DominoCheat.log` -- or
`%LOCALAPPDATA%\RDR2ASIMods\DominoCheat.log` when the game folder isn't
writable (e.g. a C:\Program Files install; the file's first line then names
the rejected path). See `src/LogFallback.h`, vendored identically into every
sibling project.

## Tests

`tests/LogFallbackTests.vcxproj` checks that logging falls back to
`%LOCALAPPDATA%\RDR2ASIMods\` instead of throwing when the game folder can't
be written (it points the logger at `C:\Windows\System32` -- skipped when run
elevated -- and at a path through a regular file). Same test, vendored into
every sibling project:

```
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" tests\LogFallbackTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal
bin\Debug\LogFallbackTests.exe
```

`tests/DominoHandEvalTests.vcxproj` unit-tests `src/DominoHandEval.h`
(the tile decode table + pip scoring) AND `src/DominoSearch.h` (the
depth-limited minimax, added Session 9) in complete isolation from the
game -- plain console app, no ScriptHookRDR2/game dependency, links
against the exact same headers the mod itself includes:

```
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" tests\DominoHandEvalTests.vcxproj /p:Configuration=Debug /p:Platform=x64 /nologo /v:minimal
bin\Debug\DominoHandEvalTests.exe
```

Exits 0 and prints `ALL PASS` if every case passes; nonzero with a
`[FAIL]` line per failing case otherwise.

**Recorded games as tests.** The Debug build appends a JSONL game log,
`DominoCheat_games.jsonl`, next to `DominoCheat.log` (rolls over at 4 MB
to `.1.jsonl`). Each line is a `decision` (my move while advice was shown:
the search's full input, the advice, what I played, whether it went on
the recommended end), an `npcMove` (an opponent play the scripted-AI
model predicted: hand, open ends, the native candidate list in order,
predicted vs played) or a `round` (final hands, scores before/after,
target, buy-in, per-round tallies). A round line is written at the next
deal, or when the table's script ends (`"endedBy":"tableExit"`, scores =
last seen; the round may be unfinished if the player left mid-round). To pin a real position as a
regression test, copy its line into `tests/fixtures/games.jsonl` and add
`"expectTile":[low,high]` (plus optional `"expectEnd"` on a decision);
`TestRecordedGames` replays decisions through `FindBestMove()` and NPC
moves through `DominoAiPolicy::SelectCandidate()`. Format:
`src/GameRecord.h`. Quick summaries: `jq -s` over the file, e.g. NPC
match rate per rule set from `npcMove` lines.

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
  best-play logic itself -- see `DominoSearch.h` below for that.
- `src/DominoSearch.h` -- pure-logic depth-limited PARANOID minimax
  (alpha-beta pruned) over fully-known hands, added Session 9
  (2026-09-13) to replace `DetermineBestMove()`'s original 1-ply
  heuristic. Zero game dependency (built on `DominoHandEval::Tile`
  alone), same isolation convention as `DominoHandEval.h`, unit-tested
  in `tests/DominoHandEvalTests.cpp`. See its own file header comment
  for the full rationale (why paranoid minimax is sound specifically
  when nothing is hidden) and explicit scope limits (board modeled as a
  SET of open pip values, not exact end-count/topology; boneyard draws
  not modeled at all).
- `src/AsyncMoveAdvisor.h` -- generic background-worker wrapper around
  `DominoSearch::FindBestMove()`, added same day as `DominoSearch.h`
  once the synchronous version froze the game live. Templated on a
  caller-supplied `Key` type (DominoCheat.cpp's own `DecisionKey`) so
  this file stays agnostic of DominoCheat.cpp's live-memory-reading
  specifics. `DetermineBestMove()` is the only caller; see its own
  comment for exactly where the thread boundary sits and why it's safe.
  Has its own concurrency sanity test in
  `tests/DominoHandEvalTests.cpp` (a real background thread, polled with
  a timeout) since the real in-game call pattern can't be exercised
  outside the game.
- `src/GameRecord.h` -- the game log's JSONL line writer/flat reader
  plus `GameState`/candidate-list (de)serialization, ported from
  BlackjackCheat's `RoundRecord.h`. Zero game dependency; shared by the
  mod's Debug-only `GameRecorder` (`DominoCheat.cpp`) and the fixture
  replay test.
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
  inipp-based approach as PokerCheat/BlackjackCheat's own. Now also holds
  a `Language` override (see `src/Localization.h/.cpp` below) and, Debug-
  only, the real-font HUD's placeholder screen positions
  (`OpponentHandBaseX/Y/StepY`, `BoneyardX/Y`, `MoveAdviceX/Y`).
- `src/Localization.h/.cpp` -- ported from Poker/BlackjackCheat's own
  Localization files (2026-09-13 release-prep pass). Localizes every
  string the real-font HUD draws (move-advice headline, SAFE/RISKY/VERY
  RISKY qualifier, world-space tile markers, opponent-hand row's "Seat N"
  header word, boneyard label) across the same 13 languages
  `LANGUAGE::_GET_CURRENT_LANGUAGE_ID()` supports, auto-detected with a
  `DominoCheat.ini` `[General] Language` override. Tile notation itself
  (`[3|5]`) stays language-agnostic digits.
- `src/ExtraNatives.h` -- `UIDEBUG::_BG_DISPLAY_TEXT`/`_BG_SET_TEXT_COLOR`,
  vendored unchanged. As of the 2026-09-13 release-prep pass, this IS
  used -- `DominoCheat.cpp`'s `BgText()`/`DrawBgText()` helpers wrap it
  with `$Font5` rich text for every Release-facing TEXT draw call (the
  opponent-hand row's "Seat N" label in `DrawOpponentHandStatus()`,
  `DrawBoneyardStatus()`, `DrawMoveAdviceStatus()`,
  `DrawMoveSafetyStatus()`, `DrawWorldMarkerOnTile()`), the same working
  replacement Poker/BlackjackCheat already documented for plain
  `UI::DRAW_TEXT`/`SET_TEXT_COLOR_RGBA` being nullsub on this game
  build. The opponent-hand tiles THEMSELVES are real 2D sprites drawn
  via plain `GRAPHICS::DRAW_SPRITE` (a different, already-confirmed-
  working native, same one Poker/BlackjackCheat use for their own card
  icons) against the game's own `"dominos_set_N"` dictionary -- see
  `BuildDominoTileTextureName()`/`FindLoadedDominoSetDict()` in
  `DominoCheat.cpp`. `DrawLine()`'s raw Debug diagnostic panel is the
  one thing still on the old plain text pipeline -- harmless there since
  it never rendered in Release anyway and stays Debug-only now (see
  `DrawOverlay()`'s own comment).

## Scripted opponent policy (build 1491.50)

The supplied `dominoes_sp.ysc.c` exposes a deterministic move-selection
path: `func_76` invokes `func_168`, which uses `func_352`, then falls back
to `func_353`. The opening path in `func_351` uses the same selectors.
There is no RNG call on this selection path (the script does use RNG
elsewhere, including shuffling and presentation).

- `func_352` (lines 14761-14798) chooses the largest nonzero scoring
  resulting-end total. `func_614` (23907-23931) recognizes multiples of
  five or three according to the table rule -- nonzero, with NO `> 0`
  test, so the negative totals the native reports for some spinner-side
  spots count as scoring (-5 on All Fives). Found live 2026-09-25 (the
  one mispredicted NPC move in a 4-seat All Fives game, now pinned in
  `tests/fixtures/games.jsonl`); `DominoAiPolicy::IsAiScoringTotal()`.
- `func_353` (14800-14835) falls back to the highest tile pip sum, using
  `func_615` (23933-23936).
- Both selectors replace the winner on equal rank: the **last valid
  native candidate** wins. A scoring tie does not use tile pips as a
  secondary criterion. `func_613` requires a valid hand index and a
  nonzero placement descriptor (candidate words f_1/f_2).
- `func_324` (14062 onward) advances seats in the order **0, 2, 1, 3**,
  skipping unoccupied seats. The old numerical-order simulation was
  incorrect for multi-opponent games and has been corrected.

`src/DominoAiPolicy.h` implements the selector independently of game
memory. Live snapshots read the rule from `Round.f_666.f_3`, exactly the
chain passed to `func_352` by `func_168`. IDs: Block=-1617663169,
Draw=-1360983891, All Threes=-382896522, All Fives=-1234859967.
This new field read is statically traced but not yet live-confirmed.

With known hands and an empty boneyard, Block/Draw search now excludes
lower-pip NPC replies. It still searches **all equal-ranked placements**
adversarially: our generated move order is not evidence of native order.
If the generated candidate count exceeds the native's 15-entry capacity,
the policy filter is not applied. Unknown modes and All Threes/Fives keep
the conservative opponent model in future search because their exact
resulting-end totals cannot be computed from a set of distinct open pips.
No guessed end sum or guessed native tie-break is used to claim a win.

Debug F12 -> **Probe Best Move** additionally logs each opponent's choice
from the current native candidate list, including hand index, tile,
candidate index, and scoring total. These are **current-board-only**
predictions: after the player places a tile, native candidates and totals
can change. The read-only native is called only on ScriptMain. No native
placement/commit call is made, and the supplied decompiled file is not
modified.

Regression tests cover native-order ties, scoring/fallback selection,
candidate validity, every occupied-seat layout, and generated small
endgames against an independent exhaustive policy solver. Prediction
quality is still conditional on the existing board abstraction; live
comparison of logged candidates with actual NPC moves remains necessary.

## Decision engine (Session 11, 2026-09-13)

A deep review of `DominoSearch.h` found the real reason live advice
still lost: the evaluation was win/loss/tie only, and because the
scripted opponent policy collapses the tree, the search solves most
positions from the first move -- so every move in a solved-lost
position scored identically and the choice fell through to the
highest-pip tiebreak, i.e. the original 1-ply heuristic in disguise.
Rewritten (all unit-tested against an exhaustive oracle). The boneyard-
draw modeling below and the deep search's general operation under it
were live-tested 2026-09-17 in a 1v1 Draw-rules game with heavy boneyard
consumption -- CONFIRMED LIVE (see the numbered Session list's item 11
above). The net-points evaluation's game-outcome awareness (points
target/score), the root scoring bonus, and the scoring-mode (All Fives/
Threes) opponent model were not exercised by that table and remain
untested:

- **Net-points evaluation** in the game's own payout, traced from
  `func_169`/`func_343`/`func_357`: the seat that dominoes (or, on a
  block, the UNIQUE lowest rounded total -- a tie pays nobody) is paid
  every other seat's total; All Fives/All Threes round each total to
  the nearest multiple of 5/3 (`DominoAiPolicy::RoundedPipTotal`).
  Score = (my gain - winner's gain) with a game win/loss (target
  reached) dominating, and a shorter-win/longer-loss tiebreak below
  the points scale.
- **Boneyard draws modeled** from the known draw order (`func_166`/
  `func_611`: draw until playable, boneyard empty, or 19-tile cap) for
  every non-Block rule set, so 2/3-seat tables get the real search.
  Under Block the boneyard is never touched. The 1-ply fallback and the
  `deckCursor >= 28` gate are deleted; the opening move is searched too.
- **Game-outcome awareness**: `seat.f_2` (accumulated score) and
  `Round.f_666.f_14[0]` (points target, `kPointsTargetFieldOffset`,
  header-word convention) feed the snapshot. Both are statically traced
  only; `DetermineBestMove()` drops them for the decision if the target
  isn't 10..1000 or any score isn't in `[0, target)`, and
  `ProbeBestMove` logs the raw values for a live check.
- **Root scoring bonus** on All Fives/All Threes: the native candidate
  list's `f_4` (resulting end total) for the local player's own hand
  credits an immediate scoring play (`QueryNativeCandidates()`, shared
  with `LogOpponentPredictions()`). Deeper scoring plays remain
  unmodeled; scoring-mode opponents are still searched paranoidly.
- **Exactness**: an iteration whose explored tree never hit the depth
  horizon is exact and stops deepening (replaces the old "score
  magnitude means solved" rule). Root moves are re-ordered by the
  previous iteration and searched with a real alpha window.
- **HUD**: the advice line appends the line's net points (`+12`, `-8`,
  `~` prefix = horizon estimate); SAFE/RISKY/VERY RISKY now mean
  "wins under the model" / "undecided or tie" / "every line loses,
  this is the least bad".

Benchmarks (scratch harness, 1 s budget, 7-tile hands, first move):
Block/Draw 4-seat solves in ~1 ms; paranoid (All Fives/Threes/Unknown)
3-seat solves in <150 ms, 4-seat reaches ~25 plies unsolved at the
first move and is exact later in the round.

## Advisor runtime

`DominoCheat.ini`, next to the ASI, has an `[Advisor]` section with
`WallClockBudget=1000` -- the maximum wall-clock allowance per decision
in milliseconds (the earlier Low/Medium/High preset names are gone).
Values are clamped to [50, 30000]; `0` (or any negative value) means
unlimited -- no deadline, the worker deepens until the position is
solved or the decision is cancelled. That's safe because the search never
touches the game thread; the cost is one core busy for as long as the
decision window stays open, which matters mainly for 4-seat All
Fives/Threes tables (paranoid search, not solved at the first move).
Use the Debug F12 menu's
Reload Config action after editing; Release loads settings when the mod
loads (restart/reload the mod to apply edits).

The worker publishes an initial fallback and each completed search depth
while refining advice. It stops on a solved position, deadline, or
cancellation; incomplete iterations never replace completed advice.
Runtime is elapsed wall-clock time starting when the worker begins the
search, excluding queue time. Deadlines are cooperative, not hard
real-time guarantees under OS scheduling.

Production no longer uses the inherited 100,000-node or eight-ply caps.
It deepens toward a finite game-tree bound within the selected allowance.
The node-budget API remains for deterministic standalone tests only.
Identical pending, running, or completed decisions are not restarted every
frame. A changed position or runtime cancels/replaces the old job.

Evaluation runs only during the player's confirmed decision window
(`turnSeat == mySeat`, `turnSubState == 4`) with at least one advice display
enabled. Turn end, table exit, disabling the mod/advice, or invalid hand
reads cancel current work and clear published advice. The worker sleeps
between requests; it does not speculate during opponents' turns. Search
also cooperatively cancels during worker teardown. All game-memory reads
remain on ScriptMain; only immutable snapshots cross to the worker.

The existing distinct-open-pip board approximation and scoring-variant
limitations remain unchanged (boneyard draws themselves ARE modeled --
see Session 11 above -- and that modeling is now live-confirmed,
2026-09-17, 1v1 Draw rules, heavy boneyard use). Runtime presets and
worker lifecycle have standalone regression coverage; broader live game
testing (other seat counts, All Fives/Threes rules, the points-target/
score reads) is still needed. Older session notes describe the
superseded node-capped worker.

## Releasing

Add a `## [X.Y.Z] - date` entry to `docs/CHANGELOG.MD`, commit it, then push an
`X.Y` tag (`git tag -a X.Y -m "X.Y.Z - summary"`, `git push origin
master X.Y`). `.github/workflows/release.yml` then runs every unit test
project under `tests/`, builds the Release `.asi` on a GitHub Windows runner
(deploy step off), zips it as `DominoCheat-X.Y.zip` (the `.asi` plus the default `DominoCheat.ini`), publishes a GitHub release named
"DominoCheat X.Y.Z" with that changelog entry as its notes and the zip and `.asi`
attached, then uploads the zip to Nexus Mods as a new version of the
mod's main file (Nexus-Mods/upload-action, pinned by commit): it archives
the old version, sets the mod's version to the tag and adds the changelog
entry (one line per bullet). That needs the `NEXUS_API_KEY` repo secret;
the mod/file ids are in the workflow's env block (`NEXUS_MOD_ID` is the v3
API id, not the mod page number). To release an existing tag again, use
"Run workflow" on the Actions tab and enter the tag; tick "nexus" only if
it should be uploaded to Nexus again too.
The projects target toolset v145 (VS 2026); if the runner only has an older
Visual Studio, the workflow builds with v143 instead.

`bin\<Configuration>\DominoCheat.ini` is generated on every Debug/Release build
(`GenerateDefaultIni` in the .vcxproj): it builds and runs
`tools/IniGen`, which runs the real `Config` code in an empty folder, so
the INI always has the current defaults and each configuration's own keys
(Debug-only ones only in Debug). It isn't deployed to the game folder, so
the INI you're testing with there is never overwritten.

## External resources

- `D:\Backup\Stuff\RDR2 Shit\Scripts\1491.50\script_rel\dominoes_sp.ysc.c`
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

1. DONE (2026-09-25): the search is CONFIRMED LIVE on 2-, 3- and
   4-seat tables, Draw and All Fives, with the exact board. The one
   remaining rule set is All Threes, only played at Blackwater, which
   the player can't reach yet.
2. **Watch a hand grow past 7 tiles** (draw from the boneyard because no
   hand tile was playable) and confirm `ProbeSeatHands()`'s widened
   (up to 19) tile dump shows the real extra tile(s) rather than garbage.
3. DONE (2026-09-25, static trace): seat.f_1 is the seat's buy-in in
   cents (`kSeatBuyInOffset`, formerly `kSeatActiveFlagOffset`), set
   from `Round.f_666.f_6`; `SeatsHolder.f_5` is the pot. Logged by the
   new `Probe Rules & Scores` item and at every new deal.
4. DONE (Session 11): boneyard draws are modeled and the 1-ply
   fallback is gone. LIVE-CONFIRMED 2026-09-17 (item 11 above, 1v1 Draw
   rules, heavy boneyard use): the search runs every decision without
   falling back, correctly collapses forced-draw plies for both seats,
   and its recommendations held up for a full won round. Still open from
   that pass: live-confirm the points-target/score reads (`ProbeBestMove`
   logs them) and, the
   biggest remaining lever, the scoring-mode opponent model -- the AI
   prefers a scoring placement when one exists, which needs the
   board's real end total; `LogOpponentPredictions` already logs each
   opponent's native candidate totals, so comparing those against the
   moves the NPCs actually make would show how often the paranoid
   fallback is wrong. Separately, WHICH end to play a
   legal tile on when a real board has independent same-value ends, and
   porting func_352/353's own "prefer a scoring-bonus tile" step, both
   still need the placement-commit native's board-layout internals
   (partially decompiled, not fully mapped -- see "Session 3"'s IDA
   writeup) or a second read-only query native we haven't looked for yet.
5. Finish calibrating Session 8's real-font/real-icon HUD -- the
   opponent-hand tile icons (`OpponentTileIconWidth/Height/SpacingX/
   LabelOffsetX`) are DONE, user-tuned live (2026-09-13, see Status
   above for the final values), which also incidentally confirmed
   `FindLoadedDominoSetDict()`/`BuildDominoTileTextureName()`'s
   `"dominos_set_N"`/`"DOMINO_<low>_<high>"` asset pair actually renders
   correct tile faces and that `ComputeDenseRowForSeat()`'s rotation
   direction put opponents in sensible spots for at least the seat
   tested. (a) DONE (2026-09-25): `OpponentHandBaseX/Y/StepY`,
   `BoneyardX/Y` and `MoveAdviceX/Y` -- user confirmed the HUD layout
   looks right in game at the current defaults. (b) DROPPED (2026-09-25):
   checking `ComputeDenseRowForSeat()`'s direction from another seat
   needs the player to match seat numbers to on-screen characters, which
   the game never shows; see the session summary below. (c) DONE: `WorldMarkerOffsetX/Y/
   FontSize` were already confirmed centering "PLAY THIS ONE!"/"WINNING
   MOVE" correctly (Session 8, `WorldMarkerOffsetX=-0.03`). A separate,
   later bug in the newer "PLAY HERE!" board marker (added Session 12)
   was found and fixed 2026-09-17 -- not an offset-tuning issue at all,
   but Scene's own base coordinate sitting ~0.82 units below real table
   height; fixed by reusing a real tile entity's own Z instead of
   trusting Scene's. Both world-space markers are now CONFIRMED LIVE
   correct -- see `DominoCheat.cpp`'s header comment's "Session 12"
   entries for the full story.

**Live-test checks (2026-09-25, Debug only):** F12 now also has `Probe
Rules & Scores` and `Probe Seat Hands`, and the per-tick Debug trace logs
`CHECK` lines on its own: rules/target/scores/buy-in at every new deal, turn order against `DominoSearch`'s `NextSeat()`,
every mid-round draw against the boneyard's draw order (plus a full
validity check once a hand passes 7 tiles), and frame time during your
decision window vs. the rest of the game. `grep CHECK DominoCheat.log`
after a session.

**Advised end (2026-09-25, NOT yet live-tested):** the "PLAY HERE" marker
used the first native spot for the tile, ignoring which end the advice
chose. `PickAdvisedCandidate()` now keeps only spots on the advised end
(`CandidateEndPip()`: a synthetic `[e|x]`, x not open, lands on exactly
end e's grid spots) and takes the highest resulting total among them. The
All Fives/Threes root bonus is now per end too
(`GameState::rootMoveBonusPoints[hand][RootBonusSlot(end)]`). Also fixed:
an empty board read as all 7 numbers open, so the opening move was
searched on a 7-ended board.

**Exact board (2026-09-25, CONFIRMED LIVE):** `BoardTracker`
(`DominoCheat.cpp`, all builds) rebuilds a `DominoSearch::Board` (each
end's pip, doubles, the spinner) from the game's own placement log,
CONFIRMED against a live stack dump: `SeatsHolder.f_6` word 0 = end total
(recounted after every placement), `f_6.f_1[i]` = SCR_ARRAY[28] of
5-word records `{low, high, gridX, gridY, orientation}` in play order,
`f_6.f_142` = placed count (records past it are last round's).
`DominoSearch::BoardReplay` attaches each tile to the nearest fitting end
on the grid; the replayed total must equal word 0 every tick or the
exact board is dropped for the round. Works mid-round. Rules (first
double = spinner, even mid-round -- confirmed; double end = 2x; unplayed
spinner side = 0; native f_4 quirk on spinner sides) are pinned by
`TestBoardRulesReplay`/`TestBoardReplayFromPlacementLog`. With the exact
board the search scores every placement and restricts All Fives/Threes
opponents to the scripted AI's choice; without it the old model applies.
`DetermineOpenEnds()`'s synthetic-double probe was seen live to MISS an
end (a double won't fit next to a `[5|5]` end the game still accepts
`[2|5]` on), which is why the tracker's open numbers now replace it.
Live result (3 full 3-seat All Fives matches, won): every placement's
replayed total matched the game's, 47/47 decisions had the exact board,
85/85 NPC moves predicted, and the only model mismatch (the native f_4
quirk also covers the first long side of a double opener) is fixed.

**Session summary (2026-09-25).** CONFIRMED LIVE: rules hash, points
target, buy-in/pot (`seat.f_1` = buy-in cents, `SeatsHolder.f_5` = pot),
seat scores (they update mid-round on All Fives), turn order 0 -> 2 -> 1
(3-seat), boneyard draw order, hands past 7 tiles, frame time (no hitches
at `WallClockBudget=1000`), the scripted All Fives AI (105/105 moves),
the advised-end marker, and the exact board. The player's raw seat
varies by chair (seats 0, 1 and 2 seen). A later session the same day
(seat 0, seats 0/1/3 occupied with seat 2 EMPTY -- the first layout where
turn order had to skip a seat, 0 -> 1 -> 3) was clean: 42/42 decisions on
the exact board and solved exactly, 75/75 NPC moves and 75/75 board totals
matched, 112/112 turn-order checks matched, every game won. Its one
`wrongEnd` was the player placing the advised tile on the other end (the
marker's spot was the right one: the next NPC played onto it). Because
play goes around the table in 0 -> 2 -> 1 -> 3 order, raw seat numbers map
to physical chairs by that order (seat 1 sits across from seat 0). The
"which NPC is each tile row next to" check was dropped: the game never
ties a seat number to a character on screen, so the player can't answer
it, and the advice doesn't depend on it. Later still: 1v1 Draw
(Emerald Station, points target 60), 1v1 All Fives, and a full 4-seat
All Fives game -- 38/38 and then 13/13 decisions on the exact board, all
wins; the 4-seat game solved exactly from the first move (depth 27-28),
worst frame 13.9 ms. Its one NPC miss was a real bug: `func_614` counts a
NEGATIVE native total (-5, spinner side) as scoring, which our port
didn't (fixed, see "Scripted opponent policy"). A `wrongEnd` flag on a
double played onto its own number was a false alarm from the post-move
check using the open-end probe (now uses the exact board). Rule sets by
location (RDR2 wiki): All Fives = Saint Denis, Draw = Emerald Station,
All Threes = Blackwater. Block isn't offered anywhere. The NPC count at a
table varies per visit. Two more 4-seat All Fives games (35/35 decisions,
89/89 NPC moves, both won) checked every decision's forecast `recPoints`
against the real score change from that decision to the round's end:
exact, or the actual result was BETTER. How to read the verdict: it
counts only points STILL TO COME (points already scored this round are
banked in `scores`, not in the forecast), so "round win +10" then "tie 0"
on the next turn is the same forecast after scoring the 10. It's a floor,
not a prediction: when an NPC has two equally ranked placements, the
search assumes the one worse for you. A round that ends the game records
its scores as 0 -> 0 (the game resets them), so round lines carry
`scoresPeak` and judge `gameOver` from it. Still untested: All Threes (Blackwater, not
reachable yet).
Debug writes `DominoCheat_games.jsonl` (see Tests) -- summarize it with a
short script after a session rather than asking the player.

Expect more corrections on anything still marked untraced -- four fixes
so far (`kSeatStride`, the boneyard header word, the ped-array header
word, and the legal-move buffer layout, all confirmed correct on the
first live test) are proof "static tracing agrees with itself" isn't the
same bar as "matches a live memory read," even when it keeps turning out
right.
