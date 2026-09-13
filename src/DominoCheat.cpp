/*
	Dominoes advisor module for the "dominoes_sp" single-player minigame.

	STATUS: STATIC TRACE ONLY -- everything below is derived purely by
	reading the decompiled script
	(D:\Backup\Stuff\RDR2 Shit\Scripts\rdr2-scripts-decompiled\1491.50\script_rel\dominoes_sp.ysc.c,
	game build 1491.50, ~34.7k lines), the same starting point
	BlackjackCheat had before its own Session 6 live memory confirmation --
	see that project's docs/JOURNAL.md for what the next several live-
	testing sessions typically look like (F12 -> Probe*, compare the log
	against the real screen, re-derive whatever's wrong). NOT yet checked
	against a running game at all. Every offset below is a plausible
	candidate, not a fact, with its own confidence note.

	Tile encoding: CONFIRMED against the decompile (not just traced) --
	dominoes_sp deals from a standard double-six 28-tile set, tile index
	0-27 decoded to a {low,high} pip pair (0-6 each, low<=high) via
	func_151 (line ~8105), transcribed exactly into
	DominoHandEval::DecodeTile() -- see that header's own comment. A tile
	occupies 2 words in memory once dealt into a hand ({low at +0, high at
	+1}), same "small struct, no header" convention as poker/blackjack's
	{rank,suit} card pair.

	Struct layout (all slots ABSOLUTE script-local indices in dominoes_sp's
	own thread, following the same "uLocal_N lives at absolute local slot
	N" convention poker_sp/bjack_sp's own uLocal_14 both used -- confirmed
	structurally here via main()'s own local-count: uLocal_15 is the last
	local declared before uScriptParam_0, matching kLaunchArgsSlot-style
	pure counting in the other two projects' HIGH-confidence candidates):

	  - Table = uLocal_15 ITSELF, not a nested sub-struct -- same
	    "Table = the whole top-level local" shape BlackjackCheat's Table
	    (uLocal_14) has, not poker's nested Table-under-a-sub-struct shape.
	    Traced via main() (line 2895): `uLocal_15.f_2804 = { uScriptParam_0 };`
	    (line 2907) and `func_9(&uLocal_15, &uScriptParam_0)` (line 2914),
	    then func_22 (line 3463, called with uLocal_15/uScriptParam_0 as its
	    own uParam0/uParam1 -- confirmed by func_22's body doing the exact
	    same `uParam0->f_2804 = { *uParam1 }` at line 3465) uses
	    `uParam0->f_1330`/`uParam0->f_7` directly, matching main()'s own
	    `uLocal_15.f_1330` (line 2943). MEDIUM-HIGH confidence (multiple
	    independent call-site cross-checks, same method that nailed
	    poker/blackjack's own Table location) but zero live confirmation.
	  - LaunchArgs = uScriptParam_0, a STRUCT here (not a scalar/hash the
	    way poker's LaunchArgs was) -- has its own f_2/f_6/f_9/f_10/f_13
	    fields (a ped model hash, a world position vector, a heading, an
	    unknown int, and a 0/1 mode flag respectively, per their usage in
	    main() lines 2907-2989). Not read by this file at all yet (no
	    stakes-tier-style field identified/needed for anything OnTick()
	    does).
	  - Round = Table.f_7 -- the per-round game-state struct, reset at the
	    start of every deal by func_67 (line 5029, called as
	    `func_67(&(uParam0->f_7))` from func_22 line 3487 and func_73 line
	    5241, both with uParam0=Table). func_67's own body is unambiguous:
	    seeds RNG (`uParam0->f_13 = ABSI(GET_FRAME_COUNT()); SET_RANDOM_SEED(...)`),
	    resets the deck cursor (`uParam0->f_699 = 0`), builds+shuffles the
	    28-tile boneyard (`func_148(&(uParam0->f_684), false)`), then deals
	    7 tiles to each occupied seat in order 0->1->2->3, sharing that one
	    cursor the whole way. HIGH confidence (unambiguous, single call
	    chain, no field-number collision found in this specific chain).
	  - SeatsHolder = Round.f_14 -- a sub-struct within Round holding the
	    seats array. Traced via func_67's own seat-occupancy check,
	    `func_149(&(uParam0->f_14), i)` (line 5045, uParam0=Round there),
	    and func_149's body (line 8063) treating ITS OWN uParam0 (=
	    Round.f_14) as having a `.f_149` array field directly --
	    consistent with func_67's own `uParam0->f_14.f_149[i]` seat access
	    (line 5052 e.g.). HIGH confidence (two independent functions agree
	    on the exact same nested path).
	  - Seats array = SeatsHolder.f_149, confirmed exactly kMaxSeats=4 seats
	    -- func_67's own deal loop is `for (i=0;i<4;i++)` (line 5043) AND
	    func_149 bounds-checks `iParam1 >= uParam0->f_149` against that same
	    array's own header/count field (line 8065) -- a real read of the
	    array's own capacity, not just an incidental loop bound, same kind
	    of evidence poker's kSeatsHeaderOffset had. CONFIRMED LIVE (2026-09-13
	    session): the header word IS present (kSeatsDataBase = header+1,
	    matching poker's own array convention) -- ProbeTableStruct() read
	    seatsHeader=4 exactly at slot 185, and ProbeSeatHands() found seat
	    0's real occupancy marker (0) sitting exactly at slot 186 = 185+1,
	    with the next 3 seats visible at a real, live-derived stride (see
	    kSeatStride below).
	  - Per-seat struct (relative offsets within one seat's own base):
	      f_0: occupancy self-index marker, `== i` when dealt/occupied
	           (func_67 line 5056: `uParam0->f_14.f_149[i] = i;`, and
	           func_149's own check `uParam0->f_149[iParam1] == iParam1`,
	           line 8069/8071) -- a different convention from poker/
	           blackjack's "!=-1 means occupied" (here the marker is the
	           seat's OWN index, not a boolean-ish sentinel). CONFIRMED
	           LIVE: a real 3-human-plus-NPC... actually a 3-occupied-seat
	           game read seat 0's marker as 0, seat 1's as 1, seat 3's as 3,
	           and the genuinely-empty seat 2's as -1 -- exactly this
	           convention, once kSeatStride (below) was corrected.
	      f_1: a second gate func_149 ALSO requires (`.f_1 > 0`, line 8071,
	           only in the branch that isn't "developer/debug mode" --
	           func_32()==-1 && func_19()). Exact meaning still UNTRACED,
	           but CONFIRMED to behave as a real occupancy-correlated field
	           live: reads 100 for every occupied seat and 0 for the empty
	           one -- "100" rather than a plain boolean 1 hints at a
	           per-seat SCORE TARGET (many block-dominoes variants play to
	           100 points) that happens to also satisfy func_149's `>0`
	           gate, rather than a dedicated boolean flag; not disambiguated
	           yet, logged as a raw value only.
	      f_2: passed into func_152 (line 5059) as a per-seat value whose
	           own body wasn't traced this session -- likely a ped/prop
	           visual handle, structurally analogous to poker's per-seat
	           fields that don't matter for hand-reading. Read live as 0
	           for every seat this session (occupied or not) -- consistent
	           with "not yet assigned a real prop this early in the round"
	           but not independently confirmed as anything specific. Not
	           read by OnTick().
	      f_3: hand tile COUNT. CONFIRMED LIVE as a real, currently-accurate
	           count (not a stale/fixed value): read 6, 6, and 7 for the
	           three occupied seats in the same live session, and
	           cross-checking against the seats' own raw tile data (below)
	           confirmed the two 6-count seats had genuinely played one
	           tile each already (see f_4's own live-confirmation note) --
	           same "don't trust a raw scan, trust the count field the game
	           itself trusts" policy BlackjackCheat settled on after its own
	           Session 7 ReadHand() bug.
	      f_4: NOT the hand array's own header after all -- CONFIRMED LIVE
	           to be a fixed, occupancy-INDEPENDENT constant (read exactly
	           19 for all 4 seats, including the empty one), so whatever it
	           really is (a per-seat capacity/config value baked in at
	           table-init time, not round state), it is NOT the array size
	           BlackjackCheat/PokerCheat's own convention would predict via
	           an `f_4.f_39`-style access. Logged as a raw value only, not
	           otherwise used. Original static trace assumed it doubled as
	           the hand array's header word -- disproven live.
	      f_5..f_18: hand tiles, kHandSize=7 elements, 2 words each ({low,
	           high} -- see DominoHandEval.h), CONFIRMED LIVE with NO header
	           word of its own (data starts immediately at f_5, matching
	           BlackjackCheat's own headerless per-hand card array
	           convention, not poker's) -- written tile-by-tile in the deal
	           loop (func_67 line 5052:
	           `func_151(num, &uParam0->f_14.f_149[i].f_4[j], &(...f_4[j].f_1))`,
	           func_151 being the SAME DecodeTile() this file's
	           DominoHandEval.h already transcribes). Live confirmation
	           (2026-09-13 session, cross-checked against a raw
	           DumpFullStackJsonl() dump): a real 3-seat game (seats 0/1/3
	           occupied, seat 2 empty, matching deck cursor=21=3*7) had seat
	           0 read [2|4][3|5][5|6][0|0][0|1][1|4] (6 real tiles, matching
	           handCount=6) plus a 7th stale leftover slot, seat 1 similarly
	           6 real + 1 stale, and seat 3 (handCount=7, hadn't played
	           anything yet) read a full, clean 7 tiles -- and separately,
	           reading the BONEYARD's own fixed shuffle order (see below) at
	           the exact positions the deal loop guarantees seat 0/1's
	           ORIGINAL 7-tile deals occupied showed each seat's current 6
	           tiles plus exactly one extra tile apiece -- the tile each of
	           them had already played to the table. Airtight, independent
	           confirmation this offset/layout is exactly right.
	  - kSeatStride: CORRECTED LIVE, 19 -> 44. The original static trace had
	    no real evidence for the per-seat struct's total size (f_5..f_18
	    only accounts for 14 of it) -- 19 was a guess assuming nothing
	    followed the hand tiles. The 2026-09-13 session's ProbeSeatHands()
	    run showed seat 1 and seat 3's occupancy markers reading 0 and 2
	    instead of 1 and 3, and a DumpFullStackJsonl() diff found the REAL
	    per-seat spacing by locating each seat's own occupancy-marker/
	    "100"/handCount signature directly in the raw dump: seat 0 at slot
	    186, seat 1 at 230, seat 3 at 318 -- exactly 44 apart both times,
	    with genuinely-unoccupied seat 2 at the predicted 274 reading a
	    clean -1. Offsets 19-43 (25 words) past the hand tiles are unread/
	    untraced -- presumably position/animation/UI-binding fields
	    analogous to poker/blackjack's own unread per-seat fields.
	  - Deck cursor = Round.f_699 -- reset to 0 at deal start, incremented
	    once per tile drawn (func_67 line 5053: `uParam0->f_699 = uParam0->f_699 + 1;`,
	    once per of the seatCount*7 deal draws). HIGH confidence (the exact
	    same field, same struct, same function that seeds/resets the whole
	    round -- no ambiguity about which Round this is). NOTE: an
	    UNRELATED field also named f_699 exists on the OUTER Table struct
	    itself (func_73, line ~5199, a spawnpoint-search counter) -- same
	    field-number-collision-across-unrelated-structs trap
	    BlackjackCheat's own docs flagged for f_9; the two are on different
	    structs (Table vs. Table.f_7) and are NOT the same field, but worth
	    remembering if a future grep for "f_699" turns up that other site.
	  - Boneyard = Round.f_684 -- built+shuffled by func_148 (line 8010): a
	    real, unbiased Fisher-Yates shuffle of indices 0-27 (2 full passes,
	    `MISC::GET_RANDOM_INT_IN_RANGE(0, 28)` swaps -- structurally the
	    same shuffle shape poker_sp/bjack_sp's own deck shuffles use, just
	    2 passes here instead of poker's 5), THEN packed 5 bits per tile
	    into a bitfield buffer (`func_314(panParam0, 28)` sizes it,
	    `func_315(panParam0, j, endRange[j])` packs each shuffled tile
	    index) -- NOT a plain int-per-slot array the way poker/blackjack's
	    decks are. func_150 (the "read boneyard slot iParam1" function,
	    line 8074) confirms the packing: reads exactly 5 bits starting at
	    bit position `5*iParam1`, one `MISC::_IS_BIT_FLAG_SET(panParam0, bit)`
	    call per bit (bits 5*iParam1 .. 5*iParam1+4), assembling a 0-31
	    value via `MISC::SET_BIT` -- i.e. the SAME per-bit
	    word/bit-within-word addressing standard flag-array natives always
	    use (word = bit/32, bit-within-word = bit%32), which
	    ReadBoneyardTile() below replicates bit-by-bit rather than
	    assuming any particular byte alignment, specifically because 5
	    bits doesn't divide evenly into 32 (tile slot 6's bits 30-34
	    straddle two 32-bit words) -- a per-bit loop sidesteps that
	    entirely instead of needing special-case straddle handling. HIGH
	    confidence on the algorithm (an exact port of func_150's own
	    logic). The buffer's own header word: CORRECTED LIVE, present after
	    all (kBoneyardSlot = Round-relative 684+1 = 685, i.e. absolute
	    slot 707, not 706) -- the original trace guessed "no header" from
	    func_150's own bit-0 usage showing no visible skip, which turned
	    out to be exactly the kind of wrong guess this project family
	    keeps running into. Found by brute-forcing which candidate base
	    slot makes the boneyard's own fixed shuffle order exactly reproduce
	    seat 3's real, untouched 7-tile hand (handCount=7, nothing played
	    yet) at boneyard positions 14-20 -- the exact range the deal loop
	    guarantees seat 3 (the third occupied seat, after seat 0 draws 0-6
	    and seat 1 draws 7-13, seat 2 being unoccupied and skipped
	    entirely) drew from. Slot 707 reproduced all 7 tiles exactly, in
	    order; every other candidate in a 200-slot search window didn't.
	    With that fix, decoding all 28 boneyard positions gave 28 unique,
	    valid tiles with zero duplicates -- full double-six set integrity
	    -- and, as a bonus finding, confirmed the buffer keeps the ORIGINAL
	    fixed shuffle order for the WHOLE round, not just undrawn tiles:
	    positions already dealt still show the real tile that was dealt
	    there even after a seat plays it out of their own hand array (this
	    is how f_5..f_18's own live confirmation cross-checked seat 0/1's
	    already-played tile, above).
	  - kTileSetSize=28 tiles total, kHandSize=7 per seat, kMaxSeats=4 --
	    with all 4 seats occupied, 4*7=28 exhausts the ENTIRE boneyard at
	    deal time (matches the deal loop's own total draw count exactly),
	    meaning a full 4-player game leaves NOTHING for
	    ShowBoneyardPrediction to show -- only relevant with 1-3 occupied
	    seats, where 28-(occupiedSeats*7) tiles remain undrawn and,
	    because the whole boneyard is shuffled and fixed before a single
	    tile is dealt (func_67 builds it BEFORE the per-seat deal loop
	    runs), fully deterministic to read ahead -- the exact same
	    "shuffled once, dealt sequentially, no reshuffling mid-round"
	    property PokerCheat's predicted board and BlackjackCheat's
	    deck-ahead prediction both depend on.
	  - "My seat": TRACED via the SAME per-seat-live-ped-handle mechanism
	    poker's kSceneSlot/kCommunityCardObjectsBase and BlackjackCheat's
	    kPedSceneSlot/kSeatPedArrayOffset/FindMySeatByPed() both already
	    use -- a real ped handle compared against
	    PLAYER::PLAYER_PED_ID(), not a guessed scalar flag. Traced via
	    func_238 (line 11348), which does
	    `if (uParam0->f_199[iParam1 -- stride 124].f_12 == PLAYER::PLAYER_PED_ID())`
	    (line 11365) -- a direct, unambiguous "is this seat's ped the
	    human" check, called once per seat 0-3 from func_91's own loop
	    (`for (i=0;i<4;i++) func_238(uParam0, i, iParam1);`, line
	    5897-5899). func_91 itself is called as
	    `func_91(&(uParam0->f_1330), iParam1)` (line 3627) from a function
	    whose OWN uParam0 is confirmed Table (matches Table's own
	    f_2840/f_2334/f_2804/f_7 fields at that same call site) -- so
	    func_238's uParam0 = Table.f_1330, the same "scene" sibling-struct
	    role poker's f_3310/blackjack's f_1724 both have. HIGH confidence
	    on the mechanism (an exact structural match to two independently
	    confirmed sibling projects' own primary mySeat method); the array
	    stride (124) and .f_12 ped-handle offset are read directly off
	    func_238's own indexing, so HIGH confidence there too. CONFIRMED
	    LIVE (2026-09-13, same session as the initial trace): f_199 DOES
	    have a header word after all -- opposite of the initial "no
	    header" guess (which had assumed BlackjackCheat's own confirmed-
	    no-header f_946[seat] was the closer precedent; it wasn't). First
	    live attempt read all 4 seats' ped handles as 0; grepping a fresh
	    DumpFullStackJsonl() dump for the exact live PLAYER::PLAYER_PED_ID()
	    value found it one word later than predicted, at every one of the
	    4 seats simultaneously (69890/130306/131842/133122, each a
	    distinct, plausible ped handle) -- and the word immediately before
	    the (corrected) first element reads exactly 4, the array's own
	    header/count field. FindMySeatByPed() below now uses the corrected
	    offset and is FULLY CONFIRMED LIVE: a follow-up ProbeMySeat() run
	    (same session, after the fix) resolved mySeat=0 with seat 0's ped
	    handle reading exactly PLAYER::PLAYER_PED_ID() and the other 3
	    seats holding 3 other distinct, plausible ped handles -- matching
	    the earlier "before the fix, all 4 seats read 0" run's own seat-0
	    slot exactly one word later, i.e. this is the SAME live table,
	    same fix, immediately re-verified working, not a different
	    session's coincidental agreement.

	Session 3 addition (2026-09-13, same day as the live-confirmation
	sessions above) -- turn-order and scoring traced, and a hard
	complication found for the "board"/legal-move goal specifically:

	- Round.f_14, read as a BARE SCALAR (not chased into its own .f_149
	  seats sub-array -- the same struct's base slot doubling as a
	  meaningful value while nested offsets within it hold sub-arrays,
	  exactly like the per-seat struct's own f_0 self-index marker), is
	  the CURRENT TURN'S SEAT INDEX. Traced via func_76 (line ~5270,
	  dominoes_sp's own per-seat turn-resolution state machine) and
	  func_167 (line ~8666, `uParam0->f_14 = iParam1;` -- a direct write
	  site, not an inference). func_76 itself is called every tick from
	  func_24 (dominoes_sp's per-frame tick function, called from main()'s
	  own loop) as `func_76(uParam0, uParam0->f_7.f_14)` -- i.e. the very
	  value being read here IS the seat argument func_76 operates on.
	  Round.f_1299 is that seat's own turn SUB-STATE (func_162, line
	  ~8592, a trivial `Round.f_1299 = newState` setter) -- func_76's own
	  switch cycles it 0 (idle/done) -> 1 -> 2 -> 3 -> 4/5 -> 6 -> back to
	  0 every turn. kCurrentTurnSeatFieldOffset is CONFIRMED LIVE
	  (2026-09-13): the overlay's "Turn: seat N" line was checked against
	  the real screen across multiple turns and correctly showed "you"
	  only when it was actually the user's turn.
	  kTurnSubStateFieldOffset's own identity is still just
	  HIGH-confidence (a direct read/write site, not independently
	  confirmed); the individual sub-state numbers' MEANING remains a
	  reasonable reading of the switch's branch structure, not confirmed
	  one state at a time.
	- seat.f_2 is ACCUMULATED SCORE toward the table's points target
	  (Round.f_666.f_14[0], one of 60/90/100 depending on table skin, set
	  in func_22's early setup) -- traced via func_169 (line ~8700, the
	  round-resolution function): the losing seats' remaining-hand pip
	  totals get summed and added to the round WINNER's own f_2, then
	  compared against the target to decide whether the whole GAME (not
	  just this round) has ended. Matches this file's own
	  DominoHandEval::HandPipTotal() -- that function was written for
	  exactly this kind of tiebreak scoring and has been sitting unused
	  until now.
	- A hand is NOT capped at 7 tiles. func_611 (line ~23876, "add a
	  drawn boneyard tile to this hand") appends at index [current count]
	  and only refuses once count reaches 19 (a literal in the decompile,
	  not read from anywhere) -- real block-dominoes rules: a seat that
	  can't play from its own hand draws from the boneyard repeatedly
	  until it can (func_166, line ~8632, is the "try to become able to
	  play" loop that calls func_348 -> func_611 up to 28 times). This
	  file's own kHandSize (7) is now documented as the INITIAL deal size
	  only; DrawOverlay()/ProbeSeatHands() both now read/loop up to the
	  new kMaxHandCapacity (19) instead, so a hand that has genuinely grown
	  isn't silently truncated the way the original 7-tile cap would have
	  done. NOT live-tested (no session yet has watched a hand actually
	  grow past 7 -- both live sessions so far happened to catch hands
	  either freshly dealt or down by exactly one already-played tile).

	IMPORTANT COMPLICATION for the "board"/legal-move goal specifically:
	func_354 (line ~14836, the function that actually COMMITS a chosen
	tile to the table once func_166/func_351 have found one that's
	playable) does the real placement via a NATIVE,
	`MINIGAME::_0x012027C28F421F46(&tileValue, &handArray)` -- not a
	plain script-local array write this file could read the same way it
	reads hands/the boneyard. The actual board layout (which tiles are
	placed, in what order, and critically what pip value each of the
	board's open ends currently needs) most likely lives INSIDE that
	native's own internal state, not in dominoes_sp's script-local stack
	at all. This is the SAME class of dead end PokerCheat's own header
	comment documents hitting with the game's hand-rank native
	(MINIGAME::_0x32A7C216344D623B) -- nine sessions of guessing at an
	opaque native's buffer layout before giving up and computing hand
	strength independently instead. Unlike that case, there's no
	obviously equivalent "compute it ourselves from data we already have"
	fallback here: this file doesn't have independent access to the
	board's open-end values at all, confirmed or otherwise. Concretely,
	this means "which tile can I legally play" advice is NOT simply a
	missing trace waiting to be found the way "my seat"/hand offsets
	were -- it may require either (a) calling more of these MINIGAME::
	natives ourselves and puzzling out their output buffers (the exact
	approach PokerCheat abandoned as unworkable for its own hand-rank
	native), or (b) IDA-level analysis of what
	MINIGAME::_0x012027C28F421F46 and its sibling query-natives actually
	do internally (see CollectorOffline's CLAUDE.md for the already-
	analyzed IDA database this build has). Flagged here explicitly rather
	than silently attempted or silently dropped -- this is a real scope
	decision for whoever picks this up next, not a small remaining trace.

	Session 4 addition (same day) -- the complication above turned out to
	apply only to the PLACEMENT-commit native, not to legality itself:
	dominoes_sp's own func_347 (line ~14691, already cited above as the
	thing that calls MINIGAME::_0x3AE451860F03CA8A) is a SEPARATE,
	READ-ONLY query native the script calls specifically to find which
	tiles in a hand are currently playable against the board -- and
	unlike the commit native, this one needed NO IDA reversing at all,
	because the script's own two consumer functions (func_352 line
	~14761, func_353 line ~14800) spell out its exact I/O contract by how
	they read the result back:
	  - Call shape: `_0x3AE451860F03CA8A(handArrayPtr, outBufferPtr)`,
	    returning an int count. `outBufferPtr` is a SCR_ARRAY the calling
	    script always declares with capacity exactly 15 (`var unk; unk =
	    15;`, the same "var x; x = N;" idiom this project already knows
	    means "declare x as an N-element array" -- see func_148's own
	    28-element shuffle buffer for the identical pattern) -- i.e. an
	    8-bytes-per-int buffer, header word = 15, then 15 elements.
	  - Each element is 5 words (`uParam2->[i]`, stride 5, in both
	    consumers). Element word 0 is a HAND INDEX: func_352/353 both
	    bounds-check it directly against the hand's own count
	    (`uParam2->[i] < uParam1->f_39`) before using it to index BACK
	    into the hand array (`uParam0->[uParam1->[i]]`, stride 2, in
	    func_353) to read the actual tile. Element word 4 is a "resulting
	    connection value"
	    used only for an optional scoring-bonus preference (func_352's
	    own func_614 call, the same "multiple of 3 or 5" bonus check
	    already found in func_354) -- not needed for basic legality.
	  - func_352/353 both trust the native's own RETURNED COUNT as their
	    loop bound when a caller passes it directly (func_168 does
	    exactly this: `num = func_347(...); func_352(..., num)`) --
	    FindPlayableTiles() below does the same, rather than replicating
	    func_613's additional "is this a real entry or just a zeroed
	    placeholder" validity check, which only matters for callers that
	    scan the full 15-capacity buffer regardless of the real count.
	This is now IMPLEMENTED (FindPlayableTiles(), wired into
	DrawOverlay() to mark the human's own currently-playable tiles) and
	CONFIRMED LIVE (2026-09-13, same day): with the real board showing
	[6|6] and [6|1] on it (open ends 6 and 1), ProbeLegalMoves() returned
	exactly 4 candidates -- hand indices resolving to [1|1], [0|6],
	[2|6], [1|3] -- and every single one of those 4 tiles contains a 6 or
	a 1, matching the board's real open ends exactly. Buffer parsing was
	also internally consistent: the native's own returned count (4)
	matched exactly 4 non-zero entries in the buffer with the rest still
	zeroed from initialization, and the hand-index word landed at the
	exact offset FindPlayableTiles() assumed for all 4 entries. No crash,
	no hang, no garbage.

	This file now reads hands, the undrawn boneyard, whose-turn/score
	state, AND which of the human's own tiles are currently legal to
	play -- the actual "best move" feature this project set out to
	build. What's still missing for a complete advisor: which SPECIFIC
	end each playable tile would go on (the query native tells us WHICH
	tiles, not WHERE), and any preference ordering beyond "legal or not"
	(func_352/353's own bonus/high-pip heuristics were read but not
	ported).
*/

