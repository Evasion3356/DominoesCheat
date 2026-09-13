// Unit tests for src/DominoHandEval.h (tile decode table + pip scoring) AND
// src/DominoSearch.h (the depth-limited paranoid minimax that replaced
// DetermineBestMove()'s original 1-ply heuristic, 2026-09-13). Both are
// self-contained, zero-game-dependency headers, linked against the SAME
// files the mod itself uses (not hand-copied duplicates), same convention
// as PokerCheat/BlackjackCheat's own *HandEvalTests.cpp -- see either
// project's own test file header comment for why this matters (an untested
// hand evaluator went 9 sessions before a real bug was caught in
// PokerCheat's case).
//
// Build & run (from the DominoCheat directory):
//   MSBuild.exe tests\DominoHandEvalTests.vcxproj /p:Configuration=Debug /p:Platform=x64
//   bin\Debug\DominoHandEvalTests.exe
// Exits 0 and prints "ALL PASS" if every case passes, exits 1 and lists
// failures otherwise.

#include "../src/DominoHandEval.h"
#include "../src/DominoSearch.h"

#include <cstdio>
#include <vector>

namespace
{
	using DominoHandEval::Tile;
	using DominoHandEval::DecodeTile;
	using DominoHandEval::EncodeTile;
	using DominoHandEval::HandPipTotal;
	using DominoHandEval::kTileSetSize;

	int g_failures = 0;

	void Check(bool condition, const char* testName, const char* detail)
	{
		if (condition)
		{
			std::printf("  [PASS] %s\n", testName);
		}
		else
		{
			std::printf("  [FAIL] %s -- %s\n", testName, detail);
			g_failures++;
		}
	}

	void TestDecodeTableEndpoints()
	{
		Tile zero = DecodeTile(0);
		Check(zero.low == 0 && zero.high == 0, "DecodeTile(0) == [0|0]", "double-blank must be index 0");

		Tile six = DecodeTile(6);
		Check(six.low == 0 && six.high == 6, "DecodeTile(6) == [0|6]", "last of the 0-row");

		Tile seven = DecodeTile(7);
		Check(seven.low == 1 && seven.high == 1, "DecodeTile(7) == [1|1]", "first of the 1-row");

		Tile last = DecodeTile(27);
		Check(last.low == 6 && last.high == 6, "DecodeTile(27) == [6|6]", "double-six must be the last index");
	}

	void TestDecodeTableExhaustive()
	{
		// Every index 0-27 must decode to a UNIQUE, valid {low<=high} pair,
		// and every one of the 28 possible pairs must appear exactly once
		// -- this is the whole double-six set, no duplicates, no gaps.
		bool seen[7][7] = {};
		bool allValid = true;
		bool allUnique = true;

		for (int i = 0; i < kTileSetSize; i++)
		{
			Tile t = DecodeTile(i);
			if (!t.IsValid() || t.low > t.high)
			{
				allValid = false;
				continue;
			}
			if (seen[t.low][t.high])
				allUnique = false;
			seen[t.low][t.high] = true;
		}

		Check(allValid, "DecodeTile(0..27) all valid with low<=high", "found an invalid or low>high pair");
		Check(allUnique, "DecodeTile(0..27) all unique", "found a duplicate pair");

		bool allPresent = true;
		for (int low = 0; low <= 6; low++)
			for (int high = low; high <= 6; high++)
				if (!seen[low][high])
					allPresent = false;

		Check(allPresent, "DecodeTile(0..27) covers every {low<=high} pair 0-6", "some pair never appeared");
	}

	void TestDecodeOutOfRange()
	{
		Check(!DecodeTile(-1).IsValid(), "DecodeTile(-1) invalid", "negative index must not decode");
		Check(!DecodeTile(28).IsValid(), "DecodeTile(28) invalid", "index 28 is one past the real set");
	}

	void TestEncodeIsInverseOfDecode()
	{
		bool ok = true;
		for (int i = 0; i < kTileSetSize; i++)
		{
			Tile t = DecodeTile(i);
			if (EncodeTile(t.low, t.high) != i)
			{
				ok = false;
				break;
			}
		}
		Check(ok, "EncodeTile(DecodeTile(i)) == i for every i", "encode/decode round-trip mismatch");

		Check(EncodeTile(3, 1) == -1, "EncodeTile(low > high) rejected", "low must be <= high");
		Check(EncodeTile(-1, 3) == -1, "EncodeTile(negative low) rejected", "low must be 0-6");
		Check(EncodeTile(2, 7) == -1, "EncodeTile(high > 6) rejected", "high must be 0-6");
	}

