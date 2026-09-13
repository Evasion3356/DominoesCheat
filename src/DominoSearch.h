/*
	Depth-limited minimax over FULLY KNOWN hands -- pure-logic header, zero
	game-memory dependency, same "pure math header, unit tested in
	isolation" convention as DominoHandEval.h (see
	tests/DominoHandEvalTests.cpp). Replaces DominoCheat.cpp's original
	DetermineBestMove(), which only ever looked one ply ahead (minimize
	CountPipAcrossOpponents() against the SINGLE resulting open end, tie-
	broken by pip total) -- confirmed live to recommend legal moves but
	still lose more than expected, because a 1-ply "safest immediate
	reply" metric can hand an opponent a great position two turns later
	without ever seeing it coming.

	Why minimax at all, and why it's sound here specifically: with all 4
	seats dealt and the boneyard empty (this file's only scope -- see
	DominoCheat.cpp's DetermineBestMove() for the boneyard-non-empty
	fallback, and CountPipAcrossOpponents()'s own header comment for why
	that scoping already exists elsewhere in this project), every one of
	the 28 tiles is visible: your own hand, every opponent's hand, and
	the board. There is no hidden information left to model
	probabilistically -- the only real uncertainty is what an opponent
	actually chooses to play, which this file resolves with a PARANOID
	assumption (treat all three opponents as a single adversary who
	always picks whichever of THEIR OWN legal replies is worst for you).
	That makes the recommended move a worst-case-safe choice, not merely
	an average-case guess -- the appropriate default when you don't know
	(and don't control) the real opponent policy.

	What this file does NOT model:
	 - The real board only exposes a SET of open pip values (0-6) via
	   DetermineOpenEnds() -- not how many independent ends currently
	   hold each value, nor the physical spur layout the "up to 4 open-
	   end child nodes" IDA trace found (see DominoCheat.cpp's file
	   header comment, Session 4). OpenEnds below models the board as one
	   abstract "slot" per distinct open pip and playing a tile REPLACES
	   that slot's value -- exactly the same abstraction the OLD 1-ply
	   heuristic already assumed (one endPip -> one resultPip), just
	   extended recursively over more plies instead of a new invention.
	   A real board with two ends sharing the same pip value collapses to
	   one slot here, which could occasionally misjudge which exact end
	   closes -- accepted the same way this project accepts every other
	   documented approximation, since the alternative needs board-
	   topology data nobody has mapped.
	 - Boneyard draws: entirely out of scope (see the "opening move"/
	   pass-through gate in DominoCheat.cpp's DetermineBestMove()) --
	   modeling "a seat that can't play draws the next known boneyard
	   tile instead of passing" is a real, tractable future extension
	   (the boneyard's remaining draw order is itself fully deterministic
	   and readable, see this project's CLAUDE.md), just not implemented
	   this pass.
	 - Opponent scoring incentives beyond "beat me": the paranoid model
	   doesn't distinguish which specific opponent benefits, only whether
	   a resulting state is good or bad for mySeat -- fine under the
	   paranoid assumption (any opponent's gain is treated as fully
	   adversarial to you) but means the search never reasons about
	   opponents blocking EACH OTHER, which a real free-for-all sometimes
	   does for you for free. Overestimating coordination is the safe
	   direction to be wrong in.

	NOT yet live-tested (2026-09-13) -- built the same day as the 1-ply
	heuristic it replaces was confirmed "plays only legal moves, still
	loses more than expected" against real play.
*/

#pragma once

