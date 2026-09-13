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

	Session 8 (2026-09-13, release-prep pass) -- four presentation
	changes, NONE of which touch a struct offset, so none of them affect
	the confidence ratings above; all NEW code in this pass is itself
	NOT yet live-tested (positions in particular are placeholder guesses,
	flagged individually below), same "implemented, not yet confirmed"
	status DetermineBestMove()/DetermineOpenEnds() already carried into
	this session:
	  1. Font: DrawLine()'s plain UI::DRAW_TEXT/SET_TEXT_COLOR_RGBA are
	     nullsub on this game build (1491.50) -- the exact same finding
	     Poker/BlackjackCheat already made and documented in their own
	     ExtraNatives.h/DrawFontTest() header comments, just never
	     ported into this file until now (its own ExtraNatives.h has
	     carried the working UIDEBUG::_BG_DISPLAY_TEXT/_BG_SET_TEXT_COLOR
	     pair, unused, since the project was first scaffolded). Every
	     NEW draw call below goes through BgText()/DrawBgText(), the
	     $Font5 rich-text pipeline -- DrawLine() itself is now
	     Debug-only (see DrawOverlay()), backing only the raw diagnostic
	     panel, which never needed a real font.
	  2. Opponent hands: previously one line per seat inside that same
	     Debug-only-in-spirit (but not actually Debug-gated) raw panel,
	     stacked in a fixed corner keyed by raw seat index -- a live user
	     report called that "nonsense" once actually seen on screen (it
	     bears no relation to where opponents actually sit). Revised same
	     day: DrawOpponentHandStatus() instead places one row per occupied
	     OPPONENT seat (never your own) using ComputeDenseRowForSeat(),
	     the exact dense-relative-seat-offset technique PokerCheat's own
	     DrawSeatCardIcons() uses to sit "next to" each opponent's
	     on-screen name/stack panel -- see that function's own header
	     comment for the full mechanism and for why this is NOT
	     independently confirmed to rotate the same way for dominoes_sp's
	     4-seat table. Screen position (Config's OpponentHandBaseX/Y/
	     StepY) is copied from PokerCheat's own PRE-calibration starting
	     numbers, not this mod's own derived values -- this mod has never
	     run a DrawCalibrationGrid()-style pass the way PokerCheat's
	     final numbers were reached. Revised AGAIN the same day, per a
	     further user request/finding: the per-tile TEXT (FormatTile()'s
	     "[low|high]") is now real 2D tile-face icons instead, via
	     GRAPHICS::DRAW_SPRITE against the game's own "dominos_set_N"
	     texture dictionary -- the exact asset family PokerCheat's own
	     card_set_N icons use, CONFIRMED to exist for dominoes via two
	     independent sources: dominoes_sp.ysc.c's own func_267 (builds
	     "dominos_set_"+N) and func_862 (builds "DOMINO_<low>_<high>"
	     per-tile texture names, matching DominoHandEval::DecodeTile()'s
	     own numbering exactly), AND the user's own read of the game's
	     `ui_minigames.txt` asset manifest listing "dominos_set_1"
	     through "dominos_set_6" alongside poker's "card_set_1..9". See
	     BuildDominoTileTextureName()/FindLoadedDominoSetDict()'s own
	     header comments for the full citation trail. Icon size/spacing
	     (Config's OpponentTileIcon*) were ALSO copied from PokerCheat's
	     own numbers at first -- CONFIRMED LIVE the same day, user-tuned
	     via Reload Config against a real table (Width/Height=0.015/0.045,
	     SpacingX=0.015, LabelOffsetX=0.035 -- notably smaller/narrower
	     than poker's portrait-card starting guess, confirming a domino
	     tile face really is a different shape). Also added
	     WorldMarkerOffsetX/Y/FontSize to Config -- a live user report
	     found DrawWorldMarkerOnTile()'s original hardcoded -0.06f/0
	     nudge sitting over the tile's LEFT side rather than centering
	     the "PLAY THIS ONE"/"WINNING MOVE" text. CONFIRMED LIVE the same
	     day: -0.03f/0 (OffsetX halved, OffsetY unchanged) centers it
	     correctly -- FontSize (26) is still an untouched placeholder.
	     Also CONFIRMED LIVE the same day: the advice/marker were showing
	     during every sub-state of mySeat's own turn, not just the real
	     decision window -- gated on turnSubState==4 now (see the
	     DrawOverlay() call site's own updated comment, which supersedes
	     kTurnSubStateFieldOffset's original "4/5 both mean committing"
	     guess). Also split into two independent Config toggles (user
	     request, same day) -- ShowAdvice (the centered headline/safety
	     readout) and ShowPlayableDomino (the "PLAY THIS ONE"/"WINNING MOVE"
	     3D-tile text), previously always-on together with no way to
	     show one without the other. DetermineBestMove() itself is now
	     also skipped entirely when both are off, not just its two
	     draw calls -- no reason to pay for the native calls it makes if
	     nothing is going to render.
	  3. Thought-engine articulation: DetermineBestMove()'s result used
	     to only ever surface as one line inside the raw panel.
	     DrawMoveAdviceStatus()/DrawMoveSafetyStatus() add a standalone,
	     centered, real-font readout (same screen slot Poker's
	     DrawWinPredictionStatus()/Blackjack's DrawAdviceStatus() use)
	     showing the recommended tile plus a SAFE/RISKY/VERY RISKY
	     qualifier derived from opponentRespondCount. That qualifier is
	     deliberately SUPPRESSED whenever the boneyard still has undrawn
	     tiles (exactBlockingKnown in DrawOverlay()) -- CountPipAcrossOpponents()'s
	     own header comment is explicit its count is only an EXACT
	     blocking measure in a full, boneyard-empty 4-seat game; showing
	     a confident-looking safety label when tiles remain hidden in
	     the boneyard would overclaim exactly the kind of thing this
	     project's confidence-rating discipline otherwise never does.
	     DrawWorldMarkerOnTile()'s "PLAY THIS ONE"/"WINNING MOVE" text
	     also switched to the same real-font pipeline (localized).
	  4. Localization.h/.cpp (new files, ported from Poker/BlackjackCheat's
	     own) cover every string the three items above draw -- auto-
	     detected from LANGUAGE::_GET_CURRENT_LANGUAGE_ID(), overridable
	     via DominoCheat.ini's [General] Language key. Tile notation
	     itself ("[3|5]") stays language-agnostic digits, same as the
	     other two mods' own numeric HUD content.
*/

#include "DominoCheat.h"
#include "DominoHandEval.h"
#include "DominoSearch.h"
#include "AsyncMoveAdvisor.h"
#include "Log.h"
#include "GamePointers.h"
#include "ScriptLocal.h"
#include "Config.h"
#include "Localization.h"
#include "script.h"

#include <string>
#include <sstream>
#include <cstdint>
#include <array>
#include <cstring>
#include <vector>

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

		DominoAiPolicy::Rules ReadAiRules(rage::scrThread* thread)
		{
			// func_168 passes Round.f_666.f_3 to func_352; func_614 maps
			// the scoring hashes. Plain scalar fields, no array headers.
			// Statically traced in build 1491.50; not yet live-confirmed.
			return DominoAiPolicy::DecodeRules(RoundLocal(thread).At(666).At(3).AsInt32());
		}
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

	// CONFIRMED LIVE (2026-09-13, same day) -- the current table's
	// "dominos_set_N" skin index (0-based -- BuildDominoTileTextureName()/
	// FindLoadedDominoSetDict()'s own N is 1-based, so the real dict name
	// is "dominos_set_" + (Scene.f_6 + 1)). Traced purely from the
	// decompile in response to a user question ("is it possible to
	// figure out which dominos_set_ the current table uses"), then
	// live-confirmed via ProbeDominoSkin(): a real table read
	// Scene.f_6=5 (implying "dominos_set_6"), exactly matching
	// FindLoadedDominoSetDict()'s own independently-streamed answer --
	// MATCH. This ALSO settles the omitted-argument ambiguity the
	// derivation below raises: a live value of 5, not 0, proves the real
	// per-location code IS reaching func_59 some way this static trace
	// didn't find (the call site read as passing no explicit iParam5,
	// which would make func_2(0)=0 the only possible static result) --
	// Scene.f_6 is NOT hardcoded to always read 0 in practice.
	//
	// Derivation: func_267 (~line 12793, builds the literal
	// `"dominos_set_" + (N+1)` string for case 1 of the shared card/
	// domino-skin switch -- see BuildDominoTileTextureName()'s own
	// header comment) is called as `func_267(uParam0, iParam3)` from
	// func_128 (~line 7277), itself called from func_60 (~line 4828) as
	// `func_128(&(uParam0->f_1), 1, false, iParam1)` -- i.e. func_60's
	// OWN iParam1 IS this N. func_60 is called exactly once in the whole
	// file (~line 3469): `func_60(&(uParam0->f_2334), uParam0->f_1330.f_6)`
	// -- uParam0 there is func_22's own Table parameter (CONFIRMED to be
	// the same Table this file's own kTableSlot resolves, per this
	// file's header comment's func_22 citation), so `uParam0->f_1330.f_6`
	// is `Scene.f_6` -- a bare scalar field, no bracket-indexing/header-
	// word question the way every ARRAY field in this file has needed.
	//
	// Scene.f_6 is itself SET a few lines earlier in the SAME function
	// (func_22, ~line 3468) via `func_59(&(uParam0->f_1330), ...)`, whose
	// body (~line 4761) does `uParam0->f_6 = func_2(iParam5)` -- func_2
	// (~line 3038) is a location-code lookup table (`38->0, 98->1, 5->2,
	// 9->3, 69->5, default->0` -- notably no case ever produces 4) using
	// the exact same magic location codes (5/9/38/69/71/98) as a
	// SEPARATE nearby switch (~line 3406) that picks the table's own
	// win-point-target, strongly suggesting `iParam5` is meant to be
	// some per-camp/location identifier. HOWEVER: the ONE call site of
	// func_59 found (~line 3468) does not pass an explicit `iParam5`
	// argument at all -- omitted trailing arguments default to 0 in
	// RAGE script's own calling convention, meaning `func_2(0)` (the
	// default-case branch, itself 0) is ALL this specific code path can
	// ever produce, statically. Either the real location code is wired
	// in through a code path this trace hasn't found (this project's own
	// CLAUDE.md already flags an untraced "func_330/func_331-style
	// table/location-skin lookup" as exactly this kind of gap), or
	// Scene.f_6 genuinely is always 0 for dominoes_sp and the per-
	// location table only matters for some OTHER script/context that
	// happens to share func_2. ProbeDominoSkin() exists specifically to
	// settle this against a real table by comparing this field's value
	// to FindLoadedDominoSetDict()'s own already-independently-working
	// "ask what's actually streamed" answer.
	constexpr std::uint32_t kSceneDominoSkinFieldOffset = 6;

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

#ifdef _DEBUG
		void LogOpponentPredictions(rage::scrThread* thread, int mySeat)
		{
			const auto rules = ReadAiRules(thread);
			Log::Write("Scripted AI: rules={} policy={} (current board only; not after your hypothetical move)",
				DominoAiPolicy::RulesName(rules), DominoAiPolicy::AlwaysUsesPipPriority(rules)
					? "highest pip; search retains native-order ties" : "scoring/native-board dependent; search remains conservative");
			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
			{
				if (static_cast<int>(seat) == mySeat || SeatLocal(thread, seat).At(kSeatOccupancyOffset).AsInt32() != static_cast<int>(seat))
					continue;
				int handCount = SeatLocal(thread, seat).At(kSeatHandCountOffset).AsInt32();
				if (handCount <= 0 || handCount > static_cast<int>(kMaxHandCapacity))
					continue;
				std::array<DominoHandEval::Tile, kMaxHandCapacity> hand{};
				for (int i = 0; i < handCount; i++)
					hand[i] = ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));
				void* handPtr = GamePointers::GetScriptLocalAddress(thread, SeatLocal(thread, seat).At(kSeatHandArrayFieldOffset).Index());
				if (!handPtr)
					continue;
				std::array<std::int64_t, 1 + kCandidateCapacity * kCandidateStride> buffer{};
				buffer[0] = kCandidateCapacity;
				for (std::uint32_t i = 0; i < kCandidateCapacity; i++)
					buffer[1 + i * kCandidateStride + 3] = -1; // func_612's placement sentinel
				int count = MINIGAME::_FIND_PLAYABLE_HAND_TILES(reinterpret_cast<Any*>(handPtr), reinterpret_cast<Any*>(buffer.data()));
				if (count < 0 || count > static_cast<int>(kCandidateCapacity))
					continue;
				std::array<DominoAiPolicy::Candidate, kCandidateCapacity> candidates{};
				for (int i = 0; i < count; i++)
				{
					std::size_t offset = 1 + static_cast<std::size_t>(i) * kCandidateStride;
					candidates[i] = { static_cast<int>(buffer[offset]), buffer[offset + 1] != 0 || buffer[offset + 2] != 0,
						static_cast<int>(buffer[offset + 4]) };
				}
				int chosen = DominoAiPolicy::SelectCandidate(rules, hand.data(), handCount, candidates.data(), count);
				if (chosen < 0)
				{
					Log::Write("  seat {}: no supported native candidate (may need to draw/pass)", seat);
					continue;
				}
				const auto& choice = candidates[chosen];
				const auto& tile = hand[choice.handIndex];
				Log::Write("  seat {}: nativeCandidate={} handIndex={} tile=[{}|{}] resultingEndTotal={} scoringPoints={}",
					seat, chosen, choice.handIndex, tile.low, tile.high, choice.resultingEndTotal,
					DominoAiPolicy::ScoringPoints(rules, choice.resultingEndTotal));
			}
		}