	void TestPipTotal()
	{
		Tile hand[3] = { Tile{0,0}, Tile{3,4}, Tile{6,6} };
		Check(HandPipTotal(hand, 3) == 19, "HandPipTotal([0|0],[3|4],[6|6]) == 19", "0+7+12");

		Tile empty[1] = { Tile{} };
		Check(HandPipTotal(empty, 0) == 0, "HandPipTotal with count 0 == 0", "no tiles to sum");
	}

	// DominoSearch.h tests below. Two occupied seats (0 = me, 1 = an
	// opponent) is the smallest game the paranoid minimax degenerates
	// correctly on -- with only one opponent, "minimize the worst of all
	// opponents' replies" is just "minimize this one opponent's reply",
	// so these cases exercise the real recursion without needing a full
	// 4-seat setup.
	using DominoSearch::GameState;
	using DominoSearch::FindBestMove;

	GameState TwoSeatState(std::vector<Tile> myHand, std::vector<Tile> oppHand, std::vector<std::int32_t> openEnds)
	{
		GameState state;
		state.occupied[0] = true;
		state.occupied[1] = true;
		state.hands[0] = std::move(myHand);
		state.hands[1] = std::move(oppHand);
		state.ends.pips = std::move(openEnds);
		state.turnSeat = 0;
		return state;
	}

	void TestSearchNoLegalMoveIsInvalid()
	{
		GameState state = TwoSeatState({ Tile{1,2} }, { Tile{3,4} }, { 5 });
		auto rec = FindBestMove(state, 0, 4);
		Check(!rec.valid, "FindBestMove with no matching tile returns invalid", "hand has no tile touching the only open end (5)");
	}

	void TestSearchImmediateWinPreferredOverAnythingElse()
	{
		// Hand has exactly one tile and it's legal -- must be reported
		// as the winning move without needing to search opponent replies.
		GameState state = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });
		auto rec = FindBestMove(state, 0, 4);
		Check(rec.valid && rec.isWinningMove, "FindBestMove: last tile playable == reported as a winning move", "single remaining legal tile must short-circuit to isWinningMove");
		Check(rec.tile.low == 5 && rec.tile.high == 5, "FindBestMove: winning move uses the only hand tile", "handIndex/tile mismatch");
	}

	// The concrete case that motivated replacing the 1-ply heuristic:
	// two candidate moves look EQUALLY safe one ply out (each leaves the
	// opponent exactly one matching tile), so the old heuristic's
	// tie-break -- "play the higher-pip tile first" -- picked tile B
	// (pip total 11) over tile A (pip total 5). Playing B hands the
	// opponent their [6|6] double, which EMPTIES the opponent's hand
	// (an outright loss) one ply later; playing A instead leads to a
	// block that I win on pip count. A minimax that looks past ply 1
	// must prefer A; a pure 1-ply-plus-pip-tiebreak heuristic would not.
	void TestSearchLooksPastImmediateReply()
	{
		// Me: [5|0] ("A", pip total 5) and [5|6] ("B", pip total 11).
		// Opponent: [0|3] and [6|6]. Open end: 5.
		GameState state = TwoSeatState(
			{ Tile{0,5}, Tile{5,6} },
			{ Tile{0,3}, Tile{6,6} },
			{ 5 });

		auto rec = FindBestMove(state, 0, 8);
		Check(rec.valid, "TestSearchLooksPastImmediateReply: a move is found", "both A and B are legal replies to open end 5");
		bool pickedA = (rec.tile.low == 0 && rec.tile.high == 5) || (rec.tile.low == 5 && rec.tile.high == 0);
		Check(pickedA, "FindBestMove avoids the move that hands the opponent an outright win", "picked [5|6] (feeds the opponent's [6|6] double, an immediate opponent win) instead of [0|5]");
	}
}

int main()
{
	TestDecodeTableEndpoints();
	TestDecodeTableExhaustive();
	TestDecodeOutOfRange();
	TestEncodeIsInverseOfDecode();
	TestPipTotal();
	TestSearchNoLegalMoveIsInvalid();
	TestSearchImmediateWinPreferredOverAnythingElse();
	TestSearchLooksPastImmediateReply();

	if (g_failures == 0)
	{
		std::printf("ALL PASS\n");
		return 0;
	}

	std::printf("%d FAILURE(S)\n", g_failures);
	return 1;
}