#include "DominoCheat.h"
#include "DominoHandEval.h"
#include "Log.h"
#include "GamePointers.h"
#include "ScriptLocal.h"
#include "Config.h"
#include "script.h"

#include <string>
#include <sstream>
#include <cstdint>
#include <array>
#include <cstring>

namespace DominoCheat
{
	bool Enabled = false;

	void Toggle()
	{
		Enabled = !Enabled;
		Log::Write("DominoCheat::Toggle -> {}", Enabled ? "ON" : "OFF");
	}

	void SetEnabled(bool enabled)
	{
		Enabled = enabled;
		Log::Write("DominoCheat::SetEnabled -> {}", Enabled ? "ON" : "OFF");
	}

	// ------------------------------------------------------------------
	// Struct layout accessors -- see file header comment above for the
	// full derivation/confidence notes on every one of these, and
	// ScriptLocal.h's own header comment for the general methodology.
	// Expressed as chainable ScriptLocal accessors mirroring the
	// decompile's own field-access syntax one step at a time, instead of
	// hand-flattening every nested field into one nameless absolute slot
	// number: the two-argument At(index, stride) automatically applies
	// the "+1 for the array's own header word" every bracket-indexed
	// array in this file has turned out to need (seats, hand tiles, ped
	// array), removing that specific guess from the call site entirely --
	// use At(index, stride) exactly where the decompile shows
	// `something[i]`, and plain At(fieldOffset) everywhere else.
	// ------------------------------------------------------------------
	constexpr std::uint32_t kTableSlot = 15; // Table = uLocal_15 itself