#include "DominoHandEval.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace DominoSearch
{
	using DominoHandEval::Tile;
	constexpr int kMaxSeats = DominoHandEval::kMaxSeats;

	// One abstract open "slot" per distinct playable pip value -- see
	// this file's header comment for why a SET (not an exact end-count/
	// topology) is the right fidelity level given what DetermineOpenEnds()
	// actually reports.
	struct OpenEnds
	{
		std::vector<std::int32_t> pips; // distinct 0-6 values currently playable

		void Replace(std::int32_t oldPip, std::int32_t newPip)
		{
			for (auto& p : pips)
			{
				if (p == oldPip)
				{
					p = newPip;
					return;
				}
			}
		}
	};

	struct GameState
	{
		std::array<std::vector<Tile>, kMaxSeats> hands;
		std::array<bool, kMaxSeats> occupied{};
		OpenEnds ends;
		int turnSeat = 0;
		int passStreak = 0; // consecutive passes across occupied seats -- == occupied count means the round is blocked
	};

	struct Move
	{
		std::size_t handIndex = 0;
		Tile tile;
		std::int32_t endPip = -1;
		std::int32_t resultPip = -1;
	};

	struct Recommendation
	{
		bool valid = false;
		std::size_t handIndex = 0;
		Tile tile;
		std::int32_t endPip = -1;
		std::int32_t resultPip = -1;
		bool isWinningMove = false;
		std::int32_t score = 0; // raw minimax value, for diagnostics only -- not a pip/tile count
	};

	namespace detail
	{
		constexpr std::int32_t kWinScore = 1'000'000;

		inline int PipTotal(const std::vector<Tile>& hand)
		{
			int total = 0;
			for (const auto& t : hand)
				total += t.PipTotal();
			return total;
		}

		inline int OccupiedCount(const GameState& state)
		{
			int n = 0;
			for (bool occ : state.occupied)
				if (occ)
					n++;
			return n;
		}

		inline int NextSeat(const GameState& state, int fromSeat)
		{
			for (int step = 1; step <= kMaxSeats; step++)
			{
				int candidate = (fromSeat + step) % kMaxSeats;
				if (state.occupied[candidate])
					return candidate;
			}
			return fromSeat; // unreachable in a real game -- fromSeat itself is always occupied
		}

		// Evaluates a BLOCKED position (or a depth-cutoff stand-in for
		// one) from mySeat's perspective using the exact same lowest-
		// pip-total-wins rule DominoHandEval::HandPipTotal()'s own
		// header comment already documents for a real blocked round.
		// Positive == I'm ahead of the best-placed opponent.
		inline std::int32_t EvaluateBlocked(const GameState& state, int mySeat)
		{
			std::int32_t myTotal = static_cast<std::int32_t>(PipTotal(state.hands[mySeat]));
			std::int32_t bestOpponent = std::numeric_limits<std::int32_t>::max();
			for (int s = 0; s < kMaxSeats; s++)
			{
				if (!state.occupied[s] || s == mySeat)
					continue;
				bestOpponent = std::min(bestOpponent, static_cast<std::int32_t>(PipTotal(state.hands[s])));
			}
			if (bestOpponent == std::numeric_limits<std::int32_t>::max())
				return 0; // no other occupied seat -- shouldn't happen in a real game
			return bestOpponent - myTotal;
		}

		inline std::vector<Move> LegalMoves(const GameState& state, int seat)
		{
			std::vector<Move> moves;
			const auto& hand = state.hands[seat];
			for (std::size_t i = 0; i < hand.size(); i++)
			{
				const Tile& tile = hand[i];
				if (!tile.IsValid())
					continue;

				for (std::int32_t endPip : state.ends.pips)
				{
					if (tile.low != endPip && tile.high != endPip)
						continue;

					Move mv;
					mv.handIndex = i;
					mv.tile = tile;
					mv.endPip = endPip;
					mv.resultPip = (tile.low == endPip) ? tile.high : tile.low;
					moves.push_back(mv);
				}
			}
			return moves;
		}

		inline GameState ApplyMove(const GameState& state, int seat, const Move& move)
		{
			GameState next = state;
			auto& hand = next.hands[seat];
			hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(move.handIndex));
			next.ends.Replace(move.endPip, move.resultPip);
			next.passStreak = 0;
			next.turnSeat = NextSeat(next, seat);
			return next;
		}

		inline GameState ApplyPass(const GameState& state, int seat)
		{
			GameState next = state;
			next.passStreak++;
			next.turnSeat = NextSeat(next, seat);
			return next;
		}

		// Paranoid two-player-equivalent minimax with alpha-beta pruning:
		// maximizes on mySeat's own turn, minimizes on every OTHER
		// seat's turn (see file header comment for why treating all
		// three opponents as one adversary is the safe choice absent a
		// known opponent policy). `depth` counts PLIES (one seat's
		// single turn or pass), not full round-robins.
		inline std::int32_t Search(const GameState& state, int mySeat, int depth, std::int32_t alpha, std::int32_t beta)
		{
			if (state.hands[mySeat].empty())
				return kWinScore - depth; // I already went out -- prefer the shallowest win among equal alternatives
			for (int s = 0; s < kMaxSeats; s++)
			{
				if (state.occupied[s] && s != mySeat && state.hands[s].empty())
					return -kWinScore + depth; // someone else went out
			}
			if (state.passStreak >= OccupiedCount(state))
				return EvaluateBlocked(state, mySeat);
			if (depth <= 0)
				return EvaluateBlocked(state, mySeat); // heuristic stand-in: "who'd win if it blocked right here"

			std::vector<Move> moves = LegalMoves(state, state.turnSeat);
			if (moves.empty())
			{
				GameState next = ApplyPass(state, state.turnSeat);
				return Search(next, mySeat, depth - 1, alpha, beta);
			}

			bool maximizing = (state.turnSeat == mySeat);
			std::int32_t best = maximizing ? std::numeric_limits<std::int32_t>::min()
											: std::numeric_limits<std::int32_t>::max();

			for (const Move& mv : moves)
			{
				GameState next = ApplyMove(state, state.turnSeat, mv);
				std::int32_t value = Search(next, mySeat, depth - 1, alpha, beta);

				if (maximizing)
				{
					best = std::max(best, value);
					alpha = std::max(alpha, best);
				}
				else
				{
					best = std::min(best, value);
					beta = std::min(beta, best);
				}
				if (alpha >= beta)
					break; // prune -- the other side already has a better option elsewhere
			}
			return best;
		}
	}

	// Top-level entry point: `state.turnSeat` MUST already be mySeat (the
	// caller's own decision point) -- DominoCheat.cpp's DetermineBestMove()
	// builds `state` from live-read hands + DetermineOpenEnds() and calls
	// this once per turn. `maxDepth` is in plies (one seat's move each) --
	// see this file's own DetermineBestMove() call site for the chosen
	// default and the branching-factor/perf tradeoff behind it.
	inline Recommendation FindBestMove(const GameState& state, int mySeat, int maxDepth)
	{
		Recommendation best;
		if (mySeat < 0 || mySeat >= kMaxSeats || !state.occupied[mySeat] || state.hands[mySeat].empty())
			return best;

		std::vector<Move> moves = detail::LegalMoves(state, mySeat);
		if (moves.empty())
			return best;

		if (state.hands[mySeat].size() == 1)
		{
			// Playing your only remaining tile wins the round outright --
			// every candidate in `moves` empties the hand equally, so
			// the first is as good as any; no search needed.
			const Move& mv = moves.front();
			best.valid = true;
			best.handIndex = mv.handIndex;
			best.tile = mv.tile;
			best.endPip = mv.endPip;
			best.resultPip = mv.resultPip;
			best.isWinningMove = true;
			best.score = detail::kWinScore;
			return best;
		}

		std::int32_t bestScore = std::numeric_limits<std::int32_t>::min();
		std::int32_t bestPipTiebreak = -1;

		for (const Move& mv : moves)
		{
			GameState next = detail::ApplyMove(state, mySeat, mv);
			std::int32_t score = detail::Search(next, mySeat, maxDepth - 1,
				std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());

			// Tie-break by the played tile's own pip total (highest
			// first, matching the real game's own func_353 fallback
			// heuristic per DominoCheat.cpp's DetermineBestMove()
			// comment) -- purely cosmetic once the minimax score is
			// equal; the score is always the primary ranking.
			std::int32_t pipTotal = mv.tile.PipTotal();
			bool better = (score > bestScore) || (score == bestScore && pipTotal > bestPipTiebreak);
			if (better)
			{
				bestScore = score;
				bestPipTiebreak = pipTotal;
				best.valid = true;
				best.handIndex = mv.handIndex;
				best.tile = mv.tile;
				best.endPip = mv.endPip;
				best.resultPip = mv.resultPip;
				best.isWinningMove = false;
				best.score = score;
			}
		}

		return best;
	}
}
