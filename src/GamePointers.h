/*
	Resolves the small set of raw engine pointers ScriptHookRDR2's SDK
	doesn't expose but this mod needs -- specifically the live pool of
	running rage::scrThread instances, so we can find dominoes_sp's own
	running thread and read its script-local variables directly. Vendored
	unchanged from PokerCheat/BlackjackCheat's own GamePointers.h -- fully
	generic, no dominoes-specific content.
*/

#pragma once

#include "..\external\RDR-Classes\script\scrThread.hpp"
#include "..\external\RDR-Classes\rage\atArray.hpp"
#include "..\external\RDR-Classes\rage\joaat.hpp"

#include <string>

namespace GamePointers
{
	// Lazily resolves and caches the address of RDR2.exe's live script
	// thread pool, via the same AOB signature HorseMenu uses. Returns
	// nullptr if the pattern isn't found (e.g. a game update changed the
	// surrounding code).
	rage::atArray<rage::scrThread*>* GetScriptThreads();

	// Finds the running scrThread for the given script name hash (e.g.
	// rage::Joaat("dominoes_sp")), or nullptr if it's not currently running.
	rage::scrThread* FindScriptThread(rage::joaat_t scriptHash);

	// Reads script-local slot `index` of `thread` as a raw pointer/value --
	// i.e. *(void**)(thread->m_Stack + index * 8). Returns nullptr if the
	// thread has no stack or the index is out of its declared stack size.
	void* ReadScriptLocal(rage::scrThread* thread, std::uint32_t index);

	// Returns the ADDRESS of script-local slot `index` -- i.e.
	// thread->m_Stack + index*8 itself, not what's stored there. Needed to
	// build pointers for native calls that take a script struct by
	// reference, since those structs live inline in the thread's own local
	// array, not behind a separately-stored pointer.
	void* GetScriptLocalAddress(rage::scrThread* thread, std::uint32_t index);

	// Dumps every script-local slot of `thread` (or just [startSlot,
	// startSlot+count) for the overload below) to a JSONL file at
	// `outPath` -- one JSON object per line, every plausible
	// interpretation of that slot's raw 8 bytes (i32/u32/i64/f32/hex), with
	// NO theory about what any given slot means. Same tool PokerCheat/
	// BlackjackCheat use to find real struct offsets by diffing two dumps
	// across a known state change (e.g. a tile placed) instead of
	// re-guessing one candidate offset at a time -- see either project's
	// docs/JOURNAL.md for the methodology this mod's own struct-layout
	// trace (DominoCheat.cpp's file header comment) still needs to go
	// through against a live game.
	bool DumpLocalStackJsonl(rage::scrThread* thread, const std::string& outPath);
	bool DumpLocalStackJsonl(rage::scrThread* thread, std::uint32_t startSlot, std::uint32_t count, const std::string& outPath);
}