	namespace
	{
		ScriptLocal TableLocal(rage::scrThread* thread) { return ScriptLocal(thread, kTableSlot); }
		ScriptLocal RoundLocal(rage::scrThread* thread) { return TableLocal(thread).At(7); } // Table.f_7 -- Round
		ScriptLocal SeatsHolderLocal(rage::scrThread* thread) { return RoundLocal(thread).At(14); } // Round.f_14 -- SeatsHolder
	}

	constexpr std::uint32_t kSeatsArrayFieldOffset = 149; // SeatsHolder.f_149[seat] -- bracket-indexed in the decompile, header CONFIRMED LIVE (reads kMaxSeats)
	constexpr std::uint32_t kSeatStride = 44; // CONFIRMED LIVE 2026-09-13 (corrected from an original guess of 19) -- a struct-size fact, not derivable from At() itself, see ScriptLocal.h's header comment

	namespace
	{
		ScriptLocal SeatLocal(rage::scrThread* thread, std::uint32_t seat)
		{
			return SeatsHolderLocal(thread).At(kSeatsArrayFieldOffset).At(seat, kSeatStride);
		}
	}

	// Per-seat relative offsets. f_5..f_18 (hand tiles) are CONFIRMED LIVE
	// (2026-09-13 session) -- see file header comment. f_4 turned out NOT
	// to be the hand array's own header (a fixed, occupancy-independent
	// constant instead, see header comment) but the decompile's own
	// `f_4[j]` bracket syntax still means At(j, 2) is the right call
	// (CONFIRMED LIVE to land exactly on f_5 -- the array behaves as if
	// it has a header slot at f_4 regardless of what value ends up
	// living there).
	constexpr std::uint32_t kSeatOccupancyOffset = 0;   // == seat's own raw index when dealt, CONFIRMED LIVE
	constexpr std::uint32_t kSeatActiveFlagOffset = 1;  // reads 100 when occupied, 0 when empty -- CONFIRMED LIVE as occupancy-correlated, exact meaning still untraced
	// seat.f_2 -- ACCUMULATED SCORE toward the table's points target (see
	// kPointsTargetFieldOffset below). Traced via func_169 (the round-
	// resolution function, ~line 8700): the round's winner's remaining-
	// hand-pip total from every OTHER occupied seat gets summed and added
	// here (`seat.f_2 = seat.f_2 + num`), then compared against the
	// target (`if (seat.f_2 >= Round.f_666.f_14[0])`) to decide whether
	// the whole GAME (not just this round) is over. Not read by OnTick()
	// yet -- logged as a raw value only.
	constexpr std::uint32_t kSeatScoreOffset = 2;
	constexpr std::uint32_t kSeatHandCountOffset = 3;   // real, currently-accurate hand count -- CONFIRMED LIVE. Also doubles as "this seat just won the round" (==0) in func_76/func_164's early-exit checks.
	constexpr std::uint32_t kSeatHandArrayFieldOffset = 4; // seat.f_4[j] -- bracket-indexed, CONFIRMED LIVE (see above)

