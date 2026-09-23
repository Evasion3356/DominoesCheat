/*
	Entry point. Registers ScriptMain as a ScriptHookRDR2 script thread and
	wires up the keyboard handler, same pattern as PokerCheat/BlackjackCheat's
	own main.cpp.
*/

// AsyncMoveAdvisor.h (via DominoSearch.h) uses std::min/std::max -- must be
// included before the ScriptHookSDK header below, which pulls in raw
// <windows.h> (no NOMINMAX) and defines min/max as macros that would
// otherwise clobber those calls. DominoCheat.cpp avoids the same trap
// purely by include order; this mirrors it.
#include "AsyncMoveAdvisor.h"

#include "..\external\ScriptHookSDK\inc\main.h"
#include "script.h"
#include "keyboard.h"
#include "DominoCheat.h"

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		// Registration only -- Config::Reload() and the scrThread-pool scan
		// run first thing in ScriptMain instead (see script.cpp): DllMain
		// holds the loader lock and, with an early ASI loader, can run before
		// RDR2.exe has finished unpacking.
		scriptRegister(hInstance, ScriptMain);
#ifdef _DEBUG
		// Release has no menu to drive with keystrokes at all (see
		// script.cpp) -- Debug-only.
		keyboardHandlerRegister(OnKeyboardMessage);
#endif
		break;
	case DLL_PROCESS_DETACH:
		// Process exit (non-null lpReserved): every other thread is already
		// dead, possibly the advisor worker while holding its mutex. Only
		// flag it -- the advisor's destructor, which the CRT still runs after
		// this, then skips all locking and waiting (see AsyncMoveAdvisor.h).
		if (lpReserved)
		{
			AsyncMoveAdvisorDetail::g_processTerminating.store(true);
			break;
		}
		// Must be set before the rest of this case -- it's read by
		// AsyncMoveAdvisor's destructor (see AsyncMoveAdvisor.h's own
		// header comment) when DetermineBestMove()'s function-local
		// static advisor is torn down later, as part of the CRT's
		// automatic static-destruction pass that runs after this
		// function returns.
		AsyncMoveAdvisorDetail::g_processDetaching.store(true);
		// Signal the advisor's worker to stop NOW, not only once its
		// destructor fires from the CRT's later static-destruction pass
		// (that happens after this whole function returns) -- gives the
		// worker the entire rest of this handler (scriptUnregister()
		// included) as extra time to actually exit before the
		// destructor's own bounded wait begins. See DominoCheat.h's
		// PrepareForShutdown() comment for why this matters for the
		// .asi-locked-on-eject failure mode.
		DominoCheat::PrepareForShutdown();
		scriptUnregister(hInstance);
#ifdef _DEBUG
		keyboardHandlerUnregister(OnKeyboardMessage);
#endif
		break;
	}
	return TRUE;
}
