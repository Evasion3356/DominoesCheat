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
	    ShowBoneyard to show -- only relevant with 1-3 occupied
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
	     StepY) now uses the current dominoes_sp user-tuned defaults,
	     mirrored by the Release constexpr values. Revised AGAIN the same
	     day, per a
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
	     SpacingX=0.015 -- notably smaller/narrower
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

	Session 11 (2026-09-13, decision-engine pass) -- a deep review of
	DominoSearch.h found that the win/loss-only evaluation, combined with
	how quickly the scripted-policy tree solves, made most live advice
	degenerate to the old highest-pip tiebreak (every move scored as the
	same "loss"). Rewritten around NET POINTS in the game's own payout
	(func_169/func_343/func_357 -- unique-lowest-total wins a block, tie
	pays nobody, All Fives/Threes round totals to the nearest 5/3),
	boneyard draws modeled from the known draw order (func_166/func_611,
	so 2/3-seat tables now get the real search instead of the 1-ply
	fallback, which is deleted), the opening move searched, and game-
	outcome awareness. Three NEW live reads feed the snapshot, all
	statically traced, none live-confirmed yet:
	  - seat.f_2 (kSeatScoreOffset, already documented above) and
	    Round.f_666.f_14[0] (kPointsTargetFieldOffset, bracket-indexed ->
	    At(0, 1) header convention) -- the accumulated score per seat
	    and the table's target. DetermineBestMove() sanity-checks both
	    (target 10..1000, every score in [0, target)) and silently drops
	    game-outcome awareness for the decision otherwise; ProbeBestMove()
	    logs the raw values for the live check against the scoreboard.
	  - The native candidate list's f_4 word (resulting board-end total)
	    for the LOCAL PLAYER's own hand at the root, on All Fives/All
	    Threes tables only, via QueryNativeCandidates() -- the same
	    buffer layout LogOpponentPredictions() already decoded, now also
	    used to credit an immediate scoring play (func_354's own rule).
	The undrawn boneyard read (ReadBoneyardTile(), CONFIRMED LIVE) is
	reused as-is. HUD: the advice line now appends the line's net points
	("+12", "-8", "~" prefix = horizon estimate) and DrawMoveSafetyStatus()
	maps the search verdict onto the existing SAFE/RISKY/VERY RISKY labels
	(see Localization.h). Builds clean, all unit tests pass (including an
	exhaustive net-points oracle with draws and rounding) -- NOT yet
	live-tested.

	Session 12 (2026-09-16) -- three live-driven fixes/additions, in the
	order a real user report and its own follow-up logs surfaced them:

	1. OPPONENT PREDICTION TIMING BUG, CONFIRMED LIVE AND FIXED: a live
	   session found EVERY SINGLE "native AI policy" opponent prediction
	   wrong (0/17 matches) despite DominoAiPolicy::SelectCandidate being
	   a verified, correct replica of func_352/353's own tie-break. Added
	   per-tick diagnostics (LogOpponentCandidateProbe(), raw candidate
	   buffer + an independently-computed DetermineOpenEnds() read, at
	   turnSubState 2/3/4) and confirmed the actual mechanism: an
	   opponent's turnSubState reaches 4 only AFTER its move already
	   commits (hand already short one tile, board already updated) --
	   unlike mySeat's own state 4, which is genuinely the pre-decision
	   human wait. States 2 and 3 are identical to each other and are the
	   real pre-move snapshot. Fixed by adding DecisionCaptureState()
	   (returns 4 for mySeat, 3 for everyone else) and keying the debug
	   trace's prediction capture on it. CONFIRMED LIVE afterward: 0
	   mismatches across several full sessions. Does NOT affect real
	   advice -- DominoSearch::OpponentMoves() never depended on this
	   native timing at all, computing legal high-pip replies from the
	   fully-known GameState with pure arithmetic.

	2. AMBIGUOUS-END BUG, CONFIRMED LIVE AND MITIGATED: a separate live
	   loss traced to a tile matching TWO different real open ends (e.g.
	   [0|3] against ends {0,3}) -- the search's own recommendation
	   ("play [0|3], end 0->3, GAME WIN +45, exact") was mathematically
	   correct for THAT specific placement (hand-verified against the
	   fully-known hands: the OTHER placement leads to a block where mySeat
	   has the unique lowest pip total, netting +45 -- exactly what was
	   claimed), but the tile got placed on the OTHER matching end in the
	   real game, turning a proven win into a real loss. Added
	   MoveRecommendation::hasAlternateEnd/alternateEndPip (computed in
	   DetermineBestMove() against the same real open-end read the search
	   used), surfaced as an explicit on-screen warning (color change +
	   Localization::AmbiguousEndWarning(), "TWO SPOTS FIT!") and in the
	   debug trace's prediction label. Also added a post-move verification
	   (re-run DetermineOpenEnds() after mySeat's own play and check
	   whether the recommended resultPip actually survived onto the real
	   board) that logs "prediction END MISMATCH" when it doesn't --
	   CONFIRMED LIVE to catch real wrong-end plays TWICE in one session
	   even with the warning shown (a 33% wrong-end rate on ambiguous
	   plays that session), proving the warning text alone doesn't fully
	   solve it.

	3. BoardTracker (2026-09-16/17, in response to (2) not fully solving
	   the problem and an explicit user decision to invest in the real
	   fix): a continuously-running, Debug+Release tracker that correlates
	   each observed play (a per-seat hand-diff, the same identity-diff
	   CountTilesByValue() technique the debug trace already used, moved
	   out of #ifdef _DEBUG so this can share it) with whichever of the 28
	   tile-props (Scene.f_746[i]) has its owner field transition from
	   that seat's own hand marker to the on-board value
	   (kTilePropBoardOwnerValue) -- giving, for the first time, a REAL
	   WORLD POSITION for a currently-open pip value, not just its numeric
	   label. GetPropForOpenPip(rec.endPip) feeds a second
	   DrawWorldMarkerOnTile() call (new Localization::PlayHereMarker(),
	   "PLAY HERE!") right on the physical board spot, alongside the
	   existing hand-tile marker. The rare ambiguous-tile case (matches
	   two tracked open ends) is disambiguated with one extra
	   DetermineOpenEnds() call, the same technique as (2)'s own post-move
	   verification.

	   THREE live-test rounds were needed before this actually worked, all
	   from the same wrong initial assumption (transition lands on the
	   SAME tick as the hand-count decrease): round 1 found 0/N
	   correlations and added a 300-TICK windowed retry (checking either
	   order, not just same-tick); round 2's window was STILL 0/N, which
	   led to an unfiltered raw dump of every prop owner change instead of
	   a filtered "did it match" check; round 3's raw dump was the one
	   that actually explained it -- the transition is real and lands
	   exactly on the tile that was played (one correlation even matched
	   under the old 300-tick window, proving the logic itself was sound),
	   but the true delay is a ~1.3-2.9s placement-slide animation, and
	   UpdateBoardTracker() measured out to run at ~165 ticks/s on the
	   test machine (from consecutive raw-dump log timestamps), not the
	   ~60/s the original window size assumed -- so 300 ticks was only
	   ~1.8s, just short of most real delays. Fixed by switching
	   PendingTransition/PendingPlay's correlation window from a tick
	   count to std::chrono::steady_clock wall-clock time (8s), immune to
	   however fast this actually ticks on a given machine. CONFIRMED LIVE
	   (2026-09-17): "PLAY HERE!" lands on the correct physical tile.
	   UpdateBoardTracker()'s own per-play correlation log and
	   LogBoardTrackerMap()'s resulting-map dump (both automatic, no F12
	   action needed) are left in place as a standing diagnostic, not
	   removed now that this works -- the same "keep the Probe" precedent
	   every other confirmed mechanism in this file already follows.

	   FOLLOW-UP (2026-09-17, same day): a live report of one hand with no
	   marker at all traced to a SEPARATE bug from the timing one above --
	   BoardTracker's own open-ends bookkeeping (patched incrementally per
	   play: remove the matched end, add the tile's other pip, mirroring
	   DominoSearch::OpenEnds's own hypothetical-search bookkeeping) had
	   DRIFTED from the real board: a stale pip was still sitting in its
	   internal set well after the real game had moved past it, confirmed
	   by comparing against LogOpponentCandidateProbe()'s own
	   independently-trusted open-end read (zero mismatches all session)
	   at the exact same moment. The recommended end was real and
	   correct -- the tracker's OWN copy of the board just didn't know it
	   was open, so GetPropForOpenPip() had nothing to return, silently,
	   no error logged. Fixed by deleting the incremental model entirely:
	   every detected play now re-reads the real open-end set fresh via
	   DetermineOpenEnds() and checks which of the played tile's own pips
	   the fresh read still shows open, rather than patching a
	   hand-rolled copy that can only ever drift further from the real
	   game the longer a round runs. One extra native-backed call per
	   detected play (a few times a minute) is the entire cost. Also
	   deleted the now-unreachable "ambiguous tile" special case this
	   made unnecessary (ground truth resolves it automatically) along
	   with RemoveOnePip()/ContainsPip(). Builds clean, tests pass.
	   NOT yet live-tested against this specific failure mode.

	4. DETERMINISTIC GHOST-POSITION FORMULA (2026-09-17, same day, per an
	   explicit user request to stop correlating and instead reverse the
	   actual mechanism): a live report of STILL-missing markers, plus the
	   user's own observation that the real game shows a "ghost" preview
	   domino while cycling placement choices with a "move key", prompted
	   tracing dominoes_sp.ysc.c's OWN ghost-preview code end to end rather
	   than continuing to patch BoardTracker's observational correlation.
	   Found:
	     - func_495 branches on func_32() (dev/debug build detection):
	       the DEV path calls func_347 directly (the same native this
	       file already uses, confirmed correct) and feeds its result
	       through func_736; the REAL (non-dev) path instead calls
	       func_229(Round, 4) -> MINIGAME::_DOMINOES_REQUEST_VALID_
	       PLACEMENTS -- a genuinely different, NAMED, ASYNC native this
	       file had never used. func_496 polls _MINIGAME_IS_REQUEST_
	       PENDING for completion. This only matters for WHICH CODE PATH
	       the real game takes -- our own direct func_347/
	       _0x3AE451860F03CA8A calls remain independently correct
	       (confirmed by 100+ opponent-prediction matches all session),
	       since the native itself doesn't care who calls it or through
	       which of the script's own two paths.
	     - func_226 is the per-turn state machine (case 0 fires the
	       request at turn start; case 1 waits for it; case 2/3 handle
	       the human's own cursor/selection UI once it's ready -- matches
	       why this file's own states 2/3 were already found to carry
	       correct, settled candidate data).
	     - func_501 populates Round.f_384[0..5], an array of TEMPORARY
	       GHOST OBJECT HANDLES (Object*, not one of the persistent 28
	       tile-props), one per native candidate sharing the currently-
	       browsed hand tile (Round.f_339, the cursor) -- i.e. exactly
	       the hasAlternateEnd scenario, one ghost per matching end.
	     - func_739 is what actually creates/repositions each ghost: it
	       computes a world Vector3 via func_843(Scene, candidate) and
	       calls OBJECT::CREATE_OBJECT/SET_ENTITY_COORDS with it directly
	       -- no correlation, no waiting on an animation, a closed-form
	       function of data already in hand.
	     - func_843/func_934 decode a candidate's own f_1/f_2 words (the
	       SAME "placement descriptor" func_613 already checks "not both
	       zero", now known to encode a literal 2D BOARD GRID COORDINATE)
	       plus f_3 (an orientation selector, tested against exactly
	       {0,2} vs anything else, picking a small recentering offset).
	     - func_935 turns that grid coordinate into world space:
	       (gridX*0.013125, gridY*0.013125, ~0.005) fed through
	       OBJECT::GET_OFFSET_FROM_COORD_AND_HEADING_IN_WORLD_COORDS
	       against Scene's OWN base coordinate (`*Scene`, its first 3
	       words as a Vector3) and heading (Scene.f_3) -- confirmed by a
	       SECOND, independent call site (func_137, the initial tile-pile
	       layout, an unrelated animation-driven code path) treating
	       `*Scene`/`Scene.f_3` the exact same way.
	   Ported the whole chain verbatim as ComputeCandidateWorldPosition()
	   plus DominoAiPolicy::Candidate's new gridF1/gridF2/gridOrientation
	   fields (populated in QueryNativeCandidates() from the SAME raw
	   buffer words this file already reads) and ScriptLocal::AsFloat()
	   (this project's first raw-float script-local read -- bit-
	   reinterpreted from the same 8-byte slot AsInt32() already uses,
	   never numerically converted). This is a CLOSED-FORM COMPUTATION,
	   not an observation: no prop-owner correlation, no placement-
	   animation delay to wait out, no drift possible.
	   VALIDATION WITHOUT A NEW LIVE TEST: LogCandidatePositionCrossCheck()
	   compares the formula's own computed position for every native
	   candidate matching the recommended hand tile against
	   BoardTracker's already-confirmed-live correlated prop's REAL
	   coordinate for the recommended pip, logging the distance between
	   them -- reusing BoardTracker's own proven-correct mechanism as
	   ground truth instead of requiring a dedicated new probe. Builds
	   clean (Debug + Release), all unit tests pass. NOT yet live-
	   confirmed -- the very next session's log has enough data to prove
	   or disprove this outright via the logged distances, whether or not
	   BoardTracker's own timing/correlation happens to cooperate that
	   session. If confirmed, this can replace BoardTracker's prop-
	   ownership correlation entirely for world-position purposes (the
	   OpenEnds ground-truth-resync logic from fix 3 above stays either
	   way -- it drives the search-facing pip bookkeeping, not just the
	   marker).

	5. (2026-09-17, same day): a prior session's LogCandidatePositionCrossCheck()
	   logs showed the formula's computed position consistently close to
	   BoardTracker's own tracked position for the same pip -- that
	   confirms the FORMULA itself, but NOT the same thing as this marker
	   actually rendering on screen, a distinction the write-up here
	   originally blurred (see fix 6 below). Per an explicit user request,
	   BoardTracker (the struct, its PendingTransition/PendingPlay queues,
	   and every helper -- GetPropForOpenPip()/ResetBoardTracker()/
	   TakeMatchingTransition()/PushPendingTransition()/TakeMatchingPlay()/
	   PushPendingPlay()/LogBoardTrackerMap()/UpdateBoardTracker(), plus
	   its own per-tick call in DrawOverlay()) has been removed outright --
	   the deterministic formula was the whole point of building it.
	   LogCandidatePositionCrossCheck() itself (a pure validation/logging
	   shim over BoardTracker's now-gone ground truth) is also removed,
	   replaced by ComputeRecommendedBoardPosition() -- the same
	   QueryNativeCandidates() filter-by-handIndex logic, minus the
	   logging, feeding the live "PLAY HERE!" marker directly via the new
	   DrawWorldMarkerAtPosition() (split out of DrawWorldMarkerOnTile()
	   so a computed Vector3 and a prop's live entity position share one
	   draw path). Builds clean (Debug + Release), all unit tests pass.

	6. Same day, live bug report: the marker from fix 5 above never
	   appeared at all in a real game (tested in Release; the underlying
	   code is identical in Debug -- this block sits outside every
	   #ifdef _DEBUG in DrawOverlay(), so the build configuration was a
	   red herring). Root cause: ComputeRecommendedBoardPosition() as
	   written for fix 5 only drew a marker when EXACTLY ONE native
	   candidate matched the recommended hand tile, on the untested
	   assumption (carried over from LogCandidatePositionCrossCheck()'s
	   own speculative comment, never actually verified against live
	   data) that more than one match only happens in the rare
	   hasAlternateEnd case. More likely: a real board commonly has more
	   than one PHYSICAL open end sharing the same pip value, which
	   QueryNativeCandidates() legitimately returns as separate candidates
	   for the same hand tile far more often than "rare" -- silently
	   suppressing the marker in exactly the ordinary case BoardTracker
	   used to handle by just picking "whichever one was most recently
	   observed" rather than refusing outright. Fixed by taking the FIRST
	   matching candidate instead of demanding uniqueness (still a
	   genuinely valid placement either way); the truly misleading case --
	   the SAME tile fitting two DIFFERENT pip values -- is already called
	   out separately via MoveRecommendation::hasAlternateEnd's own
	   on-screen warning, so it doesn't need a second gate here. Added a
	   Debug-only trace log of the native candidate count and match count
	   so the next live session has real data instead of another guess.
	   Builds clean (Debug + Release), all unit tests pass. NOT yet
	   live-tested -- this is a plausible fix based on code inspection,
	   not a confirmed one; the Debug log is there specifically to check
	   it next session.

	7. Same day, follow-up live report: fix 6's marker DID now appear, but
	   at a fixed spot near the bottom of the screen, nowhere close to the
	   real domino -- while "PLAY THIS ONE!" (the confirmed-working
	   hand-tile marker) landed correctly in the SAME log. First
	   suspected the per-tick trace logs themselves (~19000 near-identical
	   lines for one ~2-minute decision -- fixed by rate-limiting both new
	   traces to log only on an actual change, same "log on transition,
	   not every tick" convention every other trace in this file already
	   follows), then re-verified ComputeCandidateWorldPosition()'s
	   formula line-by-line against the actual decompile (func_935/
	   func_843/func_613/func_352, not just the prior session's own
	   summary of it) -- everything ported exactly, buffer layout
	   included. The rate-limited log then made the real cause visible
	   directly: pos.x-sceneX and pos.y-sceneY matched
	   gx/gy*0.013125 to four decimal places (the X/Y math is exact), and
	   pos.z-sceneZ matched fLocal_14+0.005 exactly too (assuming
	   fLocal_14=0, as originally assumed) -- but sceneZ ITSELF sat ~0.82
	   units below a real tile prop's actual height in the same log
	   entry. Scene's own base coordinate is apparently a floor/anchor
	   reference for the play area, not the tabletop surface -- the REAL
	   ghost-preview OBJECT the game creates compensates for this via its
	   own model's geometry (mesh pivot low, mesh extends up to table
	   height), which a flat 2D text marker drawn at the raw coordinate
	   never gets for free. Fixed NOT by guessing a correction constant,
	   but by overriding the computed position's Z with the recommended
	   hand tile's own real entity height (DrawOverlay() already resolves
	   this entity for "PLAY THIS ONE!", confirmed correct) -- sidesteps
	   the question of what Scene's Z actually represents entirely, since
	   every tile on the table sits at the same real height regardless.
	   The board marker is now gated on that same entity resolving
	   successfully (propHandle != 0), matching the existing "no marker
	   beats a wrong one" convention. Builds clean (Debug + Release), all
	   unit tests pass. CONFIRMED LIVE (2026-09-17, same day) -- a
	   follow-up user report confirmed "PLAY HERE!" now lands on the real
	   physical board position, matching "PLAY THIS ONE!" 's own
	   already-confirmed placement.
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
#include <string_view>
#include <charconv>
#include <sstream>
#include <cstdint>
#include <array>
#include <algorithm>
#include <cstring>
#include <vector>
#include <chrono>

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

	// Scene's OWN base world coordinate (f_0/f_1/f_2, a Vector3) and
	// heading (f_3, a float) -- CONFIRMED via decompile (2026-09-17,
	// dominoes_sp func_843/func_935, the exact functions that place the
	// game's OWN ghost-preview object for the currently-selected
	// candidate) by TWO independent call sites treating `*Scene` as a
	// Vector3 and `Scene.f_3` as a float heading the exact same way:
	// func_935 feeds both straight into
	// OBJECT::GET_OFFSET_FROM_COORD_AND_HEADING_IN_WORLD_COORDS (a pure
	// native, no entity involved -- confirms these are PLAIN floats
	// baked into the struct, not an entity handle to dereference), and
	// func_137 (the initial/reset tile-pile layout, a completely
	// separate code path using PED::GET_ANIM_INITIAL_OFFSET_POSITION
	// instead) feeds the identical `*Scene`/`Scene.f_3` pair into that
	// native's own coord/heading parameters. Bare scalar fields, no
	// bracket-indexing/header-word question the way every ARRAY field in
	// this file has needed. CONFIRMED LIVE (2026-09-17, see this file's
	// header comment's "Session 12" entry, part 5): the formula this
	// feeds, ComputeCandidateWorldPosition() below, was cross-checked
	// against BoardTracker's independently-tracked prop position and
	// matched closely every time -- BoardTracker (the observational
	// timing-correlation mechanism) has since been removed.
	constexpr std::uint32_t kSceneBaseCoordFieldOffset = 0; // 3 floats (x,y,z)
	constexpr std::uint32_t kSceneHeadingFieldOffset = 3;   // 1 float

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

		// Returns the live entity handle of ANY currently already-played
		// (board-owned, kTilePropBoardOwnerValue) tile prop, or 0 if none
		// exists yet (the opening move -- but the board marker is never
		// drawn then anyway, see its own caller's rec.endPip >= 0 gate).
		// Added 2026-09-17 to fix a live report of the board marker
		// floating a few inches above the table: an earlier version
		// borrowed the RECOMMENDED HAND TILE's own entity Z for height,
		// which is the wrong CATEGORY of reference -- hand tiles very
		// plausibly sit at a different real height than the flat board
		// area (a rack/holder in front of the player, not the table
		// surface itself), so even a confirmed-correct entity's Z was
		// wrong once reused at a different physical location on the
		// table. A board-owned prop is the same category of placement
		// (lying flat on the table) as the spot being marked, so its real
		// height is the right reference regardless of what the entity's
		// own pivot convention turns out to be.
		std::int32_t FindAnyBoardOwnedTilePropHandle(rage::scrThread* thread)
		{
			for (std::int32_t i = 0; i < static_cast<std::int32_t>(DominoHandEval::kTileSetSize); i++)
			{
				if (TilePropLocal(thread, i).At(kTilePropOwnerFieldOffset).AsInt32() == kTilePropBoardOwnerValue)
				{
					std::int32_t handle = GetTilePropHandle(thread, i);
					if (handle != 0 && ENTITY::DOES_ENTITY_EXIST(handle))
						return handle;
				}
			}
			return 0;
		}

		// Exact port of dominoes_sp.ysc.c's func_934+func_843+func_935 --
		// the chain the game's OWN ghost-preview object uses to turn a
		// native candidate's placement descriptor (gridF1/gridF2/
		// gridOrientation, i.e. words f_1/f_2/f_3 -- see
		// DominoAiPolicy::Candidate's own comment) into a REAL WORLD
		// COORDINATE, with no entity/prop lookup involved at all:
		//   func_934(f3): f3 == 0 || f3 == 2
		//   func_843: gx,gy = f1,f2; if func_934(f3): gx+=1, gy-=2;
		//             else: gx+=2, gy-=1
		//   func_935: offset = (gx*0.013125, gy*0.013125, ~0.005);
		//             return GET_OFFSET_FROM_COORD_AND_HEADING_IN_WORLD_
		//             COORDS(*Scene, Scene.f_3, offset)
		// func_935's own Z term is `fLocal_14 + 0.005` where fLocal_14 is
		// never assigned earlier in that function -- RAGE script locals
		// zero-init per call, so this is 0.005 in practice; a few
		// centimeters of Z error is inconsequential for a screen-space
		// text marker regardless, and WorldMarkerOffsetX/Y already exist
		// to absorb exactly this kind of small residual.
		//
		// The FORMULA itself was cross-checked (2026-09-17) against
		// BoardTracker's independently-tracked prop correlation and
		// logged close matches every time. This now drives the live
		// "PLAY HERE" board marker directly (see
		// ComputeRecommendedBoardPosition() and DrawOverlay()'s own
		// board-marker block) -- BoardTracker itself (the observational
		// timing-correlation mechanism) has since been removed. NOTE:
		// the cross-check confirms this formula, not that the marker
		// actually renders correctly -- a live report the same day the
		// draw path was wired in found nothing appeared at all, a
		// separate bug in the candidate-selection logic around this
		// function (see ComputeRecommendedBoardPosition()'s own comment
		// for the fix, still not itself live-confirmed).
		Vector3 ComputeCandidateWorldPosition(rage::scrThread* thread, int gridF1, int gridF2, int gridOrientation)
		{
			int gx = gridF1;
			int gy = gridF2;
			if (gridOrientation == 0 || gridOrientation == 2)
			{
				gx += 1;
				gy -= 2;
			}
			else
			{
				gx += 2;
				gy -= 1;
			}

			ScriptLocal scene = SceneLocal(thread);
			float sceneX = scene.At(kSceneBaseCoordFieldOffset).AsFloat();
			float sceneY = scene.At(kSceneBaseCoordFieldOffset + 1).AsFloat();
			float sceneZ = scene.At(kSceneBaseCoordFieldOffset + 2).AsFloat();
			float sceneHeading = scene.At(kSceneHeadingFieldOffset).AsFloat();

			constexpr float kGridCellSize = 0.013125f;
			constexpr float kZOffset = 0.005f; // see this function's own header comment
			return OBJECT::GET_OFFSET_FROM_COORD_AND_HEADING_IN_WORLD_COORDS(
				sceneX, sceneY, sceneZ, sceneHeading,
				static_cast<float>(gx) * kGridCellSize, static_cast<float>(gy) * kGridCellSize, kZOffset);
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

		// Defined further down, next to MoveRecommendation (shared with
		// DetermineBestMove()'s root scoring bonus).
		std::uint32_t QueryNativeCandidates(rage::scrThread* thread, std::uint32_t seat, DominoAiPolicy::Candidate* out, std::uint32_t maxOut);

		// Diffs two hand snapshots by TILE IDENTITY (via EncodeTile()) into
		// a per-value count, rather than by array index -- a played/drawn
		// tile shifts every later index down (see ReadHandTile()'s own
		// "COMPACTING hand-array index" comment), so comparing index-by-
		// index would misreport an unrelated shuffle-down as N tile swaps.
		// Shared by LogDebugTrace()'s own Debug-only hand-diff loop.
		void CountTilesByValue(const DominoHandEval::Tile* hand, std::int32_t count, std::array<int, DominoHandEval::kTileSetSize>& outCounts)
		{
			outCounts.fill(0);
			for (std::int32_t i = 0; i < count; i++)
			{
				if (!hand[i].IsValid())
					continue;
				std::int32_t idx = DominoHandEval::EncodeTile(hand[i].low, hand[i].high);
				if (idx >= 0 && idx < static_cast<std::int32_t>(DominoHandEval::kTileSetSize))
					outCounts[idx]++;
			}
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
				std::array<DominoAiPolicy::Candidate, kCandidateCapacity> candidates{};
				int count = static_cast<int>(QueryNativeCandidates(thread, seat, candidates.data(), static_cast<std::uint32_t>(candidates.size())));
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

		// Forward declaration -- defined further down, needed here by
		// PredictOpponentMove()'s own logging.
		std::string FormatTile(const DominoHandEval::Tile& tile);

		// Per-tick change-only debug trace (2026-09-16): unlike every other
		// diagnostic in this file, this one is NOT an on-demand F12 probe --
		// it runs unconditionally, every tick, whenever dominoes_sp is
		// found running (see LogDebugTrace()'s own call site in
		// DrawOverlay()), so a full session's DominoCheat.log captures
		// every hand change, turn transition, and predicted-vs-actual move
		// without the user needing to catch the right moment with F12.
		// Compiled ONLY in Debug -- this block and LogDebugTrace() (defined
		// further down, next to DrawOverlay()) are gated by #ifdef _DEBUG,
		// not a runtime toggle, so Release has none of this code at all,
		// same compile-time exclusion as every other Debug-only diagnostic
		// in this file.
		struct DebugHandSnapshot
		{
			std::array<DominoHandEval::Tile, kMaxHandCapacity> tiles{};
			std::int32_t count = -1; // -1 = seat not yet observed occupied since the last time it was empty
		};

		struct DebugPrediction
		{
			bool valid = false;
			DominoHandEval::Tile tile;
			std::string label;
			// mySeat only (-1 for opponent predictions, which don't carry an
			// end recommendation): which open end the search told us to play
			// on and what it becomes. Added 2026-09-16 after a live loss
			// traced to playing the recommended TILE on the WRONG of two
			// legal ends -- the old MATCH/MISMATCH check only compared tile
			// identity, so that exact failure logged as a false MATCH. See
			// the post-move verification in LogDebugTrace()'s hand-diff loop.
			std::int32_t endPip = -1;
			std::int32_t resultPip = -1;
		};

		struct DebugTraceState
		{
			std::array<DebugHandSnapshot, kMaxSeats> hands{};
			std::array<DebugPrediction, kMaxSeats> pending{};
			std::array<bool, kMaxSeats> predictionLogged{};
			std::int32_t lastTurnSeat = -2;
			std::int32_t lastTurnSubState = -2;
		};

		DebugTraceState g_debugTrace;

		// Predicts an OPPONENT seat's next play the same way the on-demand
		// LogOpponentPredictions() probe does (native candidate list +
		// DominoAiPolicy::SelectCandidate against the CURRENT board only) --
		// used by LogDebugTrace() to compare against what that seat
		// actually plays once its hand changes. Not used for mySeat --
		// LogDebugTrace() uses DetermineBestMove()'s own deep-search result
		// there instead, since that's the actual advice being given.
		DebugPrediction PredictOpponentMove(rage::scrThread* thread, std::uint32_t seat)
		{
			DebugPrediction pred;
			std::int32_t handCount = SeatLocal(thread, seat).At(kSeatHandCountOffset).AsInt32();
			if (handCount <= 0 || handCount > static_cast<std::int32_t>(kMaxHandCapacity))
				return pred;

			std::array<DominoHandEval::Tile, kMaxHandCapacity> hand{};
			for (std::int32_t i = 0; i < handCount; i++)
				hand[i] = ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));

			const auto rules = ReadAiRules(thread);
			std::array<DominoAiPolicy::Candidate, kCandidateCapacity> candidates{};
			int count = static_cast<int>(QueryNativeCandidates(thread, seat, candidates.data(), static_cast<std::uint32_t>(candidates.size())));
			int chosen = DominoAiPolicy::SelectCandidate(rules, hand.data(), handCount, candidates.data(), count);
			if (chosen < 0)
				return pred;

			pred.valid = true;
			pred.tile = hand[candidates[chosen].handIndex];
			std::ostringstream label;
			label << "native AI policy (" << DominoAiPolicy::RulesName(rules) << "): " << FormatTile(pred.tile);
			pred.label = label.str();
			return pred;
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
		// `verboseLog` (2026-09-16, added to chase a live discrepancy: the
		// native's own candidate list showed pip 4 open right after an
		// opponent played [4|4] onto an existing 4-end, while this
		// function's synthetic-double test missed it) logs every pass's
		// synthetic hand, the RAW candidate buffer the native returned for
		// it, and which entries got accepted/rejected -- specifically to
		// see whether a synthetic double colliding with a tile that's
		// ALREADY on the board (like testing pip 4 with a synthetic [4|4]
		// moments after the real [4|4] was played) gets silently rejected
		// by the native versus a normal, uncontested pip value. Off by
		// default (false) -- the real mySeat advice path calls this every
		// tick of the decision window and must not pay for or spam this.
		// LogOpponentCandidateProbe() is the only caller that turns it on.
		std::uint32_t DetermineOpenEnds(rage::scrThread* thread, std::uint32_t seat, std::int32_t* outPips, std::uint32_t maxOut, bool verboseLog = false)
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

				if (verboseLog)
				{
					// The synthetic hand this pass actually queried: doubles
					// {passStart+i, passStart+i} for i < thisPassCount, then
					// whatever REAL tiles occupy the remaining slots up to
					// handCount (untouched by this pass's overwrite) -- the
					// prime suspect for a collision, since a REAL tile the
					// opponent just played (like [4|4]) is often still
					// sitting one slot further into THIS seat's own hand.
					std::ostringstream handLine;
					for (std::uint32_t i = 0; i < thisPassCount; i++)
						handLine << "[" << (passStart + static_cast<std::int32_t>(i)) << "|" << (passStart + static_cast<std::int32_t>(i)) << "]synthetic ";
					for (std::int32_t i = static_cast<std::int32_t>(thisPassCount); i < handCount; i++)
					{
						std::size_t low = kSeatHandArrayFieldOffset + 1 + static_cast<std::size_t>(i) * 2;
						handLine << "[" << seatCopy[low] << "|" << seatCopy[low + 1] << "]real ";
					}

					std::ostringstream rawLine;
					for (int i = 0; i < count; i++)
					{
						std::size_t offset = 1 + static_cast<std::size_t>(i) * kCandidateStride;
						std::int64_t handIndex = buffer[offset], f1 = buffer[offset + 1], f2 = buffer[offset + 2], f3 = buffer[offset + 3];
						bool accepted = handIndex >= 0 && handIndex < static_cast<std::int64_t>(thisPassCount);
						rawLine << "[" << i << ": idx=" << handIndex << " f1=" << f1 << " f2=" << f2 << " f3=" << f3
							<< (accepted ? " ACCEPTED(synthetic)" : " rejected(real-tile-or-oob)") << "] ";
					}

					Log::Write("Trace: DetermineOpenEnds seat {} pass passStart={} thisPassCount={} hand=[{}] nativeCount={} raw={}",
						seat, passStart, thisPassCount, handLine.str(), count, rawLine.str());
				}

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

#ifdef _DEBUG
		// Debug-only text (the Release HUD uses AppendTile() instead).
		std::string FormatTile(const DominoHandEval::Tile& tile)
		{
			if (!tile.IsValid())
				return "--";

			std::ostringstream oss;
			oss << "[" << tile.low << "|" << tile.high << "]";
			return oss.str();
		}
#endif

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
			std::int32_t endPip = -1;    // -1 on the opening move (no open end yet -- any tile, opens both pips)
			std::int32_t resultPip = -1;
			std::int32_t opponentRespondCount = 0; // 1-ply "opponent tiles that answer the new end" -- Debug raw panel only now
			bool isWinningMove = false;
			// Search verdict (DominoSearch::Recommendation) -- see
			// DetermineBestMove(): net points for the recommended line
			// (positive = I win the round by that much, negative = the
			// winner is paid that much), whether the search finished
			// the round (exact) or hit its horizon (points is then an
			// estimate), and the outcome class.
			std::int32_t points = 0;
			bool exact = false;
			int completedDepth = 0;
			DominoSearch::Outcome outcome = DominoSearch::Outcome::Undecided;

			// Added 2026-09-16: true when the recommended TILE also legally
			// matches a DIFFERENT open end than the one recommended (a tile
			// with pips {a,b} where both a and b are currently open ends,
			// but only ONE of the two placements is the line the search
			// evaluated). A live loss traced directly to this -- the same
			// tile, played on the wrong end, turned a proven "GAME WIN +45"
			// into a real loss, and neither the on-screen advice nor the
			// debug trace's tile-only comparison caught it. DrawMoveAdviceStatus()
			// surfaces this as an explicit warning rather than silently
			// trusting the player to notice the tile fits twice.
			bool hasAlternateEnd = false;
			std::int32_t alternateEndPip = -1;
		};

		// Reads the scripted AI's own legal-move candidate list for a seat
		// (the same native FindPlayableTiles() calls, prepared exactly as
		// the script's func_350 prepares it -- func_612's f_3 = -1 sentinel
		// on every slot) and decodes the fields DominoAiPolicy::Candidate
		// needs: hand index (word 0, CONFIRMED LIVE), placement words
		// f_1/f_2 (func_613 treats "both zero" as no placement) and the
		// resulting board-end total f_4 (what func_352 hands to func_614's
		// multiple-of-five/three test). Shared by LogOpponentPredictions()
		// and DetermineBestMove()'s root scoring bonus.
		std::uint32_t QueryNativeCandidates(rage::scrThread* thread, std::uint32_t seat, DominoAiPolicy::Candidate* out, std::uint32_t maxOut)
		{
			void* handPtr = GamePointers::GetScriptLocalAddress(thread, SeatLocal(thread, seat).At(kSeatHandArrayFieldOffset).Index());
			if (!handPtr)
				return 0;

			std::array<std::int64_t, 1 + kCandidateCapacity * kCandidateStride> buffer{};
			buffer[0] = kCandidateCapacity;
			for (std::uint32_t i = 0; i < kCandidateCapacity; i++)
				buffer[1 + i * kCandidateStride + 3] = -1; // func_612's placement sentinel

			int count = MINIGAME::_FIND_PLAYABLE_HAND_TILES(reinterpret_cast<Any*>(handPtr), reinterpret_cast<Any*>(buffer.data()));
			if (count < 0 || count > static_cast<int>(kCandidateCapacity))
				return 0;

			std::uint32_t written = 0;
			for (int i = 0; i < count && written < maxOut; i++)
			{
				std::size_t offset = 1 + static_cast<std::size_t>(i) * kCandidateStride;
				DominoAiPolicy::Candidate candidate;
				candidate.handIndex = static_cast<int>(buffer[offset]);
				candidate.hasPlacement = buffer[offset + 1] != 0 || buffer[offset + 2] != 0;
				candidate.resultingEndTotal = static_cast<int>(buffer[offset + 4]);
				candidate.gridF1 = static_cast<int>(buffer[offset + 1]);
				candidate.gridF2 = static_cast<int>(buffer[offset + 2]);
				candidate.gridOrientation = static_cast<int>(buffer[offset + 3]);
				out[written++] = candidate;
			}
			return written;
		}

		// Computes the real, physical world position of the board slot
		// where hand tile `handIndex` would actually land -- the direct
		// production use of ComputeCandidateWorldPosition()'s decompile-
		// derived formula. The X/Y math is CONFIRMED LIVE exact
		// (2026-09-17): a live log showed pos.x-sceneX and pos.y-sceneY
		// matching gx/gy*0.013125 to four decimal places for the actual
		// winning candidate's grid/orientation words. Z is NOT exact in
		// the sense that matters for a drawn marker -- see DrawOverlay()'s
		// own board-marker block for why its caller overrides
		// outPos.z with a real entity's height instead of trusting this
		// function's own Z (which the SAME log confirmed matches its
		// ported formula bit-for-bit too -- fLocal_14+0.005 -- the issue
		// is what Scene's OWN base Z means, not the math built on it).
		//
		// See this function's own git history for the candidate-matching
		// logic below: an earlier version required EXACTLY ONE native
		// candidate to match `handIndex` before drawing anything, on the
		// untested assumption that more than one match only happens in
		// the rare hasAlternateEnd case -- a live report of the marker
		// never appearing at all points at that assumption being wrong: a
		// real board very plausibly has more than one PHYSICAL open end
		// sharing the same pip value, which QueryNativeCandidates() would
		// legitimately return as separate candidates for the same hand
		// tile even outside hasAlternateEnd. BoardTracker, the mechanism
		// this replaced, tolerated exactly this ("whichever one was most
		// recently observed", see its own former header comment) rather
		// than refusing to draw. This now does the same: takes the FIRST
		// matching candidate rather than demanding uniqueness -- still a
		// genuinely valid placement for the tile either way, and the
		// truly misleading case (the SAME tile fitting two DIFFERENT pip
		// values) is already called out separately via
		// MoveRecommendation::hasAlternateEnd's own on-screen warning,
		// not this function's job to gate on.
		bool ComputeRecommendedBoardPosition(rage::scrThread* thread, std::uint32_t mySeatU, std::int32_t handIndex, Vector3& outPos)
		{
			std::array<DominoAiPolicy::Candidate, kCandidateCapacity> candidates{};
			std::uint32_t count = QueryNativeCandidates(thread, mySeatU, candidates.data(), static_cast<std::uint32_t>(candidates.size()));

#ifdef _DEBUG
			int matches = 0;
			int winningGridF1 = 0, winningGridF2 = 0, winningGridOrientation = 0;
#endif
			bool found = false;
			for (std::uint32_t i = 0; i < count; i++)
			{
				const DominoAiPolicy::Candidate& c = candidates[i];
				if (!c.hasPlacement || c.handIndex != handIndex)
					continue;

#ifdef _DEBUG
				matches++;
#endif
				if (!found)
				{
					outPos = ComputeCandidateWorldPosition(thread, c.gridF1, c.gridF2, c.gridOrientation);
					found = true;
#ifdef _DEBUG
					winningGridF1 = c.gridF1;
					winningGridF2 = c.gridF2;
					winningGridOrientation = c.gridOrientation;
#endif
				}
			}

#ifdef _DEBUG
			// Rate-limited to once per actual change (not every tick this
			// runs, which is every tick of the whole decision window --
			// logging unconditionally here produced ~19000 near-identical
			// lines for a single ~2-minute decision in a live report,
			// making the log nearly useless for finding anything else).
			// Includes the raw Scene base coord/heading this call's
			// ComputeCandidateWorldPosition() read internally (duplicated
			// here rather than plumbed out as an extra return value, since
			// this is Debug-only and the read itself is cheap) so a wrong
			// Z specifically can be traced to either the Scene read or the
			// grid-offset math without another round trip.
			struct LastLogged { std::uint32_t seat; std::int32_t handIndex; std::uint32_t count; int matches; bool found; float x, y, z; bool valid = false; };
			static LastLogged last{};
			LastLogged now{ mySeatU, handIndex, count, matches, found, outPos.x, outPos.y, outPos.z, true };
			if (!last.valid || last.seat != now.seat || last.handIndex != now.handIndex || last.count != now.count ||
				last.matches != now.matches || last.found != now.found || last.x != now.x || last.y != now.y || last.z != now.z)
			{
				ScriptLocal scene = SceneLocal(thread);
				float sceneX = scene.At(kSceneBaseCoordFieldOffset).AsFloat();
				float sceneY = scene.At(kSceneBaseCoordFieldOffset + 1).AsFloat();
				float sceneZ = scene.At(kSceneBaseCoordFieldOffset + 2).AsFloat();
				float sceneHeading = scene.At(kSceneHeadingFieldOffset).AsFloat();
				Log::Write("Trace: ComputeRecommendedBoardPosition seat={} handIndex={} nativeCandidateCount={} matchingCandidates={} found={} winningGrid=({},{},{}) scene=({:.4f},{:.4f},{:.4f},hdg={:.4f}) pos=({:.4f},{:.4f},{:.4f})",
					mySeatU, handIndex, count, matches, found, winningGridF1, winningGridF2, winningGridOrientation,
					sceneX, sceneY, sceneZ, sceneHeading, outPos.x, outPos.y, outPos.z);
				last = now;
			}
#endif
			return found;
		}

		// Table points target, Round.f_666.f_14[0] -- set to 100/90/60 by
		// func_22's table setup (dominoes_sp.ysc.c lines ~3446-3453) and
		// compared against seat.f_2 by func_164/func_169 to end the game.
		// Bracket-indexed in the decompile, so the header-word convention
		// applies (At(0, 1)). Statically traced, NOT yet live-confirmed --
		// which is why callers sanity-check the value and fall back to
		// "no target" (round-only advice) rather than trusting garbage;
		// ProbeBestMove() logs it for the live check.
		constexpr std::uint32_t kRulesHolderFieldOffset = 666;
		constexpr std::uint32_t kPointsTargetFieldOffset = 14;

		std::int32_t ReadPointsTarget(rage::scrThread* thread)
		{
			std::int32_t target = RoundLocal(thread).At(kRulesHolderFieldOffset).At(kPointsTargetFieldOffset).At(0, 1).AsInt32();
			if (target < 10 || target > 1000)
				return 0;
			return target;
		}

		std::int32_t ReadSeatScore(rage::scrThread* thread, std::uint32_t seat)
		{
			return SeatLocal(thread, seat).At(kSeatScoreOffset).AsInt32();
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
	}

	// See this function's own declaration comment (DominoCheat.h) for why
	// main.cpp's DllMain calls this at the very top of DLL_PROCESS_DETACH,
	// before scriptUnregister() -- g_moveAdvisor is only ever non-null
	// once MoveAdvisor() has run at least once (i.e. a decision was
	// evaluated this session), so this safely no-ops otherwise.
	void PrepareForShutdown()
	{
		if (g_moveAdvisor)
			g_moveAdvisor->RequestStop();
	}

	namespace
	{

		// Full-information search (DominoSearch.h) over every seat's REAL
		// hand, the boneyard's known draw order, and the table's scores --
		// see that header's own file comment for the evaluation (net
		// points in the game's own payout rules) and its scope limits.
		//
		// HISTORY: the original version was a 1-ply "minimize immediate
		// opponent replies" heuristic (confirmed live to lose more than
		// expected); Session 9 replaced it with a win/loss minimax that
		// only ran when all 4 seats were dealt (empty boneyard) and fell
		// back to the 1-ply heuristic otherwise; the 2026-09-13 decision-
		// engine pass (Session 11) made the search score NET POINTS,
		// model boneyard draws (so 2/3-seat tables get the real search
		// too), search the opening move, and read the seats' accumulated
		// scores + the points target so a round that would hand an
		// opponent the game is treated as the catastrophe it is. The
		// 1-ply fallback is gone: every decision goes through the search,
		// and a snapshot that can't be read cleanly yields no advice
		// rather than a guess.
		//
		// ASYNC (2026-09-13, second live-freeze fix): DrawOverlay() calls
		// this every single tick for the ENTIRE real-world decision
		// window (however long the player just looks at the screen
		// deciding, gated on turnSubState==4 -- see DrawOverlay()'s own
		// comment), not once per turn. This function only ever reads
		// live memory into a GameState snapshot (cheap, must stay on
		// this thread) and hands it to AsyncMoveAdvisor's background
		// worker (see that header's own file comment for why crossing
		// that exact boundary is safe) -- it never blocks on the search
		// itself. The very first tick (or two) of a new decision shows
		// no recommendation until the worker publishes a matching
		// result; the worker then publishes each completed depth,
		// refining advice within the configured wall-clock allowance,
		// and stops early once the round is solved exactly.
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

			std::array<std::int32_t, 7> ends{};
			std::uint32_t endCount = DetermineOpenEnds(thread, mySeatU, ends.data(), static_cast<std::uint32_t>(ends.size()));
			std::int32_t deckCursor = RoundLocal(thread).At(kDeckCursorFieldOffset).AsInt32();

			// Build the freshness key + pure-logic search state from the
			// live read. Every field below is part of DecisionKey's
			// equality, so any change (a tile played, a draw, a score
			// update) cancels the old job and starts a fresh one.
			DecisionKey key;
			key.seat = mySeat;
			key.deckCursor = deckCursor;
			key.runtimeMs = cfg.AdvisorWallClockBudgetMs;
			DominoSearch::GameState& state = key.state;
			state.rules = ReadAiRules(thread);
			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
			{
				ScriptLocal seatLocal = SeatLocal(thread, seat);
				std::int32_t occupancyMarker = seatLocal.At(kSeatOccupancyOffset).AsInt32();
				if (occupancyMarker != static_cast<std::int32_t>(seat))
					continue;

				state.occupied[seat] = true;
				std::int32_t handCount = seatLocal.At(kSeatHandCountOffset).AsInt32();
				if (handCount <= 0 || handCount > static_cast<std::int32_t>(kMaxHandCapacity) ||
					handCount > static_cast<std::int32_t>(DominoSearch::kMaxHandTiles))
				{
					CancelMoveAdvice();
					return best;
				}

				for (std::int32_t i = 0; i < handCount; i++)
				{
					DominoHandEval::Tile tile = ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));
					if (!tile.IsValid())
					{
						CancelMoveAdvice();
						return best; // Do not search a partial read or shift live hand indices.
					}
					state.hands[seat][i] = tile;
				}
				state.handCounts[seat] = handCount;
			}
			if (!state.occupied[mySeatU])
			{
				CancelMoveAdvice();
				return best;
			}
			for (std::uint32_t ei = 0; ei < endCount; ei++)
				state.ends.pips[static_cast<std::size_t>(state.ends.count++)] = ends[ei];

			// Undrawn boneyard in draw order (positions deckCursor..27 --
			// the same read DrawBoneyardStatus() shows on screen). Only
			// drawing rules ever consume it (DominoAiPolicy::
			// DrawsFromBoneyard); under Block it's dead weight for the
			// search and for the key, so it's skipped there.
			if (DominoAiPolicy::DrawsFromBoneyard(state.rules))
			{
				if (deckCursor < 0 || deckCursor > static_cast<std::int32_t>(kTileSetSize))
				{
					CancelMoveAdvice();
					return best;
				}
				for (std::int32_t i = deckCursor; i < static_cast<std::int32_t>(kTileSetSize); i++)
				{
					DominoHandEval::Tile tile = ReadBoneyardTile(thread, i);
					if (!tile.IsValid())
					{
						CancelMoveAdvice();
						return best;
					}
					state.boneyard[static_cast<std::size_t>(state.boneyardCount++)] = tile;
				}
			}

			// Accumulated scores + points target -- statically traced only
			// (see ReadPointsTarget()); any implausible read disables
			// game-outcome awareness for this decision instead of
			// steering advice with garbage.
			std::int32_t target = ReadPointsTarget(thread);
			if (target > 0)
			{
				bool plausible = true;
				for (std::uint32_t seat = 0; seat < kMaxSeats && plausible; seat++)
				{
					if (!state.occupied[seat])
						continue;
					std::int32_t score = ReadSeatScore(thread, seat);
					if (score < 0 || score >= target)
						plausible = false;
					state.scores[seat] = score;
				}
				if (plausible)
					state.pointsTarget = target;
				else
					state.scores = {};
			}

			// Root scoring bonus on All Fives/All Threes tables: the native
			// reports each of my candidates' exact resulting end total, so
			// the points func_354 would credit me for playing that tile
			// right now are known exactly -- see DominoSearch.h's own
			// header comment. A tile that fits two ends gets its better
			// total (the HUD names the tile; the end is the player's).
			if (!DominoAiPolicy::AlwaysUsesPipPriority(state.rules) && state.rules != DominoAiPolicy::Rules::Unknown)
			{
				std::array<DominoAiPolicy::Candidate, kCandidateCapacity> candidates{};
				std::uint32_t count = QueryNativeCandidates(thread, mySeatU, candidates.data(), static_cast<std::uint32_t>(candidates.size()));
				for (std::uint32_t i = 0; i < count; i++)
				{
					const DominoAiPolicy::Candidate& candidate = candidates[i];
					if (!candidate.hasPlacement || candidate.handIndex < 0 || candidate.handIndex >= state.handCounts[mySeatU])
						continue;
					std::int32_t points = DominoAiPolicy::ScoringPoints(state.rules, candidate.resultingEndTotal);
					auto& slot = state.rootMoveBonusPoints[static_cast<std::size_t>(candidate.handIndex)];
					slot = std::max(slot, points);
				}
			}
			state.turnSeat = mySeat;

			// FindBestMove() clamps the depth to the round's own finite
			// bound; the configured deadline controls how far iterative
			// deepening gets before that (a solved round stops early).
			auto& advisor = MoveAdvisor();
			advisor.SubmitJob(key, state, mySeat, DominoSearch::detail::kMaxSearchDepth, std::chrono::milliseconds(key.runtimeMs));
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
					best.points = rec.points;
					best.exact = rec.exact;
					best.completedDepth = rec.completedDepth;
					best.outcome = rec.outcome;
					// Debug raw-panel diagnostic only -- the Release HUD's
					// SAFE/RISKY/VERY RISKY qualifier is keyed on the
					// search verdict now (DrawMoveSafetyStatus()).
					best.opponentRespondCount = (rec.isWinningMove || rec.resultPip < 0) ? 0 : CountPipAcrossOpponents(thread, mySeat, rec.resultPip);

					// Does the recommended tile ALSO fit a DIFFERENT open
					// end than the one just chosen? `ends`/`endCount` above
					// are the same real open-end read the search itself
					// used for this decision. See MoveRecommendation::
					// hasAlternateEnd's own comment for why this matters.
					if (best.endPip >= 0)
					{
						for (std::uint32_t ei = 0; ei < endCount; ei++)
						{
							if (ends[ei] == best.endPip)
								continue;
							if (best.tile.low == ends[ei] || best.tile.high == ends[ei])
							{
								best.hasAlternateEnd = true;
								best.alternateEndPip = ends[ei];
								break;
							}
						}
					}
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
		//
		// Builds into one reused buffer, so the per-frame HUD text does no heap
		// allocation once its capacity has grown. The returned pointer is valid
		// until the next call.
		const char* BgText(std::string_view text, int fontSize)
		{
			static std::string buffer;
			std::array<char, 12> digits{};
			const auto sizeEnd = std::to_chars(digits.data(), digits.data() + digits.size(), fontSize).ptr;

			buffer.assign("<TEXTFORMAT RIGHTMARGIN='0'><P ALIGN='Left'><FONT FACE='$Font5' LETTERSPACING='0' SIZE='");
			buffer.append(digits.data(), sizeEnd);
			buffer.append("'>~s~");
			buffer.append(text);
			buffer.append("</FONT></P><TEXTFORMAT>");
			return buffer.c_str();
		}

		// Appends `value` in decimal -- the allocation-free stand-in for
		// ostringstream in the per-frame HUD lines below.
		void AppendInt(std::string& out, int value)
		{
			std::array<char, 12> digits{};
			out.append(digits.data(), std::to_chars(digits.data(), digits.data() + digits.size(), value).ptr);
		}

		// Same "[low|high]" text as FormatTile(), appended in place.
		void AppendTile(std::string& out, const DominoHandEval::Tile& tile)
		{
			if (!tile.IsValid())
			{
				out += "--";
				return;
			}

			out += '[';
			AppendInt(out, tile.low);
			out += '|';
			AppendInt(out, tile.high);
			out += ']';
		}

		// Thin call-site wrapper around BgText() + the
		// UIDEBUG::_BG_SET_TEXT_COLOR/_BG_DISPLAY_TEXT pair -- every
		// real-font draw call below uses this instead of repeating the
		// three-line pattern Poker/BlackjackCheat's own call sites each
		// inline separately.
		void DrawBgText(std::string_view text, float x, float y, int fontSize, int r, int g, int b, int a = 255)
		{
			const char* formatText = BgText(text, fontSize);
			UIDEBUG::_BG_SET_TEXT_COLOR(r, g, b, a);
			UIDEBUG::_BG_DISPLAY_TEXT(GAMEPLAY::CREATE_STRING(10, const_cast<char*>("LITERAL_STRING"), const_cast<char*>(formatText)), x, y);
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
		constexpr float kReleaseWorldMarkerOffsetX = -0.03f; // CONFIRMED LIVE 2026-09-13, see Config::Values::WorldMarkerOffsetX's own comment
		constexpr float kReleaseWorldMarkerOffsetY = 0.0f;
		constexpr int kReleaseWorldMarkerFontSize = 26;
#endif

		// Off by default -- see this function's own trace block below for
		// why (2026-09-17: even a 0.5s wall-clock throttle still read as
		// "spamming" over a long decision window). Flip to true and
		// rebuild Debug to bring the trace back for a specific
		// debugging session; the throttle logic itself is untouched and
		// still applies once this is on.
		constexpr bool kLogWorldMarkerTrace = false;

		// Draws `text` at the given real world position, projected to
		// screen -- the exact same GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_
		// COORD technique PokerCheat's own community-card objects use.
		// Shared by DrawWorldMarkerOnTile() (a physical prop's live
		// position) and DrawOverlay()'s own board-marker block (a
		// computed position, see ComputeRecommendedBoardPosition()).
		void DrawWorldMarkerAtPosition(const Vector3& coords, std::string_view text)
		{
			float screenX = 0.0f, screenY = 0.0f;
			bool projected = GRAPHICS::GET_SCREEN_COORD_FROM_WORLD_COORD(coords.x, coords.y, coords.z, &screenX, &screenY);

#ifdef _DEBUG
			if (kLogWorldMarkerTrace)
			{
				// Rate-limited -- see ComputeRecommendedBoardPosition()'s
				// own comment on why (this runs every tick of the whole
				// decision window too). Logs BOTH outcomes, not just
				// failure: a live report found the marker rendering, just
				// in the wrong place, which a failure-only version had no
				// way to show (success was always silent).
				//
				// TWO live reports found this STILL spamming every tick,
				// each from a different cause -- both value-equality
				// gates, not time-based ones: first with screenX/screenY
				// included in the gate (camera sway shifts them by a tiny
				// fraction on essentially every frame even while looking
				// at the exact same static world point); then, after
				// dropping those, with the WORLD coords alone (the target
				// entity itself isn't perfectly static tick to tick -- a
				// live log showed its own Z drifting by ~0.0001 units a
				// tick, presumably some subtle physics/idle sway on the
				// prop -- so exact equality almost never held there
				// either). Switched to a WALL-CLOCK throttle instead: log
				// immediately on any STATE transition (text or projected
				// success/failure changes), otherwise at most once per
				// kLogThrottleSeconds. A THIRD live report found even
				// this "still spamming" over a long decision window
				// (0.5s x a multi-minute window is still a lot of lines
				// to scroll past when hunting for something else in the
				// log) -- rather than stretch the throttle further, this
				// whole trace is now off by default (kLogWorldMarkerTrace
				// above), same "off unless actively needed" precedent
				// LogOpponentCandidateProbe()'s own verboseLog parameter
				// already set for DetermineOpenEnds().
				constexpr double kLogThrottleSeconds = 0.5;
				struct LastLogged { std::string text; bool projected; std::chrono::steady_clock::time_point when; bool valid = false; };
				static LastLogged last{};
				auto now_time = std::chrono::steady_clock::now();
				bool stateChanged = !last.valid || last.text != text || last.projected != projected;
				bool throttleExpired = !last.valid || std::chrono::duration<double>(now_time - last.when).count() >= kLogThrottleSeconds;
				if (stateChanged || throttleExpired)
				{
					if (projected)
						Log::Write("Trace: DrawWorldMarkerAtPosition \"{}\" world=({:.4f},{:.4f},{:.4f}) -> screen=({:.4f},{:.4f})",
							text, coords.x, coords.y, coords.z, screenX, screenY);
					else
						Log::Write("Trace: DrawWorldMarkerAtPosition \"{}\" world=({:.4f},{:.4f},{:.4f}) -- GET_SCREEN_COORD_FROM_WORLD_COORD failed (off-screen/behind camera/invalid position)",
							text, coords.x, coords.y, coords.z);
					last = LastLogged{ std::string(text), projected, now_time, true };
				}
			}
#endif
			if (!projected)
				return;

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

		// Draws `text` directly over tile `rawTileIndex`'s real 3D prop
		// (GetTilePropHandle(), see that function's own header comment).
		void DrawWorldMarkerOnTile(rage::scrThread* thread, std::int32_t rawTileIndex, std::string_view text)
		{
			std::int32_t handle = GetTilePropHandle(thread, rawTileIndex);
			if (handle == 0 || !ENTITY::DOES_ENTITY_EXIST(handle))
				return;

			DrawWorldMarkerAtPosition(ENTITY::GET_ENTITY_COORDS(handle, true, true), text);
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
		constexpr float kReleaseOpponentHandBaseX = 0.17f;
		constexpr float kReleaseOpponentHandBaseY = 0.86f;
		constexpr float kReleaseOpponentHandStepY = -0.0915f;
		constexpr float kReleaseOpponentTileIconSpacingX = 0.015f; // CONFIRMED LIVE 2026-09-13, see Config::Values::OpponentTileIconSpacingX's own comment
		constexpr float kReleaseOpponentTileIconWidth = 0.015f; // CONFIRMED LIVE 2026-09-13, see Config::Values::OpponentTileIconWidth's own comment
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
		// Screen position and icon size/spacing are the current
		// dominoes_sp user-tuned defaults from Config. Debug builds can
		// retune live via Reload Config; Release bakes in the same values.
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
			float iconSpacingX = cfg.OpponentTileIconSpacingX;
			float iconWidth = cfg.OpponentTileIconWidth;
			float iconHeight = cfg.OpponentTileIconHeight;
#else
			float baseX = kReleaseOpponentHandBaseX;
			float baseY = kReleaseOpponentHandBaseY;
			float stepY = kReleaseOpponentHandStepY;
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

				float iconX = baseX;
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
		// raw panel's own boneyard line, same ShowBoneyard gate
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

			static std::string label;
			label.assign(Localization::BoneyardWord());
			label += " (";
			AppendInt(label, remaining);
			label += "):";
			DrawBgText(label, x, y, 20, 200, 220, 255);

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
		constexpr float kReleaseMoveAdviceX = 0.4f;
		constexpr float kReleaseMoveAdviceY = 0.5f;
		constexpr float kReleaseMoveSafetyYOffset = 0.045f;
#endif

		// Standalone move-advice headline -- articulates
		// DetermineBestMove()'s recommendation as a real-font, centered-
		// ish readout instead of one line buried in the raw Debug panel.
		// Uses the current dominoes_sp user-tuned HUD position. Shown in
		// both Debug and Release, gated by DrawOverlay() on it actually
		// being mySeat's turn.
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
			const std::string_view headline = rec.isWinningMove ? Localization::WinningMoveLabel() : Localization::BestMoveLabel();
			int r = rec.isWinningMove ? 255 : 140, g = rec.isWinningMove ? 220 : 255, b = rec.isWinningMove ? 120 : 140;

			static std::string line;
			line.assign(headline);
			line += ": ";
			AppendTile(line, rec.tile);
			// "on end N" rather than a bare "(N -> M)" -- a live loss (see
			// MoveRecommendation::hasAlternateEnd's own comment) showed the
			// terse arrow notation wasn't unambiguous enough about which
			// physical end N actually names when the tile fits two.
			if (!rec.isWinningMove && rec.endPip >= 0)
			{
				line += " on end ";
				AppendInt(line, rec.endPip);
				line += " (-> ";
				AppendInt(line, rec.resultPip);
				line += ')';
			}
			// Net points for the recommended line, in the game's own
			// payout (see DominoSearch.h): "+12" = I win the round by 12,
			// "-8" = the winner is paid 8. A "~" prefix means the search
			// hit its horizon and this is an estimate, not the solved
			// round. Digits only, so no localization needed.
			if (!rec.isWinningMove)
			{
				line += "  ";
				if (!rec.exact)
					line += '~';
				if (rec.points >= 0)
					line += '+';
				AppendInt(line, rec.points);
			}

			// This tile fits a DIFFERENT open end too -- override the
			// normal color with a warning one and spell out both end
			// numbers so it's unmistakable which one to avoid. This is
			// exactly the situation that turned a proven win into a real
			// loss live: same recommended tile, wrong end played.
			if (rec.hasAlternateEnd)
			{
				line += "  ";
				line += Localization::AmbiguousEndWarning();
				line += " (";
				AppendInt(line, rec.alternateEndPip);
				line += ')';
				r = 255; g = 90; b = 60;
			}

			DrawBgText(line, x, y, 40, r, g, b);
		}

		// Position-verdict qualifier, drawn just below DrawMoveAdviceStatus()
		// -- same "second line, offset below the headline" convention
		// BlackjackCheat's DrawBettingAdviceStatus() uses relative to
		// DrawAdviceStatus(). Reuses the existing SAFE/RISKY/VERY RISKY
		// labels (13 languages, see Localization.h) but keys them on the
		// SEARCH'S VERDICT since the 2026-09-13 decision-engine pass, not
		// on the old 1-ply "opponent tiles that answer the new end"
		// count: Safe = the recommended line wins the round (or the
		// game) under the model, Risky = undecided within the search
		// budget or a scoreless tie, Very Risky = every line loses and
		// this is the least-bad one. Shown on every table now -- the
		// search models boneyard draws, so a non-empty boneyard no
		// longer makes the verdict a guess.
		void DrawMoveSafetyStatus(const MoveRecommendation& rec)
		{
			if (rec.isWinningMove)
				return;

			const Config::Values& cfg = Config::Get();
#ifdef _DEBUG
			float x = cfg.MoveAdviceX;
			float y = cfg.MoveAdviceY + 0.045f;
#else
			float x = kReleaseMoveAdviceX;
			float y = kReleaseMoveAdviceY + kReleaseMoveSafetyYOffset;
#endif

			Localization::BlockingSafety safety = Localization::BlockingSafety::Risky;
			switch (rec.outcome)
			{
				case DominoSearch::Outcome::GameWin:
				case DominoSearch::Outcome::RoundWin: safety = Localization::BlockingSafety::Safe; break;
				case DominoSearch::Outcome::RoundLoss:
				case DominoSearch::Outcome::GameLoss: safety = Localization::BlockingSafety::VeryRisky; break;
				default: safety = Localization::BlockingSafety::Risky; break;
			}
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
#ifdef _DEBUG
		// Logs turn/state transitions, per-seat hand changes (diffed by
		// tile identity, not array index -- see CountTilesByValue()'s own
		// comment), and predicted-vs-actual moves to DominoCheat.log every
		// tick dominoes_sp is found running, regardless of Config toggles
		// -- see g_debugTrace's own header comment for why this exists
		// alongside the on-demand F12 probes rather than replacing any of
		// them. Called once per tick from DrawOverlay(), Debug builds
		// only.

		// Turn-boundary marker only -- CHANGED (2026-09-16, user request):
		// this used to also dump every occupied seat's COMPLETE hand and
		// the COMPLETE undrawn boneyard once per NEW turn, on top of the
		// per-tile diff loop below that already reports exactly what
		// MOVED (played/drawn, by tile identity). That made the two
		// mechanisms redundant -- every tile a full dump would show was
		// already visible in the next "hand changed ... played/drew"
		// line -- and blew up log size for no diagnostic gain. Now this
		// only marks where one turn ends and the next begins; the diff
		// loop is the sole source of hand/boneyard content.
		void LogFullTableState(rage::scrThread* thread, std::int32_t turnSeat, std::int32_t mySeat)
		{
			Log::Write("Trace: ==== turn begins: seat {}{} ====", turnSeat, (turnSeat == mySeat ? " (you)" : ""));
		}

		// Added 2026-09-16 to chase the opponent-move-prediction mismatch
		// found the same day: PredictOpponentMove()/LogOpponentPredictions()
		// query MINIGAME::_FIND_PLAYABLE_HAND_TILES only at turnSubState==4
		// and got a candidate list that didn't match the real board in
		// EVERY logged case that session (see this file's own commentary
		// on that session for the manual board reconstruction proving it),
		// even though DominoAiPolicy::SelectCandidate's tie-break was
		// verified line-by-line against the decompile. Leading theory:
		// timing -- an opponent's turnSubState reaches 4 and its hand
		// changes in the SAME tick (unlike mySeat's multi-second real
		// decision window), so whatever internal moment the SCRIPT's own
		// func_351 actually queries this native at may not line up with
		// when WE independently re-query it.
		//
		// This logs the RAW candidate buffer (every word, not just what
		// QueryNativeCandidates()/DominoAiPolicy::Candidate keeps) plus an
		// INDEPENDENTLY-computed open-end set for the SAME seat via
		// DetermineOpenEnds() -- the same local-hand-copy technique
		// mySeat's own advice already relies on (CONFIRMED LIVE there),
		// generalized here to whichever seat is passed, and never writing
		// real game memory -- so a live run can show directly whether the
		// native's own candidate list agrees with what a hand tile
		// actually matching an open pip WOULD require. Called at EVERY
		// turnSubState transition during an opponent's turn (2, 3, AND 4,
		// not just 4 -- see call site in LogDebugTrace()) to find whether
		// some earlier sub-state gives a correct read. Never called for
		// mySeat -- LogDebugTrace()'s own prediction path already covers
		// that via the real search advisor.
		void LogOpponentCandidateProbe(rage::scrThread* thread, std::uint32_t seat, std::int32_t turnSubState)
		{
			std::int32_t handCount = SeatLocal(thread, seat).At(kSeatHandCountOffset).AsInt32();
			if (handCount <= 0 || handCount > static_cast<std::int32_t>(kMaxHandCapacity))
				return;

			std::array<DominoHandEval::Tile, kMaxHandCapacity> hand{};
			for (std::int32_t i = 0; i < handCount; i++)
				hand[static_cast<std::size_t>(i)] = ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));

			void* handPtr = GamePointers::GetScriptLocalAddress(thread, SeatLocal(thread, seat).At(kSeatHandArrayFieldOffset).Index());
			if (!handPtr)
				return;

			std::array<std::int64_t, 1 + kCandidateCapacity * kCandidateStride> buffer{};
			buffer[0] = kCandidateCapacity;
			for (std::uint32_t i = 0; i < kCandidateCapacity; i++)
				buffer[1 + i * kCandidateStride + 3] = -1; // func_612's placement sentinel

			int count = MINIGAME::_FIND_PLAYABLE_HAND_TILES(reinterpret_cast<Any*>(handPtr), reinterpret_cast<Any*>(buffer.data()));

			std::array<std::int32_t, 7> pips{};
			std::uint32_t pipCount = DetermineOpenEnds(thread, seat, pips.data(), static_cast<std::uint32_t>(pips.size()), /*verboseLog=*/true);

			// What SHOULD be legal per the independently-computed open-end
			// set, versus what the native candidate list actually marks
			// valid (func_613: handIndex>=0 and not both placement words
			// zero) -- compared per hand tile below.
			std::array<bool, kMaxHandCapacity> expectedLegal{};
			for (std::int32_t i = 0; i < handCount; i++)
				for (std::uint32_t p = 0; p < pipCount; p++)
					if (hand[static_cast<std::size_t>(i)].low == pips[p] || hand[static_cast<std::size_t>(i)].high == pips[p])
						expectedLegal[static_cast<std::size_t>(i)] = true;

			std::array<bool, kMaxHandCapacity> nativeLegal{};
			std::ostringstream rawLine;
			int clampedCount = (count < 0) ? 0 : (count > static_cast<int>(kCandidateCapacity) ? static_cast<int>(kCandidateCapacity) : count);
			for (int i = 0; i < clampedCount; i++)
			{
				std::size_t offset = 1 + static_cast<std::size_t>(i) * kCandidateStride;
				std::int32_t handIndex = static_cast<std::int32_t>(buffer[offset]);
				std::int64_t f1 = buffer[offset + 1], f2 = buffer[offset + 2], f3 = buffer[offset + 3], endTotal = buffer[offset + 4];
				bool hasPlacement = handIndex >= 0 && (f1 != 0 || f2 != 0);
				std::string tileStr = (handIndex >= 0 && handIndex < handCount)
					? FormatTile(hand[static_cast<std::size_t>(handIndex)]) : std::string("(out of range)");
				if (hasPlacement && handIndex >= 0 && handIndex < static_cast<std::int32_t>(kMaxHandCapacity))
					nativeLegal[static_cast<std::size_t>(handIndex)] = true;
				rawLine << "[" << i << ": idx=" << handIndex << " " << tileStr << " f1=" << f1 << " f2=" << f2
					<< " f3=" << f3 << " end=" << endTotal << (hasPlacement ? " VALID" : "") << "] ";
			}

			std::ostringstream mismatch;
			for (std::int32_t i = 0; i < handCount; i++)
				if (expectedLegal[static_cast<std::size_t>(i)] != nativeLegal[static_cast<std::size_t>(i)])
					mismatch << FormatTile(hand[static_cast<std::size_t>(i)]) << "(expected="
						<< (expectedLegal[static_cast<std::size_t>(i)] ? "legal" : "illegal") << " native="
						<< (nativeLegal[static_cast<std::size_t>(i)] ? "legal" : "illegal") << ") ";

			std::ostringstream openEndsLine;
			for (std::uint32_t p = 0; p < pipCount; p++)
				openEndsLine << pips[p] << " ";

			Log::Write("Trace: candidate probe seat {} state {} handCount={} openEnds(local)={} nativeCount={} raw={} {}",
				seat, turnSubState, handCount,
				openEndsLine.str().empty() ? std::string("(none)") : openEndsLine.str(),
				count, rawLine.str(),
				mismatch.str().empty() ? std::string("(no mismatch)") : ("MISMATCH: " + mismatch.str()));
		}

		// CONFIRMED LIVE (2026-09-16), found via LogOpponentCandidateProbe()'s
		// own diagnostic: turnSubState==4 does NOT mean "about to decide"
		// for an opponent seat the way it does for mySeat. A live session
		// probing states 2/3/4 every opponent turn showed the candidate
		// list and open-end set are IDENTICAL and correct (zero mismatches
		// against an independently-computed open-end set) at states 2 and
		// 3, but by the time state reaches 4 the hand has ALREADY lost the
		// tile that gets reported played moments later, and the open ends
		// already reflect that same not-yet-logged move -- the NPC's
		// decide-and-commit (func_351) has already happened somewhere
		// between state 3 and state 4. mySeat's own state 4 is unaffected
		// (that's the real multi-second human decision window, unchanged
		// and still the right gate for DetermineBestMove()/the HUD).
		// PredictOpponentMove() was querying the native at the ONE state
		// that's already too late for an opponent, guaranteeing every
		// comparison it logged was really "predict a move using the hand
		// it already came from" -- structurally impossible to match. This
		// is why every single opponent prediction mismatched in the prior
		// session's log despite the candidate list itself never being
		// wrong. Fixed by capturing opponents' predictions at state 3
		// instead (states 2 and 3 were identical in every probed case, so
		// 3 -- closest to the real decision point -- was picked).
		constexpr std::int32_t DecisionCaptureState(bool isMySeat)
		{
			return isMySeat ? 4 : 3;
		}

		void LogDebugTrace(rage::scrThread* thread)
		{
			std::int32_t turnSeat = RoundLocal(thread).At(kCurrentTurnSeatFieldOffset).AsInt32();
			std::int32_t turnSubState = RoundLocal(thread).At(kTurnSubStateFieldOffset).AsInt32();
			std::int32_t mySeat = FindMySeatByPed(thread);

			if (turnSeat != g_debugTrace.lastTurnSeat)
				LogFullTableState(thread, turnSeat, mySeat);

			if (turnSeat != g_debugTrace.lastTurnSeat || turnSubState != g_debugTrace.lastTurnSubState)
			{
				Log::Write("Trace: turn -> seat {}{} state {}", turnSeat, (turnSeat == mySeat ? " (you)" : ""), turnSubState);
				g_debugTrace.lastTurnSeat = turnSeat;
				g_debugTrace.lastTurnSubState = turnSubState;

				// Reset the "already logged a prediction for this decision"
				// gate whenever we (re-)enter the seat's own capture state
				// (see DecisionCaptureState() above), so the NEXT entry
				// gets a fresh prediction logged even if this one never
				// resolved (e.g. the async worker never published in time).
				if (turnSeat >= 0 && turnSeat < static_cast<std::int32_t>(kMaxSeats))
					g_debugTrace.predictionLogged[static_cast<std::size_t>(turnSeat)] =
						(turnSubState != DecisionCaptureState(turnSeat == mySeat));

				// Opponent candidate probe (see LogOpponentCandidateProbe()'s
				// own comment) -- states 2, 3, AND 4, not just the real
				// decision window, to find whether the native's view of the
				// board becomes correct at some earlier sub-state than 4.
				if (turnSeat >= 0 && turnSeat != mySeat && turnSeat < static_cast<std::int32_t>(kMaxSeats) &&
					(turnSubState == 2 || turnSubState == 3 || turnSubState == 4))
					LogOpponentCandidateProbe(thread, static_cast<std::uint32_t>(turnSeat), turnSubState);
			}

			// Capture exactly one prediction per decision window, as soon
			// as one is available -- mySeat's own via DetermineBestMove()'s
			// real search result (the actual advice given), every other
			// seat's via the native AI policy PredictOpponentMove() uses.
			// Retried every tick (not just the transition above) since the
			// async search worker may take a tick or two past the window
			// opening to publish a result at all.
			if (turnSubState == DecisionCaptureState(turnSeat == mySeat) && turnSeat >= 0 &&
				turnSeat < static_cast<std::int32_t>(kMaxSeats) &&
				!g_debugTrace.predictionLogged[static_cast<std::size_t>(turnSeat)])
			{
				DebugPrediction pred;
				if (turnSeat == mySeat)
				{
					MoveRecommendation rec = DetermineBestMove(thread, static_cast<std::uint32_t>(turnSeat));
					if (rec.valid)
					{
						pred.valid = true;
						pred.tile = rec.tile;
						pred.endPip = rec.endPip;
						pred.resultPip = rec.resultPip;
						std::ostringstream label;
						if (rec.isWinningMove)
							label << "WINNING MOVE " << FormatTile(rec.tile);
						else
						{
							label << "search: " << FormatTile(rec.tile);
							if (rec.endPip >= 0)
								label << " (end " << rec.endPip << " -> " << rec.resultPip << ")";
							if (rec.hasAlternateEnd)
								label << " [AMBIGUOUS -- tile ALSO fits end " << rec.alternateEndPip << ", do NOT use it]";
							label << " [" << DominoSearch::OutcomeName(rec.outcome) << " "
								<< (rec.exact ? "" : "~") << (rec.points >= 0 ? "+" : "") << rec.points << " @depth " << rec.completedDepth << "]";
						}
						pred.label = label.str();
					}
				}
				else
				{
					pred = PredictOpponentMove(thread, static_cast<std::uint32_t>(turnSeat));
				}

				if (pred.valid)
				{
					Log::Write("Trace: predicted move for seat {}{}: {}", turnSeat, (turnSeat == mySeat ? " (you)" : ""), pred.label);
					g_debugTrace.pending[static_cast<std::size_t>(turnSeat)] = pred;
					g_debugTrace.predictionLogged[static_cast<std::size_t>(turnSeat)] = true;
				}
			}

			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
			{
				DebugHandSnapshot& snap = g_debugTrace.hands[seat];
				std::int32_t occupancyMarker = SeatLocal(thread, seat).At(kSeatOccupancyOffset).AsInt32();
				if (occupancyMarker != static_cast<std::int32_t>(seat))
				{
					snap.count = -1; // seat left/not dealt -- forget the snapshot so a later re-seating logs a fresh deal, not a false "change"
					continue;
				}

				std::int32_t handCount = SeatLocal(thread, seat).At(kSeatHandCountOffset).AsInt32();
				if (handCount < 0)
					handCount = 0;
				if (handCount > static_cast<std::int32_t>(kMaxHandCapacity))
					handCount = static_cast<std::int32_t>(kMaxHandCapacity);

				std::array<DominoHandEval::Tile, kMaxHandCapacity> current{};
				for (std::int32_t i = 0; i < handCount; i++)
					current[i] = ReadHandTile(thread, seat, static_cast<std::uint32_t>(i));

				if (snap.count < 0)
				{
					std::ostringstream line;
					for (std::int32_t i = 0; i < handCount; i++)
						line << FormatTile(current[i]) << " ";
					Log::Write("Trace: seat {} dealt {} tiles: {}", seat, handCount, line.str());
				}
				else if (handCount != snap.count || !std::equal(current.begin(), current.begin() + handCount, snap.tiles.begin()))
				{
					std::array<int, DominoHandEval::kTileSetSize> oldCounts{}, newCounts{};
					CountTilesByValue(snap.tiles.data(), snap.count, oldCounts);
					CountTilesByValue(current.data(), handCount, newCounts);

					std::ostringstream removedLine, addedLine;
					DominoHandEval::Tile lastRemoved;
					bool anyRemoved = false;
					for (int idx = 0; idx < DominoHandEval::kTileSetSize; idx++)
					{
						int diff = newCounts[idx] - oldCounts[idx];
						if (diff < 0)
						{
							DominoHandEval::Tile t = DominoHandEval::DecodeTile(idx);
							for (int n = 0; n < -diff; n++)
								removedLine << FormatTile(t) << " ";
							lastRemoved = t;
							anyRemoved = true;
						}
						else if (diff > 0)
						{
							DominoHandEval::Tile t = DominoHandEval::DecodeTile(idx);
							for (int n = 0; n < diff; n++)
								addedLine << FormatTile(t) << " ";
						}
					}

					std::ostringstream line;
					line << "Trace: seat " << seat << " hand changed (" << snap.count << " -> " << handCount << " tiles)";
					if (anyRemoved)
						line << ", played " << removedLine.str();
					if (!addedLine.str().empty())
						line << ", drew " << addedLine.str();
					Log::Write("{}", line.str());

					// Only a PLAY (a tile removed) resolves a pending
					// prediction -- a draw alone (hand grew, nothing
					// removed) isn't the move the prediction was about.
					DebugPrediction& pending = g_debugTrace.pending[seat];
					if (anyRemoved && pending.valid)
					{
						bool match = (lastRemoved == pending.tile);
						Log::Write("Trace: seat {} prediction {} -- predicted {}, actually played {}",
							seat, match ? "MATCH" : "MISMATCH", FormatTile(pending.tile), FormatTile(lastRemoved));

						// mySeat only, and only when the recommended tile
						// actually had an end to name: matching the TILE
						// isn't enough to prove the right MOVE was made if
						// that tile fit two different open ends (see
						// DebugPrediction::endPip's own comment). Verify by
						// checking whether the recommended resultPip
						// actually survived onto the real, post-move board
						// -- if it didn't, the tile went on the other end.
						if (match && static_cast<std::int32_t>(seat) == mySeat && pending.endPip >= 0 && handCount > 0)
						{
							std::array<std::int32_t, 7> postPips{};
							std::uint32_t postCount = DetermineOpenEnds(thread, seat, postPips.data(), static_cast<std::uint32_t>(postPips.size()));
							bool resultPipPresent = false;
							for (std::uint32_t i = 0; i < postCount; i++)
								if (postPips[i] == pending.resultPip)
									resultPipPresent = true;
							if (!resultPipPresent)
							{
								std::ostringstream postLine;
								for (std::uint32_t i = 0; i < postCount; i++)
									postLine << postPips[i] << " ";
								Log::Write("Trace: seat {} prediction END MISMATCH -- recommended end {} -> {}, but resulting open ends are [{}] (tile likely played on the WRONG end)",
									seat, pending.endPip, pending.resultPip, postLine.str());
							}
						}
						pending.valid = false;
					}
				}

				snap.count = handCount;
				for (std::int32_t i = 0; i < handCount; i++)
					snap.tiles[i] = current[i];
			}
		}
#endif

		void DrawOverlay(rage::scrThread* thread)
		{
#ifdef _DEBUG
			LogDebugTrace(thread);

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
							else if (rec.endPip < 0)
								recLine << "Best opening: " << FormatTile(rec.tile);
							else
								recLine << "Best move: " << FormatTile(rec.tile) << " on end " << rec.endPip
									<< " -> new end " << rec.resultPip << " (opponent tiles that answer: " << rec.opponentRespondCount << ")";
							if (rec.hasAlternateEnd)
								recLine << "  [AMBIGUOUS: tile ALSO fits end " << rec.alternateEndPip << " -- do NOT play it there]";
							if (!rec.isWinningMove)
								recLine << "  [" << DominoSearch::OutcomeName(rec.outcome) << " " << (rec.exact ? "" : "~")
									<< (rec.points >= 0 ? "+" : "") << rec.points << " @depth " << rec.completedDepth << "]";
							DrawLine(x, y, recLine.str().c_str());
							y += lineHeight;
						}
					}
				}

				if (cfg.ShowBoneyard)
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

				if (cfg.ShowBoneyard)
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
						DrawMoveSafetyStatus(rec);
					}

					if (cfg.ShowPlayableDomino)
					{
						// Mark the real, physical 3D tile itself -- see
						// FindTilePropForTileValue()'s own header comment
						// for the value-based (via .f_3, CONFIRMED LIVE)
						// lookup this uses. CONFIRMED LIVE (2026-09-17) --
						// lands on the right physical tile.
						std::int32_t rawTileValue = DominoHandEval::EncodeTile(rec.tile.low, rec.tile.high);
						std::int32_t propSlot = FindTilePropForTileValue(thread, mySeat, rawTileValue);
						std::int32_t propHandle = (propSlot >= 0) ? GetTilePropHandle(thread, propSlot) : 0;
						if (propHandle != 0)
							DrawWorldMarkerOnTile(thread, propSlot, rec.isWinningMove ? Localization::WinningTileMarker() : Localization::PlayThisTileMarker());

						// Mark the actual BOARD POSITION to place it on --
						// see ComputeRecommendedBoardPosition()'s own
						// header comment for the X/Y formula (CONFIRMED
						// LIVE exact). Its own Z is NOT used here: see
						// FindAnyBoardOwnedTilePropHandle()'s own header
						// comment for why -- Scene's raw Z sits ~0.82
						// units below a real tile's height (the game's own
						// ghost-preview object gets away with this via its
						// model's own low pivot, which a flat text marker
						// doesn't), and a FIRST fix borrowing the
						// RECOMMENDED HAND TILE's own entity Z instead
						// (CONFIRMED WRONG live the same day -- landed a
						// few inches above the table) turned out to be the
						// wrong CATEGORY of reference: a hand tile very
						// plausibly sits at a different real height than
						// the flat board (a rack/holder, not the table
						// surface). Using an ALREADY-PLAYED board tile's
						// own height instead -- the SAME category of
						// placement as the spot being marked -- is what's
						// actually correct regardless of what either
						// entity's own pivot convention turns out to be.
						// NOT yet live-tested.
						if (rec.endPip >= 0)
						{
							std::int32_t boardRefHandle = FindAnyBoardOwnedTilePropHandle(thread);
							Vector3 boardPos{};
							if (boardRefHandle != 0 && ComputeRecommendedBoardPosition(thread, static_cast<std::uint32_t>(mySeat), rec.handIndex, boardPos))
							{
								boardPos.z = ENTITY::GET_ENTITY_COORDS(boardRefHandle, true, true).z;
								DrawWorldMarkerAtPosition(boardPos, Localization::PlayHereMarker());
							}
						}
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

		// Score/target read for the search's game-outcome awareness --
		// statically traced only (see ReadPointsTarget()), so log the raw
		// values for a live check against the on-screen scoreboard.
		{
			std::ostringstream scores;
			for (std::uint32_t seat = 0; seat < kMaxSeats; seat++)
				scores << "seat" << seat << "=" << ReadSeatScore(thread, seat) << " ";
			Log::Write("DominoCheat::ProbeBestMove: pointsTarget={} (0 = implausible read, game-outcome awareness off) accumulated scores: {}",
				ReadPointsTarget(thread), scores.str());
		}

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
			Log::Write("DominoCheat::ProbeBestMove: mySeat={} handIndex={} tile={} playOnEnd={} resultingNewEnd={} opponentTilesThatAnswer={} verdict={} netPoints={}{} exact={} completedDepth={}",
				mySeat, rec.handIndex, FormatTile(rec.tile), rec.endPip, rec.resultPip, rec.opponentRespondCount,
				DominoSearch::OutcomeName(rec.outcome), rec.exact ? "" : "~", rec.points, rec.exact, rec.completedDepth);
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