	constexpr std::uint32_t kMaxSeats = DominoHandEval::kMaxSeats;
	constexpr std::uint32_t kHandSize = DominoHandEval::kHandSize; // INITIAL deal size (7) -- NOT a hard cap, see kMaxHandCapacity below
	constexpr std::uint32_t kTileSetSize = DominoHandEval::kTileSetSize;

	// A hand can grow past its initial 7 tiles: func_611 (~line 23876,
	// "add a drawn boneyard tile to this hand") appends at index
	// [current count] and only refuses once that count reaches 19 (a
	// literal the decompile hardcodes, not a value read from anywhere) --
	// real block-dominoes rules where a seat that can't play draws from
	// the boneyard repeatedly until it can. kSeatStride=44 leaves
	// exactly enough room for a 19-tile hand (4 leading scalar words + up
	// to 19*2=38 tile words = 42, +2 spare/unidentified words), which is
	// at least consistent with 19 being the real cap rather than an
	// arbitrary safety margin -- not independently confirmed live (no
	// session yet has watched a hand actually grow past 7).
	constexpr std::uint32_t kMaxHandCapacity = 19;

	constexpr std::uint32_t kDeckCursorFieldOffset = 699; // Round.f_699 -- plain scalar field (no brackets in the decompile), CONFIRMED LIVE (read 21 for a real 3-occupied-seat game, exactly 3*7)

	// Round.f_14, read as a BARE SCALAR (not chased into .f_149's seats
	// sub-array -- both live at the same base address, exactly the same
	// "a struct's own base slot doubles as a meaningful scalar while
	// nested offsets within it hold sub-arrays" convention already
	// confirmed for the per-seat struct's own f_0 self-index marker) --
	// the CURRENT TURN'S SEAT INDEX. Traced via func_76 (~line 5270, the
	// per-tick turn-resolution state machine): called from func_24
	// (dominoes_sp's own per-frame tick function) as
	// `func_76(uParam0, uParam0->f_7.f_14)`, i.e. iParam1 (the seat
	// func_76 operates on all session long) IS this bare value; func_167
	// (~line 8666, called from func_76's own state 2) confirms it's
	// WRITABLE the same way: `uParam0->f_14 = iParam1;` sets it to
	// whichever seat is starting its turn. HIGH confidence (a direct,
	// unambiguous read/write site, not an inference) but NOT yet
	// live-tested against the real screen's own turn indicator.
	constexpr std::uint32_t kCurrentTurnSeatFieldOffset = 14;

	// Round.f_1299 -- the current turn's own sub-state (func_162,
	// ~line 8592, is a trivial setter: `Round.f_1299 = newState`). The 7
	// states func_76's own switch cycles through (0 idle/done -> 1 ->
	// 2 -> 3 -> 4 -> 5 -> 6 -> back to 0) roughly read as: 1 = turn just
	// started (branches straight to 5 if it's the human's own turn and
	// the round has begun), 2 = deciding/drawing a move for this seat,
	// 3 = re-check after deciding, 4/5 = committing the chosen move
	// (func_168 for an NPC, func_169 -- the round-resolution function --
	// for committing/finishing), 6 = waiting for func_170 (`return
	// Round.f_9`, an untraced completion flag) before resetting to 0.
	// These labels are a reasonable READING of the switch's own branch
	// structure, not independently confirmed against live states one at
	// a time -- logged as a raw number, not a claimed label, until that
	// happens.
	constexpr std::uint32_t kTurnSubStateFieldOffset = 1299;

	// Round.f_684 -- CONFIRMED LIVE to have a header word too, even
	// though the decompile never shows `f_684[i]` bracket syntax to infer
	// it from up front (func_150 addresses this buffer via raw
	// MISC::_IS_BIT_FLAG_SET bit math on a passed POINTER, not the VM's
	// own array-index opcode) -- see file header comment for how this was
	// found (brute-forcing which base slot reproduces a real seat's known
	// deal order). The "+1" below is supplied from that live evidence,
	// not inferred from syntax the way kSeatsArrayFieldOffset/
	// kSeatHandArrayFieldOffset/kScenePedArrayFieldOffset's are.
	constexpr std::uint32_t kBoneyardFieldOffset = 684;
	constexpr std::uint32_t kBitsPerTile = 5;

	namespace
	{
		ScriptLocal BoneyardDataLocal(rage::scrThread* thread)
		{
			return RoundLocal(thread).At(kBoneyardFieldOffset).At(1); // .At(1): CONFIRMED LIVE header word, see comment above
		}
	}

	// "My seat" -- see file header comment for the full func_238/func_91
	// derivation. Table.f_1330 is the "scene" sibling-struct (same role as
	// poker's f_3310/blackjack's f_1724); f_199 is a per-seat ped-tracking
	// array, stride 124, live ped handle at relative offset +12.
	// kScenePedArrayFieldOffset's header word IS directly inferrable from
	// the decompile's own `f_199[iParam1]` bracket syntax (CONFIRMED LIVE
	// to be present, reading exactly kMaxSeats -- see header comment for
	// the brute-force-by-PLAYER_PED_ID() evidence).
	constexpr std::uint32_t kSceneFieldOffset = 1330; // Table.f_1330
	constexpr std::uint32_t kScenePedArrayFieldOffset = 199; // Scene.f_199[seat] -- bracket-indexed
	constexpr std::uint32_t kScenePedStride = 124;
	constexpr std::uint32_t kScenePedHandleOffset = 12;

	// Real 3D per-tile props -- same "the game already made a real object
	// for this, get its live coords and project to screen" technique
	// PokerCheat's own community-card objects use (see that project's
	// kCommunityCardObjectsBase header comment for the original
	// precedent). Traced via func_65 (dominoes_sp.ysc.c, line ~4921,
	// called as `func_65(&(uParam0->f_1330))` at line 3480 -- confirmed
	// Scene, same struct as the ped array above): it CREATE_OBJECTs ALL
	// 28 tiles' worth of physical props UP FRONT, one per RAW TILE INDEX
	// 0-27 (the exact same index DominoHandEval::DecodeTile()/
	// EncodeTile() already use for the boneyard) --
	// `Scene.f_746[i].f_1 = OBJECT::CREATE_OBJECT_NO_OFFSET(...)` in a
	// `for (i=0;i<28;i++)` loop -- and each tile's own single persistent
	// prop is presumably just MOVED as it travels from the pile to a
	// hand to the board, never destroyed/recreated (matching how a real
	// physical set of dominoes behaves). Bracket-indexed in the
	// decompile (`f_746[i]`, stride 8), so the header-word convention
	// applies the same way it has for every other bracket-indexed array
	// in this file. CONFIRMED LIVE (2026-09-13): ProbeTileProps() found
	// all 28 raw indices resolve to a real, existing entity handle with
	// tightly-clustered, physically plausible world coordinates (all
	// within about a meter of each other, matching a real tabletop), and
	// roughly half project to valid on-screen positions (the rest
	// presumably held off-camera in a hand).
	// CORRECTION (2026-09-13, live bug report): array index `i` here is
	// NOT the tile's own value/identity the way DecodeTile()/EncodeTile()
	// number things -- it's a fixed PHYSICAL PROP SLOT (one of 28 generic
	// props) whose current OWNER gets reassigned as tiles move between
	// the boneyard, a hand, and the board -- e.g. func_698 (line ~25579):
	// `switch (uParam0->f_746[iParam2]) { case 2: CLEAR_BIT(&f_981[0],
	// f_746[iParam2].f_4); case 3: ...f_981[1]...; case 4: ...f_981[2]...;
	// case 5: ...f_981[3]...; }` -- the bare element value (2,3,4,5) maps
	// to seat (0,1,2,3) with a +2 offset. CONFIRMED LIVE via
	// ProbeTilePropOwnership(): every prop's bare value fell into exactly
	// 5 buckets -- 2/3/4/5 (21 props total, matching all 4 seats'
	// combined hand counts exactly) plus a 5th value (6) for every
	// ALREADY-PLAYED tile (7 props, all with `.f_4=0`/`.f_5=-1` --
	// exactly the tiles missing from each seat's hand-count-vs-7
	// deficit) -- i.e. bare value 6 = "on the board", not a 5th seat.
	// `.f_4`/`.f_5` turned out to be a DEAD END for finding a SPECIFIC
	// tile: real data showed gaps (e.g. one seat's props read f_4 =
	// 1,2,3,4,5 -- never 0) once ANY tile had already been played from
	// that hand, meaning `.f_4` is some STABLE per-prop bookkeeping value
	// (unaffected by later plays) that does NOT track this file's own
	// COMPACTING hand-array index (ReadHandTile()'s 0-based index shifts
	// down when an earlier tile is removed -- confirmed back in the
	// original hand-reading session). `.f_3` is the real find: CONFIRMED
	// LIVE to hold the prop's own raw tile VALUE (0-27, decodable via
	// DecodeTile() exactly like the boneyard) for the LOCAL PLAYER's own
	// hand specifically -- real, distinct, in-range values only for
	// seat-owned props matching mySeat, a constant out-of-range sentinel
	// (28) for every other seat's props (presumably because the game
	// only needs to resolve a real face texture for tiles the camera can
	// actually see up close). This means finding a SPECIFIC one of your
	// own tiles' props needs no hand-slot bookkeeping at all: search
	// owner-matched props for `.f_3 == EncodeTile(tile.low, tile.high)`.
	constexpr std::uint32_t kSceneTilePropArrayFieldOffset = 746;
	constexpr std::uint32_t kTilePropStride = 8;
	constexpr std::uint32_t kTilePropOwnerFieldOffset = 0; // bare element value -- CONFIRMED LIVE: seat+2 for a hand-owned prop, 6 for an already-played (on-board) prop
	constexpr std::uint32_t kTilePropValueFieldOffset = 3; // .f_3 -- CONFIRMED LIVE: raw tile value (0-27) for the LOCAL PLAYER's own props only; sentinel 28 otherwise
	constexpr std::uint32_t kTilePropHandleOffset = 1;
	constexpr std::int32_t kTilePropSeatOwnerBase = 2;   // seat 0 -> bare value 2, seat 1 -> 3, etc. -- CONFIRMED LIVE
	constexpr std::int32_t kTilePropBoardOwnerValue = 6; // CONFIRMED LIVE: an already-played tile's prop

