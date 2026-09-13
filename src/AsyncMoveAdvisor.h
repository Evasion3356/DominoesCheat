/*
	Runs DominoSearch::FindBestMove() on a dedicated background thread so
	the game's own ScriptMain thread never blocks on it -- added
	2026-09-13 after the synchronous version froze the game (see
	DominoCheat.cpp's DetermineBestMove() and CLAUDE.md's Session 9
	addendum for the full incident). The earlier fix (memoization + a
	hard node budget + fixed-capacity arrays) made a single search call
	fast and bounded; this goes further and makes it impossible for a
	search -- however long it takes -- to stall a single frame at all.

	Why this is safe to thread at all: DominoSearch::GameState is already
	a plain, self-contained value type with ZERO game-memory dependency
	(no pointers into scrThread/script-local memory, see DominoSearch.h's
	own file header comment) -- that separation was already the whole
	point of that header. The boundary this file adds is exactly there:
	the caller (DetermineBestMove(), on the game's own thread) reads live
	game memory into a GameState snapshot -- cheap, and must stay on the
	game thread, since touching scrThread/script-local memory from any
	other thread while the game keeps ticking would be genuinely unsafe
	(no documented locking exists for that memory). Everything on the
	OTHER side of that boundary -- the snapshot itself and
	FindBestMove()'s own recursion over it -- never touches game memory
	again, so it's exactly as safe to run on a second thread as any other
	pure computation would be.

	Usage pattern (see DetermineBestMove() for the real call site):
	  1. Build a Key (a cheap, comparable snapshot of "what real-world
	     decision is this for" -- DominoCheat.cpp's own DecisionKey) and
	     a GameState from the CURRENT live read, on the game thread.
	  2. Call GetLatest() (non-blocking) and compare its key to the one
	     just built. If they match, that published result IS the answer
	     for the current decision -- safe to show immediately.
	  3. If they don't match (stale, or nothing published yet), call
	     SubmitJob() (non-blocking -- just hands the snapshot to the
	     worker and returns) and show NO recommendation for this tick
	     rather than a stale one for a hand that may have already
	     changed. The worker typically publishes a fresh result within a
	     handful of milliseconds (bounded by DominoSearch::kDefaultNodeBudget
	     regardless of board complexity) -- an unnoticeable gap against a
	     human decision, and never a blocked frame.

	Lifetime: one worker thread lives for as long as the AsyncMoveAdvisor
	instance does (typically a function-local static in DetermineBestMove(),
	so effectively the whole game session) -- see the destructor for
	shutdown. The destructor always signals shutdown (sets
	m_shuttingDown, wakes the condition variable) but only JOINS
	conditionally: an earlier version of this file joined unconditionally
	and reasoned that was safe because this worker never touches the
	loader (no LoadLibrary, no calling into a DLL mid-unload) -- but that
	reasoning missed a sharper problem. A function-local static's
	destructor runs as part of a statically-linked-CRT DLL's teardown
	from DllMain(DLL_PROCESS_DETACH), which Windows always calls under
	the process's loader lock, and a THREAD'S OWN EXIT triggers
	DLL_THREAD_DETACH notifications to every other loaded module -- which
	itself needs that same loader lock, regardless of what code the
	exiting thread ran. So joining here can deadlock against our own
	DllMain even though this worker's actual workload (FindBestMove()
	over a self-contained GameState) never goes near the loader -- the
	thread's mere act of exiting still does. Matches Microsoft's own
	DllMain guidance: never wait on a thread handle from DllMain or
	anything torn down as part of it.

	The fix is NOT to always detach() instead -- detach() has its own
	sharp edge: it returns before the worker has necessarily woken up and
	stopped touching m_jobMutex/m_jobCv/m_shuttingDown (members of THIS
	object), so if the destructor's caller then frees that storage while
	the worker is still mid-wait, that's a real use-after-free. That's
	fine for the one production caller (a function-local static destroyed
	exactly once, at process/DLL teardown, where the storage is never
	reused before the OS reclaims the whole address space moments later)
	but NOT fine for a normal-lifetime instance -- and
	tests/DominoHandEvalTests.cpp's own TestAsyncAdvisorPublishesMatchingResult()
	constructs exactly that: a short-lived local AsyncMoveAdvisor<int>
	that must be fully stopped, not just signaled, before its stack frame
	goes away.

	So the destructor distinguishes the two cases via
	AsyncMoveAdvisorDetail::g_processDetaching, a flag main.cpp's DllMain
	sets at the top of its DLL_PROCESS_DETACH case -- which runs BEFORE
	the CRT's automatic static-destruction pass that tears down function-
	local statics, so the flag is already true by the time this
	destructor fires from that path. join() (safe, correct, waits for
	real) is the default for every normal-lifetime instance, including
	the test's; detach() (never blocks, so safe under the loader lock) is
	used ONLY when g_processDetaching is set, where the impending process
	death makes its use-after-free risk moot.

	NOT yet live-tested (2026-09-13) -- written immediately after the
	synchronous version's own live freeze, as the more robust follow-up
	fix requested by the user rather than just tuning the existing bound.
	The join()/detach() split above was added the same day, before any
	live test of this file at all, after review caught the original
	unconditional join()'s DllMain deadlock risk.
*/

