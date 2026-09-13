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

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <new>
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