	namespace
	{
		ScriptLocal SceneLocal(rage::scrThread* thread) { return TableLocal(thread).At(kSceneFieldOffset); }
		ScriptLocal ScenePedLocal(rage::scrThread* thread, std::uint32_t seat)
		{
			return SceneLocal(thread).At(kScenePedArrayFieldOffset).At(seat, kScenePedStride);
		}

		ScriptLocal TilePropLocal(rage::scrThread* thread, std::int32_t propSlot)
		{
			return SceneLocal(thread).At(kSceneTilePropArrayFieldOffset).At(static_cast<std::uint32_t>(propSlot), kTilePropStride);
		}

		// Returns the live entity handle for physical prop slot
		// `propSlot` (0-27 -- a fixed prop identity, NOT a tile value/
		// index, see this section's header comment), or 0 if none/
		// out of range.
		std::int32_t GetTilePropHandle(rage::scrThread* thread, std::int32_t propSlot)
		{
			if (propSlot < 0 || propSlot >= static_cast<std::int32_t>(DominoHandEval::kTileSetSize))
				return 0;

			return TilePropLocal(thread, propSlot).At(kTilePropHandleOffset).AsInt32();
		}

		// Searches all 28 prop slots for the one currently owned by
		// `seat`'s hand whose own `.f_3` matches `rawTileValue` (0-27,
		// EncodeTile()'s own numbering) -- CONFIRMED LIVE (see
		// kSceneTilePropArrayFieldOffset's own header comment). Only
		// meaningful for the LOCAL PLAYER's own seat, since `.f_3` reads
		// a constant sentinel (28) for every other seat's props. Returns
		// -1 if none match.
		std::int32_t FindTilePropForTileValue(rage::scrThread* thread, std::int32_t seat, std::int32_t rawTileValue)
		{
			std::int32_t wantOwner = seat + kTilePropSeatOwnerBase;
			for (std::int32_t i = 0; i < static_cast<std::int32_t>(DominoHandEval::kTileSetSize); i++)
			{
				ScriptLocal prop = TilePropLocal(thread, i);
				std::int32_t owner = prop.At(kTilePropOwnerFieldOffset).AsInt32();
				std::int32_t value = prop.At(kTilePropValueFieldOffset).AsInt32();
				if (owner == wantOwner && value == rawTileValue)
					return i;
			}
			return -1;
		}

		rage::joaat_t DominoesScriptHash()
		{
			static rage::joaat_t hash = rage::Joaat("dominoes_sp");
			return hash;
		}

		// Finds which seat's live ped handle matches the real player --
		// see file header comment ("My seat") for the func_238/func_91
		// derivation this is a direct port of, and BlackjackCheat's own
		// FindMySeatByPed() for the exact precedent this mirrors. Returns
		// -1 if no seat's handle matches (e.g. dominoes_sp hasn't finished
		// seating peds yet).
		std::int32_t FindMySeatByPed(rage::scrThread* thread)
		{
			std::int32_t myPed = static_cast<std::int32_t>(PLAYER::PLAYER_PED_ID());

			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
			{
				std::int32_t pedHandle = ScenePedLocal(thread, seat).At(kScenePedHandleOffset).AsInt32();
				if (pedHandle != 0 && pedHandle == myPed)
					return static_cast<std::int32_t>(seat);
			}

			return -1;
		}

		// Reads one bit of a packed flag buffer starting at `base`,
		// mirroring MISC::_IS_BIT_FLAG_SET's own word/bit-within-word
		// addressing (word = bit/32, bit-within-word = bit%32) -- each
		// script "int" array element occupies one full 8-byte slot (low 4
		// bytes used), same convention ScriptLocal::AsInt32() already
		// assumes.
		bool ReadPackedBit(const ScriptLocal& base, std::uint32_t bitIndex)
		{
			std::int32_t word = base.At(bitIndex / 32).AsInt32();
			return (static_cast<std::uint32_t>(word) >> (bitIndex % 32)) & 1u;
		}

		// Exact port of dominoes_sp.ysc.c's func_150 (see this file's
		// header comment) -- reads the 5-bit tile index stored at boneyard
		// position `boneyardIndex` (0-27) and decodes it via
		// DominoHandEval::DecodeTile(). Returns an invalid Tile for an
		// out-of-range index, matching func_150's own "return 29" (an
		// intentionally-invalid sentinel > kTileSetSize-1) for the same
		// case.
		DominoHandEval::Tile ReadBoneyardTile(rage::scrThread* thread, std::int32_t boneyardIndex)
		{
			if (boneyardIndex < 0 || boneyardIndex >= static_cast<std::int32_t>(kTileSetSize))
				return DominoHandEval::Tile{};

			ScriptLocal base = BoneyardDataLocal(thread);
			std::int32_t rawIndex = 0;
			std::uint32_t firstBit = static_cast<std::uint32_t>(boneyardIndex) * kBitsPerTile;
			for (std::uint32_t i = 0; i < kBitsPerTile; i++)
			{
				if (ReadPackedBit(base, firstBit + i))
					rawIndex |= (1 << i);
			}

			return DominoHandEval::DecodeTile(rawIndex);
		}

		DominoHandEval::Tile ReadHandTile(rage::scrThread* thread, std::uint32_t seat, std::uint32_t tileIndex)
		{
			ScriptLocal handArray = SeatLocal(thread, seat).At(kSeatHandArrayFieldOffset);
			std::int32_t low = handArray.At(tileIndex, 2).AsInt32();
			std::int32_t high = handArray.At(tileIndex, 2).At(1).AsInt32();
			return DominoHandEval::Tile{ low, high };
		}

		// Real legal-move query -- calls the same read-only native
		// dominoes_sp itself calls (see ExtraNatives.h's own comment on
		// MINIGAME::_FIND_PLAYABLE_HAND_TILES) with the target seat's real
		// hand array and a caller-owned scratch buffer shaped to match
		// what the script's own consumers (func_352/func_353) expect: an
		// 8-bytes-per-"int" buffer, header word = kCandidateCapacity (15,
		// the fixed value every real call site uses), then
		// kCandidateCapacity elements of kCandidateStride (5) words each.
		// Writes up to `maxOut` playable HAND INDICES into `outHandIndices`
		// and returns how many were written (0 if nothing in this hand is
		// currently playable, e.g. it's not this seat's turn or the hand
		// is genuinely stuck). CONFIRMED LIVE 2026-09-13 -- see the file
		// header comment's "Session 4 addition" for the real board state
		// (open ends 6 and 1) this was checked against.
		constexpr std::uint32_t kCandidateCapacity = 15;
		constexpr std::uint32_t kCandidateStride = 5;

		std::uint32_t FindPlayableTiles(rage::scrThread* thread, std::uint32_t seat, std::int32_t* outHandIndices, std::uint32_t maxOut)
		{
			void* handPtr = GamePointers::GetScriptLocalAddress(thread, SeatLocal(thread, seat).At(kSeatHandArrayFieldOffset).Index());
			if (!handPtr)
				return 0;

			std::array<std::int64_t, 1 + kCandidateCapacity * kCandidateStride> buffer{};
			buffer[0] = kCandidateCapacity;

			int count = MINIGAME::_FIND_PLAYABLE_HAND_TILES(reinterpret_cast<Any*>(handPtr), reinterpret_cast<Any*>(buffer.data()));
			if (count < 0)
				count = 0;
			if (count > static_cast<int>(kCandidateCapacity))
				count = static_cast<int>(kCandidateCapacity);

			std::uint32_t written = 0;
			for (int i = 0; i < count && written < maxOut; i++)
			{
				std::int64_t handIndex = buffer[1 + static_cast<std::size_t>(i) * kCandidateStride];
				outHandIndices[written++] = static_cast<std::int32_t>(handIndex);
			}

			return written;
		}

		bool IsHandIndexPlayable(const std::int32_t* playableIndices, std::uint32_t playableCount, std::int32_t handIndex)
		{
			for (std::uint32_t i = 0; i < playableCount; i++)
				if (playableIndices[i] == handIndex)
					return true;
			return false;
		}

