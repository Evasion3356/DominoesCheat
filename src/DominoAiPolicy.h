/*
	Policy derived from dominoes_sp.ysc.c, build 1491.50:
	func_168/351 call func_352 (scoring choice), then func_353 (pip choice).
	func_613 validates the placement descriptor; func_614 tests scoring;
	func_615 sums the tile's pips. Equal ranks select the LAST candidate
	in the native's order, not the highest hand index or a random move.

	This selector is exact for the supplied native candidate list. It does
	not generate hypothetical board placements or guess their order/totals.

	Also here (2026-09-13, decision-engine pass): the two other rule-
	dependent facts the search needs, both read straight off the script:
	  - RoundedPipTotal(): func_357 (line ~14897) -- the per-seat remaining-
	    pip total the round-end scorer actually uses. Plain pip sum for
	    Block/Draw; All Fives rounds it to the NEAREST multiple of five
	    (`num = num + 2; num = num - (num % 5)`), All Threes to the nearest
	    multiple of three (`+1`, `% 3`). func_169 (blocked round) and the
	    domino-out path (line ~8553, via func_343) both pick the round
	    winner and pay them by THIS total, not by raw pips.
	  - DrawsFromBoneyard(): func_166 (line ~8632) returns 0 immediately
	    for the Block rules hash and otherwise draws from the boneyard
	    until the seat can play or the boneyard/19-tile hand cap runs out.
	    Draw, All Fives and All Threes all draw; an undecoded rules id is
	    treated as drawing too, since "anything but Block draws" is the
	    script's own test.
*/
#pragma once

#include "DominoHandEval.h"

#include <cstdint>

namespace DominoAiPolicy
{
	enum class Rules { Unknown, Block, Draw, AllThrees, AllFives };

	inline Rules DecodeRules(std::int32_t id)
	{
		switch (id)
		{
		case -1617663169: return Rules::Block;
		case -1360983891: return Rules::Draw;
		case -382896522: return Rules::AllThrees;
		case -1234859967: return Rules::AllFives;
		default: return Rules::Unknown;
		}
	}

	inline const char* RulesName(Rules rules)
	{
		switch (rules)
		{
		case Rules::Block: return "Block";
		case Rules::Draw: return "Draw";
		case Rules::AllThrees: return "All Threes";
		case Rules::AllFives: return "All Fives";
		default: return "Unknown";
		}
	}

	inline bool AlwaysUsesPipPriority(Rules rules)
	{
		return rules == Rules::Block || rules == Rules::Draw;
	}

	// func_166: only the Block rules never touch the boneyard.
	inline bool DrawsFromBoneyard(Rules rules)
	{
		return rules != Rules::Block;
	}

	// func_357: the remaining-hand total the round-end scorer compares
	// and pays out. `pipTotal` is the plain sum of the seat's tile pips.
	inline int RoundedPipTotal(Rules rules, int pipTotal)
	{
		if (rules == Rules::AllFives)
		{
			int n = pipTotal + 2;
			return n - (n % 5);
		}
		if (rules == Rules::AllThrees)
		{
			int n = pipTotal + 1;
			return n - (n % 3);
		}
		return pipTotal;
	}

	inline int ScoringPoints(Rules rules, int resultingEndTotal)
	{
		int divisor = rules == Rules::AllThrees ? 3 : (rules == Rules::AllFives ? 5 : 0);
		return divisor != 0 && resultingEndTotal > 0 && resultingEndTotal % divisor == 0
			? resultingEndTotal : 0;
	}

	struct Candidate
	{
		int handIndex = -1;
		bool hasPlacement = false; // native words f_1/f_2 are not both zero (func_613)
		int resultingEndTotal = 0; // native word f_4, NOT merely the new matching pip

		// Raw native words f_1/f_2/f_3 -- CONFIRMED via decompile (2026-09-17,
		// dominoes_sp func_843/func_934/func_935, the exact functions that
		// place the game's OWN ghost-preview object) to be a 2D BOARD GRID
		// COORDINATE (f_1/f_2, in 0.013125-world-unit cells relative to
		// Scene's own base coord+heading) plus an orientation selector
		// (f_3, tested against exactly {0,2} vs anything else) that picks
		// which small recentering offset applies. See
		// ComputeCandidateWorldPosition() in DominoCheat.cpp for the exact
		// port of that formula -- this is what finally makes "where do I
		// place it" a computed fact instead of an observed correlation.
		int gridF1 = 0;
		int gridF2 = 0;
		int gridOrientation = 0;
	};

	// Returns a candidate-list index, not a hand index. Unknown rules are
	// deliberately unsupported rather than guessed from an unverified ID.
	inline int SelectCandidate(Rules rules, const DominoHandEval::Tile* hand, int handCount,
		const Candidate* candidates, int candidateCount)
	{
		if (rules == Rules::Unknown || !hand || !candidates || handCount <= 0)
			return -1;
		int best = -1;
		int bestPoints = 0;
		int bestPips = -1;
		for (int i = 0; i < candidateCount; i++)
		{
			const Candidate& candidate = candidates[i];
			if (!candidate.hasPlacement || candidate.handIndex < 0 || candidate.handIndex >= handCount ||
				!hand[candidate.handIndex].IsValid())
				continue;
			int points = ScoringPoints(rules, candidate.resultingEndTotal);
			int pips = hand[candidate.handIndex].PipTotal();
			if (points > bestPoints || (points == bestPoints && (points > 0 || pips >= bestPips)))
			{
				best = i;
				bestPoints = points;
				bestPips = pips;
			}
		}
		return best;
	}
}
