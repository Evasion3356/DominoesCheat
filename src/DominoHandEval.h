/*
	Domino tile encoding + pip-total scoring, fully self-contained with zero
	game-memory dependency -- same "pure math header, unit tested in
	isolation" convention as PokerCheat's PokerHandEval.h/BlackjackCheat's
	BlackjackHandEval.h (see tests/DominoHandEvalTests.cpp).

	Tile decode table transcribed EXACTLY from dominoes_sp.ysc.c's own
	func_151 (game build 1491.50, ~line 8105) -- a 28-case switch, each case
	an explicit literal pip pair, not derived from a formula in the
	decompile itself (though it does follow the standard triangular
	ordering for a double-six set: every unique {low,high} pair with
	0<=low<=high<=6, enumerated low-major then high-ascending -- e.g. index
	0=(0,0), 6=(0,6), 7=(1,1), 27=(6,6)). Copied case-by-case here rather
	than reimplemented from the formula so a future re-check against the
	decompile is a direct line-by-line diff, not a "does my formula match
	their table" derivation.

	This is CONFIRMED correct against the decompile (not yet against a live
	game -- see DominoCheat.cpp's file header comment for what's still
	static-trace-only about how tiles reach the player's/opponents' hands
	in the first place).

	NOT yet implemented: a legal-move / "best tile to play" advisor. That
	needs the on-table tile-chain (the "board") and whose-turn struct,
	neither of which has been traced yet (DominoCheat.cpp's OnTick() only
	reads hands + the undrawn boneyard so far) -- flagged as the next
	concrete step, same as BlackjackCheat's own advice engine came after
	its hand/deck reads were already working.
*/

#pragma once

#include <cstdint>
#include <array>

namespace DominoHandEval
{
	struct Tile
	{
		std::int32_t low = -1;  // 0-6, or -1 for "no tile" (matches the game's own -1-padding convention seen in poker/blackjack card arrays)
		std::int32_t high = -1; // 0-6, high >= low by construction (see DecodeTile())

		bool IsValid() const { return low >= 0 && low <= 6 && high >= low && high <= 6; }
		bool IsDouble() const { return IsValid() && low == high; }
		int PipTotal() const { return IsValid() ? (low + high) : 0; }
		bool operator==(const Tile& other) const { return low == other.low && high == other.high; }
	};

	constexpr int kTileSetSize = 28; // double-six set: 7+6+5+4+3+2+1 unique {low<=high} pairs, 0-6 each
	constexpr int kHandSize = 7;     // 4 players x 7 tiles = 28 = the whole set, no boneyard left over in a full 4-player game
	constexpr int kMaxSeats = 4;

	// Exact transposition of dominoes_sp.ysc.c's func_151 (see this file's
	// header comment) -- index must be 0-27; returns an invalid Tile
	// (low=high=-1) for anything outside that range, matching func_151's
	// own "return 0" (failure) default case.
	inline Tile DecodeTile(std::int32_t index)
	{
		static constexpr std::array<std::pair<std::int8_t, std::int8_t>, kTileSetSize> kTable = { {
			{0,0}, {0,1}, {0,2}, {0,3}, {0,4}, {0,5}, {0,6},
			{1,1}, {1,2}, {1,3}, {1,4}, {1,5}, {1,6},
			{2,2}, {2,3}, {2,4}, {2,5}, {2,6},
			{3,3}, {3,4}, {3,5}, {3,6},
			{4,4}, {4,5}, {4,6},
			{5,5}, {5,6},
			{6,6},
		} };

		if (index < 0 || index >= kTileSetSize)
			return Tile{};

		auto [low, high] = kTable[static_cast<std::size_t>(index)];
		return Tile{ low, high };
	}

	// Inverse of DecodeTile() -- returns -1 if low/high aren't both 0-6 or
	// low > high. Not used by the game (it only ever decodes, never
	// re-encodes), but useful for tests and for matching a raw tile read
	// back to "which boneyard slot was this" if ever needed.
	inline std::int32_t EncodeTile(std::int32_t low, std::int32_t high)
	{
		if (low < 0 || low > 6 || high < low || high > 6)
			return -1;

		// Triangular index: count all entries for lower "low" rows, then
		// offset within this row.
		std::int32_t index = 0;
		for (std::int32_t l = 0; l < low; l++)
			index += (7 - l);
		index += (high - low);
		return index;
	}

	// Sum of every valid tile's pip total across a hand -- the standard
	// "block" (nobody can play) endgame tiebreak: lowest total pip count
	// among remaining hands wins the round. Not yet wired into any advice
	// (see this file's header comment) but exercised by
	// tests/DominoHandEvalTests.cpp now so it doesn't go untested the way
	// PokerCheat's own hand scorer did for 9 sessions before anyone wrote
	// a test for it.
	inline int HandPipTotal(const Tile* tiles, int count)
	{
		int total = 0;
		for (int i = 0; i < count; i++)
			total += tiles[i].PipTotal();
		return total;
	}
}