#endif

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

		// Real 2D tile-face texture, same asset family PokerCheat's own
		// card_set_N/BuildCardTextureName() use, replacing the plain
		// FormatTile() text this file used to draw for opponent hands
		// (per user request/finding, 2026-09-13). Dictionary name
		// CONFIRMED via two independent sources: dominoes_sp.ysc.c's own
		// func_267 (line ~12793) builds it exactly as `"dominos_set_" +
		// (N+1)` (case 1 of a switch shared with poker/blackjack's own
		// `"card_set_" + (N+1)`, case 0/2 -- same function, same pattern,
		// different literal), written into a
		// "gameTokenSetTextureDictionary" DATABINDING string that drives
		// the game's own Scaleform hand-tile display; AND the user found
		// "dominos_set_1".."dominos_set_6" listed directly in the game's
		// own `ui_minigames.txt` asset manifest, alongside poker's
		// "card_set_1".."card_set_9" (a 9th poker skin exists with no
		// domino equivalent, so this file's own probe range should stop
		// at 6, not 8 -- see kDominoSetProbeHi below). Per-tile texture
		// NAME is `"DOMINO_" + low + "_" + high` (e.g. "DOMINO_5_6") --
		// CONFIRMED directly from dominoes_sp.ysc.c's func_862 (line
		// ~29683), a raw-tile-index-to-string switch statement whose 28
		// cases exactly match DominoHandEval::DecodeTile()'s own
		// triangular index<->{low,high} numbering (case 0="DOMINO_0_0",
		// case 1="DOMINO_0_1", ... case 27="DOMINO_6_6") -- so this
		// builds the string directly from a Tile's own low/high fields
		// rather than re-deriving func_862's switch, same simplification
		// PokerCheat's own BuildCardTextureName() makes relative to
		// poker_sp.ysc.c's own switch-based equivalent. func_863 (line
		// ~29807) then writes that name into the SAME per-slot
		// DATABINDING string func_270 (line ~12879) created empty as
		// `"textureName"` on a `"single_game_token"` UI item -- i.e. this
		// IS the exact same reveal panel the user observed showing
		// opponent tiles in 2D at round end, not a guessed-at lookalike
		// asset. NOT yet independently confirmed by actually seeing this
		// mod's own GRAPHICS::DRAW_SPRITE calls render a correct tile on
		// screen (the dictionary/name STRINGS are confirmed from the
		// decompile + asset manifest; GRAPHICS::DRAW_SPRITE itself is
		// unconfirmed for dominoes specifically, though it's the exact
		// same native Poker/BlackjackCheat already use successfully for
		// their own card_set_N icons on this build).
		std::string BuildDominoTileTextureName(const DominoHandEval::Tile& tile)
		{
			std::ostringstream oss;
			oss << "DOMINO_" << tile.low << "_" << tile.high;
			return oss.str();
		}

		// Same "probe which skin's dict the game already has streamed"
		// technique as PokerCheat's own FindLoadedCardSetDict() -- avoids
		// replicating dominoes_sp.ysc.c's own func_330/func_331-style
		// table/location-skin lookup (not traced here) by just asking
		// which dominos_set_N is ALREADY loaded, since the game itself
		// must have one streamed in to be showing its own tiles right
		// now. kDominoSetProbeHi is 6, not 8 like Poker's card_set probe
		// range -- the user's own ui_minigames.txt read found exactly
		// "dominos_set_1".."dominos_set_6" (poker's own manifest entry
		// goes to card_set_9, one skin further, with no domino
		// equivalent). Falls back to requesting dominos_set_1 if none
		// are found loaded yet (e.g. called before the table has
		// finished setting up) -- same fallback PokerCheat's own version
		// uses.
		constexpr int kDominoSetProbeLo = 1;
		constexpr int kDominoSetProbeHi = 6;

		bool FindLoadedDominoSetDict(std::string& outDict)
		{
			for (int n = kDominoSetProbeLo; n <= kDominoSetProbeHi; n++)
			{
				std::string candidate = "dominos_set_" + std::to_string(n);
				if (TEXTURE::HAS_STREAMED_TEXTURE_DICT_LOADED(const_cast<char*>(candidate.c_str())))
				{
					outDict = candidate;
					return true;
				}
			}

			return false;
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

		// Legal-move generation shared by both DetermineBestMove() paths
		// below: a hand tile is legal wherever it matches one of the
		// given open pip values -- exactly the rule DominoSearch.h's own
		// LegalMoves() uses internally, and exactly what
		// MINIGAME::_FIND_PLAYABLE_HAND_TILES was answering all along
		// (that native is textbook dominoes legality, not hidden game-
		// specific logic) -- so once DetermineOpenEnds() has told us
		// which pips are open, the native query FindPlayableTiles() adds
		// nothing here and is no longer called from DetermineBestMove()
		// (still used elsewhere: the HUD's own "*" marker on playable
		// tiles, and ProbeLegalMoves()). `endsToTry`/synthetic 0-6 range
		// covers the one real edge case DetermineOpenEnds() can't
		// resolve -- the very first move of a round, before anything's
		// been played, where endCount comes back 0 because nothing is
		// open yet rather than because a small hand couldn't be fully
		// tested.
		struct CandidateMove
		{
			std::int32_t handIndex;
			DominoHandEval::Tile tile;
			std::int32_t endPip;
			std::int32_t resultPip;
		};

		std::vector<CandidateMove> LocalLegalMovesFromHand(const DominoHandEval::Tile* hand, std::int32_t handCount, const std::int32_t* ends, std::uint32_t endCount)
		{
			std::vector<CandidateMove> moves;
			std::uint32_t endsToTry = (endCount > 0) ? endCount : 7;

			for (std::int32_t handIndex = 0; handIndex < handCount; handIndex++)
			{
				const DominoHandEval::Tile& tile = hand[handIndex];
				if (!tile.IsValid())
					continue;

				for (std::uint32_t ei = 0; ei < endsToTry; ei++)
				{
					std::int32_t endPip = (endCount > 0) ? ends[ei] : static_cast<std::int32_t>(ei);
					if (tile.low != endPip && tile.high != endPip)
						continue;

					std::int32_t resultPip = (tile.low == endPip) ? tile.high : tile.low;
					moves.push_back(CandidateMove{ handIndex, tile, endPip, resultPip });
				}
			}
			return moves;
		}

		// Advice is fresh only if ALL search inputs still match, including
		// opponents' hands when our hand and the open pip set are unchanged.
		struct DecisionKey
		{
			std::int32_t seat = -1;
			std::int32_t deckCursor = -1;
			int runtimeMs = 1000;
			DominoSearch::GameState state;

			bool operator==(const DecisionKey& o) const
			{
				return seat == o.seat && deckCursor == o.deckCursor && runtimeMs == o.runtimeMs && state == o.state;
			}
		};

		AsyncMoveAdvisor<DecisionKey>* g_moveAdvisor = nullptr;

		AsyncMoveAdvisor<DecisionKey>& MoveAdvisor()
		{
			static AsyncMoveAdvisor<DecisionKey> advisor;
			g_moveAdvisor = &advisor;
			return advisor;
		}

		void CancelMoveAdvice()
		{
			if (g_moveAdvisor)
				g_moveAdvisor->Cancel();
		}

		// Depth-limited paranoid minimax (DominoSearch.h) over every
		// seat's REAL hand -- see that header's own file comment for the
		// full rationale. Only sound when nothing is hidden: all 4 seats
		// dealt and the boneyard empty (`allHandsKnown` below, the same
		// scope CountPipAcrossOpponents()'s own header comment already
		// draws for its exact-blocking-measure claim). Falls back to the
		// ORIGINAL 1-ply "minimize immediate opponent replies" heuristic
		// whenever that doesn't hold (a real boneyard means opponents can
		// draw their way back into the game in ways this file doesn't
		// model, see DominoSearch.h's own scope notes) or when
		// endCount==0 (the opening move of a round, where "look ahead at
		// the board" is moot -- nothing has constrained anything yet).
		// The fast-heuristic branch stays fully synchronous (unchanged in
		// cost from before the minimax existed, never the source of the
		// freeze below).
		//
		// ASYNC (2026-09-13, second live-freeze fix): DrawOverlay() calls
		// this every single tick for the ENTIRE real-world decision
		// window (however long the player just looks at the screen
		// deciding, gated on turnSubState==4 -- see DrawOverlay()'s own
		// comment), not once per turn. A first fix memoized the deep-
		// search branch against the last hand/board it ran for, which
		// stopped the repeat computation but still meant the very FIRST
		// tick of every new decision ran the search synchronously,
		// blocking that frame for however long the search took. This
		// goes further per the user's own request: the deep-search
		// branch now only ever reads live memory into a GameState
		// snapshot (cheap, must stay on this thread) and hands it to
		// AsyncMoveAdvisor's background worker (see that header's own
		// file comment for why crossing that exact boundary is safe) --
		// this function itself never blocks on the search at all
		// anymore. The tradeoff: the very first tick (or two) of a new
		// decision shows no recommendation yet, until the worker
		// publishes a matching result. The worker publishes each complete
		// depth, refining advice within the configured wall-clock allowance.
		MoveRecommendation DetermineBestMove(rage::scrThread* thread, std::uint32_t mySeatU)
		{
			MoveRecommendation best;
			std::int32_t mySeat = static_cast<std::int32_t>(mySeatU);
			const auto& cfg = Config::Get();
			if (mySeatU >= kMaxSeats || RoundLocal(thread).At(kCurrentTurnSeatFieldOffset).AsInt32() != mySeat ||
				RoundLocal(thread).At(kTurnSubStateFieldOffset).AsInt32() != 4 || (!cfg.ShowAdvice && !cfg.ShowPlayableDomino))
			{
				CancelMoveAdvice();
				return best;
			}

			std::int32_t myHandCount = SeatLocal(thread, mySeatU).At(kSeatHandCountOffset).AsInt32();
			if (myHandCount <= 0 || myHandCount > static_cast<std::int32_t>(kMaxHandCapacity))
			{
				CancelMoveAdvice();
				return best;
			}

			std::array<DominoHandEval::Tile, kMaxHandCapacity> myHand{};
			for (std::int32_t i = 0; i < myHandCount; i++)
				myHand[i] = ReadHandTile(thread, mySeatU, static_cast<std::uint32_t>(i));

			std::array<std::int32_t, 7> ends{};
			std::uint32_t endCount = DetermineOpenEnds(thread, mySeatU, ends.data(), static_cast<std::uint32_t>(ends.size()));

			std::int32_t deckCursor = RoundLocal(thread).At(kDeckCursorFieldOffset).AsInt32();
			bool allHandsKnown = (deckCursor >= static_cast<std::int32_t>(kTileSetSize));

			if (!allHandsKnown || endCount == 0)
			{
				CancelMoveAdvice();
				std::vector<CandidateMove> moves = LocalLegalMovesFromHand(myHand.data(), myHandCount, ends.data(), endCount);
				if (moves.empty())
					return best;

				if (myHandCount == 1)
				{
					// Playing your only remaining tile wins the round
					// outright -- nothing about blocking matters once
					// your hand is empty.
					const CandidateMove& mv = moves.front();
					best.valid = true;
					best.handIndex = mv.handIndex;
					best.tile = mv.tile;
					best.endPip = mv.endPip;
					best.resultPip = mv.resultPip;
					best.isWinningMove = true;
					return best;
				}

				std::int32_t bestScore = -1;
				std::int32_t bestPipTotalTiebreak = -1;
				for (const CandidateMove& mv : moves)
				{
					std::int32_t score = CountPipAcrossOpponents(thread, mySeat, mv.resultPip);
					std::int32_t pipTotal = mv.tile.PipTotal();

					bool better = (bestScore < 0) || (score < bestScore) || (score == bestScore && pipTotal > bestPipTotalTiebreak);
					if (better)
					{
						bestScore = score;
						bestPipTotalTiebreak = pipTotal;
						best.valid = true;
						best.handIndex = mv.handIndex;
						best.tile = mv.tile;
						best.endPip = mv.endPip;
						best.resultPip = mv.resultPip;
						best.opponentRespondCount = score;
						best.isWinningMove = false;
					}
				}
				return best;
			}

			// Full-information path: ASYNC from here down. Build the
			// freshness key + pure-logic search state from live-read
			// hands (reusing myHand[] for mySeat rather than re-reading
			// it), then check whether the background worker has already
			// published a result for this EXACT decision.
			DecisionKey key;
			key.seat = mySeat;
			key.deckCursor = deckCursor;
			key.runtimeMs = Config::RuntimeMilliseconds(cfg.AdvisorRuntime);
			DominoSearch::GameState& state = key.state;
			state.rules = ReadAiRules(thread);
			int totalTiles = 0;
			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
			{
				ScriptLocal seatLocal = SeatLocal(thread, seat);
				std::int32_t occupancyMarker = seatLocal.At(kSeatOccupancyOffset).AsInt32();
				if (occupancyMarker != static_cast<std::int32_t>(seat))
					continue;

				state.occupied[seat] = true;
				bool isMe = (static_cast<std::int32_t>(seat) == mySeat);
				std::int32_t handCount = isMe ? myHandCount : seatLocal.At(kSeatHandCountOffset).AsInt32();
				if (handCount <= 0 || handCount > static_cast<std::int32_t>(kMaxHandCapacity) ||
					handCount > static_cast<std::int32_t>(DominoSearch::kMaxHandTiles))
				{
					CancelMoveAdvice();
					return best;
				}

				for (std::int32_t i = 0; i < handCount; i++)
				{
					DominoHandEval::Tile tile = isMe ? myHand[i] : ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));
					if (!tile.IsValid())
					{
						CancelMoveAdvice();
						return best; // Do not search a partial read or shift live hand indices.
					}
					state.hands[seat][i] = tile;
				}
				state.handCounts[seat] = handCount;
				totalTiles += handCount;
			}
			for (std::uint32_t ei = 0; ei < endCount; ei++)
				state.ends.pips[static_cast<std::size_t>(state.ends.count++)] = ends[ei];
			state.turnSeat = mySeat;

			// Finite game-tree bound, not an eight-ply policy limit. The
			// configured deadline controls how far iterative deepening gets.
			int maxSearchDepth = (totalTiles + 1) * DominoSearch::detail::OccupiedCount(state);
			auto& advisor = MoveAdvisor();
			advisor.SubmitJob(key, state, mySeat, maxSearchDepth, std::chrono::milliseconds(key.runtimeMs));
			AsyncMoveAdvisor<DecisionKey>::Published published = advisor.GetLatest();
			if (published.valid && published.key == key)
			{
				const DominoSearch::Recommendation& rec = published.rec;
				if (rec.valid)
				{
					best.valid = true;
					best.handIndex = static_cast<std::int32_t>(rec.handIndex);
					best.tile = rec.tile;
					best.endPip = rec.endPip;
					best.resultPip = rec.resultPip;
					best.isWinningMove = rec.isWinningMove;
					// Recomputed for display only -- DrawMoveSafetyStatus()'s
					// SAFE/RISKY/VERY RISKY qualifier is keyed on "opponent
					// tiles that can answer the resulting end", a distinct,
					// already-understood metric from the minimax score itself.
					best.opponentRespondCount = rec.isWinningMove ? 0 : CountPipAcrossOpponents(thread, mySeat, rec.resultPip);
				}
				return best;
			}

			// No result for this decision yet; never display stale advice.
			return best;
		}

		// Wraps `text` in the Scaleform rich-text tags needed to actually
		// render through the UIDEBUG::_BG_DISPLAY_TEXT pipeline --
		// UI::DRAW_TEXT/SET_TEXT_COLOR_RGBA (what DrawLine() below still
		// uses) are nullsub on this game build (1491.50), the same
		// finding Poker/BlackjackCheat already made and documented in
		// their own ExtraNatives.h/DrawFontTest() header comments --
		// $Font5 is the confirmed working replacement. Ported from
		// BlackjackCheat's identical WrapBgFormatText(). Every Release-
		// facing draw call added this session (DrawSeatHandStatus(),
		// DrawBoneyardStatus(), DrawMoveAdviceStatus(),
		// DrawMoveSafetyStatus(), DrawWorldMarkerOnTile()) goes through
		// this, via DrawBgText() below -- DrawLine()'s own pipeline stays
		// exactly as it was, now Debug-only (see DrawOverlay()).
		std::string BgText(const std::string& text, int fontSize)
		{
			std::ostringstream oss;
			oss << "<TEXTFORMAT RIGHTMARGIN='0'><P ALIGN='Left'><FONT FACE='$Font5' LETTERSPACING='0' SIZE='"
				<< fontSize << "'>~s~" << text << "</FONT></P><TEXTFORMAT>";
			return oss.str();
		}

		// Thin call-site wrapper around BgText() + the
		// UIDEBUG::_BG_SET_TEXT_COLOR/_BG_DISPLAY_TEXT pair -- every
		// real-font draw call below uses this instead of repeating the
		// three-line pattern Poker/BlackjackCheat's own call sites each
		// inline separately.
		void DrawBgText(const std::string& text, float x, float y, int fontSize, int r, int g, int b, int a = 255)
		{
			std::string formatText = BgText(text, fontSize);
			UIDEBUG::_BG_SET_TEXT_COLOR(r, g, b, a);
			UIDEBUG::_BG_DISPLAY_TEXT(GAMEPLAY::CREATE_STRING(10, const_cast<char*>("LITERAL_STRING"), const_cast<char*>(formatText.c_str())), x, y);
		}

