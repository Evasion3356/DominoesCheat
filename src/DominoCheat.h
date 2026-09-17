#pragma once

// Dominoes advisor: reads live hand/boneyard/turn/score data out of
// dominoes_sp's own script memory (struct layout traced from the
// decompiled script, most of it now live-confirmed -- see
// DominoCheat.cpp's file header comment for the full derivation and
// confidence per field) and draws an on-screen HUD showing every occupied
// seat's real hand, the undrawn boneyard tiles, whose turn it is, and
// (via a read-only native call, see FindPlayableTiles() -- CONFIRMED LIVE)
// which of your own tiles are currently legal to play.
namespace DominoCheat
{
	extern bool Enabled;

	// Flips Enabled and logs the new state. Wired to the F12 menu's
	// "Toggle Domino Cheat" item.
	void Toggle();

	// Sets Enabled directly (idempotent, unlike Toggle()) -- Release's
	// ScriptMain calls this instead, same reasoning as PokerCheat/
	// BlackjackCheat's own SetEnabled (guards against ScriptHookRDR2
	// re-entering ScriptMain and silently toggling the advisor back off).
	void SetEnabled(bool enabled);

	// Called every ScriptMain tick regardless of Enabled state (Enabled is
	// checked internally). Draws the HUD when Enabled and dominoes_sp is
	// running; no-ops otherwise.
	void OnTick();

	// Signals the background move-advisor worker (see AsyncMoveAdvisor.h)
	// to stop as early as possible during shutdown. Called from
	// main.cpp's DllMain at the very top of DLL_PROCESS_DETACH -- BEFORE
	// scriptUnregister() and the CRT's later static-destruction pass that
	// runs AsyncMoveAdvisor's own destructor -- so the worker gets that
	// entire extra window to actually finish, on top of the destructor's
	// own bounded wait, rather than only learning shutdown is happening
	// once the destructor itself fires. Safe to call even if the advisor
	// was never constructed (no-ops) or is called again later from the
	// destructor path (idempotent). This is what keeps a slow in-flight
	// search from still being alive when the module unmaps, which is
	// what was leaving the .asi file locked on disk after eject.
	void PrepareForShutdown();

#ifdef _DEBUG
	// Everything below is wired to the F12 test menu only (see
	// script.cpp's BuildMenu(), Debug-only) -- dev-tuning/reversing tools
	// with no caller at all in a Release build. These exist specifically
	// because DominoCheat.cpp's struct offsets are UNCONFIRMED static-trace
	// candidates -- running these against a real game session (F12 while
	// actually seated at a dominoes table) is the concrete next step needed
	// to confirm or correct them, same iterative process PokerCheat/
	// BlackjackCheat's own docs/JOURNAL.md document.

	// Diagnostic: finds dominoes_sp's running scrThread and logs every
	// candidate struct field (Table/Round/SeatsHolder base slots, deck
	// cursor + boneyard tile-set-size check, every seat's occupancy marker
	// + hand) to DominoCheat.log. Wired to the F12 menu's "Probe Table
	// Struct" item.
	void ProbeTableStruct();

	// Diagnostic: logs DetermineBestMove()'s full recommendation --
	// which tile to play, which end, the resulting new open end, and how
	// many opponent tiles could answer it (an exact blocking measure
	// specifically because every session so far has been a full,
	// boneyard-empty 4-player game -- see that function's own comment).
	// Wired to the F12 menu's "Probe Best Move" item.
	void ProbeBestMove();

	// Diagnostic: reads Scene.f_6 (kSceneDominoSkinFieldOffset in
	// DominoCheat.cpp -- see its own header comment for the full
	// derivation trail), the current table's "dominos_set_N" skin
	// index, and logs it side-by-side with whatever
	// FindLoadedDominoSetDict() independently finds already streamed for
	// the SAME real table. CONFIRMED LIVE (2026-09-13): read Scene.f_6=5
	// against a real "dominos_set_6" table -- MATCH. Wired to the F12
	// menu's "Probe Domino Skin" item.
	void ProbeDominoSkin();

	// Diagnostic: dumps EVERY script-local slot of dominoes_sp's running
	// thread to DominoCheat_stackdump_YYYYMMDD_HHMMSS.jsonl (see
	// GamePointers::DumpLocalStackJsonl) -- one JSON object per slot, no
	// assumption about what any slot means. The single most useful tool
	// for correcting this file's UNCONFIRMED offsets: take one dump, note
	// a real on-screen tile, grep/jq the dump for its {low,high} pip
	// pattern the way BlackjackCheat's Session 6 found its own corrected
	// offsets. Wired to the F12 menu's "Dump Full Stack JSONL" item.
	void DumpFullStackJsonl();
#endif
}
