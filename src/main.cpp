/*
	Entry point. Registers ScriptMain as a ScriptHookRDR2 script thread and
	wires up the keyboard handler. Vendored unchanged from PokerCheat/
	BlackjackCheat's own main.cpp -- see either project's header comment for
	why Config::Reload()/GamePointers::GetScriptThreads() run here (from
	DllMain) rather than lazily from ScriptMain's fiber.
*/

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
		scriptUnregister(hInstance);
#ifdef _DEBUG
		keyboardHandlerUnregister(OnKeyboardMessage);
#endif
		break;
	}
	return TRUE;
}
