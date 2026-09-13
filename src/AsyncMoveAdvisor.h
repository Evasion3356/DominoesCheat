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
	sharp edge, and it's not just theoretical: this DLL is loaded under
	ScriptHookRDR2, and its ".dev" hot-reload mode does a REAL
	FreeLibrary()+LoadLibrary() cycle on this exact module while the game
	process keeps running (confirmed 2026-09-13, the same day this file
	was written) -- unlike real process exit, where detach()'s "storage
	is never reused" reasoning is sound because the whole address space
	is about to disappear together, a hot-reload genuinely unmaps this
	module while the process lives on. If the worker is still executing
	code from that mapping (or still touching m_jobMutex/m_jobCv/
	m_shuttingDown, members of THIS object) when that happens, that's a
	real crash risk, not a harmless no-op -- and separately, ANY
	shorter-than-process-lifetime instance (e.g.
	tests/DominoHandEvalTests.cpp's own
	TestAsyncAdvisorPublishesMatchingResult(), a short-lived local
	AsyncMoveAdvisor<int>) needs the worker actually stopped, not just
	signaled, before its storage goes away regardless of DllMain at all.

	So the destructor picks one of three behaviors via
	AsyncMoveAdvisorDetail::g_processDetaching, a flag main.cpp's DllMain
	sets at the top of its DLL_PROCESS_DETACH case -- which runs BEFORE
	the CRT's automatic static-destruction pass that tears down function-
	local statics, so the flag is already true by the time this
	destructor fires from that path (hot-reload OR real process exit;
	main.cpp does not currently distinguish the two, see its own
	DLL_PROCESS_DETACH case comment):
	  - g_processDetaching == false (any normal-lifetime instance,
	    including the test's): plain join(). Safe -- nothing here is
	    running under the loader lock -- and necessary, since the worker
	    must be fully stopped before this object's members are destroyed.
	  - g_processDetaching == true: NEVER join() (the deadlock risk above
	    applies unconditionally here, real exit or hot-reload alike) --
	    but also never a bare, immediate detach(). Instead, poll
	    m_workerExited (a plain atomic WorkerLoop sets immediately before
	    it returns, BEFORE any of the CRT's own loader-lock-needing
	    thread-exit machinery even begins) for up to
	    kProcessDetachWaitTimeout. This is NOT the same hazard as
	    join(): join() blocks on the OS thread HANDLE, which only
	    signals once that loader-lock-guarded machinery has fully run;
	    polling our own atomic instead only waits for WorkerLoop's body
	    to finish, a point strictly BEFORE the worker could ever need the
	    lock we're already holding, so it can't deadlock against us no
	    matter how long it takes. Gives the worker a real chance to
	    actually finish for the hot-reload case (where it matters) while
	    remaining bounded either way; detach() (never blocks, so safe
	    unconditionally) is called afterward regardless of whether the
	    wait succeeded or timed out, since the std::thread object itself
	    still needs joining or detaching or its own destructor will
	    std::terminate.

	NOT yet live-tested (2026-09-13) -- written immediately after the
	synchronous version's own live freeze, as the more robust follow-up
	fix requested by the user rather than just tuning the existing bound.
	The join()/wait-then-detach() split above was added the same day,
	before any live test of this file at all: first as a plain
	join()-vs-detach() split (after review caught the original
	unconditional join()'s DllMain deadlock risk), then refined into the
	bounded wait once the user's own live testing via
	ScriptHookRDR2.dev's hot-reload surfaced that unconditional detach()
	was leaving the .asi file locked on disk after eject -- exactly the
	"worker still running when the module unmaps" hazard this comment
	describes above, not yet itself re-tested against that exact repro.
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

	// How long the destructor will wait (polling m_workerExited, never
	// blocking on the thread handle -- see this file's own header
	// comment) for the worker to finish on its own before giving up and
	// detach()ing anyway, when torn down via DllMain(DLL_PROCESS_DETACH).
	// Generous relative to DominoSearch::kDefaultNodeBudget's own "a
	// handful of milliseconds" typical case, so a hot-reload (the
	// scenario this exists for) almost always sees the worker actually
	// gone rather than merely detached.
	inline constexpr std::chrono::milliseconds kProcessDetachWaitTimeout{ 500 };
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
			// file's own header comment) -- so never join() in this
			// branch. But don't just detach() immediately either: give
			// the worker a bounded chance to actually finish first, by
			// polling m_workerExited (never the thread handle) -- see
			// the header comment for exactly why that's safe against the
			// same deadlock join() risks, and why it matters for
			// ScriptHookRDR2.dev's hot-reload specifically (unlike real
			// process exit, that scenario genuinely unmaps this module
			// while the process keeps running).
			auto deadline = std::chrono::steady_clock::now() + AsyncMoveAdvisorDetail::kProcessDetachWaitTimeout;
			while (!m_workerExited.load() && std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			// Always detach() here, whether the worker already exited or
			// the wait timed out: detach() never blocks, so it's safe
			// unconditionally, and the std::thread object itself must be
			// joined or detached or its destructor will std::terminate().
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
					// Set BEFORE returning -- this is the signal the
					// destructor's bounded wait polls (see its own
					// comment), and it must land before anything in this
					// thread's own exit sequence could need the loader
					// lock, which starts only once this function actually
					// returns.
					m_workerExited.store(true, std::memory_order_release);
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

	// Set by WorkerLoop() immediately before it returns on shutdown;
	// polled (never joined) by the destructor's process-detaching
	// branch. See both call sites' own comments for why this is safe
	// where join() would not be.
	std::atomic<bool> m_workerExited{ false };

	// Test-only, see Testing_SetArtificialJobDelay(). Milliseconds, 0 = off.
	std::atomic<long long> m_testDelayMs{ 0 };

	// Declared LAST: its constructor starts running WorkerLoop()
	// immediately on another thread, which touches every member above --
	// C++ constructs members in DECLARATION order regardless of
	// initializer-list order, so this guarantees they're all already
	// alive before the worker can touch them.
	std::thread m_worker;
};