		// Determines the board's real open-end pip values by testing
		// synthetic double tiles against the same CONFIRMED-working query
		// native FindPlayableTiles() already uses, but against a LOCAL
		// copy of the seat's hand -- never a write to real game memory.
		// Copies the ENTIRE per-seat struct (kSeatStride words, exactly
		// as confirmed live -- not just the hand array) into a local
		// buffer so any field the native reads besides the tile data
		// itself is preserved byte-for-byte from a genuine live seat
		// rather than fabricated.
		//
		// CORRECTED (2026-09-13, live bug report): the original version
		// only ran ONE pass, overwriting hand slots [0, handCount) with
		// doubles {0,0}..{handCount-1,handCount-1} -- so a hand with
		// fewer than 7 tiles could never test every pip value. This bit
		// in real play: a 3-tile hand only tested pips 0-2, silently
		// missing a real open end of 3 that TWO of that same hand's
		// tiles actually matched (confirmed separately by
		// FindPlayableTiles(), which doesn't have this limitation at
		// all) -- DetermineBestMove() then found zero candidates and
		// gave up despite two real legal moves existing. Fixed by
		// running MULTIPLE passes, each overwriting the SAME real slots
		// with a fresh chunk of untested pip values (slots stay within
		// the real hand count every pass, so this never needs to
		// fabricate an "extra" slot or guess at a hidden count field),
		// accumulating the union of open ends found across all passes
		// until every one of the 7 possible pip values has been tried
		// at least once. A hand of size N now takes ceil(7/N) native
		// calls instead of 1 -- still cheap (at most 7, for a 1-tile
		// hand). NOT yet live-tested (the bug above WAS caught live;
		// this specific fix has not been re-run against a real board
		// yet).
		std::uint32_t DetermineOpenEnds(rage::scrThread* thread, std::uint32_t seat, std::int32_t* outPips, std::uint32_t maxOut)
		{
			ScriptLocal seatLocal = SeatLocal(thread, seat);
			std::int32_t handCount = seatLocal.At(kSeatHandCountOffset).AsInt32();
			if (handCount < 0)
				handCount = 0;
			if (handCount > static_cast<std::int32_t>(kMaxHandCapacity))
				handCount = static_cast<std::int32_t>(kMaxHandCapacity);

			std::uint32_t slotsPerPass = static_cast<std::uint32_t>(handCount);
			if (slotsPerPass == 0)
				return 0;
			if (slotsPerPass > 7)
				slotsPerPass = 7; // never need more than 7 slots in a single pass -- only 7 distinct pip values exist

			void* seatAddr = GamePointers::GetScriptLocalAddress(thread, seatLocal.Index());
			if (!seatAddr)
				return 0;

			bool foundPip[7] = {};
			std::uint32_t written = 0;

			for (std::int32_t passStart = 0; passStart < 7 && written < maxOut; passStart += static_cast<std::int32_t>(slotsPerPass))
			{
				std::array<std::int64_t, kSeatStride> seatCopy{};
				std::memcpy(seatCopy.data(), seatAddr, seatCopy.size() * sizeof(std::int64_t));

				std::uint32_t thisPassCount = 0;
				for (std::uint32_t i = 0; i < slotsPerPass; i++)
				{
					std::int32_t pip = passStart + static_cast<std::int32_t>(i);
					if (pip > 6)
						break;

					std::size_t low = kSeatHandArrayFieldOffset + 1 + static_cast<std::size_t>(i) * 2;
					seatCopy[low] = pip;
					seatCopy[low + 1] = pip;
					thisPassCount++;
				}
				if (thisPassCount == 0)
					break;

				void* handPtr = &seatCopy[kSeatHandArrayFieldOffset];

				std::array<std::int64_t, 1 + kCandidateCapacity * kCandidateStride> buffer{};
				buffer[0] = kCandidateCapacity;

				int count = MINIGAME::_FIND_PLAYABLE_HAND_TILES(reinterpret_cast<Any*>(handPtr), reinterpret_cast<Any*>(buffer.data()));
				if (count < 0)
					count = 0;
				if (count > static_cast<int>(kCandidateCapacity))
					count = static_cast<int>(kCandidateCapacity);

				for (int i = 0; i < count; i++)
				{
					std::int64_t handIndex = buffer[1 + static_cast<std::size_t>(i) * kCandidateStride];
					if (handIndex < 0 || handIndex >= static_cast<std::int64_t>(thisPassCount))
						continue;

					std::int32_t pip = passStart + static_cast<std::int32_t>(handIndex);
					if (pip < 0 || pip > 6 || foundPip[pip])
						continue;

					foundPip[pip] = true;
					if (written < maxOut)
						outPips[written++] = pip;
				}
			}

			return written;
		}

		std::string FormatTile(const DominoHandEval::Tile& tile)
		{
			if (!tile.IsValid())
				return "--";

			std::ostringstream oss;
			oss << "[" << tile.low << "|" << tile.high << "]";
			return oss.str();
		}

		// Counts how many tiles across every OTHER occupied seat contain
		// `pip` (a tile with pip on both sides, i.e. a double, still
		// counts once) -- a direct blocking-strength measure ONLY because
		// every real game observed this session dealt all 4 seats (deck
		// cursor always 28, no boneyard left), meaning every one of the
		// 28 tiles is accounted for across the 4 hands with nothing
		// hidden: if this comes back 0, no opponent can respond to that
		// pip value at all. With fewer than 4 seats occupied (a real
		// boneyard to draw from), this UNDERSTATES an opponent's ability
		// to eventually respond by drawing -- not accounted for here.
		std::int32_t CountPipAcrossOpponents(rage::scrThread* thread, std::int32_t mySeat, std::int32_t pip)
		{
			std::int32_t total = 0;
			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
			{
				if (static_cast<std::int32_t>(seat) == mySeat)
					continue;

				ScriptLocal seatLocal = SeatLocal(thread, seat);
				std::int32_t occupancyMarker = seatLocal.At(kSeatOccupancyOffset).AsInt32();
				if (occupancyMarker != static_cast<std::int32_t>(seat))
					continue;

				std::int32_t handCount = seatLocal.At(kSeatHandCountOffset).AsInt32();
				if (handCount < 0)
					handCount = 0;
				if (handCount > static_cast<std::int32_t>(kMaxHandCapacity))
					handCount = static_cast<std::int32_t>(kMaxHandCapacity);

				for (std::int32_t i = 0; i < handCount; i++)
				{
					DominoHandEval::Tile tile = ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));
					if (tile.low == pip || tile.high == pip)
						total++;
				}
			}
			return total;
		}

		struct MoveRecommendation
		{
			bool valid = false;
			std::int32_t handIndex = -1;
			DominoHandEval::Tile tile;
			std::int32_t endPip = -1;
			std::int32_t resultPip = -1;
			std::int32_t opponentRespondCount = 0;
			bool isWinningMove = false;
		};

		// Combines FindPlayableTiles() (CONFIRMED LIVE) and
		// DetermineOpenEnds() (NOT yet live-tested) with pure local logic
		// -- no new native calls -- to recommend a move: an immediate WIN
		// (playing your last tile) always wins outright and is returned
		// the instant one is found; otherwise, among every (legal tile,
		// matching open end) pair, picks the one whose resulting NEW open
		// end the fewest opponent tiles can answer
		// (CountPipAcrossOpponents(), see that function's own header
		// comment for why this is an exact blocking measure specifically
		// because every session so far has been a full, boneyard-empty
		// 4-player game), tie-broken by the tile's own pip total (highest
		// first, matching the real game's own func_353 fallback
		// heuristic, dominoes_sp.ysc.c line ~14800). NOT yet live-tested.
		MoveRecommendation DetermineBestMove(rage::scrThread* thread, std::uint32_t mySeatU)
		{
			MoveRecommendation best;
			std::int32_t mySeat = static_cast<std::int32_t>(mySeatU);

			std::array<std::int32_t, kCandidateCapacity> legal{};
			std::uint32_t legalCount = FindPlayableTiles(thread, mySeatU, legal.data(), static_cast<std::uint32_t>(legal.size()));
			if (legalCount == 0)
				return best;

			std::array<std::int32_t, 7> ends{};
			std::uint32_t endCount = DetermineOpenEnds(thread, mySeatU, ends.data(), static_cast<std::uint32_t>(ends.size()));

			std::int32_t myHandCount = SeatLocal(thread, mySeatU).At(kSeatHandCountOffset).AsInt32();

			std::int32_t bestScore = -1;
			std::int32_t bestPipTotalTiebreak = -1;

			for (std::uint32_t li = 0; li < legalCount; li++)
			{
				std::int32_t handIndex = legal[li];
				if (handIndex < 0 || handIndex >= myHandCount)
					continue;

				DominoHandEval::Tile tile = ReadHandTile(thread, mySeatU, static_cast<std::uint32_t>(handIndex));
				if (!tile.IsValid())
					continue;

				if (myHandCount == 1)
				{
					// Playing your only remaining tile wins the round
					// outright -- nothing about blocking matters once
					// your hand is empty, so stop searching immediately.
					best.valid = true;
					best.handIndex = handIndex;
					best.tile = tile;
					best.isWinningMove = true;
					for (std::uint32_t ei = 0; ei < endCount; ei++)
					{
						if (tile.low == ends[ei] || tile.high == ends[ei])
						{
							best.endPip = ends[ei];
							best.resultPip = (tile.low == ends[ei]) ? tile.high : tile.low;
							break;
						}
					}
					return best;
				}

				// endCount==0 only happens if DetermineOpenEnds()
				// couldn't test all 7 pips (a hand with fewer than 7
				// tiles) and genuinely found none among what it could
				// test -- fall back to trying every pip 0-6 directly
				// rather than giving up on a recommendation entirely.
				std::uint32_t endsToTry = (endCount > 0) ? endCount : 7;
				for (std::uint32_t ei = 0; ei < endsToTry; ei++)
				{
					std::int32_t endPip = (endCount > 0) ? ends[ei] : static_cast<std::int32_t>(ei);
					if (tile.low != endPip && tile.high != endPip)
						continue;

					std::int32_t resultPip = (tile.low == endPip) ? tile.high : tile.low;
					std::int32_t score = CountPipAcrossOpponents(thread, mySeat, resultPip);
					std::int32_t pipTotal = tile.PipTotal();

					bool better = (bestScore < 0) || (score < bestScore) || (score == bestScore && pipTotal > bestPipTotalTiebreak);
					if (better)
					{
						bestScore = score;
						bestPipTotalTiebreak = pipTotal;
						best.valid = true;
						best.handIndex = handIndex;
						best.tile = tile;
						best.endPip = endPip;
						best.resultPip = resultPip;
						best.opponentRespondCount = score;
						best.isWinningMove = false;
					}
				}
			}

			return best;
		}

#ifndef _DEBUG
		constexpr float kPanelX = 0.015f;
		constexpr float kPanelY = 0.30f;
		constexpr float kTextScale = 0.32f;
		constexpr float kTitleTextScale = 0.38f;
