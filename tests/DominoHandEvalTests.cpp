// Unit tests for src/DominoHandEval.h -- the self-contained domino tile
// decode table + pip scoring. Links against the SAME header the mod itself
// uses (not a hand-copied duplicate), same convention as PokerCheat/
// BlackjackCheat's own *HandEvalTests.cpp -- see either project's own test
// file header comment for why this matters (an untested hand evaluator went
// 9 sessions before a real bug was caught in PokerCheat's case).
//
// Build & run (from the DominoCheat directory):
//   MSBuild.exe tests\DominoHandEvalTests.vcxproj /p:Configuration=Debug /p:Platform=x64
//   bin\Debug\DominoHandEvalTests.exe
// Exits 0 and prints "ALL PASS" if every case passes, exits 1 and lists
// failures otherwise.

#include "../src/DominoHandEval.h"

#include <cstdio>

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
}

int main()
{
	TestDecodeTableEndpoints();
	TestDecodeTableExhaustive();
	TestDecodeOutOfRange();
	TestEncodeIsInverseOfDecode();
	TestPipTotal();

	if (g_failures == 0)
	{
		std::printf("ALL PASS\n");
		return 0;
	}

	std::printf("%d FAILURE(S)\n", g_failures);
	return 1;
}