#ifndef _DEBUG
		constexpr float kPanelX = 0.015f;
		constexpr float kPanelY = 0.30f;
		constexpr float kTextScale = 0.32f;
		constexpr float kTitleTextScale = 0.38f;
#endif

		// Backs ONLY the raw Debug diagnostic panel now (see
		// DrawOverlay()) -- UI::DRAW_TEXT/SET_TEXT_COLOR_RGBA are nullsub
		// in Release on this game build anyway (see BgText()'s own
		// header comment), so this was never a real Release HUD to begin
		// with; every user-facing element added this session uses
		// DrawBgText() instead.
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

#ifndef _DEBUG
		constexpr float kReleaseWorldMarkerOffsetX = -0.06f;
		constexpr float kReleaseWorldMarkerOffsetY = 0.0f;
		constexpr int kReleaseWorldMarkerFontSize = 26;
#endif

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

			// UI::SET_TEXT_CENTRE/DRAW_TEXT (this function's original
			// version) are nullsub on this game build -- switched to the
			// BgText()/DrawBgText() pipeline (see that function's own
			// header comment). That pipeline has no SET_TEXT_CENTRE
			// equivalent (PokerCheat.cpp's DrawSeatCardIcons() header
			// comment notes the same gap for $Font5 text), so this is
			// left-aligned from screenX rather than centered on it --
			// nudged by a configurable offset as a rough approximation
			// instead. A live user report (2026-09-13) found the
			// original hardcoded -0.06f nudge sat over the tile's LEFT
			// side instead of centering the text -- WorldMarkerOffsetX/Y
			// (Debug-tunable via Config, Release bakes the same starting
			// numbers) let this be adjusted live via F12 -> Reload Config
			// instead of a recompile per guess. The coordinate math
			// itself (GetTilePropHandle()/GET_SCREEN_COORD_FROM_WORLD_COORD)
			// is unchanged and already CONFIRMED LIVE (see this file's
			// header comment) -- only the offset/pipeline is still being
			// tuned.
			const Config::Values& cfg = Config::Get();
