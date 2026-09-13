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
	`nodeBudget` that Search() decrements on every call, returning early
	once exhausted -- a worst-case-bounded fallback answer instead of
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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
	};

	// Hard cap on total Search() calls within one FindBestMove() --
	// bounds worst-case wall-clock regardless of how bad branching gets,
	// see file header comment for why this exists. 100k plain-array
	// node visits (no heap allocation anywhere in the loop, see above)
	// runs in low-single-digit milliseconds even unoptimized; picked as
	// a budget that's generous for real board branching (typically 2-5)
	// while still being a hard, predictable ceiling. NOT yet profiled
	// against a real frame-time budget.
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
			for (int step = 1; step <= kMaxSeats; step++)
			{
				int candidate = (fromSeat + step) % kMaxSeats;
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

		// Paranoid two-player-equivalent minimax with alpha-beta pruning:
		// maximizes on mySeat's own turn, minimizes on every OTHER
		// seat's turn (see file header comment for why treating all
		// three opponents as one adversary is the safe choice absent a
		// known opponent policy). `depth` counts PLIES (one seat's
		// single turn or pass), not full round-robins. `nodeBudget` is
		// decremented once per call and shared across the WHOLE search
		// (not reset per branch) -- once it hits zero every further call
		// short-circuits to the same heuristic a depth cutoff would use,
		// giving a hard, predictable ceiling on total work independent
		// of how bad branching gets (see file header comment).
		inline std::int32_t Search(const GameState& state, int mySeat, int depth, std::int32_t alpha, std::int32_t beta, int& nodeBudget)
		{
			if (--nodeBudget <= 0)
				return EvaluateBlocked(state, mySeat);

			if (state.handCounts[mySeat] == 0)
				return kWinScore - depth; // I already went out -- prefer the shallowest win among equal alternatives
			for (int s = 0; s < kMaxSeats; s++)
			{
				if (state.occupied[s] && s != mySeat && state.handCounts[s] == 0)
					return -kWinScore + depth; // someone else went out
			}
			if (state.passStreak >= OccupiedCount(state))
				return EvaluateBlocked(state, mySeat);
			if (depth <= 0)
				return EvaluateBlocked(state, mySeat); // heuristic stand-in: "who'd win if it blocked right here"

			MoveList moves = LegalMoves(state, state.turnSeat);
			if (moves.Empty())
			{
				GameState next = ApplyPass(state, state.turnSeat);
				return Search(next, mySeat, depth - 1, alpha, beta, nodeBudget);
			}

			bool maximizing = (state.turnSeat == mySeat);
			std::int32_t best = maximizing ? std::numeric_limits<std::int32_t>::min()
											: std::numeric_limits<std::int32_t>::max();

			for (const Move& mv : moves)
			{
				GameState next = ApplyMove(state, state.turnSeat, mv);
				std::int32_t value = Search(next, mySeat, depth - 1, alpha, beta, nodeBudget);

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
				if (nodeBudget <= 0)
					break; // budget exhausted mid-loop -- stop exploring further siblings too
			}
			return best;
		}
	}

	// Top-level entry point: `state.turnSeat` MUST already be mySeat (the
	// caller's own decision point) -- DominoCheat.cpp's DetermineBestMove()
	// builds `state` from live-read hands + DetermineOpenEnds() and calls
	// this once per REAL decision (memoized there, see its own comment --
	// this function must never be called once per frame). `maxDepth` is
	// in plies (one seat's move each); `nodeBudget` bounds total work
	// regardless of depth/branching (see kDefaultNodeBudget's own
	// comment).
	inline Recommendation FindBestMove(const GameState& state, int mySeat, int maxDepth, int nodeBudget = kDefaultNodeBudget)
	{
		Recommendation best;
		if (mySeat < 0 || mySeat >= kMaxSeats || !state.occupied[mySeat] || state.handCounts[mySeat] == 0)
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

		std::int32_t bestScore = std::numeric_limits<std::int32_t>::min();
		std::int32_t bestPipTiebreak = -1;

		for (const Move& mv : moves)
		{
			GameState next = detail::ApplyMove(state, mySeat, mv);
			std::int32_t score = detail::Search(next, mySeat, maxDepth - 1,
				std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(), nodeBudget);

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
			if (nodeBudget <= 0)
				break; // budget exhausted -- stop exploring further root moves, keep the best found so far
		}

		return best;
	}
}
