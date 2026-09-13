/*
	Entry point. Registers ScriptMain as a ScriptHookRDR2 script thread and
	wires up the keyboard handler. Vendored unchanged from PokerCheat/
	BlackjackCheat's own main.cpp -- see either project's header comment for
	why Config::Reload()/GamePointers::GetScriptThreads() run here (from
	DllMain) rather than lazily from ScriptMain's fiber.
*/

// AsyncMoveAdvisor.h (via DominoSearch.h) uses std::min/std::max -- must be
// included before the ScriptHookSDK header below, which pulls in raw
// <windows.h> (no NOMINMAX) and defines min/max as macros that would
// otherwise clobber those calls. DominoCheat.cpp avoids the same trap
// purely by include order; this mirrors it.
#include "AsyncMoveAdvisor.h"

#include "..\..\ScriptHookSDK\inc\main.h"
#include "script.h"
#include "keyboard.h"
#include "Config.h"
#include "GamePointers.h"

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		Config::Reload();
		GamePointers::GetScriptThreads();

		scriptRegister(hInstance, ScriptMain);
#ifdef _DEBUG
		// Release has no menu to drive with keystrokes at all (see
		// script.cpp) -- Debug-only.
		keyboardHandlerRegister(OnKeyboardMessage);
#endif
		break;
	case DLL_PROCESS_DETACH:
		// Must be set before anything else in this case -- it's read by
		// AsyncMoveAdvisor's destructor (see AsyncMoveAdvisor.h's own
		// header comment) when DetermineBestMove()'s function-local
		// static advisor is torn down later, as part of the CRT's
		// automatic static-destruction pass that runs after this
		// function returns.
		AsyncMoveAdvisorDetail::g_processDetaching.store(true);
		scriptUnregister(hInstance);
#ifdef _DEBUG
		keyboardHandlerUnregister(OnKeyboardMessage);
#endif
		break;
	}
	return TRUE;
}