#endif

		void DrawLine(float x, float y, const char* text, bool title = false)
		{
#ifdef _DEBUG
			const Config::Values& cfg = Config::Get();
			float textScale = title ? cfg.TitleTextScale : cfg.TextScale;
#else
			float textScale = title ? kTitleTextScale : kTextScale;
#endif
			UI::SET_TEXT_SCALE(0.0f, textScale);
			UI::SET_TEXT_COLOR_RGBA(235, 222, 194, 235);
			UI::SET_TEXT_CENTRE(0);
			UI::SET_TEXT_DROPSHADOW(1, 0, 0, 0, 200);
			UI::DRAW_TEXT(GAMEPLAY::CREATE_STRING(10, const_cast<char*>("LITERAL_STRING"), const_cast<char*>(text)), x, y);
		}

		// Draws `text` directly over tile `rawTileIndex`'s real 3D prop
		// (GetTilePropHandle(), see that function's own header comment)
		// by projecting its live world position to screen -- the exact
		// same ENTITY::GET_ENTITY_COORDS + GRAPHICS::GET_SCREEN_COORD_
		// FROM_WORLD_COORD technique PokerCheat's own community-card
		// objects use. No-ops if the prop doesn't exist or projects
		// off-screen (behind the camera, etc.) rather than drawing
		// garbage coordinates. The prop-resolution/coordinate half of
		// this is CONFIRMED LIVE (ProbeTileProps(), 2026-09-13 -- see
		// kSceneTilePropArrayFieldOffset's own comment); actually seeing
		// the drawn text land on the right physical tile on screen is
		// not yet separately confirmed.
		void DrawWorldMarkerOnTile(rage::scrThread* thread, std::int32_t rawTileIndex, const char* text)
		{
			std::int32_t handle = GetTilePropHandle(thread, rawTileIndex);
			if (handle == 0 || !ENTITY::DOES_ENTITY_EXIST(handle))
				return;

			Vector3 coords = ENTITY::GET_ENTITY_COORDS(handle, true, true);

			float screenX = 0.0f, screenY = 0.0f;
			if (!GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(coords.x, coords.y, coords.z, &screenX, &screenY))
				return;

			UI::SET_TEXT_SCALE(0.0f, 0.35f);
			UI::SET_TEXT_COLOR_RGBA(255, 240, 120, 255);
			UI::SET_TEXT_CENTRE(1);
			UI::SET_TEXT_DROPSHADOW(1, 0, 0, 0, 220);
			UI::DRAW_TEXT(GAMEPLAY::CREATE_STRING(10, const_cast<char*>("LITERAL_STRING"), const_cast<char*>(text)), screenX, screenY);
		}

		// Draws every occupied seat's hand plus the undrawn boneyard, as
		// plain debug text. Your own seat (FindMySeatByPed(), see file
		// header comment's "My seat" section) is always shown regardless
		// of ShowOpponentHands and marked "(you)" -- showing your own
		// already-visible hand isn't the cheat, hiding opponents' is. Also
		// shows whose turn it currently is (kCurrentTurnSeatFieldOffset,
		// CONFIRMED LIVE both via func_76/func_167's own read/write sites
		// and visually against the real screen -- see that constant's own
		// comment). No calibrated icon
		// overlay yet (unlike PokerCheat/BlackjackCheat's own Release
		// HUDs) -- this is a deliberate, temporary deviation from that
		// convention: with zero live confirmation of ANY offset here,
		// there's nothing to calibrate icon positions against yet, so
		// this stays Release+Debug plain text until a live session
		// confirms the struct layout enough to be worth the calibration
		// pass (see docs/JOURNAL.md-equivalent notes once this project has
		// one).
		void DrawOverlay(rage::scrThread* thread)
		{
			const Config::Values& cfg = Config::Get();

#ifdef _DEBUG
			float x = cfg.PanelX;
			float y = cfg.PanelY;
			float lineHeight = 0.022f;
#else
			float x = kPanelX;
			float y = kPanelY;
			float lineHeight = 0.022f;
#endif

			DrawLine(x, y, "DominoCheat", true);
			y += lineHeight * 1.4f;

			std::int32_t deckCursor = RoundLocal(thread).At(kDeckCursorFieldOffset).AsInt32();
			std::int32_t mySeat = FindMySeatByPed(thread);
			std::int32_t turnSeat = RoundLocal(thread).At(kCurrentTurnSeatFieldOffset).AsInt32();
			std::int32_t turnSubState = RoundLocal(thread).At(kTurnSubStateFieldOffset).AsInt32();

			{
				std::ostringstream turnLine;
				turnLine << "Turn: seat " << turnSeat << (turnSeat == mySeat ? " (you)" : "") << ", state " << turnSubState;
				DrawLine(x, y, turnLine.str().c_str());
				y += lineHeight;
			}

			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
			{
				ScriptLocal seatLocal = SeatLocal(thread, seat);
				std::int32_t occupancyMarker = seatLocal.At(kSeatOccupancyOffset).AsInt32();
				if (occupancyMarker != static_cast<std::int32_t>(seat))
					continue; // not dealt/occupied per kSeatOccupancyOffset's convention

				bool isMySeat = (mySeat == static_cast<std::int32_t>(seat));

				// Clamped to kMaxHandCapacity, NOT kHandSize -- a hand can
				// grow past its initial 7 tiles via boneyard draws (see
				// kMaxHandCapacity's own comment); clamping to 7 here would
				// silently hide real tiles the moment a seat draws its
				// first extra one.
				std::int32_t handCount = seatLocal.At(kSeatHandCountOffset).AsInt32();
				if (handCount < 0)
					handCount = 0;
				if (handCount > static_cast<std::int32_t>(kMaxHandCapacity))
					handCount = static_cast<std::int32_t>(kMaxHandCapacity);

				// For your own seat, mark which tiles the game itself
				// currently considers playable (FindPlayableTiles(), see
				// file header comment's "Session 4 addition" -- CONFIRMED
				// LIVE against a real board with open ends 6 and 1).
				std::array<std::int32_t, kCandidateCapacity> playable{};
				std::uint32_t playableCount = 0;
				if (isMySeat)
					playableCount = FindPlayableTiles(thread, seat, playable.data(), static_cast<std::uint32_t>(playable.size()));

				std::ostringstream line;
				line << "Seat " << seat << (isMySeat ? " (you, " : " (") << handCount << " tiles): ";
				for (std::int32_t i = 0; i < handCount; i++)
				{
					line << FormatTile(ReadHandTile(thread, seat, static_cast<std::uint32_t>(i)));
					if (isMySeat && IsHandIndexPlayable(playable.data(), playableCount, i))
						line << "*";
					line << " ";
				}
				if (isMySeat)
					line << (playableCount > 0 ? "  (* = playable now)" : "  (no playable tile found)");

				if (!isMySeat && !cfg.ShowOpponentHands)
					line.str("Seat " + std::to_string(seat) + " (hidden -- ShowOpponentHands off)");

				DrawLine(x, y, line.str().c_str());
				y += lineHeight;

				// Best-move recommendation -- see DetermineBestMove()'s
				// own header comment. Only shown when it's genuinely your
				// turn (a live bug report showed this appearing during
				// other turn sub-states, e.g. state 4, which isn't a
				// state where acting on the advice makes sense). NOT yet
				// live-tested.
				if (isMySeat && turnSeat == static_cast<std::int32_t>(seat))
				{
					MoveRecommendation rec = DetermineBestMove(thread, seat);
					if (rec.valid)
					{
						std::ostringstream recLine;
						if (rec.isWinningMove)
							recLine << "WINNING MOVE: play " << FormatTile(rec.tile) << " to empty your hand!";
						else
							recLine << "Best move: " << FormatTile(rec.tile) << " on end " << rec.endPip
								<< " -> new end " << rec.resultPip << " (opponent tiles that answer: " << rec.opponentRespondCount << ")";
						DrawLine(x, y, recLine.str().c_str());
						y += lineHeight;

						// Mark the real, physical 3D tile itself -- see
						// FindTilePropForTileValue()'s own header comment
						// for the value-based (via .f_3, CONFIRMED LIVE)
						// lookup this uses. NOT yet live-tested end to
						// end (the lookup mechanism is confirmed; seeing
						// the drawn text land on the right physical tile
						// is not).
						std::int32_t rawTileValue = DominoHandEval::EncodeTile(rec.tile.low, rec.tile.high);
						std::int32_t propSlot = FindTilePropForTileValue(thread, static_cast<std::int32_t>(seat), rawTileValue);
						if (propSlot >= 0)
							DrawWorldMarkerOnTile(thread, propSlot, rec.isWinningMove ? "WINNING MOVE" : "PLAY THIS ONE");
					}
				}
			}

			if (cfg.ShowBoneyardPrediction)
			{
				std::int32_t remaining = static_cast<std::int32_t>(kTileSetSize) - deckCursor;
				if (remaining > 0 && remaining <= static_cast<std::int32_t>(kTileSetSize))
				{
					std::ostringstream line;
					line << "Boneyard (" << remaining << " left): ";
					for (std::int32_t i = deckCursor; i < static_cast<std::int32_t>(kTileSetSize); i++)
						line << FormatTile(ReadBoneyardTile(thread, i)) << " ";

					DrawLine(x, y, line.str().c_str());
					y += lineHeight;
				}
			}
		}
	}

	void OnTick()
	{
		if (!Enabled)
			return;

		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
			return;

		DrawOverlay(thread);
	}