#ifdef _DEBUG
			float offsetX = cfg.WorldMarkerOffsetX;
			float offsetY = cfg.WorldMarkerOffsetY;
			int fontSize = static_cast<int>(cfg.WorldMarkerFontSize);
#else
			float offsetX = kReleaseWorldMarkerOffsetX;
			float offsetY = kReleaseWorldMarkerOffsetY;
			int fontSize = kReleaseWorldMarkerFontSize;
#endif
			DrawBgText(text, screenX + offsetX, screenY + offsetY, fontSize, 255, 240, 120);
		}

		// Maps each OPPONENT (non-you) seat to a DENSE row index (1, 2,
		// 3... no gaps) -- ported from PokerCheat's own
		// ComputeDenseRowForSeat() (see that function's header comment
		// for the full rationale): the real vanilla per-seat UI panel
		// COMPACTS, an empty/unoccupied seat doesn't leave a blank row,
		// so DrawOpponentHandStatus()'s own calibrated per-row positions
		// need to match that compacting instead of a fixed per-raw-seat-
		// index slot. Walks seats in decreasing raw index from mySeat,
		// wrapping mod kMaxSeats -- the SAME direction PokerCheat
		// confirmed LIVE for its own 6-seat table (mySeat=5, then seat
		// 4, 3, 2 going up the screen, i.e. the list counts DOWN from
		// your own seat number as it goes up). NOT independently
		// confirmed for dominoes_sp's 4-seat table -- this mod has never
		// live-tested whether its table/camera rotates seats relative to
		// you the same way poker_sp's does. Flagged explicitly: if a
		// live session shows an opponent's tiles next to the WRONG
		// avatar, this direction (or the whole "rotates relative to you"
		// assumption itself) is the first thing to re-check. outDenseRow
		// must have kMaxSeats entries; stays 0 for mySeat itself and for
		// any unoccupied seat.
		void ComputeDenseRowForSeat(rage::scrThread* thread, std::int32_t mySeat, int (&outDenseRow)[kMaxSeats])
		{
			for (std::uint32_t i = 0; i < kMaxSeats; i++)
				outDenseRow[i] = 0;

			if (mySeat < 0 || mySeat >= static_cast<std::int32_t>(kMaxSeats))
				return;

			int nextRow = 1;
			for (int rawOffset = 1; rawOffset < static_cast<int>(kMaxSeats); rawOffset++)
			{
				int otherSeat = (mySeat - rawOffset + static_cast<int>(kMaxSeats)) % static_cast<int>(kMaxSeats);
				std::int32_t occupancyMarker = SeatLocal(thread, static_cast<std::uint32_t>(otherSeat)).At(kSeatOccupancyOffset).AsInt32();
				if (occupancyMarker == otherSeat)
				{
					outDenseRow[otherSeat] = nextRow;
					nextRow++;
				}
			}
		}

