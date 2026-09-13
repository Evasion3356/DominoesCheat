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
#include "../src/AsyncMoveAdvisor.h"
#include "../src/Config.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <new>
#include <random>
#include <thread>
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

	GameState TwoSeatState(const std::vector<Tile>& myHand, const std::vector<Tile>& oppHand, const std::vector<std::int32_t>& openEnds)
	{
		GameState state;
		state.occupied[0] = true;
		state.occupied[1] = true;
		for (const Tile& t : myHand)
			state.hands[0][static_cast<std::size_t>(state.handCounts[0]++)] = t;
		for (const Tile& t : oppHand)
			state.hands[1][static_cast<std::size_t>(state.handCounts[1]++)] = t;
		for (std::int32_t pip : openEnds)
			state.ends.pips[static_cast<std::size_t>(state.ends.count++)] = pip;
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

	void TestSearchBudgetKeepsCompletedIteration()
	{
		GameState state = TwoSeatState(
			{ Tile{0,5}, Tile{5,6}, Tile{1,5} },
			{ Tile{0,3}, Tile{6,6}, Tile{1,2} }, { 5 });
		auto fallback = FindBestMove(state, 0, 20, 0);
		Check(fallback.valid && fallback.tile.low == 5 && fallback.tile.high == 6,
			"zero budget considers every root move", "the highest-pip static choice is not the first tile");
		bool consistent = true;
		for (int budget = 0; budget <= 200; budget++)
		{
			auto rec = FindBestMove(state, 0, 20, budget);
			auto completed = FindBestMove(state, 0, rec.completedDepth, 1000000);
			consistent = consistent && rec.valid && rec.handIndex == completed.handIndex &&
				rec.endPip == completed.endPip && rec.score == completed.score;
		}
		Check(consistent, "budget interruptions retain the last complete root iteration",
			"a partial iteration changed the recommendation or score");
		std::swap(state.hands[0][0], state.hands[0][1]);
		auto reordered = FindBestMove(state, 0, 20, 0);
		Check(reordered.tile.low == fallback.tile.low && reordered.tile.high == fallback.tile.high,
			"static fallback is not biased toward the first hand tile", "reordering changed the uniquely best static tile");
	}

	void TestSearchTerminalScores()
	{
		using namespace DominoSearch;
		GameState state = TwoSeatState({ Tile{0,1} }, { Tile{5,6} }, { 3 });
		state.passStreak = 2;
		int budget = 0;
		bool complete = true;
		auto score = detail::Search(state, 0, 4, -2000000, 2000000, budget, complete);
		Check(complete && score >= detail::kWinScore && budget == 0,
			"blocked win is terminal even with no budget", "terminal win was treated as a small pip estimate");
		std::swap(state.hands[0][0], state.hands[1][0]);
		score = detail::Search(state, 0, 4, -2000000, 2000000, budget, complete);
		Check(complete && score <= -detail::kWinScore,
			"blocked loss dominates heuristic scores", "terminal loss was treated as an ordinary cutoff");
		state.hands[0][0] = Tile{1,4};
		state.hands[1][0] = Tile{2,3};
		score = detail::Search(state, 0, 4, -2000000, 2000000, budget, complete);
		Check(score == 0, "equal blocked pip totals remain neutral", "tie was classified as a win or loss");
		state.passStreak = 0;
		state.handCounts[0] = 0;
		auto earlyWin = detail::Search(state, 0, 5, -2000000, 2000000, budget, complete);
		auto lateWin = detail::Search(state, 0, 1, -2000000, 2000000, budget, complete);
		Check(complete && earlyWin > lateWin && lateWin >= detail::kWinScore,
			"prefer earlier wins and recognize empty hands without budget", "remaining-depth preference is reversed");
		state.handCounts[0] = 1;
		state.handCounts[1] = 0;
		auto earlyLoss = detail::Search(state, 0, 5, -2000000, 2000000, budget, complete);
		auto lateLoss = detail::Search(state, 0, 1, -2000000, 2000000, budget, complete);
		Check(earlyLoss < lateLoss && lateLoss <= -detail::kWinScore,
			"prefer delaying a forced loss", "remaining-depth loss preference is reversed");
	}

	void TestSearchSnapshotAndTurnHandling()
	{
		GameState state = TwoSeatState({ Tile{0,5}, Tile{5,6} }, { Tile{0,3}, Tile{6,6} }, { 5, 5 });
		GameState changed = state;
		Check(state == changed, "identical search snapshots compare equal", "unchanged decision must remain cacheable");
		changed.hands[1][0] = Tile{0,4};
		Check(!(state == changed), "opponent tile changes invalidate advice", "own hand and open ends alone are insufficient");
		changed = state;
		changed.handCounts[1]--;
		Check(!(state == changed), "opponent hand count changes invalidate advice", "count omitted from snapshot key");
		changed = state;
		changed.occupied[2] = true;
		Check(!(state == changed), "seat occupancy changes invalidate advice", "occupancy omitted from snapshot key");
		changed = state;
		changed.ends.pips[1] = 6;
		Check(!(state == changed), "open end changes invalidate advice", "board omitted from snapshot key");
		changed = state;
		changed.passStreak = 1;
		Check(!(state == changed), "pass streak changes invalidate advice", "pass history omitted from snapshot key");
		changed = state;
		changed.turnSeat = 1;
		Check(!(state == changed) && !FindBestMove(changed, 0, 8).valid,
			"wrong-turn requests are rejected", "advisor must not play out of turn");
		Check(DominoSearch::detail::LegalMoves(state, 0).count == 2,
			"repeated open pips do not duplicate equivalent moves", "duplicates waste search capacity");
		auto moves = DominoSearch::detail::LegalMoves(state, 0);
		auto next = DominoSearch::detail::ApplyMove(state, 0, moves.moves[0]);
		Check(next.ends.pips[0] == 0 && next.ends.pips[1] == 5,
			"playing one repeated end preserves the other slot", "deduplication must not erase board ends");
		changed = state;
		changed.handCounts[1] = 0;
		Check(!FindBestMove(changed, 0, 8).valid, "finished rounds produce no advice", "opponent already went out");
	}

	// Exhaustive outcome-only reference: no pruning, cutoff, budget or
	// production terminal scorer. Shares only move generation/transitions.
	int ReferenceOutcome(const GameState& state, int mySeat)
	{
		if (state.handCounts[mySeat] == 0)
			return 1;
		int occupied = 0;
		int myPips = HandPipTotal(state.hands[mySeat].data(), state.handCounts[mySeat]);
		int opponentPips = 1000;
		for (int seat = 0; seat < DominoSearch::kMaxSeats; seat++)
		{
			if (!state.occupied[seat])
				continue;
			occupied++;
			if (seat == mySeat)
				continue;
			if (state.handCounts[seat] == 0)
				return -1;
			opponentPips = std::min(opponentPips, HandPipTotal(state.hands[seat].data(), state.handCounts[seat]));
		}
		if (state.passStreak >= occupied)
			return (opponentPips > myPips) - (opponentPips < myPips);
		auto moves = DominoSearch::detail::LegalMoves(state, state.turnSeat);
		if (moves.Empty())
			return ReferenceOutcome(DominoSearch::detail::ApplyPass(state, state.turnSeat), mySeat);
		bool maximizing = state.turnSeat == mySeat;
		int requiredPips = -1;
		if (!maximizing && moves.count <= 15 &&
			(state.rules == DominoAiPolicy::Rules::Block || state.rules == DominoAiPolicy::Rules::Draw))
			for (const auto& move : moves)
				requiredPips = std::max(requiredPips, move.tile.low + move.tile.high);
		int best = maximizing ? -2 : 2;
		for (const auto& move : moves)
		{
			if (requiredPips >= 0 && move.tile.low + move.tile.high != requiredPips)
				continue;
			int outcome = ReferenceOutcome(DominoSearch::detail::ApplyMove(state, state.turnSeat, move), mySeat);
			best = maximizing ? std::max(best, outcome) : std::min(best, outcome);
		}
		return best;
	}

	void TestSearchAgainstExhaustiveEndgames()
	{
		std::mt19937 random(20260913);
		std::array<int, kTileSetSize> deck{};
		for (int i = 0; i < kTileSetSize; i++)
			deck[i] = i;
		bool correct = true;
		int decisions = 0;
		for (int sample = 0; sample < 240; sample++)
		{
			std::shuffle(deck.begin(), deck.end(), random);
			GameState state;
			int seats = 2 + sample % 3;
			int mySeat = sample % seats;
			int cursor = 0;
			for (int seat = 0; seat < seats; seat++)
			{
				state.occupied[seat] = true;
				state.handCounts[seat] = 2;
				for (int i = 0; i < 2; i++)
					state.hands[seat][i] = DecodeTile(deck[cursor++]);
			}
			Tile board = DecodeTile(deck[cursor]);
			state.ends.count = 2;
			state.ends.pips[0] = board.low;
			state.ends.pips[1] = board.high;
			state.turnSeat = mySeat;
			auto moves = DominoSearch::detail::LegalMoves(state, mySeat);
			auto rec = FindBestMove(state, mySeat, (cursor + 1) * seats);
			if (moves.Empty())
			{
				correct = correct && !rec.valid;
				continue;
			}
			decisions++;
			bool legal = false;
			for (const auto& move : moves)
				if (rec.valid && move.handIndex == rec.handIndex && move.endPip == rec.endPip)
				{
					legal = true;
					int actual = ReferenceOutcome(DominoSearch::detail::ApplyMove(state, mySeat, move), mySeat);
					correct = correct && actual == ReferenceOutcome(state, mySeat);
				}
			correct = correct && legal;
		}
		std::printf("  Exhaustive comparison: %d legal decisions across 240 seeded 2/3/4-seat positions\n", decisions);
		Check(correct && decisions > 100, "bounded search matches exhaustive small-endgame outcomes",
			"an illegal or outcome-inferior move was recommended");
	}

	void TestScriptedCandidateSelection()
	{
		using namespace DominoAiPolicy;
		Tile hand[] = { {0,1}, {6,6}, {2,4}, {3,3} };
		Candidate candidates[] = { {1,true,7}, {0,true,5}, {2,true,10}, {3,true,10} };
		Check(SelectCandidate(Rules::AllFives, hand, 4, candidates, 4) == 3,
			"AI chooses largest scoring total and last scoring tie", "score priority or native tie order differs from script");
		Check(SelectCandidate(Rules::Block, hand, 4, candidates, 4) == 0 &&
			SelectCandidate(Rules::Draw, hand, 4, candidates, 4) == 0,
			"Block/Draw AI discards highest-pip tile", "non-scoring policy incorrectly prioritized board total");
		Candidate reversed[] = { {3,true,10}, {2,true,10}, {0,true,5}, {1,true,7} };
		Check(SelectCandidate(Rules::AllFives, hand, 4, reversed, 4) == 1,
			"native order, not hand index, determines scoring ties", "selector invented a hand-index tie-break");
		Candidate scoringTie[] = { {1,true,10}, {0,true,10} };
		Check(SelectCandidate(Rules::AllFives, hand, 4, scoringTie, 2) == 1,
			"scoring tie ignores played tile pip total", "AI must take last equal-scoring candidate even with fewer pips");
		Candidate pipTie[] = { {3,true,0}, {2,true,0}, {3,false,0}, {50,true,30}, {-1,true,30} };
		Check(SelectCandidate(Rules::AllThrees, hand, 4, pipTie, 5) == 1,
			"pip fallback uses last valid tie and ignores invalid candidates", "invalid placement or out-of-range index selected");
		Candidate threes[] = { {0,true,3}, {1,true,0}, {2,true,9}, {3,true,5} };
		Check(SelectCandidate(Rules::AllThrees, hand, 4, threes, 4) == 2 &&
			ScoringPoints(Rules::AllThrees, 0) == 0 && ScoringPoints(Rules::AllFives, 9) == 0,
			"All Threes uses nonzero multiples of three", "zero or nonmultiple incorrectly scores");
		Check(SelectCandidate(Rules::Unknown, hand, 4, candidates, 4) == -1 &&
			SelectCandidate(Rules::Block, hand, 0, candidates, 4) == -1 &&
			SelectCandidate(Rules::Draw, hand, 4, candidates, 0) == -1,
			"unsupported or empty candidate inputs do not invent predictions", "unexpected prediction");
		Check(DecodeRules(-1617663169) == Rules::Block && DecodeRules(-1360983891) == Rules::Draw &&
			DecodeRules(-382896522) == Rules::AllThrees && DecodeRules(-1234859967) == Rules::AllFives &&
			DecodeRules(0) == Rules::Unknown, "script rule hashes map to verified modes", "rule ID mismatch");
	}

	void TestScriptedSeatOrder()
	{
		constexpr int cycle[] = { 0, 2, 1, 3 };
		bool correct = true;
		for (int mask = 1; mask < 16; mask++)
		{
			GameState state;
			for (int seat = 0; seat < 4; seat++)
				state.occupied[seat] = (mask & (1 << seat)) != 0;
			for (int from = 0; from < 4; from++)
			{
				int position = 0;
				while (cycle[position] != from)
					position++;
				int expected = from;
				for (int step = 1; step <= 4; step++)
					if (state.occupied[cycle[(position + step) % 4]])
					{
						expected = cycle[(position + step) % 4];
						break;
					}
				correct = correct && DominoSearch::detail::NextSeat(state, from) == expected &&
					DominoSearch::detail::ApplyPass(state, from).turnSeat == expected;
			}
		}
		Check(correct, "turn order matches func_324 for all occupancy layouts", "expected 0->2->1->3, skipping empty seats");
		GameState state = TwoSeatState({ Tile{0,5} }, { Tile{3,4} }, { 5 });
		state.occupied[2] = state.occupied[3] = true;
		auto move = DominoSearch::detail::LegalMoves(state, 0).moves[0];
		Check(DominoSearch::detail::ApplyMove(state, 0, move).turnSeat == 2,
			"placements advance to seat 2 after seat 0", "numeric seat-order regression");
	}

	void TestPolicyRestrictionsPreserveUncertainty()
	{
		using DominoAiPolicy::Rules;
		GameState state = TwoSeatState({ Tile{4,4} }, { Tile{0,6}, Tile{1,5}, Tile{2,2} }, { 0, 1, 2 });
		state.rules = Rules::Draw;
		auto replies = DominoSearch::detail::OpponentMoves(state, 1);
		Check(replies.count == 2 && replies.moves[0].tile.PipTotal() == 6 && replies.moves[1].tile.PipTotal() == 6,
			"search keeps all highest-pip NPC ties", "guessed native order or retained lower-pip reply");
		GameState changed = state;
		changed.rules = Rules::Block;
		Check(!(state == changed), "table-rule changes invalidate advice", "rules omitted from snapshot equality");
		bool conservative = true;
		for (auto rules : { Rules::AllFives, Rules::AllThrees, Rules::Unknown })
		{
			state.rules = rules;
			conservative = conservative && DominoSearch::detail::OpponentMoves(state, 1).count == 3;
		}
		Check(conservative, "unmodeled scoring/unknown modes retain all replies", "distinct-end sum was used as an exact scoring total");
		state = TwoSeatState({ Tile{4,4} }, { Tile{1,2}, Tile{0,3} }, { 1, 2, 3 });
		state.rules = Rules::Block;
		Check(DominoSearch::detail::OpponentMoves(state, 1).count == 3,
			"equal-pip placements on different ends remain possible", "placement uncertainty was discarded");
	}

	int RecommendationOutcome(const GameState& state, const DominoSearch::Recommendation& rec)
	{
		for (const auto& move : DominoSearch::detail::LegalMoves(state, state.turnSeat))
			if (rec.valid && move.handIndex == rec.handIndex && move.endPip == rec.endPip)
				return ReferenceOutcome(DominoSearch::detail::ApplyMove(state, state.turnSeat, move), state.turnSeat);
		return -2;
	}

	void TestPolicyAwareEndgames()
	{
		std::mt19937 random(324353);
		std::array<int, kTileSetSize> deck{};
		for (int i = 0; i < kTileSetSize; i++)
			deck[i] = i;
		bool correct = true;
		int decisions = 0;
		int improved = 0;
		for (int sample = 0; sample < 768; sample++)
		{
			std::shuffle(deck.begin(), deck.end(), random);
			GameState state;
			int seats = 2 + sample % 3;
			int tilesPerSeat = seats == 2 ? 3 : 2;
			int cursor = 0;
			for (int seat = 0; seat < seats; seat++)
			{
				state.occupied[seat] = true;
				state.handCounts[seat] = tilesPerSeat;
				for (int i = 0; i < tilesPerSeat; i++)
					state.hands[seat][i] = DecodeTile(deck[cursor++]);
			}
			state.turnSeat = sample % seats;
			state.rules = sample % 2 ? DominoAiPolicy::Rules::Draw : DominoAiPolicy::Rules::Block;
			Tile board = DecodeTile(deck[cursor]);
			state.ends.count = 2;
			state.ends.pips[0] = board.low;
			state.ends.pips[1] = board.high;
			int depth = (cursor + 1) * seats;
			auto rec = FindBestMove(state, state.turnSeat, depth);
			if (DominoSearch::detail::LegalMoves(state, state.turnSeat).Empty())
			{
				correct = correct && !rec.valid;
				continue;
			}
			decisions++;
			int actual = RecommendationOutcome(state, rec);
			correct = correct && actual == ReferenceOutcome(state, state.turnSeat);
			GameState paranoid = state;
			paranoid.rules = DominoAiPolicy::Rules::Unknown;
			auto old = FindBestMove(paranoid, state.turnSeat, depth);
			if (actual > RecommendationOutcome(state, old))
				improved++;
		}
		std::printf("  Script-policy comparison: %d legal decisions, %d strictly better outcomes than paranoid choices\n", decisions, improved);
		Check(correct && decisions > 300, "policy-aware search matches exhaustive scripted-opponent outcomes", "inferior or illegal policy-aware recommendation");
		Check(improved > 0, "search exploits scripted replies to improve outcomes", "no policy exploitation found in seeded endgames");
	}

	void TestRuntimePresets()
	{
		Check(Config::Values{}.AdvisorRuntime == Config::RuntimePreset::Medium &&
			Config::RuntimeMilliseconds(Config::RuntimePreset::Medium) == 1000,
			"runtime defaults to Medium (1000 ms)", "default runtime changed");
		Check(Config::RuntimeMilliseconds(Config::RuntimePreset::Low) == 250 &&
			Config::RuntimeMilliseconds(Config::RuntimePreset::High) == 5000,
			"Low and High runtime allowances", "preset timing mismatch");
		Check(Config::ParseRuntimePreset("HIGH") == Config::RuntimePreset::High &&
			Config::ParseRuntimePreset("low") == Config::RuntimePreset::Low &&
			Config::ParseRuntimePreset("invalid") == Config::RuntimePreset::Medium &&
			Config::ParseRuntimePreset("") == Config::RuntimePreset::Medium,
			"runtime parsing is case-insensitive with safe fallback", "invalid runtime must select Medium");
		Check(std::string(Config::RuntimePresetName(Config::RuntimePreset::Medium)) == "Medium",
			"runtime serializes to canonical preset name", "INI value must be Medium");
	}

	void TestTimedSearchStopsAtCompletedDepth()
	{
		GameState state = TwoSeatState({ Tile{0,5}, Tile{5,6}, Tile{1,5} },
			{ Tile{0,3}, Tile{6,6}, Tile{1,2} }, { 5 });
		DominoSearch::SearchControl control;
		control.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
		auto expired = FindBestMove(state, 0, 20, -1, &control);
		Check(expired.valid && expired.completedDepth == 0,
			"expired deadline retains whole-root fallback", "expired search should not deepen");
		std::atomic<std::uint64_t> generation{ 1 };
		control.deadline = std::chrono::steady_clock::time_point::max();
		control.generation = &generation;
		control.expectedGeneration = 1;
		int publications = 0;
		control.publish = [&](const DominoSearch::Recommendation& rec)
		{
			publications++;
			if (rec.completedDepth == 2)
				generation.fetch_add(1);
		};
		auto canceled = FindBestMove(state, 0, 20, -1, &control);
		auto reference = FindBestMove(state, 0, 2);
		Check(publications == 3 && canceled.completedDepth == 2 && canceled.score == reference.score &&
			canceled.handIndex == reference.handIndex,
			"cancellation retains and publishes only completed depths", "canceled iteration changed the answer");
		control.generation = nullptr;
		control.publish = [&](const DominoSearch::Recommendation& rec)
		{
			if (rec.completedDepth == 1)
				control.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
		};
		auto timed = FindBestMove(state, 0, 20, -1, &control);
		Check(timed.completedDepth == 1, "deadline stops progressive deepening", "deadline ignored between iterations");
	}

	GameState BusySearchState()
	{
		GameState state;
		std::array<int, kTileSetSize> deck{};
		for (int i = 0; i < kTileSetSize; i++)
			deck[i] = i;
		std::mt19937 random(517);
		std::shuffle(deck.begin(), deck.end(), random);
		for (int seat = 0; seat < 4; seat++)
		{
			state.occupied[seat] = true;
			state.handCounts[seat] = 6;
			for (int i = 0; i < 6; i++)
				state.hands[seat][i] = DecodeTile(deck[seat * 6 + i]);
		}
		state.ends.count = 2;
		state.ends.pips[0] = 1;
		state.ends.pips[1] = 4;
		return state;
	}

	bool WaitForFinished(AsyncMoveAdvisor<int>& advisor, int key, std::chrono::milliseconds timeout)
	{
		auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			auto result = advisor.GetLatest();
			if (result.valid && result.finished && result.key == key)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}

	void TestWorkerDeduplicatesAndCancels()
	{
		AsyncMoveAdvisor<int> advisor;
		advisor.Testing_SetArtificialJobDelay(std::chrono::milliseconds(80));
		GameState state = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });
		advisor.SubmitJob(1, state, 0, 4);
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		while (advisor.Testing_JobsStarted() == 0 && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		for (int i = 0; i < 100; i++)
			advisor.SubmitJob(1, state, 0, 4);
		Check(WaitForFinished(advisor, 1, std::chrono::seconds(1)), "duplicate requests do not starve running job", "no finished result");
		for (int i = 0; i < 100; i++)
			advisor.SubmitJob(1, state, 0, 4);
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		Check(advisor.Testing_JobsStarted() == 1, "pending/running/finished requests are deduplicated", "identical job ran more than once");
		advisor.SubmitJob(2, state, 0, 4);
		advisor.Cancel();
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		Check(!advisor.GetLatest().valid, "canceled jobs cannot republish stale advice", "canceled generation published");
		advisor.SubmitJob(2, state, 0, 4);
		Check(WaitForFinished(advisor, 2, std::chrono::seconds(1)), "same position can resume after turn cancellation", "canceled key remained deduplicated");
		int started = advisor.Testing_JobsStarted();
		advisor.SubmitJob(2, state, 0, 4, std::chrono::milliseconds(250));
		Check(WaitForFinished(advisor, 2, std::chrono::seconds(1)) && advisor.Testing_JobsStarted() == started + 1,
			"runtime changes trigger fresh evaluation", "runtime omitted from job identity");
	}

	void TestWorkerTimedRuntimeAndPreemption()
	{
		AsyncMoveAdvisor<int> advisor;
		GameState state = BusySearchState();
		auto start = std::chrono::steady_clock::now();
		advisor.SubmitJob(1, state, 0, 100, std::chrono::milliseconds(30));
		Check(WaitForFinished(advisor, 1, std::chrono::seconds(1)), "wall-clock allowance bounds worker evaluation", "30 ms job exceeded scheduling tolerance");
		Check(advisor.GetLatest().rec.valid && std::chrono::steady_clock::now() - start < std::chrono::seconds(1),
			"timed worker retains legal completed advice", "deadline lost the recommendation");
		advisor.SubmitJob(2, state, 0, 100, std::chrono::seconds(5));
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		while (!advisor.GetLatest().valid && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		auto progressive = advisor.GetLatest();
		Check(progressive.valid && !progressive.finished, "worker publishes advice before long evaluation finishes", "no progressive result from busy position");
		GameState next = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });
		advisor.SubmitJob(3, next, 0, 4);
		Check(WaitForFinished(advisor, 3, std::chrono::seconds(1)) && advisor.GetLatest().rec.isWinningMove,
			"fresh decision preempts five-second search promptly", "worker waited for stale job's full runtime");
		advisor.Cancel();
		Check(!advisor.GetLatest().valid, "turn cancellation clears published advice", "stale advice survived turn end");
	}

	// AsyncMoveAdvisor.h sanity check -- added alongside DetermineBestMove()'s
	// threading rewrite (2026-09-13, second live-freeze fix) since the
	// real in-game plumbing can't be exercised here. Submits a job with
	// an obvious answer (a single winning tile) to a real background
	// worker thread and polls GetLatest() (exactly as DetermineBestMove()
	// does) until a matching result shows up, verifying the worker
	// actually runs, publishes under its own mutex correctly, and
	// produces the same answer FindBestMove() gives synchronously for
	// the identical input.
	void TestAsyncAdvisorPublishesMatchingResult()
	{
		GameState state = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });

		AsyncMoveAdvisor<int> advisor;
		advisor.SubmitJob(1, state, 0, 4);

		AsyncMoveAdvisor<int>::Published published;
		auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
		{
			published = advisor.GetLatest();
			if (published.valid && published.key == 1)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		Check(published.valid && published.key == 1, "AsyncMoveAdvisor publishes a result for a submitted job", "timed out waiting 2s for the background worker to publish");
		Check(published.rec.valid && published.rec.isWinningMove, "AsyncMoveAdvisor's published result matches the synchronous answer", "expected the single legal tile to be reported as a winning move, same as FindBestMove() gives directly");
	}

	// Waits (with a generous timeout, since this is real OS thread
	// scheduling) for AsyncMoveAdvisorDetail::g_liveWorkerCount to reach
	// a target value. Returns false on timeout.
	bool WaitForLiveWorkerCount(int target, std::chrono::milliseconds timeout)
	{
		auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < timeout)
		{
			if (AsyncMoveAdvisorDetail::g_liveWorkerCount.load() == target)
				return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return AsyncMoveAdvisorDetail::g_liveWorkerCount.load() == target;
	}

	// Covers the destructor's NORMAL path (AsyncMoveAdvisorDetail::
	// g_processDetaching == false, the state for any ordinary instance --
	// this is the same path a plain local AsyncMoveAdvisor takes, exactly
	// like the one right above), added the same day as the join()/detach()
	// split itself (2026-09-13, review response to the earlier
	// unconditional-join() version's DllMain deadlock risk). Forces a job
	// to still be running (via Testing_SetArtificialJobDelay()) at the
	// exact moment the destructor fires, then confirms two things a
	// silent regression back to unconditional detach() would break:
	// (1) the destructor actually BLOCKS until that in-flight job (and
	// the worker thread itself) finishes -- proven by elapsed wall time,
	// not just "didn't crash" -- and (2) the worker has FULLY exited
	// (g_liveWorkerCount back to 0) by the time the destructor returns,
	// which join()'s happens-before guarantee makes deterministic, not a
	// race.
	void TestAsyncAdvisorJoinsSynchronouslyOnNormalTeardown()
	{
		AsyncMoveAdvisorDetail::g_processDetaching.store(false);

		constexpr auto kJobDelay = std::chrono::milliseconds(150);
		constexpr auto kHeadStart = std::chrono::milliseconds(30); // time given to the worker, before beforeDestroy, to pick up the job and start sleeping
		std::chrono::steady_clock::time_point beforeDestroy;
		{
			AsyncMoveAdvisor<int> advisor;
			Check(WaitForLiveWorkerCount(1, std::chrono::seconds(2)), "TestAsyncAdvisorJoinsSynchronouslyOnNormalTeardown: worker starts", "g_liveWorkerCount never reached 1");

			advisor.Testing_SetArtificialJobDelay(kJobDelay);
			GameState state = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });
			advisor.SubmitJob(1, state, 0, 1);
			// Give the worker time to pick the job up and enter the
			// artificial delay before we destroy `advisor` below.
			std::this_thread::sleep_for(kHeadStart);

			beforeDestroy = std::chrono::steady_clock::now();
			// ~AsyncMoveAdvisor() runs at the closing brace below.
		}
		auto elapsed = std::chrono::steady_clock::now() - beforeDestroy;

		// By beforeDestroy, roughly kHeadStart of the artificial delay has
		// already elapsed, so the destructor should block for roughly
		// (kJobDelay - kHeadStart) more -- generous slack below that for
		// real OS scheduling jitter, since this only needs to distinguish
		// "blocked for a real chunk of the remaining delay" from "returned
		// near-instantly" (what an unconditional detach() would do).
		Check(elapsed >= kJobDelay - kHeadStart - std::chrono::milliseconds(50), "normal teardown's destructor blocks until the in-flight job finishes", "expected the destructor to take roughly the remaining artificial job delay (join, not detach)");
		Check(AsyncMoveAdvisorDetail::g_liveWorkerCount.load() == 0, "normal teardown's destructor guarantees the worker has fully exited before returning", "join()'s happens-before should make this deterministic, not a race");
	}

	// Both cover the destructor's PROCESS-DETACHING path
	// (AsyncMoveAdvisorDetail::g_processDetaching == true, set for real by
	// main.cpp's DllMain at the top of DLL_PROCESS_DETACH), added/revised
	// same day (2026-09-13) after the user's own live testing via
	// ScriptHookRDR2.dev's hot-reload found an earlier, simpler
	// unconditional-detach() version left the .asi file locked on disk
	// after eject -- exactly the "worker still running when the module
	// unmaps" hazard the bounded-wait redesign exists to shrink. Neither
	// test destroys a normal, stack- or heap-owned instance: doing so
	// would exercise the exact use-after-free this file's own header
	// comment warns about (the destructor can return via detach() before
	// the worker has necessarily stopped touching this object's members,
	// and unlike the real production caller -- a function-local static
	// whose storage is never reused before the whole process dies -- this
	// TEST PROCESS keeps running afterward and could reuse that memory
	// for something else). Instead, each object is placement-new'd into
	// function-local static storage and never deallocated (only ever
	// destructed) -- mirroring the real justification exactly: the bytes
	// remain valid for the rest of the process's life either way, so a
	// still-running worker touching them after the destructor returns is
	// safe here too, not just assumed safe.

	// Case A: the in-flight job finishes WELL WITHIN
	// AsyncMoveAdvisorDetail::kProcessDetachWaitTimeout. The destructor
	// should actually wait for it (not return instantly) and the worker
	// should be fully exited (g_liveWorkerCount back to 0) by the time it
	// does -- proving the bounded wait, not just the fallback detach(),
	// is what's doing the work here.
	void TestAsyncAdvisorProcessDetachWaitCompletesForQuickJob()
	{
		using Advisor = AsyncMoveAdvisor<int>;
		static alignas(Advisor) std::byte storage[sizeof(Advisor)];
		Advisor* advisor = new (&storage) Advisor();

		Check(WaitForLiveWorkerCount(1, std::chrono::seconds(2)), "TestAsyncAdvisorProcessDetachWaitCompletesForQuickJob: worker starts", "g_liveWorkerCount never reached 1");

		constexpr auto kJobDelay = std::chrono::milliseconds(80); // well under kProcessDetachWaitTimeout (500ms)
		advisor->Testing_SetArtificialJobDelay(kJobDelay);
		GameState state = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });
		advisor->SubmitJob(1, state, 0, 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(20)); // let it pick the job up and start the delay

		AsyncMoveAdvisorDetail::g_processDetaching.store(true);
		auto start = std::chrono::steady_clock::now();
		advisor->~Advisor();
		auto elapsed = std::chrono::steady_clock::now() - start;
		AsyncMoveAdvisorDetail::g_processDetaching.store(false);

		Check(elapsed < AsyncMoveAdvisorDetail::kProcessDetachWaitTimeout, "process-detaching teardown's bounded wait returns well before the timeout when the job finishes on its own", "expected the destructor to notice m_workerExited and return long before the 500ms timeout, not wait the full budget every time");
		Check(AsyncMoveAdvisorDetail::g_liveWorkerCount.load() == 0, "process-detaching teardown's bounded wait actually waits for the worker, not just the fallback detach()", "expected the worker to have fully exited by the time the destructor returns, since the job finished within the timeout");
	}

	// Case B: the in-flight job does NOT finish within
	// kProcessDetachWaitTimeout. The destructor should block for roughly
	// the bounded timeout (not indefinitely -- this is the guarantee that
	// keeps this path safe against DllMain, see the header comment) and
	// then fall back to detach(), leaving the worker to finish on its own
	// afterward.
	void TestAsyncAdvisorProcessDetachTimesOutThenDetachesForSlowJob()
	{
		using Advisor = AsyncMoveAdvisor<int>;
		static alignas(Advisor) std::byte storage[sizeof(Advisor)];
		Advisor* advisor = new (&storage) Advisor();

		Check(WaitForLiveWorkerCount(1, std::chrono::seconds(2)), "TestAsyncAdvisorProcessDetachTimesOutThenDetachesForSlowJob: worker starts", "g_liveWorkerCount never reached 1");

		constexpr auto kJobDelay = std::chrono::milliseconds(800); // well over kProcessDetachWaitTimeout (500ms)
		advisor->Testing_SetArtificialJobDelay(kJobDelay);
		GameState state = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });
		advisor->SubmitJob(1, state, 0, 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(20));

		AsyncMoveAdvisorDetail::g_processDetaching.store(true);
		auto start = std::chrono::steady_clock::now();
		advisor->~Advisor();
		auto elapsed = std::chrono::steady_clock::now() - start;
		AsyncMoveAdvisorDetail::g_processDetaching.store(false);

		Check(elapsed >= AsyncMoveAdvisorDetail::kProcessDetachWaitTimeout - std::chrono::milliseconds(50), "process-detaching teardown blocks for roughly the full timeout when the job outlives it", "expected the destructor to wait close to kProcessDetachWaitTimeout before giving up");
		Check(elapsed < AsyncMoveAdvisorDetail::kProcessDetachWaitTimeout + std::chrono::milliseconds(200), "process-detaching teardown's wait is still bounded, not indefinite", "a regression back to join() here would block for the job's full ~800ms (or hang outright under real DllMain) instead of stopping at the timeout");
		Check(WaitForLiveWorkerCount(0, std::chrono::seconds(2)), "the detached worker still exits cleanly on its own once its job finally finishes", "expected g_liveWorkerCount back to 0 within 2s even though the destructor gave up waiting");
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
	TestSearchBudgetKeepsCompletedIteration();
	TestSearchTerminalScores();
	TestSearchSnapshotAndTurnHandling();
	TestSearchAgainstExhaustiveEndgames();
	TestScriptedCandidateSelection();
	TestScriptedSeatOrder();
	TestPolicyRestrictionsPreserveUncertainty();
	TestPolicyAwareEndgames();
	TestRuntimePresets();
	TestTimedSearchStopsAtCompletedDepth();
	TestWorkerDeduplicatesAndCancels();
	TestWorkerTimedRuntimeAndPreemption();
	TestAsyncAdvisorPublishesMatchingResult();
	TestAsyncAdvisorJoinsSynchronouslyOnNormalTeardown();
	TestAsyncAdvisorProcessDetachWaitCompletesForQuickJob();
	TestAsyncAdvisorProcessDetachTimesOutThenDetachesForSlowJob();

	if (g_failures == 0)
	{
		std::printf("ALL PASS\n");
		return 0;
	}

	std::printf("%d FAILURE(S)\n", g_failures);
	return 1;
}