#ifdef _DEBUG
	void ProbeTableStruct()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeTableStruct: dominoes_sp not running");
			return;
		}

		std::int32_t deckCursor = RoundLocal(thread).At(kDeckCursorFieldOffset).AsInt32();
		ScriptLocal seatsArrayField = SeatsHolderLocal(thread).At(kSeatsArrayFieldOffset);
		std::int32_t seatsHeader = seatsArrayField.AsInt32();
		std::int32_t turnSeat = RoundLocal(thread).At(kCurrentTurnSeatFieldOffset).AsInt32();
		std::int32_t turnSubState = RoundLocal(thread).At(kTurnSubStateFieldOffset).AsInt32();

		Log::Write("DominoCheat::ProbeTableStruct: kTableSlot={} RoundSlot={} SeatsHolderSlot={} SeatsHeaderSlot={} (raw={}, expect 4) DeckCursorSlot={} (raw={}, expect 0-28) BoneyardDataSlot={} TurnSeat={} TurnSubState={}",
			kTableSlot, RoundLocal(thread).Index(), SeatsHolderLocal(thread).Index(), seatsArrayField.Index(), seatsHeader,
			RoundLocal(thread).At(kDeckCursorFieldOffset).Index(), deckCursor, BoneyardDataLocal(thread).Index(), turnSeat, turnSubState);
	}

	void ProbeMySeat()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeMySeat: dominoes_sp not running");
			return;
		}

		std::int32_t myPed = static_cast<std::int32_t>(PLAYER::PLAYER_PED_ID());
		std::int32_t mySeat = FindMySeatByPed(thread);

		Log::Write("DominoCheat::ProbeMySeat: PLAYER_PED_ID()={} SceneSlot={} resolved mySeat={}",
			myPed, SceneLocal(thread).Index(), mySeat);

		for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
		{
			ScriptLocal pedLocal = ScenePedLocal(thread, seat).At(kScenePedHandleOffset);
			std::int32_t pedHandle = pedLocal.AsInt32();
			Log::Write("  seat {} ped handle (slot {}) = {}{}",
				seat, pedLocal.Index(), pedHandle, (static_cast<std::int32_t>(seat) == mySeat) ? "  <-- matches PLAYER::PLAYER_PED_ID()" : "");
		}
	}

	void ProbeSeatHands()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeSeatHands: dominoes_sp not running");
			return;
		}

		for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
		{
			ScriptLocal seatLocal = SeatLocal(thread, seat);
			std::int32_t occupancyMarker = seatLocal.At(kSeatOccupancyOffset).AsInt32();
			std::int32_t activeFlag = seatLocal.At(kSeatActiveFlagOffset).AsInt32();
			std::int32_t score = seatLocal.At(kSeatScoreOffset).AsInt32();
			std::int32_t handCount = seatLocal.At(kSeatHandCountOffset).AsInt32();

			// Dumps up to kMaxHandCapacity (not just kHandSize) raw slots
			// regardless of handCount -- deliberately shows stale leftover
			// data past the real count too (this is how the earlier
			// duplicate-tile pattern that confirmed f_3, not a raw scan,
			// is the trustworthy count was originally spotted), and now
			// also covers the range a boneyard-grown hand could occupy.
			std::ostringstream tiles;
			for (std::int32_t i = 0; i < static_cast<std::int32_t>(kMaxHandCapacity); i++)
			{
				auto tile = ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));
				tiles << FormatTile(tile) << " ";
			}

			Log::Write("DominoCheat::ProbeSeatHands: seat {} (base slot {}): occupancyMarker={} (expect =={} if dealt) activeFlag={} score={} handCount={} tiles={}",
				seat, seatLocal.Index(), occupancyMarker, seat, activeFlag, score, handCount, tiles.str());
		}
	}

	void ProbeBoneyard()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeBoneyard: dominoes_sp not running");
			return;
		}

		std::int32_t deckCursor = RoundLocal(thread).At(kDeckCursorFieldOffset).AsInt32();

		std::ostringstream tiles;
		for (std::int32_t i = deckCursor; i < static_cast<std::int32_t>(kTileSetSize); i++)
			tiles << FormatTile(ReadBoneyardTile(thread, i)) << " ";

		Log::Write("DominoCheat::ProbeBoneyard: deckCursor={} remaining={} tiles={}",
			deckCursor, static_cast<std::int32_t>(kTileSetSize) - deckCursor, tiles.str());
	}

	void ProbeLegalMoves()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeLegalMoves: dominoes_sp not running");
			return;
		}

		std::int32_t mySeat = FindMySeatByPed(thread);
		if (mySeat < 0)
		{
			Log::Write("DominoCheat::ProbeLegalMoves: mySeat not resolved (FindMySeatByPed returned -1)");
			return;
		}

		void* handPtr = GamePointers::GetScriptLocalAddress(thread, SeatLocal(thread, static_cast<std::uint32_t>(mySeat)).At(kSeatHandArrayFieldOffset).Index());

		std::array<std::int64_t, 1 + kCandidateCapacity * kCandidateStride> buffer{};
		buffer[0] = kCandidateCapacity;

		int count = MINIGAME::_FIND_PLAYABLE_HAND_TILES(reinterpret_cast<Any*>(handPtr), reinterpret_cast<Any*>(buffer.data()));

		std::ostringstream rawWords;
		for (std::size_t i = 0; i < buffer.size(); i++)
			rawWords << static_cast<std::int32_t>(buffer[i]) << " ";

		Log::Write("DominoCheat::ProbeLegalMoves: mySeat={} handPtr={:#x} native returned count={} raw buffer (76 words: header + 15x[hand idx, w1, w2, w3, sum])={}",
			mySeat, reinterpret_cast<std::uintptr_t>(handPtr), count, rawWords.str());

		std::int32_t handCount = SeatLocal(thread, static_cast<std::uint32_t>(mySeat)).At(kSeatHandCountOffset).AsInt32();
		std::array<std::int32_t, kCandidateCapacity> playable{};
		std::uint32_t playableCount = FindPlayableTiles(thread, static_cast<std::uint32_t>(mySeat), playable.data(), static_cast<std::uint32_t>(playable.size()));

		std::ostringstream resolved;
		for (std::uint32_t i = 0; i < playableCount; i++)
		{
			std::int32_t handIndex = playable[i];
			std::string tileStr = "(out of range)";
			if (handIndex >= 0 && handIndex < handCount)
				tileStr = FormatTile(ReadHandTile(thread, static_cast<std::uint32_t>(mySeat), static_cast<std::uint32_t>(handIndex)));
			resolved << "handIndex=" << handIndex << " -> " << tileStr << "  ";
		}

		Log::Write("DominoCheat::ProbeLegalMoves: resolved {} playable tile(s): {}", playableCount, resolved.str());
	}

	void ProbeOpenEnds()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeOpenEnds: dominoes_sp not running");
			return;
		}

		std::int32_t mySeat = FindMySeatByPed(thread);
		if (mySeat < 0)
		{
			Log::Write("DominoCheat::ProbeOpenEnds: mySeat not resolved (FindMySeatByPed returned -1)");
			return;
		}

		std::int32_t handCount = SeatLocal(thread, static_cast<std::uint32_t>(mySeat)).At(kSeatHandCountOffset).AsInt32();

		std::array<std::int32_t, 7> pips{};
		std::uint32_t pipCount = DetermineOpenEnds(thread, static_cast<std::uint32_t>(mySeat), pips.data(), static_cast<std::uint32_t>(pips.size()));

		std::ostringstream out;
		for (std::uint32_t i = 0; i < pipCount; i++)
			out << pips[i] << " ";

		Log::Write("DominoCheat::ProbeOpenEnds: mySeat={} handCount={} open end pip value(s) found: {}",
			mySeat, handCount, out.str().empty() ? std::string("(none)") : out.str());
	}

	void ProbeBestMove()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeBestMove: dominoes_sp not running");
			return;
		}

		std::int32_t mySeat = FindMySeatByPed(thread);
		if (mySeat < 0)
		{
			Log::Write("DominoCheat::ProbeBestMove: mySeat not resolved (FindMySeatByPed returned -1)");
			return;
		}

		MoveRecommendation rec = DetermineBestMove(thread, static_cast<std::uint32_t>(mySeat));
		if (!rec.valid)
		{
			Log::Write("DominoCheat::ProbeBestMove: mySeat={} -- no recommendation (no legal move found)", mySeat);
			return;
		}

		if (rec.isWinningMove)
		{
			Log::Write("DominoCheat::ProbeBestMove: mySeat={} WINNING MOVE: handIndex={} tile={} (empties your hand)",
				mySeat, rec.handIndex, FormatTile(rec.tile));
		}
		else
		{
			Log::Write("DominoCheat::ProbeBestMove: mySeat={} handIndex={} tile={} playOnEnd={} resultingNewEnd={} opponentTilesThatAnswer={}",
				mySeat, rec.handIndex, FormatTile(rec.tile), rec.endPip, rec.resultPip, rec.opponentRespondCount);
		}
	}

	// Consolidated (2026-09-13, to save F12 menu space -- was two
	// probes, ProbeTileProps + ProbeTilePropOwnership, superseded now
	// that ownership/value ARE confirmed -- see
	// kSceneTilePropArrayFieldOffset's own header comment) diagnostic:
	// for all 28 physical props, logs owner/value plus (for your own
	// seat's props specifically) the live entity coords/screen position,
	// which is the part still worth re-checking if a marker ever looks
	// wrong on screen.
	void ProbeTilePropOwnership()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeTilePropOwnership: dominoes_sp not running");
			return;
		}

		std::int32_t mySeat = FindMySeatByPed(thread);
		Log::Write("DominoCheat::ProbeTilePropOwnership: mySeat={} (hand-owned prop's bare value == seat+{}, board-owned == {})",
			mySeat, kTilePropSeatOwnerBase, kTilePropBoardOwnerValue);

		for (std::int32_t propSlot = 0; propSlot < static_cast<std::int32_t>(DominoHandEval::kTileSetSize); propSlot++)
		{
			ScriptLocal prop = TilePropLocal(thread, propSlot);
			std::int32_t owner = prop.At(kTilePropOwnerFieldOffset).AsInt32();
			std::int32_t value = prop.At(kTilePropValueFieldOffset).AsInt32();
			std::int32_t handle = prop.At(kTilePropHandleOffset).AsInt32();

			bool isMine = (mySeat >= 0) && (owner == mySeat + kTilePropSeatOwnerBase);
			std::string tileStr = (isMine && value >= 0 && value < static_cast<std::int32_t>(DominoHandEval::kTileSetSize))
				? FormatTile(DominoHandEval::DecodeTile(value)) : "--";

			std::string coordStr = "(no handle)";
			if (handle != 0 && ENTITY::DOES_ENTITY_EXIST(handle))
			{
				Vector3 coords = ENTITY::GET_ENTITY_COORDS(handle, true, true);
				float screenX = 0.0f, screenY = 0.0f;
				bool onScreen = GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(coords.x, coords.y, coords.z, &screenX, &screenY);
				std::ostringstream cs;
				cs << "world=(" << coords.x << "," << coords.y << "," << coords.z << ") screen=";
				if (onScreen)
					cs << "(" << screenX << "," << screenY << ")";
				else
					cs << "off-screen";
				coordStr = cs.str();
			}

			Log::Write("DominoCheat::ProbeTilePropOwnership: propSlot={} owner={} value={} tile={} handle={}{} {}",
				propSlot, owner, value, tileStr, handle, isMine ? "  <-- mine" : "", coordStr);
		}
	}

	void DumpFullStackJsonl()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::DumpFullStackJsonl: dominoes_sp not running");
			return;
		}

		SYSTEMTIME st;
		GetLocalTime(&st);
		std::ostringstream name;
		name << "DominoCheat_stackdump_"
			<< st.wYear
			<< (st.wMonth < 10 ? "0" : "") << st.wMonth
			<< (st.wDay < 10 ? "0" : "") << st.wDay
			<< "_"
			<< (st.wHour < 10 ? "0" : "") << st.wHour
			<< (st.wMinute < 10 ? "0" : "") << st.wMinute
			<< (st.wSecond < 10 ? "0" : "") << st.wSecond
			<< ".jsonl";

		GamePointers::DumpLocalStackJsonl(thread, name.str());
	}
#endif
}