#ifndef _DEBUG
		constexpr float kReleaseOpponentHandBaseX = 0.18f;
		constexpr float kReleaseOpponentHandBaseY = 0.83f;
		constexpr float kReleaseOpponentHandStepY = -0.0915f;
		constexpr float kReleaseOpponentTileIconLabelOffsetX = 0.05f;
		constexpr float kReleaseOpponentTileIconSpacingX = 0.022f;
		constexpr float kReleaseOpponentTileIconWidth = 0.02f;
		constexpr float kReleaseOpponentTileIconHeight = 0.045f;
		constexpr float kReleaseBoneyardX = 0.02f;
		constexpr float kReleaseBoneyardY = 0.30f;
		constexpr float kReleaseBoneyardTileIconLabelOffsetX = 0.055f; // CONFIRMED LIVE 2026-09-13, see Config::Values::BoneyardTileIconLabelOffsetX's own comment
		constexpr float kReleaseBoneyardTileIconSpacingX = 0.015f;
		constexpr float kReleaseBoneyardTileIconWidth = 0.015f;
		constexpr float kReleaseBoneyardTileIconHeight = 0.045f;
#endif

		// Real 2D tile icons -- one row per occupied OPPONENT seat (NEVER
		// your own -- you already see your own hand as real physical
		// tiles, same reasoning PokerCheat's DrawSeatCardIcons() uses for
		// opponent-only hole cards), each positioned via
		// ComputeDenseRowForSeat() the same way PokerCheat's own icon
		// strip sits "next to" each opponent's on-screen name/stack panel
		// instead of a fixed corner list keyed by raw seat index (a live
		// user report called the original absolute-seat-index TEXT
		// version of this "nonsense" -- that version then got replaced
		// with the dense-row TEXT version below, and THIS version
		// replaces the tile TEXT itself with the game's own real
		// "dominos_set_N"/"DOMINO_<low>_<high>" 2D tile-face sprites --
		// see BuildDominoTileTextureName()/FindLoadedDominoSetDict()'s
		// own header comments for the confirmation trail -- per a
		// further user request/finding, 2026-09-13). Skips a row
		// entirely when ShowOpponentHands is off, same as before. Drawn
		// FROM RIGHT WHERE the streamed dict actually already is (see
		// FindLoadedDominoSetDict()) -- if it isn't loaded yet, this
		// requests it and draws NOTHING this tick (same as PokerCheat's
		// own DrawSeatCardIcons(), not a text fallback -- the dict
		// streams in within a frame or two of the table loading, so a
		// tick or two of nothing here isn't worth a second code path).
		//
		// Screen position AND icon size/spacing: copied from PokerCheat's
		// OWN pre-calibration starting numbers as a reasonable jumping-
		// off point, NOT calibrated against dominoes_sp's own table/
		// camera OR the real "DOMINO_a_b" sprite's own aspect ratio at
		// all -- this mod has never run a DrawCalibrationGrid()-style
		// pass, a 4-seat table's layout may not even resemble poker's
		// 6-seat one, and a domino tile face is a different shape than a
		// playing card (poker's own 0.02x0.045 width/height was tuned
		// for a portrait-oriented card, not necessarily right for
		// whatever aspect dominos_set_N's own tile sprites actually are).
		// Debug builds retune live via Config's OpponentHandBaseX/Y/StepY
		// + OpponentTileIcon*/Reload Config; Release bakes in the same
		// placeholder numbers until a live session says otherwise.
		void DrawOpponentHandStatus(rage::scrThread* thread, std::int32_t mySeat)
		{
			const Config::Values& cfg = Config::Get();
			if (!cfg.ShowOpponentHands || mySeat < 0)
				return;

			std::string dominoSetDict;
			if (!FindLoadedDominoSetDict(dominoSetDict))
			{
				TEXTURE::REQUEST_STREAMED_TEXTURE_DICT(const_cast<char*>("dominos_set_1"), false);
				return;
			}

#ifdef _DEBUG
			float baseX = cfg.OpponentHandBaseX;
			float baseY = cfg.OpponentHandBaseY;
			float stepY = cfg.OpponentHandStepY;
			float labelOffsetX = cfg.OpponentTileIconLabelOffsetX;
			float iconSpacingX = cfg.OpponentTileIconSpacingX;
			float iconWidth = cfg.OpponentTileIconWidth;
			float iconHeight = cfg.OpponentTileIconHeight;
#else
			float baseX = kReleaseOpponentHandBaseX;
			float baseY = kReleaseOpponentHandBaseY;
			float stepY = kReleaseOpponentHandStepY;
			float labelOffsetX = kReleaseOpponentTileIconLabelOffsetX;
			float iconSpacingX = kReleaseOpponentTileIconSpacingX;
			float iconWidth = kReleaseOpponentTileIconWidth;
			float iconHeight = kReleaseOpponentTileIconHeight;
#endif

			int denseRow[kMaxSeats];
			ComputeDenseRowForSeat(thread, mySeat, denseRow);

			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
			{
				if (denseRow[seat] == 0)
					continue; // mySeat itself, or an unoccupied seat

				std::int32_t handCount = SeatLocal(thread, seat).At(kSeatHandCountOffset).AsInt32();
				if (handCount < 0)
					handCount = 0;
				if (handCount > static_cast<std::int32_t>(kMaxHandCapacity))
					handCount = static_cast<std::int32_t>(kMaxHandCapacity);

				float y = baseY + static_cast<float>(denseRow[seat] - 1) * stepY;

				std::ostringstream label;
				label << Localization::SeatWord() << " " << seat;
				DrawBgText(label.str(), baseX, y, 20, 255, 210, 140);

				float iconX = baseX + labelOffsetX;
				for (std::int32_t i = 0; i < handCount; i++)
				{
					DominoHandEval::Tile tile = ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));
					if (!tile.IsValid())
						continue;

					std::string textureName = BuildDominoTileTextureName(tile);
					GRAPHICS::DRAW_SPRITE(const_cast<char*>(dominoSetDict.c_str()), const_cast<char*>(textureName.c_str()), iconX, y, iconWidth, iconHeight, 0.0f, 255, 255, 255, 255, 0);
					iconX += iconSpacingX;
				}
			}
		}

		// Real-font boneyard readout -- Release-facing equivalent of the
		// raw panel's own boneyard line, same ShowBoneyardPrediction gate
		// (see DrawOverlay()). Own fixed placeholder position (Config's
		// BoneyardX/Y) rather than stacking below the opponent-hand list
		// -- that list's rows now scatter to per-seat calibrated
		// positions instead of a single top-to-bottom stack, so there's
		// no longer a single "next free y" to continue from.
		//
		// Real 2D tile icons (2026-09-13, per user request) -- replaced
		// the plain FormatTile() text list with the same real
		// "dominos_set_N"/"DOMINO_<low>_<high>" sprites
		// DrawOpponentHandStatus() already draws (see
		// BuildDominoTileTextureName()/FindLoadedDominoSetDict()'s own
		// header comments for the confirmation trail), same "draw
		// nothing this tick if the dict isn't streamed yet, no text
		// fallback" convention that function already established. Only
		// the "(N):" count in the label stays as plain text -- the tiles
		// themselves are icons now.
		void DrawBoneyardStatus(rage::scrThread* thread)
		{
			std::int32_t deckCursor = RoundLocal(thread).At(kDeckCursorFieldOffset).AsInt32();
			std::int32_t remaining = static_cast<std::int32_t>(kTileSetSize) - deckCursor;
			if (remaining <= 0 || remaining > static_cast<std::int32_t>(kTileSetSize))
				return;

			std::string dominoSetDict;
			if (!FindLoadedDominoSetDict(dominoSetDict))
			{
				TEXTURE::REQUEST_STREAMED_TEXTURE_DICT(const_cast<char*>("dominos_set_1"), false);
				return;
			}

			const Config::Values& cfg = Config::Get();
#ifdef _DEBUG
			float x = cfg.BoneyardX;
			float y = cfg.BoneyardY;
			float labelOffsetX = cfg.BoneyardTileIconLabelOffsetX;
			float iconSpacingX = cfg.BoneyardTileIconSpacingX;
			float iconWidth = cfg.BoneyardTileIconWidth;
			float iconHeight = cfg.BoneyardTileIconHeight;
#else
			float x = kReleaseBoneyardX;
			float y = kReleaseBoneyardY;
			float labelOffsetX = kReleaseBoneyardTileIconLabelOffsetX;
			float iconSpacingX = kReleaseBoneyardTileIconSpacingX;
			float iconWidth = kReleaseBoneyardTileIconWidth;
			float iconHeight = kReleaseBoneyardTileIconHeight;
#endif

			std::ostringstream label;
			label << Localization::BoneyardWord() << " (" << remaining << "):";
			DrawBgText(label.str(), x, y, 20, 200, 220, 255);

			float iconX = x + labelOffsetX;
			for (std::int32_t i = deckCursor; i < static_cast<std::int32_t>(kTileSetSize); i++)
			{
				DominoHandEval::Tile tile = ReadBoneyardTile(thread, i);
				if (!tile.IsValid())
					continue;

				std::string textureName = BuildDominoTileTextureName(tile);
				GRAPHICS::DRAW_SPRITE(const_cast<char*>(dominoSetDict.c_str()), const_cast<char*>(textureName.c_str()), iconX, y, iconWidth, iconHeight, 0.0f, 255, 255, 255, 255, 0);
				iconX += iconSpacingX;
			}
		}

