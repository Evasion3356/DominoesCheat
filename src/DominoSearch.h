/*
	Current production scheduling: deadline-controlled iterative deepening
	with cooperative cancellation and completed-depth publication. The
	background worker disables the historical node cap described below;
	standalone tests still use it for deterministic interruption coverage.
	Opponent branches now follow the verified highest-pip policy for
	Block/Draw, retaining unknown native-order ties. Scoring/unknown modes
	remain paranoid until exact native board totals can be simulated.
	Turn advancement follows the script's 0 -> 2 -> 1 -> 3 seat cycle.

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

	PERFORMANCE, and why a hard node budget exists (added same day as the
	first version, 2026-09-13 live bug report): the very first version
	used std::vector for every hand/move list, meaning EVERY node visited
	during the search heap-allocated (a GameState copy copying 4 vectors,
	plus a fresh std::vector<Move> for that node's legal moves). Running
	"Probe Best Move" froze the game -- DrawOverlay() *also* calls
	DetermineBestMove() every single tick for the entire real-world
	decision window (however long the player just looks at the screen),
	so an expensive search was being rebuilt from scratch 30-60 times a
	second, and depth 8 with no cap on branching gave no worst-case
	guarantee at all. Fixed two ways: (1) DetermineBestMove() itself now
	memoizes against the last hand/board it computed for (see its own
	comment) so this file is only invoked once per REAL decision, not
	once per frame; (2) this file now uses fixed-capacity arrays
	everywhere (zero heap allocations anywhere in the search) and a hard
	`nodeBudget` for nonterminal Search() visits. Iterative deepening
	keeps the last complete iteration when exhausted -- a bounded answer instead of
	worst-case-unbounded exact search, regardless of how bad branching
	gets.

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

	NOT yet live-tested against a real table -- the FIRST version caused
	a live game freeze (2026-09-13); this rewrite fixes the two causes
	above but hasn't itself been re-confirmed live yet.
*/

#pragma once

