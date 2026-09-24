/*
	Current scheduling: configurable wall-clock runtime (<= 0 = no
	deadline, run until solved or cancelled), progressive
	completed-depth publication, and generation cancellation. Identical
	pending/running/completed jobs are deduplicated. Production no longer
	uses the historical node cap mentioned in the original incident notes
	below. The caller cancels work outside the player's decision window.

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
	m_shuttingDown, wakes the condition variable) but only waits
	UNBOUNDED conditionally: an earlier version of this file waited
	unbounded unconditionally and reasoned that was safe because this
	worker never touches the loader (no LoadLibrary, no calling into a
	DLL mid-unload) -- but that reasoning missed a sharper problem. A
	function-local static's destructor runs as part of a
	statically-linked-CRT DLL's teardown from DllMain(DLL_PROCESS_DETACH),
	which Windows always calls under the process's loader lock, and a
	THREAD'S OWN EXIT triggers DLL_THREAD_DETACH notifications to every
	other loaded module -- which itself needs that same loader lock,
	regardless of what code the exiting thread ran. So waiting unbounded
	here can deadlock against our own DllMain even though this worker's
	actual workload (FindBestMove() over a self-contained GameState)
	never goes near the loader -- the thread's mere act of exiting still
	does. Matches Microsoft's own DllMain guidance: never wait on a
	thread handle from DllMain or anything torn down as part of it.

	The fix is NOT to just abandon the handle without waiting at all --
	that has its own sharp edge, and it's not just theoretical: this DLL
	is loaded under ScriptHookRDR2, and its ".dev" hot-reload mode does a
	REAL FreeLibrary()+LoadLibrary() cycle on this exact module while the
	game process keeps running (confirmed 2026-09-13, the same day this
	file was written) -- unlike real process exit, where "the whole
	address space is about to disappear together" reasoning is sound, a
	hot-reload genuinely unmaps this module while the process lives on.
	If the worker is still executing code from that mapping (or still
	touching m_jobMutex/m_jobCv/m_shuttingDown, members of THIS object)
	when that happens, that's a real crash risk, not a harmless no-op --
	and separately, ANY shorter-than-process-lifetime instance (e.g.
	tests/DominoHandEvalTests.cpp's own
	TestAsyncAdvisorPublishesMatchingResult(), a short-lived local
	AsyncMoveAdvisor<int>) needs the worker actually stopped, not just
	signaled, before its storage goes away regardless of DllMain at all.

	So the destructor picks one of two behaviors via
	AsyncMoveAdvisorDetail::g_processDetaching, a flag main.cpp's DllMain
	sets at the top of its DLL_PROCESS_DETACH case -- which runs BEFORE
	the CRT's automatic static-destruction pass that tears down function-
	local statics, so the flag is already true by the time this
	destructor fires from that path (hot-reload OR real process exit;
	main.cpp does not currently distinguish the two, see its own
	DLL_PROCESS_DETACH case comment):
	  - g_processDetaching == false (any normal-lifetime instance,
		including the test's): WaitForSingleObject(m_worker, INFINITE).
		Safe -- nothing here is running under the loader lock -- and
		necessary, since the worker must be fully stopped before this
		object's members are destroyed.
	  - g_processTerminating == true (process exit, not an eject): does
		nothing at all -- see that flag's own comment.
	  - g_processDetaching == true: a BOUNDED WaitForSingleObject on
		m_workerExited, an event the worker sets as its last action
		(kProcessDetachWaitTimeout, never INFINITE) instead -- waiting
		unbounded here can deadlock against our own DllMain (see above).
		This still gives the worker a real chance to actually finish for
		the hot-reload case (where it matters) while remaining bounded
		either way; CloseHandle() (never blocks, so safe unconditionally)
		is called afterward regardless of whether the wait succeeded or
		timed out.

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

// NOMINMAX guards this include specifically so this header can safely
// pull in <windows.h> for WaitForSingleObject() regardless of what order
// callers include it in relative to their own <windows.h> -- without it,
// windows.h's raw min/max macros would clobber DominoSearch.h's own
// std::min/std::max calls above if this header ever ended up included
// after them without NOMINMAX already defined (see main.cpp's own header
// comment for the same hazard from the other direction).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace AsyncMoveAdvisorDetail
{
	// Set from main.cpp's DllMain at the top of its DLL_PROCESS_DETACH
	// case -- which runs BEFORE the CRT's automatic static-destruction
	// pass, so this is already true by the time a function-local static
	// AsyncMoveAdvisor's destructor fires from that path. See this file's
	// own header comment for why the destructor needs to know.
	inline std::atomic<bool> g_processDetaching{ false };

	// Set by main.cpp's DllMain when DLL_PROCESS_DETACH comes from process
	// EXIT (non-null lpReserved), not an eject. By then Windows has already
	// killed every other thread -- possibly the worker while it held
	// m_jobMutex, in which case RequestStop() would block forever and hang
	// the game on exit. The destructor does nothing at all in that case:
	// the process's memory and handles are going away regardless.
	inline std::atomic<bool> g_processTerminating{ false };

	// Test-only instrumentation (tests/DominoHandEvalTests.cpp): counts
	// currently-running worker threads across every AsyncMoveAdvisor
	// instance in the process. Incremented at the top of WorkerLoop(),
	// decremented right before it returns. Never read by production code
	// -- exists purely so a unit test can observe "has the worker
	// actually started" / "has it actually finished" without any real
	// in-game plumbing to watch.
	inline std::atomic<int> g_liveWorkerCount{ 0 };

	// How long the destructor will block on WaitForSingleObject(m_worker,
	// ...) for the worker to finish on its own before giving up and
	// closing the handle anyway, when torn down via
	// DllMain(DLL_PROCESS_DETACH). Generous relative to
	// DominoSearch::kDefaultNodeBudget's own "a handful of milliseconds"
	// typical case, so a hot-reload (the scenario this exists for) almost
	// always sees the worker actually finish within the bound.
	inline constexpr std::chrono::milliseconds kProcessDetachWaitTimeout{ 500 };

	}

template <typename Key>
class AsyncMoveAdvisor
{
public:
	struct Published
	{
		bool valid = false; // false until the worker has published its first result ever
		bool finished = false;
		Key key{};
		DominoSearch::Recommendation rec;
	};

	// CreateThread (rather than std::thread) so this class owns a raw
	// Windows HANDLE directly -- m_worker is that HANDLE, closed exactly
	// once in the destructor. ThreadEntry is a static trampoline (a
	// non-static member function can't match LPTHREAD_START_ROUTINE's
	// plain DWORD WINAPI(LPVOID) signature) that casts lpParam back to
	// this instance and calls the real WorkerLoop().
	AsyncMoveAdvisor()
		: m_workerExited(::CreateEventW(nullptr, TRUE, FALSE, nullptr)),
		  m_worker(::CreateThread(nullptr, 0, &AsyncMoveAdvisor::ThreadEntry, this, 0, nullptr)) {}

	// Signals the worker to stop (sets m_shuttingDown, bumps the
	// generation so any in-flight Search() aborts on its very next node
	// check, wakes the condition variable) WITHOUT joining or detaching.
	// Idempotent -- safe to call multiple times (e.g. once early from
	// main.cpp's DllMain, then again implicitly via the destructor).
	// Exists so shutdown can be signalled as early as possible: the
	// destructor alone only fires from the CRT's static-destruction pass,
	// which for a DLL_PROCESS_DETACH-driven teardown runs AFTER
	// scriptUnregister() and any other DllMain work -- calling this
	// first gives the worker that entire extra window to actually finish
	// before the destructor's own bounded wait even begins.
	void RequestStop()
	{
		{
			std::lock_guard<std::mutex> lock(m_jobMutex);
			if (m_shuttingDown)
				return;
			m_shuttingDown = true;
			m_generation.fetch_add(1, std::memory_order_relaxed);
		}
		m_jobCv.notify_all();
	}

	~AsyncMoveAdvisor()
	{
		if (AsyncMoveAdvisorDetail::g_processTerminating.load())
			return; // see g_processTerminating -- touching the mutex here can hang process exit

		RequestStop();
		if (!m_worker)
		{
			if (m_workerExited)
				::CloseHandle(m_workerExited);
			return;
		}
		if (AsyncMoveAdvisorDetail::g_processDetaching.load())
		{
			// WaitForSingleObject-ing on this thread's own HANDLE from
			// DllMain can deadlock against our own DllMain (see this
			// file's own header comment) if allowed to block
			// indefinitely -- so this never waits INFINITE here. But it
			// doesn't just abandon the handle immediately either: give
			// the worker a bounded chance to actually finish first. A
			// bounded timeout keeps this exactly as safe as an immediate
			// abandon against the DllMain deadlock risk: it can never
			// block forever, only time out.
			//
			// Waits on m_workerExited, NOT the thread handle: a thread
			// handle only signals once the thread has fully exited, and
			// thread exit needs the loader lock this DllMain path is
			// holding -- so waiting on the handle here always ran out the
			// whole timeout. The worker sets m_workerExited as its very
			// last action, after which it runs no more of this module's
			// code that matters, so that's what actually needs waiting for.
			::WaitForSingleObject(m_workerExited ? m_workerExited : m_worker,
				static_cast<DWORD>(AsyncMoveAdvisorDetail::kProcessDetachWaitTimeout.count()));
			// Always close the handle here, whether the wait succeeded
			// or timed out -- CloseHandle() never blocks on the thread
			// itself finishing, it just releases OUR reference to it, so
			// it's safe unconditionally.
			::CloseHandle(m_worker);
		}
		else
		{
			// Normal lifetime (anything NOT torn down from
			// DllMain(DLL_PROCESS_DETACH), e.g. a plain local instance) --
			// waiting unbounded is both safe and necessary here, so the
			// worker is guaranteed to have stopped touching this object's
			// members before they're destroyed.
			::WaitForSingleObject(m_worker, INFINITE);
			::CloseHandle(m_worker);
		}
		if (m_workerExited)
			::CloseHandle(m_workerExited);
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

	int Testing_JobsStarted() const { return m_jobsStarted.load(std::memory_order_relaxed); }

	AsyncMoveAdvisor(const AsyncMoveAdvisor&) = delete;
	AsyncMoveAdvisor& operator=(const AsyncMoveAdvisor&) = delete;

	// Deduplicate pending, running AND completed requests. A new request
	// cancels the old generation and replaces any queued work.
	void SubmitJob(const Key& key, const DominoSearch::GameState& state, int mySeat, int maxDepth,
		std::chrono::milliseconds runtime = std::chrono::milliseconds(1000))
	{
		std::lock_guard<std::mutex> lock(m_jobMutex);
		if (m_hasRequest && m_pendingKey == key && m_pendingState == state &&
			m_pendingSeat == mySeat && m_pendingDepth == maxDepth && m_pendingRuntime == runtime)
			return;
		m_generation.fetch_add(1, std::memory_order_relaxed);
		m_pendingKey = key;
		m_pendingState = state;
		m_pendingSeat = mySeat;
		m_pendingDepth = maxDepth;
		m_pendingRuntime = runtime;
		m_hasPendingJob = true;
		m_hasRequest = true;
		{
			std::lock_guard<std::mutex> resultLock(m_resultMutex);
			m_latest = Published{};
		}
		m_jobCv.notify_one();
	}

	void Cancel()
	{
		std::lock_guard<std::mutex> lock(m_jobMutex);
		if (!m_hasRequest)
			return;
		m_generation.fetch_add(1, std::memory_order_relaxed);
		m_hasRequest = false;
		m_hasPendingJob = false;
		std::lock_guard<std::mutex> resultLock(m_resultMutex);
		m_latest = Published{};
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
	// Trampoline CreateThread actually calls (must match
	// LPTHREAD_START_ROUTINE's plain DWORD WINAPI(LPVOID) signature --
	// CreateThread cannot bind to a non-static member function directly).
	static DWORD WINAPI ThreadEntry(LPVOID param)
	{
		auto* self = static_cast<AsyncMoveAdvisor*>(param);
		self->WorkerLoop();
		// Last touch of this object -- see the destructor's detach path.
		if (self->m_workerExited)
			::SetEvent(self->m_workerExited);
		return 0;
	}

	void WorkerLoop()
	{
		AsyncMoveAdvisorDetail::g_liveWorkerCount.fetch_add(1, std::memory_order_relaxed);
		for (;;)
		{
			Key key{};
			DominoSearch::GameState state;
			int seat = -1;
			int depth = 0;
			std::chrono::milliseconds runtime{ 0 };
			std::uint64_t generation = 0;
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
				runtime = m_pendingRuntime;
				generation = m_generation.load(std::memory_order_relaxed);
				m_hasPendingJob = false;
			}
			m_jobsStarted.fetch_add(1, std::memory_order_relaxed);

			// Test-only hook, zero on every real call path -- see
			// Testing_SetArtificialJobDelay()'s own comment.
			if (auto delayMs = m_testDelayMs.load(std::memory_order_relaxed); delayMs > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

			auto publish = [this, &key, generation](const DominoSearch::Recommendation& rec, bool finished)
			{
				std::lock_guard<std::mutex> jobLock(m_jobMutex);
				if (m_shuttingDown || generation != m_generation.load(std::memory_order_relaxed))
					return;
				std::lock_guard<std::mutex> resultLock(m_resultMutex);
				m_latest.valid = true;
				m_latest.finished = finished;
				m_latest.key = key;
				m_latest.rec = rec;
			};
			DominoSearch::SearchControl control;
			// runtime <= 0 is Config's "unlimited" budget: leave the
			// default time_point::max() deadline in place.
			if (runtime.count() > 0)
				control.deadline = std::chrono::steady_clock::now() + runtime;
			control.generation = &m_generation;
			control.expectedGeneration = generation;
			control.publish = [&publish](const DominoSearch::Recommendation& rec) { publish(rec, false); };
			// No inherited node cap: only the deadline (if any),
			// cancellation, or a solved position ends production evaluation.
			auto rec = DominoSearch::FindBestMove(state, seat, depth, -1, &control);
			publish(rec, true);
		}
	}

	std::mutex m_jobMutex;
	std::condition_variable m_jobCv;
	Key m_pendingKey{};
	DominoSearch::GameState m_pendingState;
	int m_pendingSeat = -1;
	int m_pendingDepth = 0;
	std::chrono::milliseconds m_pendingRuntime{ 1000 };
	bool m_hasPendingJob = false;
	bool m_hasRequest = false;
	bool m_shuttingDown = false;
	std::atomic<std::uint64_t> m_generation{ 0 };
	std::atomic<int> m_jobsStarted{ 0 };

	std::mutex m_resultMutex;
	Published m_latest;

	// Test-only, see Testing_SetArtificialJobDelay(). Milliseconds, 0 = off.
	std::atomic<long long> m_testDelayMs{ 0 };

	// Manual-reset event the worker sets on its way out (see ThreadEntry).
	// Declared before m_worker so it exists before the thread starts.
	HANDLE m_workerExited;

	// Declared LAST: its constructor starts running WorkerLoop()
	// immediately on another thread, which touches every member above --
	// C++ constructs members in DECLARATION order regardless of
	// initializer-list order, so this guarantees they're all already
	// alive before the worker can touch them.
	HANDLE m_worker;
};