#ifndef _DEBUG
		constexpr float kReleaseMoveAdviceX = 0.48f;
		constexpr float kReleaseMoveAdviceY = 0.5f;
		constexpr float kReleaseMoveSafetyYOffset = 0.045f;
#endif

		// Standalone move-advice headline -- articulates
		// DetermineBestMove()'s recommendation as a real-font, centered-
		// ish readout instead of one line buried in the raw Debug panel.
		// Same screen-center placeholder slot Poker's
		// DrawWinPredictionStatus()/Blackjack's DrawAdviceStatus() start
		// from (their own comments call this "a rough screen-center
		// starting point... not calibrated against anything" -- same
		// honesty applies here, doubly so since this mod has never run a
		// calibration pass at all). Shown in both Debug and Release,
		// gated by DrawOverlay() on it actually being mySeat's turn.
		void DrawMoveAdviceStatus(const MoveRecommendation& rec)
		{
			const Config::Values& cfg = Config::Get();
#ifdef _DEBUG
			float x = cfg.MoveAdviceX;
			float y = cfg.MoveAdviceY;
#else
			float x = kReleaseMoveAdviceX;
			float y = kReleaseMoveAdviceY;
#endif
			const char* headline = rec.isWinningMove ? Localization::WinningMoveLabel() : Localization::BestMoveLabel();
			int r = rec.isWinningMove ? 255 : 140, g = rec.isWinningMove ? 220 : 255, b = rec.isWinningMove ? 120 : 140;

			std::ostringstream line;
			line << headline << ": " << FormatTile(rec.tile);
			if (!rec.isWinningMove)
				line << " (" << rec.endPip << " -> " << rec.resultPip << ")";

			DrawBgText(line.str(), x, y, 40, r, g, b);
		}

		// Blocking-safety qualifier, drawn just below DrawMoveAdviceStatus()
		// -- same "second line, offset below the headline" convention
		// BlackjackCheat's DrawBettingAdviceStatus() uses relative to
		// DrawAdviceStatus(). Deliberately suppressed
		// (exactBlockingKnown=false, passed in by DrawOverlay() based on
		// whether the boneyard is empty) when the boneyard still has
		// undrawn tiles -- CountPipAcrossOpponents()'s own header comment
		// is explicit its count is only an EXACT blocking measure when
		// every one of the 28 tiles is already visible across the 4
		// hands (a full, boneyard-empty game); with tiles still in the
		// boneyard, an opponent could draw into a response this count
		// can't see, so a confident-looking SAFE/RISKY label there would
		// overclaim exactly the kind of thing this project's confidence
		// ratings are otherwise careful never to do.
		void DrawMoveSafetyStatus(const MoveRecommendation& rec, bool exactBlockingKnown)
		{
			if (rec.isWinningMove || !exactBlockingKnown)
				return;

			const Config::Values& cfg = Config::Get();
#ifdef _DEBUG
			float x = cfg.MoveAdviceX;
			float y = cfg.MoveAdviceY + 0.045f;
#else
			float x = kReleaseMoveAdviceX;
			float y = kReleaseMoveAdviceY + kReleaseMoveSafetyYOffset;
#endif

			Localization::BlockingSafety safety = Localization::ClassifyBlockingSafety(rec.opponentRespondCount);
			int r = 140, g = 255, b = 140;
			switch (safety)
			{
				case Localization::BlockingSafety::Safe: r = 140; g = 255; b = 140; break;
				case Localization::BlockingSafety::Risky: r = 255; g = 220; b = 140; break;
				case Localization::BlockingSafety::VeryRisky: r = 255; g = 140; b = 140; break;
			}

			DrawBgText(Localization::BlockingSafetyLabel(safety), x, y, 28, r, g, b);
		}

		// Raw Debug diagnostic panel (title/turn/per-seat/boneyard, via
		// DrawLine()'s plain UI::DRAW_TEXT pipeline) PLUS the real-font
		// Release+Debug HUD (DrawSeatHandStatus()/DrawBoneyardStatus()/
		// DrawMoveAdviceStatus()/DrawMoveSafetyStatus(), all added this
		// session -- see this file's header comment's "Session 8" entry).
		// The raw panel is now genuinely Debug-only (UI::DRAW_TEXT is
		// nullsub in Release regardless, see BgText()'s own header
		// comment, so it drew nothing there anyway) -- kept for exactly
		// the reason Poker/BlackjackCheat keep their own DrawLine()/
		// DrawPanel() panels Debug-only: a dev diagnostic surface every
		// Probe* menu item's own live-confirmation work has been checked
		// against.
		void DrawOverlay(rage::scrThread* thread)
		{
#ifdef _DEBUG
			// Raw diagnostic dump -- unchanged from before this session,
			// still the thing every Probe* menu item's own live
			// confirmation has been checked against. Never shown in
			// Release (DrawLine()'s UI::DRAW_TEXT is nullsub there
			// anyway, see BgText()'s own header comment).
			{
				const Config::Values& cfg = Config::Get();
				float x = cfg.PanelX;
				float y = cfg.PanelY;
				float lineHeight = 0.022f;

				DrawLine(x, y, "DominoCheat", true);
				y += lineHeight * 1.4f;

				std::int32_t debugMySeat = FindMySeatByPed(thread);
				std::int32_t debugTurnSeat = RoundLocal(thread).At(kCurrentTurnSeatFieldOffset).AsInt32();
				std::int32_t turnSubState = RoundLocal(thread).At(kTurnSubStateFieldOffset).AsInt32();

				{
					std::ostringstream turnLine;
					turnLine << "Turn: seat " << debugTurnSeat << (debugTurnSeat == debugMySeat ? " (you)" : "") << ", state " << turnSubState;
					DrawLine(x, y, turnLine.str().c_str());
					y += lineHeight;
				}

				for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
				{
					ScriptLocal seatLocal = SeatLocal(thread, seat);
					std::int32_t occupancyMarker = seatLocal.At(kSeatOccupancyOffset).AsInt32();
					if (occupancyMarker != static_cast<std::int32_t>(seat))
						continue; // not dealt/occupied per kSeatOccupancyOffset's convention

					bool isMySeat = (debugMySeat == static_cast<std::int32_t>(seat));

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

					if (isMySeat && debugTurnSeat == static_cast<std::int32_t>(seat) && turnSubState == 4 &&
						(cfg.ShowAdvice || cfg.ShowPlayableDomino))
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
						}
					}
				}

				if (cfg.ShowBoneyardPrediction)
				{
					std::int32_t deckCursor = RoundLocal(thread).At(kDeckCursorFieldOffset).AsInt32();
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
#endif

			// Real-font HUD (Release+Debug) -- see this file's header
			// comment's "Session 8" entry for what each piece is and why
			// it's still flagged NOT yet live-tested.
			const Config::Values& cfg = Config::Get();
			std::int32_t mySeat = FindMySeatByPed(thread);
			std::int32_t turnSeat = RoundLocal(thread).At(kCurrentTurnSeatFieldOffset).AsInt32();
			std::int32_t turnSubState = RoundLocal(thread).At(kTurnSubStateFieldOffset).AsInt32();

			// turnSubState==0 is func_76's own "idle/done" state -- the
			// gap between one turn's cycle finishing and the next seat's
			// actually starting (see this file's header comment's
			// Round.f_1299 entry: 0 (idle/done) -> 1 -> 2 -> 3 -> 4/5 -> 6
			// -> back to 0 every turn). Nothing real-font-HUD-worthy is
			// happening during that gap, so draw nothing at all rather
			// than risk a frame of stale/transitional hand or boneyard
			// data.
			if (turnSubState != 0)
			{
				DrawOpponentHandStatus(thread, mySeat);

				if (cfg.ShowBoneyardPrediction)
					DrawBoneyardStatus(thread);
			}

			// Best-move recommendation -- see DetermineBestMove()'s own
			// header comment. CONFIRMED LIVE (2026-09-13, user report):
			// the advice (and the "PLAY THIS ONE"/"WINNING MOVE" world
			// marker) must only render during sub-state 4 of mySeat's
			// own turn -- every OTHER sub-state (1/2/3/5/6, see
			// kTurnSubStateFieldOffset's own header comment for what
			// each was originally guessed to mean) showed it too under
			// the old turnSeat==mySeat-only gate, which the user
			// reported as wrong. This SUPERSEDES that field's own
			// header-comment guess that sub-states 4/5 both mean
			// "committing the chosen move" -- that reading was
			// explicitly flagged there as an unconfirmed switch-
			// structure guess, and this is the first live data point
			// actually pinning one of these states down: state 4 is the
			// real decision window, not a committing state.
			if (mySeat >= 0 && turnSeat == mySeat && turnSubState == 4 && (cfg.ShowAdvice || cfg.ShowPlayableDomino))
			{
				MoveRecommendation rec = DetermineBestMove(thread, static_cast<std::uint32_t>(mySeat));
				if (rec.valid)
				{
					if (cfg.ShowAdvice)
					{
						DrawMoveAdviceStatus(rec);

						std::int32_t deckCursor = RoundLocal(thread).At(kDeckCursorFieldOffset).AsInt32();
						bool exactBlockingKnown = (deckCursor >= static_cast<std::int32_t>(kTileSetSize));
						DrawMoveSafetyStatus(rec, exactBlockingKnown);
					}

					if (cfg.ShowPlayableDomino)
					{
						// Mark the real, physical 3D tile itself -- see
						// FindTilePropForTileValue()'s own header comment
						// for the value-based (via .f_3, CONFIRMED LIVE)
						// lookup this uses. NOT yet live-tested end to
						// end (the lookup mechanism is confirmed; seeing
						// the drawn text land on the right physical tile
						// is not).
						std::int32_t rawTileValue = DominoHandEval::EncodeTile(rec.tile.low, rec.tile.high);
						std::int32_t propSlot = FindTilePropForTileValue(thread, mySeat, rawTileValue);
						if (propSlot >= 0)
							DrawWorldMarkerOnTile(thread, propSlot, rec.isWinningMove ? Localization::WinningTileMarker() : Localization::PlayThisTileMarker());
					}
				}
			}
		}
	}

	void OnTick()
	{
		if (!Enabled)
		{
			CancelMoveAdvice();
			return;
		}

		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			CancelMoveAdvice();
			return;
		}

		const auto& cfg = Config::Get();
		int mySeat = FindMySeatByPed(thread);
		if (mySeat < 0 || RoundLocal(thread).At(kCurrentTurnSeatFieldOffset).AsInt32() != mySeat ||
			RoundLocal(thread).At(kTurnSubStateFieldOffset).AsInt32() != 4 || (!cfg.ShowAdvice && !cfg.ShowPlayableDomino))
			CancelMoveAdvice();

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

		LogOpponentPredictions(thread, mySeat);
		MoveRecommendation rec = DetermineBestMove(thread, static_cast<std::uint32_t>(mySeat));
		if (!rec.valid)
		{
			Log::Write("DominoCheat::ProbeBestMove: mySeat={} -- no player recommendation yet (inactive turn, pending search, or no legal move)", mySeat);
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

	// Diagnostic: reads Scene.f_6 (kSceneDominoSkinFieldOffset -- see its
	// own header comment for the full func_267/func_60/func_59/func_2
	// derivation trail) and logs it next to whatever
	// FindLoadedDominoSetDict() independently finds already streamed for
	// THIS same real table. CONFIRMED LIVE (2026-09-13): a real table
	// read Scene.f_6=5 vs FindLoadedDominoSetDict()="dominos_set_6" --
	// MATCH, and see kSceneDominoSkinFieldOffset's own comment for why a
	// nonzero live value also resolves this field's earlier omitted-
	// argument ambiguity. Wired to the F12 menu's "Probe Domino Skin"
	// item.
	void ProbeDominoSkin()
	{
		rage::scrThread* thread = GamePointers::FindScriptThread(DominoesScriptHash());
		if (!thread)
		{
			Log::Write("DominoCheat::ProbeDominoSkin: dominoes_sp not running");
			return;
		}

		std::int32_t sceneSkinField = SceneLocal(thread).At(kSceneDominoSkinFieldOffset).AsInt32();

		std::string streamedDict;
		bool foundStreamed = FindLoadedDominoSetDict(streamedDict);
		std::string streamedStr = foundStreamed ? streamedDict : std::string("(none streamed yet)");

		if (sceneSkinField >= 0 && sceneSkinField < kDominoSetProbeHi)
		{
			std::string impliedDict = "dominos_set_" + std::to_string(sceneSkinField + 1);
			bool match = foundStreamed && impliedDict == streamedDict;
			Log::Write("DominoCheat::ProbeDominoSkin: Scene.f_6={} (implies {}) vs FindLoadedDominoSetDict()={} -- {}",
				sceneSkinField, impliedDict, streamedStr, match ? "MATCH" : "MISMATCH");
		}
		else
		{
			Log::Write("DominoCheat::ProbeDominoSkin: Scene.f_6={} (outside the 0-{} range dominos_set_N covers -- offset is likely wrong, or this isn't really the skin field) vs FindLoadedDominoSetDict()={}",
				sceneSkinField, kDominoSetProbeHi - 1, streamedStr);
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
