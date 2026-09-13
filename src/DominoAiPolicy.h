/*
	Policy derived from dominoes_sp.ysc.c, build 1491.50:
	func_168/351 call func_352 (scoring choice), then func_353 (pip choice).
	func_613 validates the placement descriptor; func_614 tests scoring;
	func_615 sums the tile's pips. Equal ranks select the LAST candidate
	in the native's order, not the highest hand index or a random move.

	This selector is exact for the supplied native candidate list. It does
	not generate hypothetical board placements or guess their order/totals.
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