#include "DominoHandEval.h"
#include "DominoAiPolicy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace DominoSearch
{
	using DominoHandEval::Tile;
	constexpr int kMaxSeats = DominoHandEval::kMaxSeats;

	// Matches DominoCheat.cpp's own kMaxHandCapacity (a hand can grow
	// past its initial 7 tiles via boneyard draws) -- duplicated here
	// rather than shared because this header is deliberately a
	// standalone, zero-game-dependency module (see file header comment);
	// DominoCheat.cpp's DetermineBestMove() only ever populates this
	// search's GameState from the full-information (boneyard-empty)
	// case anyway, where real hands never actually exceed 7, but the
	// capacity is sized to the documented hard cap regardless.
	constexpr std::size_t kMaxHandTiles = 19;

	// A hand rarely has more than 2-4 legal replies to the current open
	// ends, but a tile whose low AND high both match different open ends
	// produces 2 moves from one tile, and a maximally degenerate board
	// (every one of 7 pips open) against a max-size hand could in theory
	// approach kMaxHandTiles*2 -- sized generously above the realistic
	// case rather than exactly at the theoretical one.
	constexpr std::size_t kMaxMovesPerNode = 48;

	// One abstract open "slot" per distinct playable pip value -- see
	// this file's header comment for why a SET (not an exact end-count/
	// topology) is the right fidelity level given what DetermineOpenEnds()
	// actually reports. Fixed-capacity (max 7 distinct pip values can
	// ever exist) -- no heap allocation, see file header comment.
	struct OpenEnds
	{
		std::array<std::int32_t, 7> pips{};
		int count = 0;

		void Replace(std::int32_t oldPip, std::int32_t newPip)
		{
			for (int i = 0; i < count; i++)
			{
				if (pips[i] == oldPip)
				{
					pips[i] = newPip;
					return;
				}
			}
		}
	};

	struct GameState
	{
		std::array<std::array<Tile, kMaxHandTiles>, kMaxSeats> hands{};
		std::array<int, kMaxSeats> handCounts{};
		std::array<bool, kMaxSeats> occupied{};
		OpenEnds ends;
		DominoAiPolicy::Rules rules = DominoAiPolicy::Rules::Unknown;
		int turnSeat = 0;
		int passStreak = 0; // consecutive passes across occupied seats -- == occupied count means the round is blocked

		bool operator==(const GameState& other) const
		{
			if (occupied != other.occupied || handCounts != other.handCounts || rules != other.rules ||
				turnSeat != other.turnSeat || passStreak != other.passStreak || ends.count != other.ends.count)
				return false;
			for (int e = 0; e < ends.count; e++)
				if (ends.pips[e] != other.ends.pips[e])
					return false;
			for (int s = 0; s < kMaxSeats; s++)
				for (int i = 0; i < handCounts[s]; i++)
					if (hands[s][i].low != other.hands[s][i].low || hands[s][i].high != other.hands[s][i].high)
						return false;
			return true;
		}
	};

	struct Move
	{
		std::size_t handIndex = 0;
		Tile tile;
		std::int32_t endPip = -1;
		std::int32_t resultPip = -1;
	};

	// Fixed-capacity move list -- see kMaxMovesPerNode above. Excess
	// candidates beyond capacity are silently dropped rather than
	// overflowing; in practice this never gets close to full.
	struct MoveList
	{
		std::array<Move, kMaxMovesPerNode> moves{};
		int count = 0;

		void Push(const Move& m)
		{
			if (count < static_cast<int>(kMaxMovesPerNode))
				moves[count++] = m;
		}

		bool Empty() const { return count == 0; }
		Move* begin() { return moves.data(); }
		Move* end() { return moves.data() + count; }
		const Move* begin() const { return moves.data(); }
		const Move* end() const { return moves.data() + count; }
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
		int completedDepth = 0; // last fully evaluated root iteration; zero means the static fallback
	};

	struct SearchControl
	{
		std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
		const std::atomic<std::uint64_t>* generation = nullptr;
		std::uint64_t expectedGeneration = 0;
		std::function<void(const Recommendation&)> publish;

		bool Stopped() const
		{
			return (generation && generation->load(std::memory_order_relaxed) != expectedGeneration) ||
				std::chrono::steady_clock::now() >= deadline;
		}
	};

	// Deterministic budget for standalone callers/tests. The live worker
	// uses -1 (no node cap) with a deadline and cancellation generation.
	constexpr int kDefaultNodeBudget = 100'000;

	namespace detail
	{
		constexpr std::int32_t kWinScore = 1'000'000;

		inline int PipTotal(const GameState& state, int seat)
		{
			int total = 0;
			for (int i = 0; i < state.handCounts[seat]; i++)
				total += state.hands[seat][i].PipTotal();
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
			// func_324: 0 -> 2 -> 1 -> 3 -> 0, skipping unoccupied seats.
			static constexpr std::array<int, kMaxSeats> successor{ 2, 3, 1, 0 };
			int candidate = fromSeat;
			for (int step = 1; step <= kMaxSeats; step++)
			{
				candidate = successor[candidate];
				if (state.occupied[candidate])
					return candidate;
			}
			return fromSeat; // unreachable in a real game -- fromSeat itself is always occupied
		}

		// Evaluates a BLOCKED position (or a depth-cutoff/budget-cutoff
		// stand-in for one) from mySeat's perspective using the exact
		// same lowest-pip-total-wins rule DominoHandEval::HandPipTotal()'s
		// own header comment already documents for a real blocked round.
		// Positive == I'm ahead of the best-placed opponent.
		inline std::int32_t EvaluateBlocked(const GameState& state, int mySeat)
		{
			std::int32_t myTotal = static_cast<std::int32_t>(PipTotal(state, mySeat));
			std::int32_t bestOpponent = std::numeric_limits<std::int32_t>::max();
			for (int s = 0; s < kMaxSeats; s++)
			{
				if (!state.occupied[s] || s == mySeat)
					continue;
				bestOpponent = std::min(bestOpponent, static_cast<std::int32_t>(PipTotal(state, s)));
			}
			if (bestOpponent == std::numeric_limits<std::int32_t>::max())
				return 0; // no other occupied seat -- shouldn't happen in a real game
			return bestOpponent - myTotal;
		}

		inline MoveList LegalMoves(const GameState& state, int seat)
		{
			MoveList moves;
			int handCount = state.handCounts[seat];
			for (int i = 0; i < handCount; i++)
			{
				const Tile& tile = state.hands[seat][i];
				if (!tile.IsValid())
					continue;

				for (int e = 0; e < state.ends.count; e++)
				{
					std::int32_t endPip = state.ends.pips[e];
					bool duplicate = false;
					for (int previous = 0; previous < e; previous++)
						if (state.ends.pips[previous] == endPip)
							duplicate = true;
					if (duplicate)
						continue;
					if (tile.low != endPip && tile.high != endPip)
						continue;

					Move mv;
					mv.handIndex = static_cast<std::size_t>(i);
					mv.tile = tile;
					mv.endPip = endPip;
					mv.resultPip = (tile.low == endPip) ? tile.high : tile.low;
					moves.Push(mv);
				}
			}
			return moves;
		}

		inline MoveList OpponentMoves(const GameState& state, int seat)
		{
			MoveList legal = LegalMoves(state, seat);
			// Scoring modes need the native's exact resulting end totals.
			// Never substitute a sum of distinct pip values for those totals.
			// Also avoid predicting a truncated native list (capacity 15).
			if (!DominoAiPolicy::AlwaysUsesPipPriority(state.rules) || legal.count > 15)
				return legal;
			int highestPips = -1;
			for (const Move& move : legal)
				highestPips = std::max(highestPips, move.tile.PipTotal());
			MoveList plausible;
			for (const Move& move : legal)
				if (move.tile.PipTotal() == highestPips)
					plausible.Push(move);
			// The script selects the last native candidate among ties, but
			// simulated move order is not native order. Keep all such ties.
			return plausible;
		}

		inline GameState ApplyMove(const GameState& state, int seat, const Move& move)
		{
			GameState next = state;
			int& count = next.handCounts[seat];
			auto& hand = next.hands[seat];
			int removeAt = static_cast<int>(move.handIndex);
			for (int i = removeAt; i + 1 < count; i++)
				hand[i] = hand[i + 1];
			count--;
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

		// Terminal outcomes must dominate all nonterminal pip estimates.
		// Remaining depth is larger for an earlier win (or earlier loss).
		inline bool TerminalScore(const GameState& state, int mySeat, int depth, std::int32_t& score)
		{
			if (state.handCounts[mySeat] == 0)
			{
				score = kWinScore + depth;
				return true;
			}
			for (int s = 0; s < kMaxSeats; s++)
			{
				if (state.occupied[s] && s != mySeat && state.handCounts[s] == 0)
				{
					score = -kWinScore - depth;
					return true;
				}
			}
			if (state.passStreak >= OccupiedCount(state))
			{
				const auto margin = EvaluateBlocked(state, mySeat);
				score = margin > 0 ? kWinScore + depth : (margin < 0 ? -kWinScore - depth : 0);
				return true;
			}
			return false;
		}

		// Alpha-beta over player choices and policy-consistent opponent
		// replies. Unknown policy/placement details remain adversarial.
		// Interrupted iterations never publish partial bounds as exact scores.
		inline std::int32_t Search(const GameState& state, int mySeat, int depth, std::int32_t alpha, std::int32_t beta, int& nodeBudget, bool& complete, const SearchControl* control = nullptr)
		{
			if (control && control->Stopped())
			{
				complete = false;
				return 0;
			}
			std::int32_t terminal = 0;
			if (TerminalScore(state, mySeat, depth, terminal))
				return terminal;
			if (nodeBudget == 0)
			{
				complete = false;
				return 0;
			}
			if (nodeBudget > 0)
				nodeBudget--;
			if (depth <= 0)
				return EvaluateBlocked(state, mySeat);

			MoveList moves = state.turnSeat == mySeat
				? LegalMoves(state, state.turnSeat) : OpponentMoves(state, state.turnSeat);
			if (moves.Empty())
			{
				GameState next = ApplyPass(state, state.turnSeat);
				return Search(next, mySeat, depth - 1, alpha, beta, nodeBudget, complete, control);
			}

			bool maximizing = (state.turnSeat == mySeat);
			std::int32_t best = maximizing ? std::numeric_limits<std::int32_t>::min()
											: std::numeric_limits<std::int32_t>::max();

			for (const Move& mv : moves)
			{
				GameState next = ApplyMove(state, state.turnSeat, mv);
				std::int32_t value = Search(next, mySeat, depth - 1, alpha, beta, nodeBudget, complete, control);
				if (!complete)
					return 0;

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
	// this once per REAL decision (memoized there, see its own comment --
	// this function must never be called once per frame). `maxDepth` is
	// in plies (one seat's move each). A negative nodeBudget disables the
	// node cap; the live worker supplies a timed, cancellable control.
	inline Recommendation FindBestMove(const GameState& state, int mySeat, int maxDepth, int nodeBudget = kDefaultNodeBudget, const SearchControl* control = nullptr)
	{
		Recommendation best;
		if (mySeat < 0 || mySeat >= kMaxSeats || !state.occupied[mySeat] || state.turnSeat != mySeat)
			return best;
		std::int32_t terminal = 0;
		if (detail::TerminalScore(state, mySeat, 0, terminal))
			return best;

		MoveList moves = detail::LegalMoves(state, mySeat);
		if (moves.Empty())
			return best;

		if (state.handCounts[mySeat] == 1)
		{
			// Playing your only remaining tile wins the round outright --
			// every candidate in `moves` empties the hand equally, so
			// the first is as good as any; no search needed.
			const Move& mv = moves.moves[0];
			best.valid = true;
			best.handIndex = mv.handIndex;
			best.tile = mv.tile;
			best.endPip = mv.endPip;
			best.resultPip = mv.resultPip;
			best.isWinningMove = true;
			best.score = detail::kWinScore;
			return best;
		}

		// Iteration zero evaluates EVERY root move without recursion, even
		// with no budget. Deeper iterations replace it only when complete.
		int totalTiles = 0;
		for (int s = 0; s < kMaxSeats; s++)
			if (state.occupied[s])
				totalTiles += state.handCounts[s];
		maxDepth = std::clamp(maxDepth, 0, (totalTiles + 1) * detail::OccupiedCount(state));
		for (int iteration = 0; iteration <= maxDepth; iteration++)
		{
			Recommendation candidate;
			std::int32_t bestScore = std::numeric_limits<std::int32_t>::min();
			int bestPipTiebreak = -1;
			bool complete = true;
			for (const Move& mv : moves)
			{
				GameState next = detail::ApplyMove(state, mySeat, mv);
				std::int32_t score = 0;
				if (iteration == 0)
				{
					if (!detail::TerminalScore(next, mySeat, 0, score))
						score = detail::EvaluateBlocked(next, mySeat);
				}
				else
					score = detail::Search(next, mySeat, iteration - 1,
						std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(), nodeBudget, complete, control);
				if (!complete)
					break;
				int pipTotal = mv.tile.PipTotal();
				if (score > bestScore || (score == bestScore && pipTotal > bestPipTiebreak))
				{
					bestScore = score;
					bestPipTiebreak = pipTotal;
					candidate.valid = true;
					candidate.handIndex = mv.handIndex;
					candidate.tile = mv.tile;
					candidate.endPip = mv.endPip;
					candidate.resultPip = mv.resultPip;
					candidate.score = score;
					candidate.completedDepth = iteration;
				}
			}
			if (!complete)
				break;
			best = candidate;
			if (control && control->publish)
				control->publish(best);
			if ((control && control->Stopped()) || nodeBudget == 0 || best.score >= detail::kWinScore || best.score <= -detail::kWinScore)
				break;
		}

		return best;
	}
}