#pragma once

#include "DominoSearch.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace AsyncMoveAdvisorDetail
{
	// Set from main.cpp's DllMain at the top of its DLL_PROCESS_DETACH
	// case -- which runs BEFORE the CRT's automatic static-destruction
	// pass, so this is already true by the time a function-local static
	// AsyncMoveAdvisor's destructor fires from that path. See this file's
	// own header comment for why the destructor needs to know.
	inline std::atomic<bool> g_processDetaching{ false };

	// Test-only instrumentation (tests/DominoHandEvalTests.cpp): counts
	// currently-running worker threads across every AsyncMoveAdvisor
	// instance in the process. Incremented at the top of WorkerLoop(),
	// decremented right before it returns. Never read by production code
	// -- exists purely so a unit test can observe "has the worker
	// actually started" / "has it actually finished" without any real
	// in-game plumbing to watch.
	inline std::atomic<int> g_liveWorkerCount{ 0 };
}

template <typename Key>
class AsyncMoveAdvisor
{
public:
	struct Published
	{
		bool valid = false; // false until the worker has published its first result ever
		Key key{};
		DominoSearch::Recommendation rec;
	};

	AsyncMoveAdvisor() : m_worker(&AsyncMoveAdvisor::WorkerLoop, this) {}

	~AsyncMoveAdvisor()
	{
		{
			std::lock_guard<std::mutex> lock(m_jobMutex);
			m_shuttingDown = true;
		}
		m_jobCv.notify_all();
		if (!m_worker.joinable())
			return;
		if (AsyncMoveAdvisorDetail::g_processDetaching.load())
		{
			// join() here can deadlock against our own DllMain (see this
			// file's own header comment) -- detach() never blocks, so
			// it's safe unconditionally. Its own use-after-free risk is
			// moot specifically here: the only real caller reaching this
			// branch is a function-local static torn down at process/DLL
			// death, where the storage is never reused before the OS
			// reclaims the whole address space moments later.
			m_worker.detach();
		}
		else
		{
			// Normal lifetime (anything NOT torn down from
			// DllMain(DLL_PROCESS_DETACH), e.g. a plain local instance) --
			// join() is both safe and necessary here, so the worker is
			// guaranteed to have stopped touching this object's members
			// before they're destroyed.
			m_worker.join();
		}
	}

