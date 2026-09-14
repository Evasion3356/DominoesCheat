/*
	Full-information search over the dominoes round -- pure-logic header,
	zero game-memory dependency, same "pure math header, unit tested in
	isolation" convention as DominoHandEval.h (see
	tests/DominoHandEvalTests.cpp). DominoCheat.cpp's DetermineBestMove()
	snapshots the live table into a GameState on the game thread and hands
	it to AsyncMoveAdvisor's worker, which calls FindBestMove() here.

	WHAT THE SEARCH KNOWS (and why that makes it exact rather than a
	guess): dominoes_sp shuffles the whole 28-tile set once and deals from
	it sequentially, and every seat's hand plus the undrawn boneyard are
	readable (see DominoCheat.cpp's file header comment). So nothing is
	hidden: every tile's location is known, and the boneyard's remaining
	DRAW ORDER is known too. The only uncertainty is what each opponent
	chooses to play -- and even that is a scripted, deterministic policy
	(DominoAiPolicy.h): under Block/Draw rules the AI always plays its
	highest-pip legal tile, so an opponent node has one or two plausible
	replies, not "any legal move". What remains genuinely unknown (native
	candidate order on equal-pip ties, and the exact board-end totals the
	scoring tables' AI prefers) is handled adversarially: the search
	minimizes over every reply it can't rule out.

	THE EVALUATION (2026-09-13 decision-engine pass -- the change that
	matters most): the score is the round's NET POINTS for mySeat, in the
	game's own scoring. Traced from func_169 (blocked round) and the
	domino-out path at line ~8553 via func_343/func_357:
	  - the round winner is the seat that emptied its hand, or, on a
	    block (every occupied seat passed in a row), the seat with the
	    UNIQUE lowest remaining-pip total (a tie for lowest means nobody
	    wins and nobody scores);
	  - the winner is paid the SUM of every other seat's remaining total,
	    each rounded per the table rules (DominoAiPolicy::RoundedPipTotal
	    -- plain pips for Block/Draw, nearest multiple of five/three for
	    All Fives/All Threes);
	  - a seat whose accumulated score reaches the table's target ends
	    the whole game.
	A terminal position is therefore worth (my gain - the winner's gain)
	in points, with a game win/loss (target reached) dominating any
	round result, and a small "prefer the shorter win / longer loss"
	tiebreak below the points scale. The previous version scored a round
	as a bare win/loss/tie. That looked fine in unit tests but had a
	nasty failure mode live: because the AI's policy collapses the tree,
	the search "solves" most positions from the very first move, and in
	every position it solved as lost (most of them, under the paranoid
	model) EVERY move scored identically -- so the recommendation fell
	through to the pip-total tiebreak, i.e. exactly the old 1-ply "play
	the highest tile" heuristic that was already known to lose. Scoring
	net points makes the search distinguish "lose by 6" from "lose by
	40", and "win by 12" from "win by 3", which is what actually decides
	a game played to 60/90/100.

	BONEYARD DRAWS are modeled (same pass). The previous version only ran
	when all four seats were dealt (empty boneyard) and fell back to the
	1-ply heuristic otherwise -- but a 2- or 3-seat table is the common
	case, and the draw order is fully known. Under any non-Block rules,
	a seat with no legal move draws the next boneyard tile until it can
	play or the boneyard/19-tile hand cap runs out (func_166/func_611),
	then plays or passes -- a deterministic transition, no chance node.
	Under Block rules the boneyard is never touched, so the leftover
	tiles are simply out of play and the search is exact as-is.

	The opening move (no open ends yet) is also searched: every hand tile
	is legal and playing one opens both of its pips. The scripted AI opens
	with the same selectors it uses mid-round (func_351).

	ROOT SCORING BONUS: on All Fives/All Threes tables the game credits a
	seat immediately for a placement whose resulting end total is a
	multiple of five/three (func_354). The search can't compute those
	totals from its board abstraction (below), but at the ROOT the native
	legal-move query reports each of mySeat's candidates' exact resulting
	total, so DetermineBestMove() fills rootMoveBonusPoints and the
	search credits it to that root move's gain. Deeper scoring plays,
	ours or theirs, remain unmodeled.

	PERFORMANCE: fixed-capacity arrays everywhere (no heap allocation in
	the search), iterative deepening with root moves re-ordered by the
	previous iteration, alpha-beta at every level including the root, a
	cooperative deadline/cancellation (SearchControl) for the live worker
	and a deterministic node budget for tests. Under Block/Draw a full
	4-seat round solves from the first move in a few milliseconds; the
	paranoid modes take up to a few hundred milliseconds early in the
	round and are exact later. An iteration whose explored tree never hit
	the depth horizon is exact, and the search stops there.

	What this file does NOT model:
	 - The real board only exposes a SET of open pip values (0-6) via
	   DetermineOpenEnds() -- not how many independent ends show each
	   value, nor the spinner/spur layout the IDA trace found (see
	   DominoCheat.cpp's file header comment, Session 4). OpenEnds models
	   the board as one abstract "slot" per distinct open pip and playing
	   a tile REPLACES that slot's value. A real board with two ends
	   sharing a pip collapses to one slot here, which can occasionally
	   misjudge which exact end closes.
	 - All Fives/All Threes mid-round scoring beyond the root move, and
	   the scoring-mode AI's preference for a scoring placement (it
	   needs the exact board-end total). Opponents on those tables are
	   searched paranoidly over every legal reply.
	 - Opponents blocking EACH OTHER for their own benefit: every reply
	   the policy leaves open is minimized from mySeat's perspective.
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

	// Matches DominoCheat.cpp's own kMaxHandCapacity and the script's own
	// func_611 hard cap: a hand grows past its initial 7 via boneyard
	// draws and the script refuses the 20th tile.
	constexpr std::size_t kMaxHandTiles = 19;
	constexpr int kBoneyardCapacity = DominoHandEval::kTileSetSize;

	// A tile matching two different open ends yields two moves; a
	// maximally degenerate board (all 7 pips open) against a max-size
	// hand could in theory approach kMaxHandTiles*2 -- sized generously.
	constexpr std::size_t kMaxMovesPerNode = 48;

	// One abstract open "slot" per distinct playable pip value -- see the
	// file header comment for why a SET is the right fidelity level.
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

		// Undrawn boneyard in DRAW ORDER: boneyard[boneyardNext..boneyardCount)
		// is still available. Consumed only under DrawsFromBoneyard(rules).
		std::array<Tile, kBoneyardCapacity> boneyard{};
		int boneyardCount = 0;
		int boneyardNext = 0;

		// Accumulated game score per seat and the table's points target
		// (seat.f_2 / Round.f_666.f_14[0]); pointsTarget == 0 disables
		// game-outcome awareness and the search scores the round alone.
		std::array<std::int32_t, kMaxSeats> scores{};
		std::int32_t pointsTarget = 0;

		// Points already credited during the round being searched (only
		// ever nonzero for mySeat, via rootMoveBonusPoints below).
		std::array<std::int32_t, kMaxSeats> roundGain{};

		// Root-only: points the game credits mySeat immediately for
		// playing hand tile [i] from THIS position (All Fives/All Threes
		// end-total scoring, read exactly from the native at the root).
		std::array<std::int32_t, kMaxHandTiles> rootMoveBonusPoints{};

		bool operator==(const GameState& other) const
		{
			if (occupied != other.occupied || handCounts != other.handCounts || rules != other.rules ||
				turnSeat != other.turnSeat || passStreak != other.passStreak || ends.count != other.ends.count ||
				boneyardCount != other.boneyardCount || boneyardNext != other.boneyardNext ||
				scores != other.scores || pointsTarget != other.pointsTarget || roundGain != other.roundGain ||
				rootMoveBonusPoints != other.rootMoveBonusPoints)
				return false;
			for (int e = 0; e < ends.count; e++)
				if (ends.pips[e] != other.ends.pips[e])
					return false;
			for (int s = 0; s < kMaxSeats; s++)
				for (int i = 0; i < handCounts[s]; i++)
					if (hands[s][i].low != other.hands[s][i].low || hands[s][i].high != other.hands[s][i].high)
						return false;
			for (int i = boneyardNext; i < boneyardCount; i++)
				if (boneyard[i].low != other.boneyard[i].low || boneyard[i].high != other.boneyard[i].high)
					return false;
			return true;
		}
	};

	struct Move
	{
		std::size_t handIndex = 0;
		Tile tile;
		std::int32_t endPip = -1;    // -1: opening move on an empty board
		std::int32_t resultPip = -1; // -1: opening move (both pips open afterwards)
	};

	// Fixed-capacity move list -- see kMaxMovesPerNode above.
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

	// What the search proved about the recommended line. Undecided means
	// the horizon was hit somewhere that could still change the value.
	enum class Outcome { Undecided, GameWin, RoundWin, Tie, RoundLoss, GameLoss };

	inline const char* OutcomeName(Outcome outcome)
	{
		switch (outcome)
		{
		case Outcome::GameWin: return "GAME WIN";
		case Outcome::RoundWin: return "round win";
		case Outcome::Tie: return "tie";
		case Outcome::RoundLoss: return "round loss";
		case Outcome::GameLoss: return "GAME LOSS";
		default: return "undecided";
		}
	}

	struct Recommendation
	{
		bool valid = false;
		std::size_t handIndex = 0;
		Tile tile;
		std::int32_t endPip = -1;
		std::int32_t resultPip = -1;
		bool isWinningMove = false;  // this tile empties my hand right now
		std::int32_t score = 0;      // raw search value (points * kPointUnit + tiebreak, plus game class)
		int completedDepth = 0;      // last fully evaluated root iteration; zero means the static fallback
		bool exact = false;          // the completed iteration never hit the horizon: outcome/points are the round's real result under the model
		Outcome outcome = Outcome::Undecided;
		std::int32_t points = 0;     // net points (my gain - the round winner's gain) at the end of the recommended line; a horizon estimate when !exact
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
		// Score layout: points * kPointUnit + tiebreak (|tiebreak| < kPointUnit),
		// plus +-kGameScore when a seat reaches the table's target. Round
		// points never exceed 168 (the whole set's pips), so a game
		// outcome always dominates any round result.
		constexpr std::int32_t kPointUnit = 256;
		constexpr std::int32_t kGameScore = 4'000'000;
		constexpr int kMaxSearchDepth = 200; // tiebreak term must stay below kPointUnit

		inline int PipTotal(const GameState& state, int seat)
		{
			int total = 0;
			for (int i = 0; i < state.handCounts[seat]; i++)
				total += state.hands[seat][i].PipTotal();
			return total;
		}

		inline int RoundedTotal(const GameState& state, int seat)
		{
			return DominoAiPolicy::RoundedPipTotal(state.rules, PipTotal(state, seat));
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

		// Horizon estimate for a NON-terminal position, in the same units
		// as a terminal score: how far ahead of the best-placed opponent
		// I am on remaining pips (the block tiebreak), plus any points
		// already banked this round. Deliberately bounded well inside the
		// game-outcome class.
		inline std::int32_t Heuristic(const GameState& state, int mySeat)
		{
			std::int32_t myTotal = static_cast<std::int32_t>(RoundedTotal(state, mySeat));
			std::int32_t bestOpponent = std::numeric_limits<std::int32_t>::max();
			std::int32_t bestOpponentGain = 0;
			for (int s = 0; s < kMaxSeats; s++)
			{
				if (!state.occupied[s] || s == mySeat)
					continue;
				bestOpponent = std::min(bestOpponent, static_cast<std::int32_t>(RoundedTotal(state, s)));
				bestOpponentGain = std::max(bestOpponentGain, state.roundGain[s]);
			}
			if (bestOpponent == std::numeric_limits<std::int32_t>::max())
				return 0;
			return (bestOpponent - myTotal + state.roundGain[mySeat] - bestOpponentGain) * kPointUnit;
		}

		// Round winner per func_169/func_164: the seat that emptied its
		// hand, else (blocked) the UNIQUE lowest rounded total, else -1.
		// Returns false when the round is still in progress.
		inline bool RoundResult(const GameState& state, int& winner)
		{
			winner = -1;
			for (int s = 0; s < kMaxSeats; s++)
			{
				if (state.occupied[s] && state.handCounts[s] == 0)
				{
					winner = s;
					return true;
				}
			}
			if (state.passStreak < OccupiedCount(state))
				return false;
			int lowest = std::numeric_limits<int>::max();
			for (int s = 0; s < kMaxSeats; s++)
			{
				if (!state.occupied[s])
					continue;
				int total = RoundedTotal(state, s);
				if (total == lowest)
					winner = -1;
				if (total < lowest)
				{
					winner = s;
					lowest = total;
				}
			}
			return true;
		}

		// Terminal score from mySeat's perspective. `depth` is the
		// remaining search depth at this node: an earlier win (larger
		// remaining depth) or a later loss is preferred on equal points.
		inline bool TerminalScore(const GameState& state, int mySeat, int depth, std::int32_t& score)
		{
			std::array<std::int32_t, kMaxSeats> gain = state.roundGain;

			// func_164: a seat reaching the target ends the round (and the
			// game) immediately, before any further play -- only ever
			// reachable here through a root scoring bonus.
			int gameWinner = -1;
			if (state.pointsTarget > 0)
				for (int s = 0; s < kMaxSeats; s++)
					if (state.occupied[s] && state.scores[s] + gain[s] >= state.pointsTarget)
						gameWinner = s;

			int winner = -1;
			if (gameWinner < 0)
			{
				if (!RoundResult(state, winner))
					return false;
				if (winner >= 0)
				{
					std::int32_t points = 0;
					for (int s = 0; s < kMaxSeats; s++)
						if (state.occupied[s] && s != winner)
							points += static_cast<std::int32_t>(RoundedTotal(state, s));
					gain[winner] += points;
					if (state.pointsTarget > 0 && state.scores[winner] + gain[winner] >= state.pointsTarget)
						gameWinner = winner;
				}
			}

			std::int32_t bestOpponentGain = 0;
			for (int s = 0; s < kMaxSeats; s++)
				if (state.occupied[s] && s != mySeat)
					bestOpponentGain = std::max(bestOpponentGain, gain[s]);

			score = (gain[mySeat] - bestOpponentGain) * kPointUnit;
			if (gameWinner == mySeat)
				score += kGameScore;
			else if (gameWinner >= 0)
				score -= kGameScore;
			if (score > 0)
				score += depth;
			else if (score < 0)
				score -= depth;
			return true;
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

				if (state.ends.count == 0)
				{
					// Opening move: any tile, opens both of its pips.
					Move mv;
					mv.handIndex = static_cast<std::size_t>(i);
					mv.tile = tile;
					moves.Push(mv);
					continue;
				}

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

		// The replies an opponent seat can actually make under the
		// scripted policy (DominoAiPolicy.h). Block/Draw: only its
		// highest-pip legal tiles (native-order ties kept, minimized over
		// adversarially). Scoring/unknown modes: every legal reply, since
		// the scoring preference needs the native's exact end totals.
		// Also every legal reply if the candidate list would overflow the
		// native's 15-entry buffer, where the script's own view is cut.
		inline MoveList OpponentMoves(const GameState& state, int seat)
		{
			MoveList legal = LegalMoves(state, seat);
			if (!DominoAiPolicy::AlwaysUsesPipPriority(state.rules) || legal.count > 15)
				return legal;
			int highestPips = -1;
			for (const Move& move : legal)
				highestPips = std::max(highestPips, move.tile.PipTotal());
			MoveList plausible;
			for (const Move& move : legal)
				if (move.tile.PipTotal() == highestPips)
					plausible.Push(move);
			return plausible;
		}

		inline MoveList MovesFor(const GameState& state, int seat, int mySeat)
		{
			return seat == mySeat ? LegalMoves(state, seat) : OpponentMoves(state, seat);
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
			if (move.endPip < 0)
			{
				next.ends.count = 0;
				next.ends.pips[static_cast<std::size_t>(next.ends.count++)] = move.tile.low;
				if (move.tile.high != move.tile.low)
					next.ends.pips[static_cast<std::size_t>(next.ends.count++)] = move.tile.high;
			}
			else
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

		// func_166/func_611: with no legal move under drawing rules, the
		// seat takes boneyard tiles in order until one is playable, the
		// boneyard runs out, or the hand hits the 19-tile cap. Returns
		// true if at least one tile was drawn (state modified in place).
		inline bool DrawUntilPlayable(GameState& state, int seat)
		{
			if (!DominoAiPolicy::DrawsFromBoneyard(state.rules))
				return false;
			bool drew = false;
			while (state.boneyardNext < state.boneyardCount &&
				state.handCounts[seat] < static_cast<int>(kMaxHandTiles))
			{
				state.hands[seat][static_cast<std::size_t>(state.handCounts[seat]++)] = state.boneyard[static_cast<std::size_t>(state.boneyardNext++)];
				drew = true;
				if (!LegalMoves(state, seat).Empty())
					break;
			}
			return drew;
		}

		struct SearchStats
		{
			int nodeBudget = -1;        // < 0: unbounded; decremented per nonterminal visit, 0 stops the search
			bool complete = true;       // false once stopped by budget/deadline/cancellation -- the value is meaningless
			bool horizonCut = false;    // some explored node returned the heuristic instead of a terminal score
		};

		// Alpha-beta over mySeat's choices and policy-consistent opponent
		// replies (paranoid over whatever the policy leaves open).
		inline std::int32_t Search(const GameState& state, int mySeat, int depth, std::int32_t alpha, std::int32_t beta, SearchStats& stats, const SearchControl* control = nullptr)
		{
			if (control && control->Stopped())
			{
				stats.complete = false;
				return 0;
			}
			std::int32_t terminal = 0;
			if (TerminalScore(state, mySeat, depth, terminal))
				return terminal;
			if (stats.nodeBudget == 0)
			{
				stats.complete = false;
				return 0;
			}
			if (stats.nodeBudget > 0)
				stats.nodeBudget--;
			if (depth <= 0)
			{
				stats.horizonCut = true;
				return Heuristic(state, mySeat);
			}

			int seat = state.turnSeat;
			MoveList moves = MovesFor(state, seat, mySeat);
			GameState drawn;
			const GameState* position = &state;
			if (moves.Empty())
			{
				drawn = state;
				if (DrawUntilPlayable(drawn, seat))
				{
					position = &drawn;
					moves = MovesFor(drawn, seat, mySeat);
				}
			}
			if (moves.Empty())
			{
				GameState next = ApplyPass(*position, seat);
				return Search(next, mySeat, depth - 1, alpha, beta, stats, control);
			}

			bool maximizing = (seat == mySeat);
			std::int32_t best = maximizing ? std::numeric_limits<std::int32_t>::min()
											: std::numeric_limits<std::int32_t>::max();

			for (const Move& mv : moves)
			{
				GameState next = ApplyMove(*position, seat, mv);
				std::int32_t value = Search(next, mySeat, depth - 1, alpha, beta, stats, control);
				if (!stats.complete)
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

		// Back-compat shim for callers/tests using the older signature.
		inline std::int32_t Search(const GameState& state, int mySeat, int depth, std::int32_t alpha, std::int32_t beta, int& nodeBudget, bool& complete, const SearchControl* control = nullptr)
		{
			SearchStats stats;
			stats.nodeBudget = nodeBudget;
			std::int32_t value = Search(state, mySeat, depth, alpha, beta, stats, control);
			nodeBudget = stats.nodeBudget;
			complete = stats.complete;
			return value;
		}

		inline void FillOutcome(Recommendation& rec)
		{
			std::int32_t raw = rec.score;
			bool gameWin = raw >= kGameScore / 2;
			bool gameLoss = raw <= -kGameScore / 2;
			if (gameWin)
				raw -= kGameScore;
			else if (gameLoss)
				raw += kGameScore;
			rec.points = raw / kPointUnit; // truncation toward zero strips the tiebreak
			if (!rec.exact)
				rec.outcome = Outcome::Undecided;
			else if (gameWin)
				rec.outcome = Outcome::GameWin;
			else if (gameLoss)
				rec.outcome = Outcome::GameLoss;
			else if (raw > 0)
				rec.outcome = Outcome::RoundWin;
			else if (raw < 0)
				rec.outcome = Outcome::RoundLoss;
			else
				rec.outcome = Outcome::Tie;
		}
	}

	// Top-level entry point: state.turnSeat MUST already be mySeat (the
	// caller's own decision point). maxDepth is in plies (one seat's move
	// each) and is clamped to the round's own finite bound. A negative
	// nodeBudget disables the node cap; the live worker supplies a timed,
	// cancellable control and receives every completed iteration through
	// control->publish.
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

		// Iteration zero evaluates EVERY root move without recursion, even
		// with no budget. Deeper iterations replace it only when complete.
		int totalTiles = std::max(0, state.boneyardCount - state.boneyardNext);
		for (int s = 0; s < kMaxSeats; s++)
			if (state.occupied[s])
				totalTiles += state.handCounts[s];
		maxDepth = std::clamp(maxDepth, 0, std::min(detail::kMaxSearchDepth, (totalTiles + 1) * detail::OccupiedCount(state)));

		std::array<int, kMaxMovesPerNode> order{};
		std::array<std::int32_t, kMaxMovesPerNode> lastScores{};
		for (int i = 0; i < moves.count; i++)
			order[static_cast<std::size_t>(i)] = i;

		detail::SearchStats stats;
		stats.nodeBudget = nodeBudget;
		for (int iteration = 0; iteration <= maxDepth; iteration++)
		{
			Recommendation candidate;
			std::int32_t bestScore = std::numeric_limits<std::int32_t>::min();
			int bestPipTiebreak = -1;
			bool anyHorizonCut = false;
			std::int32_t alpha = std::numeric_limits<std::int32_t>::min();
			for (int k = 0; k < moves.count; k++)
			{
				int moveIndex = order[static_cast<std::size_t>(k)];
				const Move& mv = moves.moves[static_cast<std::size_t>(moveIndex)];
				GameState next = detail::ApplyMove(state, mySeat, mv);
				if (mv.handIndex < kMaxHandTiles && state.rootMoveBonusPoints[mv.handIndex] > 0)
					next.roundGain[mySeat] += state.rootMoveBonusPoints[mv.handIndex];

				std::int32_t score = 0;
				stats.horizonCut = false;
				if (iteration == 0)
				{
					if (!detail::TerminalScore(next, mySeat, 0, score))
					{
						score = detail::Heuristic(next, mySeat);
						stats.horizonCut = true;
					}
				}
				else
					score = detail::Search(next, mySeat, iteration - 1, alpha, std::numeric_limits<std::int32_t>::max(), stats, control);
				if (!stats.complete)
					break;
				anyHorizonCut = anyHorizonCut || stats.horizonCut;
				lastScores[static_cast<std::size_t>(moveIndex)] = score;

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
					candidate.isWinningMove = (next.handCounts[mySeat] == 0);
					candidate.score = score;
					candidate.completedDepth = iteration;
				}
				// Window (bestScore - 1, +inf): a later move that returns
				// exactly bestScore is then a real tie (not a fail-low
				// bound), so the pip tiebreak above stays sound.
				alpha = bestScore - 1;
			}
			if (!stats.complete)
				break;

			candidate.exact = !anyHorizonCut;
			detail::FillOutcome(candidate);
			best = candidate;
			if (control && control->publish)
				control->publish(best);
			if ((control && control->Stopped()) || stats.nodeBudget == 0 || best.exact)
				break;

			// Re-order root moves by this iteration's values so the next
			// iteration's alpha-beta sees its best candidates first.
			std::stable_sort(order.begin(), order.begin() + moves.count, [&](int a, int b)
			{
				return lastScores[static_cast<std::size_t>(a)] > lastScores[static_cast<std::size_t>(b)];
			});
		}

		return best;
	}
}
