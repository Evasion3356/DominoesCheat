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
#include "../src/GameRecord.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <new>
#include <random>
#include <string>
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
		constexpr std::int32_t lo = std::numeric_limits<std::int32_t>::min();
		constexpr std::int32_t hi = std::numeric_limits<std::int32_t>::max();
		GameState state = TwoSeatState({ Tile{0,1} }, { Tile{5,6} }, { 3 });
		state.passStreak = 2;
		int budget = 0;
		bool complete = true;
		auto score = detail::Search(state, 0, 4, lo, hi, budget, complete);
		Check(complete && score == 11 * detail::kPointUnit + 4 && budget == 0,
			"blocked win scores the opponent's remaining pips as net points, even with no budget", "expected +11 points (the [5|6] left in the opponent's hand) plus the remaining-depth tiebreak");
		std::swap(state.hands[0][0], state.hands[1][0]);
		score = detail::Search(state, 0, 4, lo, hi, budget, complete);
		Check(complete && score == -(11 * detail::kPointUnit + 4),
			"blocked loss scores my own remaining pips as conceded points", "expected -11 points minus the remaining-depth tiebreak");
		state.hands[0][0] = Tile{1,4};
		state.hands[1][0] = Tile{2,3};
		score = detail::Search(state, 0, 4, lo, hi, budget, complete);
		Check(score == 0, "equal blocked pip totals remain neutral", "tie was classified as a win or loss");
		state.passStreak = 0;
		state.handCounts[0] = 0;
		auto earlyWin = detail::Search(state, 0, 5, lo, hi, budget, complete);
		auto lateWin = detail::Search(state, 0, 1, lo, hi, budget, complete);
		Check(complete && earlyWin > lateWin && lateWin > 0,
			"prefer earlier wins and recognize empty hands without budget", "remaining-depth preference is reversed");
		state.handCounts[0] = 1;
		state.handCounts[1] = 0;
		auto earlyLoss = detail::Search(state, 0, 5, lo, hi, budget, complete);
		auto lateLoss = detail::Search(state, 0, 1, lo, hi, budget, complete);
		Check(earlyLoss < lateLoss && lateLoss < 0,
			"prefer delaying a forced loss", "remaining-depth loss preference is reversed");

		// Three seats blocked with TWO opponents tied for lowest: func_169
		// declares no winner (num3 = -1), so nobody scores -- a tie for
		// me, not the loss "best opponent beats me" would have implied.
		GameState three = TwoSeatState({ Tile{6,6} }, { Tile{0,1} }, { 3 });
		three.occupied[2] = true;
		three.hands[2][0] = Tile{0,1};
		three.handCounts[2] = 1;
		three.passStreak = 3;
		score = detail::Search(three, 0, 4, lo, hi, budget, complete);
		Check(score == 0, "a tie for lowest total among opponents means no winner and no points", "the script pays nobody on a tied block");

		// All Fives rounds every seat's total to the nearest multiple of
		// five before comparing and paying (func_357): [1|1] -> 0 beats
		// [3|4] -> 5 and pays 5, and [2|2] -> 5 ties [3|3] -> 5.
		GameState fives = TwoSeatState({ Tile{1,1} }, { Tile{3,4} }, { 6 });
		fives.rules = DominoAiPolicy::Rules::AllFives;
		fives.passStreak = 2;
		score = detail::Search(fives, 0, 4, lo, hi, budget, complete);
		Check(score == 5 * detail::kPointUnit + 4, "All Fives pays the opponent's total rounded to the nearest five", "expected +5 ([3|4] = 7 rounds to 5)");
		fives.hands[0][0] = Tile{2,2};
		fives.hands[1][0] = Tile{3,3};
		score = detail::Search(fives, 0, 4, lo, hi, budget, complete);
		Check(score == 0, "All Fives rounding can turn a 4-vs-6 block into a scoreless tie", "expected both totals to round to 5");
		Check(DominoAiPolicy::RoundedPipTotal(DominoAiPolicy::Rules::AllThrees, 7) == 6 &&
			DominoAiPolicy::RoundedPipTotal(DominoAiPolicy::Rules::AllThrees, 8) == 9 &&
			DominoAiPolicy::RoundedPipTotal(DominoAiPolicy::Rules::Block, 8) == 8,
			"All Threes rounds to the nearest multiple of three; Block keeps raw pips", "func_357 rounding mismatch");
	}

	// The whole point of the net-points evaluation: when every line is
	// a loss, prefer the smaller loss; when every line wins, prefer the
	// bigger payout. Hand-built from the game's own payout rule.
	void TestSearchMaximizesNetPoints()
	{
		// 2 seats, Block rules (opponent always plays its highest-pip
		// legal tile). Open end 4. Me: [4|6] (10 pips) or [4|0] (4).
		// Opponent: [6|6] and [0|3].
		//  - Play [4|6]: end 6, opponent plays [6|6] (end 6), I pass, the
		//    opponent passes ([0|3] doesn't fit) -> blocked: me 4, opp 3
		//    -> opponent wins 4 points.
		//  - Play [4|0]: end 0, opponent plays [0|3] (end 3), I pass,
		//    opponent passes -> blocked: me 10, opp 12 -> I win 12 points.
		GameState state = TwoSeatState({ Tile{4,6}, Tile{0,4} }, { Tile{6,6}, Tile{0,3} }, { 4 });
		state.rules = DominoAiPolicy::Rules::Block;
		auto rec = DominoSearch::FindBestMove(state, 0, 20);
		Check(rec.valid && rec.exact && rec.tile.low == 0 && rec.tile.high == 4 && rec.points == 12 &&
			rec.outcome == DominoSearch::Outcome::RoundWin,
			"search picks the line that wins 12 over the one that concedes 4", "expected [0|4] with +12 exact");

		// Both lines lose against opponent [2|6], [0|1]:
		//  - [4|6] -> [2|6] (end 2) -> pass -> pass: me 4, opp 1 -> -4.
		//  - [0|4] -> [0|1] (end 1) -> pass -> pass: me 10, opp 8 -> -10.
		GameState losing = TwoSeatState({ Tile{0,4}, Tile{4,6} }, { Tile{2,6}, Tile{0,1} }, { 4 });
		losing.rules = DominoAiPolicy::Rules::Block;
		rec = DominoSearch::FindBestMove(losing, 0, 20);
		Check(rec.valid && rec.exact && rec.tile.low == 4 && rec.tile.high == 6 && rec.points == -4 &&
			rec.outcome == DominoSearch::Outcome::RoundLoss,
			"search concedes 4 rather than 10 when every line loses", "expected [4|6] with -4 exact");
		Check(rec.completedDepth < 20, "a solved round stops deepening early", "exact search should not keep iterating to the depth cap");
	}

	// Draw/All Fives/All Threes tables: a seat with no legal move takes
	// boneyard tiles in order until it can play (func_166). The draw
	// order is fully known, so the search follows it exactly.
	void TestSearchModelsBoneyardDraws()
	{
		// Me: [0|2], [1|1]. Opponent: [2|2], [0|0]. Open end 0.
		// Boneyard (draw order): [1|2], [3|3].
		// Block: [0|2] -> opp [2|2] -> I pass -> opp passes -> me 2 vs
		//   opp 0 -> concede 2.
		// Draw: [0|2] -> opp [2|2] (end 2) -> I can't play, draw [1|2],
		//   play it (end 1) -> opp can't play, draws [3|3], still can't,
		//   boneyard empty -> pass -> I play [1|1] and domino out: the
		//   opponent holds [0|0]+[3|3] = 6 -> +6.
		GameState state = TwoSeatState({ Tile{0,2}, Tile{1,1} }, { Tile{2,2}, Tile{0,0} }, { 0 });
		state.boneyard[0] = Tile{1,2};
		state.boneyard[1] = Tile{3,3};
		state.boneyardCount = 2;
		state.rules = DominoAiPolicy::Rules::Block;
		auto block = DominoSearch::FindBestMove(state, 0, 30);
		Check(block.valid && block.exact && block.points == -2, "Block rules never draw: leftover boneyard tiles stay out of play", "expected -2 (blocked with [1|1] in hand)");
		state.rules = DominoAiPolicy::Rules::Draw;
		auto draw = DominoSearch::FindBestMove(state, 0, 30);
		Check(draw.valid && draw.exact && draw.points == 6 && draw.outcome == DominoSearch::Outcome::RoundWin,
			"Draw rules follow the known boneyard order for every seat", "expected +6 (opponent forced to draw the dead [3|3])");

		GameState reordered = state;
		std::swap(reordered.boneyard[0], reordered.boneyard[1]);
		Check(!(state == reordered), "boneyard order is part of the decision snapshot", "a reshuffled boneyard must not match the old key");
	}

	void TestSearchOpeningMove()
	{
		GameState state = TwoSeatState({ Tile{6,6}, Tile{0,1}, Tile{2,5} }, { Tile{1,1}, Tile{5,5} }, {});
		auto moves = DominoSearch::detail::LegalMoves(state, 0);
		Check(moves.count == 3 && moves.moves[0].endPip == -1, "with no open ends every hand tile is an opening move", "expected 3 opening candidates");
		auto next = DominoSearch::detail::ApplyMove(state, 0, moves.moves[2]);
		Check(next.ends.count == 2 && next.ends.pips[0] == 2 && next.ends.pips[1] == 5,
			"an opening tile opens both of its pips", "expected ends {2,5}");
		next = DominoSearch::detail::ApplyMove(state, 0, moves.moves[0]);
		Check(next.ends.count == 1 && next.ends.pips[0] == 6, "an opening double opens one distinct pip", "expected ends {6}");
		auto rec = DominoSearch::FindBestMove(state, 0, 20);
		Check(rec.valid && rec.endPip == -1, "the opening move is searched rather than left to the fallback", "expected a valid opening recommendation");
	}

	// Game-target awareness: an opponent at 55/60 winning even a small
	// round ends the game, so the search must prefer conceding more
	// points to a different opponent. Seats 0 (me), 1 (A, 55 points),
	// 2 (B). Turn order 0 -> 2 -> 1. Open end 4, Block rules.
	//  - [4|6]: B passes, A plays [6|6] and dominoes: A gets my [4|0]
	//    (4) + B's [0|3] (3) = 7 -> 62 >= 60, GAME LOSS.
	//  - [4|0]: B plays [0|3] and dominoes: B gets 10 + 12 = 22 -> a
	//    round loss of 22, B at 22.
	void TestSearchAvoidsHandingOpponentTheGame()
	{
		GameState state = TwoSeatState({ Tile{4,6}, Tile{0,4} }, { Tile{6,6} }, { 4 });
		state.occupied[2] = true;
		state.hands[2][0] = Tile{0,3};
		state.handCounts[2] = 1;
		state.rules = DominoAiPolicy::Rules::Block;

		auto roundOnly = DominoSearch::FindBestMove(state, 0, 20);
		Check(roundOnly.valid && roundOnly.exact && roundOnly.tile.high == 6 && roundOnly.points == -7,
			"without a target the smaller round loss (-7) is preferred", "expected [4|6] conceding 7");

		state.pointsTarget = 60;
		state.scores = { 0, 55, 0 };
		auto aware = DominoSearch::FindBestMove(state, 0, 20);
		Check(aware.valid && aware.exact && aware.tile.high == 4 && aware.tile.low == 0 && aware.points == -22 &&
			aware.outcome == DominoSearch::Outcome::RoundLoss,
			"with A at 55/60 the search concedes 22 to B rather than 7 to A", "expected [0|4]: the -7 line is a game loss");

		// Root scoring bonus (All Fives end-total credit read at the root):
		// if playing [4|6] itself scores me 5 and I sit at 55/60, the game
		// ends in my favour before A ever moves.
		state.scores = { 55, 55, 0 };
		state.rootMoveBonusPoints[0].fill(5);
		auto bonus = DominoSearch::FindBestMove(state, 0, 20);
		Check(bonus.valid && bonus.exact && bonus.tile.high == 6 && bonus.outcome == DominoSearch::Outcome::GameWin && bonus.points == 5,
			"a root scoring bonus that reaches the target is an immediate game win", "expected [4|6] as GAME WIN +5");
		state.scores = { 0, 0, 0 };
		state.pointsTarget = 0;
		state.rootMoveBonusPoints[0].fill(5);
		bonus = DominoSearch::FindBestMove(state, 0, 20);
		Check(bonus.valid && bonus.tile.high == 6 && bonus.points == -2,
			"a root scoring bonus nets against the round result", "expected +5 bonus - 7 conceded = -2");
	}

	// Exact board rules, replayed against a real recorded 3-seat All Fives
	// round (2026-09-25, DominoCheat_games.jsonl 14:37-14:39): every
	// placement's end total must equal what the game recounted (the score
	// changes and the native candidate totals of that round), and a play
	// onto an unplayed spinner side must reproduce the native's quirk.
	// Targets are named by what they cover: a regular end (pip, double?),
	// the spinner's long side, or an unplayed spinner side.
	std::int8_t FindTarget(const DominoSearch::Board& board, char kind, int pip, bool dbl)
	{
		if (kind == 'L')
			return DominoSearch::kTargetSpinnerLong;
		if (kind == 'S')
			return DominoSearch::kTargetEmptySide;
		if (kind == 'O')
			return DominoSearch::kTargetOpening;
		for (int e = 0; e < board.count; e++)
			if (board.ends[e].pip == pip && board.ends[e].dbl == dbl)
				return static_cast<std::int8_t>(e);
		return -2;
	}

	void TestBoardRulesReplay()
	{
		struct Play { Tile tile; char kind; int endPip; bool endDbl; int total; int native; };
		// kind: O opening, E regular end (endPip/endDbl), L spinner long side, S unplayed spinner side.
		const Play plays[] = {
			{ {5,5}, 'O', -1, false, 10, 10 }, // seat 0 opens: spinner 5
			{ {0,5}, 'L',  5, false, 10,  0 }, // me: scored 10; first long side of a double opener: native 10 - 10
			{ {0,0}, 'E',  0, false, 10, 10 }, // seat 1: scored 10
			{ {3,5}, 'L',  5, false,  3,  3 }, // spinner long sides both covered: sides open
			{ {0,2}, 'E',  0, true,   5,  5 }, // me: scored 5
			{ {4,5}, 'S',  5, false,  9, -1 }, // side play: native reports 9 - 10
			{ {3,3}, 'E',  3, false, 12, 12 },
			{ {3,6}, 'E',  3, true,  12, 12 },
			{ {4,6}, 'E',  6, false, 10, 10 }, // seat 1: scored 10
			{ {2,2}, 'E',  2, false, 12, 12 },
			{ {4,4}, 'E',  4, false, 16, 16 },
			{ {2,3}, 'E',  2, true,  15, 15 }, // seat 1: scored 15
			{ {1,3}, 'E',  3, false, 13, 13 },
			{ {1,4}, 'E',  4, false, 10, 10 }, // me: scored 10 (on the [4|4] end it would be 6)
			{ {1,6}, 'E',  1, false, 15, 15 }, // seat 1: scored 15
			{ {6,6}, 'E',  6, false, 21, 21 },
			{ {0,1}, 'E',  1, false, 20, 20 }, // me: scored 20
			{ {1,5}, 'S',  5, false, 21, 11 }, // side play: native 21 - 10
			{ {0,6}, 'E',  0, false, 27, 27 },
		};

		DominoSearch::Board board;
		bool ok = true;
		std::string detail;
		for (std::size_t i = 0; i < sizeof(plays) / sizeof(plays[0]) && ok; i++)
		{
			const Play& play = plays[i];
			std::int8_t target = FindTarget(board, play.kind, play.endPip, play.endDbl);
			int pip = DominoSearch::TargetPip(board, target);
			if (target == -2 || (play.kind != 'O' && pip != play.endPip))
			{
				ok = false;
				detail = "play " + std::to_string(i) + ": target not on the board";
				break;
			}
			DominoSearch::Board after = DominoSearch::PlaceOnBoard(board, play.tile, target, play.endPip);
			int total = DominoSearch::BoardTotal(after);
			int native = DominoSearch::NativeEndTotal(board, after, target);
			if (total != play.total || native != play.native)
			{
				ok = false;
				detail = "play " + std::to_string(i) + ": total " + std::to_string(total) + " native " + std::to_string(native) +
					", game had " + std::to_string(play.total) + " / " + std::to_string(play.native);
			}
			board = after;
		}
		Check(ok, "exact board reproduces every end total of a recorded All Fives round", detail.c_str());

		// The same data's other candidates: from the board after play 13
		// ([1|3]), [1|4] on the double-4 end would total 6 (native list
		// showed 6 and 10 for the two 4-spots).
		DominoSearch::Board b;
		for (std::size_t i = 0; i < 13; i++)
			b = DominoSearch::PlaceOnBoard(b, plays[i].tile, FindTarget(b, plays[i].kind, plays[i].endPip, plays[i].endDbl), plays[i].endPip);
		DominoSearch::Board onDouble = DominoSearch::PlaceOnBoard(b, Tile{1,4}, FindTarget(b, 'E', 4, true), 4);
		Check(DominoSearch::BoardTotal(onDouble) == 6, "a double at an end counts twice until covered", "expected [1|4] on the [4|4] end to total 6");
	}

	// BoardTracker's source: the game's own placement log, read from a
	// live stack dump (2026-09-25 14:56, 3-seat All Fives, 11 tiles down;
	// SeatsHolder.f_6 records [low, high, grid x, grid y]). The replay must
	// land on the game's own recounted total (word 0 = 11) and open ends
	// {0, 1, 5, 6} -- including the [5|5] end the synthetic-double probe
	// missed that turn, and [6|6] becoming the spinner mid-round.
	void TestBoardReplayFromPlacementLog()
	{
		const int log[][4] = {
			{ 2, 3, -2, 1 }, { 2, 6, 2, 1 }, { 0, 3, -6, 1 }, { 6, 6, 6, 2 }, { 1, 6, 8, 1 }, { 0, 5, -10, 1 },
			{ 1, 4, 12, 1 }, { 4, 5, 16, 1 }, { 5, 5, 17, 3 }, { 1, 5, -14, 1 }, { 0, 6, 6, -2 },
		};
		DominoSearch::BoardReplay replay;
		for (const auto& r : log)
		{
			DominoSearch::PlacedRecord placed;
			placed.tile = Tile{ r[0], r[1] };
			placed.gx = r[2];
			placed.gy = r[3];
			replay.Apply(placed);
		}
		const DominoSearch::Board& b = replay.board;
		bool open[7] = {};
		for (int e = 0; e < b.count; e++)
			open[b.ends[e].pip] = true;
		if (b.spinnerLong > 0 || b.emptySides > 0)
			open[b.spinnerPip] = true;
		Check(replay.ok && replay.applied == 11 && DominoSearch::BoardTotal(b) == 11,
			"placement-log replay reaches the game's own end total", "expected total 11 after 11 tiles");
		Check(open[0] && open[1] && open[5] && open[6] && !open[2] && !open[3] && !open[4],
			"placement-log replay finds every open end, including the [5|5] the probe missed", "expected open ends {0,1,5,6}");
		Check(b.spinnerPip == 6 && b.emptySides == 1, "the first double becomes the spinner even mid-round", "expected spinner 6 with one unplayed side");
	}

	// Exact board in the search: every placement scores for whoever
	// makes it, and a scoring-table opponent plays the scripted AI's
	// choice (its highest scoring native total) instead of paranoid play.
	void TestExactBoardSearch()
	{
		// Board after [5|5] opened and [0|5] went on one long side: spinner
		// 5 with one long side open, a plain 0 end. Total 10.
		GameState state;
		state.rules = DominoAiPolicy::Rules::AllFives;
		state.occupied = { true, true, false, false };
		state.exactBoard = true;
		state.board.placed = true;
		state.board.spinnerPip = 5;
		state.board.spinnerLong = 1;
		state.board.ends[0] = { 0, false };
		state.board.count = 1;
		state.ends.pips[0] = 0;
		state.ends.pips[1] = 5;
		state.ends.count = 2;
		state.turnSeat = 0;

		// Me: [0|5] on the 0 end -> new 5 end + spinner 10 = 15, scores 15.
		// On the spinner's long side -> two 0 ends, sides open: total 0.
		state.hands[0][0] = Tile{0,5};
		state.hands[0][1] = Tile{2,3};
		state.handCounts[0] = 2;
		state.hands[1][0] = Tile{6,6};
		state.hands[1][1] = Tile{1,2};
		state.handCounts[1] = 2;

		DominoSearch::MoveList moves = DominoSearch::detail::LegalMoves(state, 0);
		Check(moves.count == 2, "exact board: a tile fitting a regular end and the spinner is two moves", "expected [0|5] on 0 and on the spinner");

		GameState scored = DominoSearch::detail::ApplyMove(state, 0, moves.moves[0].target == DominoSearch::kTargetSpinnerLong ? moves.moves[1] : moves.moves[0]);
		Check(scored.roundGain[0] == 15, "exact board: a placement that makes a multiple of five scores it", "expected +15 for [0|5] on the 0 end");

		// Opponent choice: plain 5 and 0 ends. [4|5] is the higher-pip tile
		// (the Block/Draw choice), but [0|5] on the 0 end totals 10 and
		// scores, so the All Fives AI plays that.
		GameState npc;
		npc.rules = DominoAiPolicy::Rules::AllFives;
		npc.occupied = { true, true, false, false };
		npc.exactBoard = true;
		npc.board.placed = true;
		npc.board.ends[0] = { 5, false };
		npc.board.ends[1] = { 0, false };
		npc.board.count = 2;
		npc.turnSeat = 1;
		npc.hands[1][0] = Tile{4,5}; // on 5 -> 4 + 0 = 4, 9 pips
		npc.hands[1][1] = Tile{0,5}; // on 0 -> 5 + 5 = 10 (scores), on 5 -> 0 + 0 = 0
		npc.handCounts[1] = 2;
		npc.hands[0][0] = Tile{6,6};
		npc.handCounts[0] = 1;
		DominoSearch::MoveList replies = DominoSearch::detail::OpponentMoves(npc, 1);
		bool onlyScoring = replies.count == 1 && replies.moves[0].tile == (Tile{0,5}) && replies.moves[0].endPip == 0;
		Check(onlyScoring, "exact board: an All Fives opponent plays its scoring placement, not any legal reply",
			"expected only [0|5] on the 0 end (total 10)");

		npc.exactBoard = false;
		npc.ends.pips[0] = 5;
		npc.ends.pips[1] = 0;
		npc.ends.count = 2;
		replies = DominoSearch::detail::OpponentMoves(npc, 1);
		Check(replies.count > 1, "without the exact board an All Fives opponent is still searched paranoidly", "expected every legal reply");
	}

	// The root scoring bonus is per END: [2|5] fits both open ends, only
	// the end-2 placement scores. Everything else is symmetric (nobody can
	// follow, the round blocks with equal hands either way), so the
	// search must advise end 2 and credit exactly those 10 points.
	void TestSearchRootBonusIsPerEnd()
	{
		GameState state = TwoSeatState({ Tile{2,5}, Tile{0,0} }, { Tile{6,6} }, { 2, 5 });
		state.rules = DominoAiPolicy::Rules::AllFives;
		state.rootMoveBonusPoints[0][2] = 10;
		auto rec = DominoSearch::FindBestMove(state, 0, 20);
		Check(rec.valid && rec.tile == (Tile{2,5}) && rec.endPip == 2,
			"root scoring bonus goes to the end that scores", "expected [2|5] on end 2");

		state.rootMoveBonusPoints[0][2] = 0;
		state.rootMoveBonusPoints[0][5] = 10;
		rec = DominoSearch::FindBestMove(state, 0, 20);
		Check(rec.valid && rec.tile == (Tile{2,5}) && rec.endPip == 5,
			"root scoring bonus on the other end moves the advice with it", "expected [2|5] on end 5");
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

	// Exhaustive NET-POINTS reference: no pruning, cutoff, budget or
	// production terminal scorer. Shares only move generation/transitions.
	// Independently codes the script's own round payout (func_169 /
	// func_343 / func_357): the seat that empties its hand, else the
	// unique lowest rounded total on a block, is paid every other seat's
	// rounded total; a tie for lowest pays nobody. Draws follow func_166.
	int ReferenceRounded(const GameState& state, int seat)
	{
		return DominoAiPolicy::RoundedPipTotal(state.rules, HandPipTotal(state.hands[seat].data(), state.handCounts[seat]));
	}

	int ReferenceNetPoints(const GameState& state, int mySeat)
	{
		int occupied = 0;
		int winner = -1;
		for (int seat = 0; seat < DominoSearch::kMaxSeats; seat++)
		{
			if (!state.occupied[seat])
				continue;
			occupied++;
			if (state.handCounts[seat] == 0)
				winner = seat;
		}
		bool over = winner >= 0;
		if (!over && state.passStreak >= occupied)
		{
			over = true;
			int lowest = 1000;
			for (int seat = 0; seat < DominoSearch::kMaxSeats; seat++)
			{
				if (!state.occupied[seat])
					continue;
				int total = ReferenceRounded(state, seat);
				if (total == lowest)
					winner = -1;
				else if (total < lowest)
				{
					lowest = total;
					winner = seat;
				}
			}
		}
		if (over)
		{
			if (winner < 0)
				return 0;
			int payout = 0;
			for (int seat = 0; seat < DominoSearch::kMaxSeats; seat++)
				if (state.occupied[seat] && seat != winner)
					payout += ReferenceRounded(state, seat);
			return winner == mySeat ? payout : -payout;
		}

		GameState position = state;
		auto moves = DominoSearch::detail::LegalMoves(position, position.turnSeat);
		if (moves.Empty() && position.rules != DominoAiPolicy::Rules::Block)
		{
			while (position.boneyardNext < position.boneyardCount && position.handCounts[position.turnSeat] < 19)
			{
				position.hands[position.turnSeat][position.handCounts[position.turnSeat]++] = position.boneyard[position.boneyardNext++];
				moves = DominoSearch::detail::LegalMoves(position, position.turnSeat);
				if (!moves.Empty())
					break;
			}
		}
		if (moves.Empty())
			return ReferenceNetPoints(DominoSearch::detail::ApplyPass(position, position.turnSeat), mySeat);
		bool maximizing = position.turnSeat == mySeat;
		int requiredPips = -1;
		if (!maximizing && moves.count <= 15 &&
			(position.rules == DominoAiPolicy::Rules::Block || position.rules == DominoAiPolicy::Rules::Draw))
			for (const auto& move : moves)
				requiredPips = std::max(requiredPips, move.tile.low + move.tile.high);
		int best = maximizing ? -1000 : 1000;
		for (const auto& move : moves)
		{
			if (requiredPips >= 0 && move.tile.low + move.tile.high != requiredPips)
				continue;
			int outcome = ReferenceNetPoints(DominoSearch::detail::ApplyMove(position, position.turnSeat, move), mySeat);
			best = maximizing ? std::max(best, outcome) : std::min(best, outcome);
		}
		return best;
	}

	int ReferenceOutcome(const GameState& state, int mySeat)
	{
		int points = ReferenceNetPoints(state, mySeat);
		return (points > 0) - (points < 0);
	}

	// What the OLD binary evaluation effectively recommended: among the
	// moves with the best outcome sign, the highest-pip tile (its
	// tiebreak). Returns that move's exact net points.
	int OldStyleChoicePoints(const GameState& state, int mySeat)
	{
		int bestSign = -2;
		int bestPips = -1;
		int chosenPoints = 0;
		for (const auto& move : DominoSearch::detail::LegalMoves(state, mySeat))
		{
			int points = ReferenceNetPoints(DominoSearch::detail::ApplyMove(state, mySeat, move), mySeat);
			int sign = (points > 0) - (points < 0);
			int pips = move.tile.PipTotal();
			if (sign > bestSign || (sign == bestSign && pips > bestPips))
			{
				bestSign = sign;
				bestPips = pips;
				chosenPoints = points;
			}
		}
		return chosenPoints;
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
					int actual = ReferenceNetPoints(DominoSearch::detail::ApplyMove(state, mySeat, move), mySeat);
					int optimal = ReferenceNetPoints(state, mySeat);
					correct = correct && actual == optimal && rec.exact && rec.points == optimal;
				}
			correct = correct && legal;
		}
		std::printf("  Exhaustive comparison: %d legal decisions across 240 seeded 2/3/4-seat positions\n", decisions);
		Check(correct && decisions > 100, "bounded search matches exhaustive small-endgame net points exactly",
			"an illegal or points-inferior move was recommended, or the reported points/exactness disagree with the oracle");
	}

	// Same oracle, larger random endgames WITH a boneyard under every
	// rule set (so draws and All Fives/All Threes rounding both get
	// exercised), and a count of how often net points beat the old
	// binary evaluation's effective choice.
	void TestSearchNetPointsAgainstExhaustiveDrawEndgames()
	{
		std::mt19937 random(9137);
		std::array<int, kTileSetSize> deck{};
		for (int i = 0; i < kTileSetSize; i++)
			deck[i] = i;
		bool correct = true;
		int decisions = 0;
		int improved = 0;
		int drawsUsed = 0;
		const DominoAiPolicy::Rules ruleSets[] = { DominoAiPolicy::Rules::Block, DominoAiPolicy::Rules::Draw,
			DominoAiPolicy::Rules::AllFives, DominoAiPolicy::Rules::AllThrees };
		for (int sample = 0; sample < 320; sample++)
		{
			std::shuffle(deck.begin(), deck.end(), random);
			GameState state;
			int seats = 2 + sample % 3;
			int mySeat = (sample / 3) % seats;
			int tilesPerSeat = seats == 2 ? 3 : 2;
			int cursor = 0;
			for (int seat = 0; seat < seats; seat++)
			{
				state.occupied[seat] = true;
				state.handCounts[seat] = tilesPerSeat;
				for (int i = 0; i < tilesPerSeat; i++)
					state.hands[seat][i] = DecodeTile(deck[cursor++]);
			}
			Tile board = DecodeTile(deck[cursor++]);
			state.ends.count = 2;
			state.ends.pips[0] = board.low;
			state.ends.pips[1] = board.high;
			int boneyard = 1 + sample % 3;
			for (int i = 0; i < boneyard; i++)
				state.boneyard[i] = DecodeTile(deck[cursor++]);
			state.boneyardCount = boneyard;
			state.rules = ruleSets[(sample / 4) % 4];
			state.turnSeat = mySeat;
			if (DominoSearch::detail::LegalMoves(state, mySeat).Empty())
				continue;
			decisions++;
			auto rec = FindBestMove(state, mySeat, 60);
			int optimal = ReferenceNetPoints(state, mySeat);
			int actual = -1000;
			for (const auto& move : DominoSearch::detail::LegalMoves(state, mySeat))
				if (rec.valid && move.handIndex == rec.handIndex && move.endPip == rec.endPip)
					actual = ReferenceNetPoints(DominoSearch::detail::ApplyMove(state, mySeat, move), mySeat);
			correct = correct && rec.valid && rec.exact && actual == optimal && rec.points == optimal;
			if (optimal > OldStyleChoicePoints(state, mySeat))
				improved++;
			if (state.rules != DominoAiPolicy::Rules::Block)
				drawsUsed++;
		}
		std::printf("  Boneyard/rounding comparison: %d decisions, %d strictly more points than the old binary choice\n", decisions, improved);
		Check(correct && decisions > 200, "search matches exhaustive net points with boneyard draws and scoring-mode rounding",
			"points-inferior recommendation, or reported points/exactness disagree with the oracle");
		Check(improved > 0 && drawsUsed > 0, "net-points evaluation strictly improves on the old outcome-only choice in seeded endgames",
			"expected at least one position where the old highest-pip tiebreak leaves points on the table");
	}

	// GameRecord.h: a decision line's state and an npcMove line's
	// candidate list must read back exactly as written, or a recorded
	// position replays as a different position.
	void TestGameRecordRoundTrip()
	{
		DominoSearch::GameState state;
		state.rules = DominoAiPolicy::Rules::AllFives;
		state.occupied = { true, false, true, true };
		state.hands[0][0] = { 1, 4 };
		state.hands[0][1] = { 6, 6 };
		state.handCounts[0] = 2;
		state.hands[2][0] = { 0, 0 };
		state.handCounts[2] = 1;
		state.hands[3][0] = { 2, 5 };
		state.hands[3][1] = { 3, 3 };
		state.hands[3][2] = { 0, 6 };
		state.handCounts[3] = 3;
		state.ends.pips[0] = 4;
		state.ends.pips[1] = 6;
		state.ends.count = 2;
		state.boneyard[0] = { 1, 1 };
		state.boneyard[1] = { 2, 3 };
		state.boneyardCount = 2;
		state.scores = { 40, 0, 15, 85 };
		state.pointsTarget = 100;
		state.rootMoveBonusPoints[1][6] = 10;
		state.exactBoard = true;
		state.board.placed = true;
		state.board.ends[0] = { 4, false };
		state.board.ends[1] = { 6, true };
		state.board.count = 2;
		state.board.spinnerPip = 5;
		state.board.emptySides = 1;
		state.turnSeat = 0;

		GameRecord::JsonLine line;
		line.Add("type", "decision");
		GameRecord::WriteState(line, state, 0);
		DominoSearch::GameState back;
		int mySeat = -1;
		Check(GameRecord::ReadState(line.Str(), back, mySeat) && mySeat == 0 && back == state,
			"game record: decision state round-trips", line.Str().c_str());

		DominoAiPolicy::Candidate candidates[] = { { 1, true, 10, 3, -2, 0 }, { 0, false, 0, 0, 0, 0 }, { 2, true, 5, -1, 4, 2 } };
		GameRecord::JsonLine npc;
		GameRecord::WriteCandidates(npc, candidates, 3);
		std::vector<DominoAiPolicy::Candidate> read;
		bool same = GameRecord::ReadCandidates(npc.Str(), read) && read.size() == 3;
		for (std::size_t i = 0; same && i < read.size(); i++)
			same = read[i].handIndex == candidates[i].handIndex && read[i].hasPlacement == candidates[i].hasPlacement &&
				read[i].resultingEndTotal == candidates[i].resultingEndTotal && read[i].gridF1 == candidates[i].gridF1 &&
				read[i].gridF2 == candidates[i].gridF2 && read[i].gridOrientation == candidates[i].gridOrientation;
		Check(same, "game record: candidate list round-trips in native order", npc.Str().c_str());
	}

	// Replays tests/fixtures/games.jsonl -- lines copied out of the mod's
	// DominoCheat_games.jsonl (Debug build) with an expect* key added.
	// decision + "expectTile" (and optional "expectEnd"): FindBestMove()
	// on the recorded position must recommend it. npcMove + "expectTile":
	// SelectCandidate() on the recorded native candidate list must pick
	// it. Lines without an expect* key are skipped. See GameRecord.h.
	void TestRecordedGames()
	{
		std::ifstream file;
		for (const std::filesystem::path& candidate : {
			std::filesystem::path(__FILE__).parent_path() / "fixtures" / "games.jsonl",
			std::filesystem::path("tests") / "fixtures" / "games.jsonl",
			std::filesystem::path("fixtures") / "games.jsonl" })
		{
			file.open(candidate);
			if (file.is_open())
				break;
		}
		if (!file.is_open())
		{
			Check(false, "tests/fixtures/games.jsonl opens", "run from the DominoCheat directory");
			return;
		}

		int replayed = 0;
		std::string line;
		while (std::getline(file, line))
		{
			if (line.empty() || line[0] == '#')
				continue;

			Tile expect;
			if (!GameRecord::GetTile(line, "expectTile", expect))
				continue;

			std::string type, id, note;
			GameRecord::GetString(line, "type", type);
			GameRecord::GetString(line, "id", id);
			GameRecord::GetString(line, "note", note);
			std::string name = "recorded " + type + " " + id + (note.empty() ? "" : " (" + note + ")");

			if (type == "decision")
			{
				DominoSearch::GameState state;
				int mySeat = -1;
				if (!GameRecord::ReadState(line, state, mySeat))
				{
					Check(false, name.c_str(), "decision line does not parse");
					continue;
				}
				DominoSearch::Recommendation rec = DominoSearch::FindBestMove(state, mySeat, DominoSearch::detail::kMaxSearchDepth, 2'000'000);
				std::int32_t expectEnd = -1;
				bool endOk = !GameRecord::GetInt(line, "expectEnd", expectEnd) || rec.endPip == expectEnd;
				std::string detail = "search recommends [" + std::to_string(rec.tile.low) + "|" + std::to_string(rec.tile.high) +
					"] on end " + std::to_string(rec.endPip) + ", expected [" + std::to_string(expect.low) + "|" + std::to_string(expect.high) + "]";
				Check(rec.valid && rec.tile == expect && endOk, name.c_str(), detail.c_str());
				replayed++;
			}
			else if (type == "npcMove")
			{
				std::string rules;
				std::vector<Tile> hand;
				std::vector<DominoAiPolicy::Candidate> candidates;
				if (!GameRecord::GetString(line, "rules", rules) || !GameRecord::GetTiles(line, "hand", hand) ||
					!GameRecord::ReadCandidates(line, candidates))
				{
					Check(false, name.c_str(), "npcMove line does not parse");
					continue;
				}
				int chosen = DominoAiPolicy::SelectCandidate(GameRecord::RulesFromName(rules), hand.data(), static_cast<int>(hand.size()),
					candidates.data(), static_cast<int>(candidates.size()));
				bool ok = chosen >= 0 && hand[static_cast<std::size_t>(candidates[static_cast<std::size_t>(chosen)].handIndex)] == expect;
				Check(ok, name.c_str(), "scripted-AI model picks a different tile than the NPC played");
				replayed++;
			}
		}
		Check(replayed > 0, "tests/fixtures/games.jsonl has replayable lines", "no line with an expect* key");
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
		Check(Config::Values{}.AdvisorWallClockBudgetMs == 1000,
			"wall-clock budget defaults to 1000 ms", "default budget changed");
		Check(Config::ClampWallClockBudgetMs(1000) == 1000 &&
			Config::ClampWallClockBudgetMs(5) == 50 &&
			Config::ClampWallClockBudgetMs(1000000) == 30000,
			"raw INI budget is clamped to a sane [50, 30000] ms range", "clamp bounds mismatch");
		Check(Config::ClampWallClockBudgetMs(0) == Config::kUnlimitedWallClockBudgetMs &&
			Config::ClampWallClockBudgetMs(-1) == Config::kUnlimitedWallClockBudgetMs,
			"zero/negative INI budget means unlimited", "unlimited budget clamped to a finite value");
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

	void TestWorkerUnlimitedRuntime()
	{
		AsyncMoveAdvisor<int> advisor;
		GameState state = BusySearchState();
		// A zero budget must not act as an already-expired deadline (which
		// would finish instantly at completedDepth 0): the worker keeps
		// deepening until solved or cancelled.
		advisor.SubmitJob(1, state, 0, 100, std::chrono::milliseconds(Config::kUnlimitedWallClockBudgetMs));
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		auto latest = advisor.GetLatest();
		Check(latest.valid && latest.key == 1 && latest.rec.completedDepth > 0,
			"unlimited budget keeps deepening instead of expiring immediately", "zero budget treated as expired deadline");
		GameState next = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });
		advisor.SubmitJob(2, next, 0, 4, std::chrono::milliseconds(Config::kUnlimitedWallClockBudgetMs));
		Check(WaitForFinished(advisor, 2, std::chrono::seconds(1)) && advisor.GetLatest().rec.isWinningMove,
			"new decision preempts an unlimited search", "unlimited search ignored cancellation");
		advisor.Cancel();
		Check(!advisor.GetLatest().valid, "cancel clears unlimited-search advice", "stale advice survived cancel");
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

	// Process EXIT (g_processTerminating, set by DllMain when lpReserved is
	// non-null): Windows has already killed the worker, possibly while it
	// held the job mutex, so the destructor must not lock or wait at all --
	// returning immediately even with a job in flight is the point. Must run
	// LAST: the worker here is deliberately never stopped, so
	// g_liveWorkerCount stays at 1 for the rest of the process.
	void TestAsyncAdvisorProcessExitReturnsImmediately()
	{
		using Advisor = AsyncMoveAdvisor<int>;
		static alignas(Advisor) std::byte storage[sizeof(Advisor)];
		Advisor* advisor = new (&storage) Advisor();

		Check(WaitForLiveWorkerCount(1, std::chrono::seconds(2)), "TestAsyncAdvisorProcessExitReturnsImmediately: worker starts", "g_liveWorkerCount never reached 1");

		advisor->Testing_SetArtificialJobDelay(std::chrono::milliseconds(800));
		GameState state = TwoSeatState({ Tile{5,5} }, { Tile{0,1}, Tile{2,3} }, { 5 });
		advisor->SubmitJob(1, state, 0, 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(20));

		AsyncMoveAdvisorDetail::g_processTerminating.store(true);
		auto start = std::chrono::steady_clock::now();
		advisor->~Advisor();
		auto elapsed = std::chrono::steady_clock::now() - start;
		AsyncMoveAdvisorDetail::g_processTerminating.store(false);

		Check(elapsed < std::chrono::milliseconds(50), "process-exit teardown returns immediately without locking or waiting", "a wait here can hang game exit if the killed worker held the job mutex");
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
	TestSearchMaximizesNetPoints();
	TestSearchModelsBoneyardDraws();
	TestSearchOpeningMove();
	TestSearchAvoidsHandingOpponentTheGame();
	TestSearchRootBonusIsPerEnd();
	TestBoardRulesReplay();
	TestBoardReplayFromPlacementLog();
	TestExactBoardSearch();
	TestSearchSnapshotAndTurnHandling();
	TestSearchAgainstExhaustiveEndgames();
	TestSearchNetPointsAgainstExhaustiveDrawEndgames();
	TestScriptedCandidateSelection();
	TestGameRecordRoundTrip();
	TestRecordedGames();
	TestScriptedSeatOrder();
	TestPolicyRestrictionsPreserveUncertainty();
	TestPolicyAwareEndgames();
	TestRuntimePresets();
	TestTimedSearchStopsAtCompletedDepth();
	TestWorkerDeduplicatesAndCancels();
	TestWorkerTimedRuntimeAndPreemption();
	TestWorkerUnlimitedRuntime();
	TestAsyncAdvisorPublishesMatchingResult();
	TestAsyncAdvisorJoinsSynchronouslyOnNormalTeardown();
	TestAsyncAdvisorProcessDetachWaitCompletesForQuickJob();
	TestAsyncAdvisorProcessDetachTimesOutThenDetachesForSlowJob();
	TestAsyncAdvisorProcessExitReturnsImmediately();

	if (g_failures == 0)
	{
		std::printf("ALL PASS\n");
		return 0;
	}

	std::printf("%d FAILURE(S)\n", g_failures);
	return 1;
}