	// Test-only (tests/DominoHandEvalTests.cpp): makes the worker sleep
	// this long before running each job, so a test can force a job to
	// still be "in flight" at a known moment (e.g. right when a
	// destructor runs) without depending on FindBestMove()'s own real,
	// hard-to-predict timing. Zero (default) on every real call path --
	// production code never calls this.
	void Testing_SetArtificialJobDelay(std::chrono::milliseconds delay)
	{
		m_testDelayMs.store(delay.count(), std::memory_order_relaxed);
	}

	AsyncMoveAdvisor(const AsyncMoveAdvisor&) = delete;
	AsyncMoveAdvisor& operator=(const AsyncMoveAdvisor&) = delete;

	// Non-blocking. Overwrites any not-yet-started pending job -- only
	// the LATEST real decision matters, so an older queued-but-not-yet-
	// picked-up job is simply replaced. Skips the resubmission entirely
	// if an identical job is already pending or already the one most
	// recently published, since the caller (DetermineBestMove()) calls
	// this every tick until a matching result shows up.
	void SubmitJob(const Key& key, const DominoSearch::GameState& state, int mySeat, int maxDepth)
	{
		std::lock_guard<std::mutex> lock(m_jobMutex);
		if (m_hasPendingJob && m_pendingKey == key)
			return;
		m_pendingKey = key;
		m_pendingState = state;
		m_pendingSeat = mySeat;
		m_pendingDepth = maxDepth;
		m_hasPendingJob = true;
		m_jobCv.notify_one();
	}

	// Non-blocking. Returns whatever the worker most recently finished,
	// regardless of key -- the caller compares the key itself to decide
	// whether it's still the answer to the current real-world decision.
	Published GetLatest()
	{
		std::lock_guard<std::mutex> lock(m_resultMutex);
		return m_latest;
	}

private:
	void WorkerLoop()
	{
		AsyncMoveAdvisorDetail::g_liveWorkerCount.fetch_add(1, std::memory_order_relaxed);
		for (;;)
		{
			Key key{};
			DominoSearch::GameState state;
			int seat = -1;
			int depth = 0;
			{
				std::unique_lock<std::mutex> lock(m_jobMutex);
				m_jobCv.wait(lock, [this] { return m_shuttingDown || m_hasPendingJob; });
				if (m_shuttingDown)
				{
					AsyncMoveAdvisorDetail::g_liveWorkerCount.fetch_sub(1, std::memory_order_relaxed);
					return;
				}
				key = m_pendingKey;
				state = m_pendingState;
				seat = m_pendingSeat;
				depth = m_pendingDepth;
				m_hasPendingJob = false;
			}

			// Test-only hook, zero on every real call path -- see
			// Testing_SetArtificialJobDelay()'s own comment.
			if (auto delayMs = m_testDelayMs.load(std::memory_order_relaxed); delayMs > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

			// The only line in this whole file that does real work --
			// everything else is bookkeeping. Bounded by
			// DominoSearch::kDefaultNodeBudget regardless of branching,
			// see that constant's own comment.
			DominoSearch::Recommendation rec = DominoSearch::FindBestMove(state, seat, depth);

			{
				std::lock_guard<std::mutex> lock(m_resultMutex);
				m_latest.valid = true;
				m_latest.key = key;
				m_latest.rec = rec;
			}
		}
	}

	std::mutex m_jobMutex;
	std::condition_variable m_jobCv;
	Key m_pendingKey{};
	DominoSearch::GameState m_pendingState;
	int m_pendingSeat = -1;
	int m_pendingDepth = 0;
	bool m_hasPendingJob = false;
	bool m_shuttingDown = false;

	std::mutex m_resultMutex;
	Published m_latest;

	// Test-only, see Testing_SetArtificialJobDelay(). Milliseconds, 0 = off.
	std::atomic<long long> m_testDelayMs{ 0 };

	// Declared LAST: its constructor starts running WorkerLoop()
	// immediately on another thread, which touches every member above --
	// C++ constructs members in DECLARATION order regardless of
	// initializer-list order, so this guarantees they're all already
	// alive before the worker can touch them.
	std::thread m_worker;
};
