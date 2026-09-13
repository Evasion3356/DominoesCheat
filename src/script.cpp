/*
	DominoCheat -- ScriptHookRDR2 ASI mod advisor for the "dominoes_sp"
	single-player minigame. See DominoCheat.h/.cpp for the actual cheat
	module and that .cpp's header comment for the full (STATIC TRACE ONLY)
	struct-layout trace, and CLAUDE.md for where the already-decompiled
	dominoes_sp.ysc.c source lives.

	Press F12 in-game to open the test menu (NUMPAD 8/2 to move, NUMPAD 5 to
	select, NUMPAD 0/Backspace/F12 to back out -- same controls as
	PokerCheat/BlackjackCheat's own menus, F12 chosen specifically so all
	three can be loaded into the game at once without a key collision).
*/

#include "scriptmenu.h" // pulls in script.h (natives/types/enums/main) and keyboard.h
#include "Log.h"
#include "DominoCheat.h"
#include "Config.h"

namespace
{
#ifdef _DEBUG
	MenuController g_menuController;
	MenuBase* g_mainMenu = nullptr;

	void BuildMenu()
	{
		g_mainMenu = new MenuBase(new MenuItemTitle("DominoCheat"));
		g_mainMenu->AddItem(new MenuItemAction("Toggle Domino Cheat (see log)", DominoCheat::Toggle));
		g_mainMenu->AddItem(new MenuItemAction("Probe Table Struct (see log)", DominoCheat::ProbeTableStruct));
		g_mainMenu->AddItem(new MenuItemAction("Probe My Seat (see log)", DominoCheat::ProbeMySeat));
		g_mainMenu->AddItem(new MenuItemAction("Probe Seat Hands (see log)", DominoCheat::ProbeSeatHands));
		g_mainMenu->AddItem(new MenuItemAction("Probe Boneyard (see log)", DominoCheat::ProbeBoneyard));
		g_mainMenu->AddItem(new MenuItemAction("Probe Legal Moves (see log)", DominoCheat::ProbeLegalMoves));
		g_mainMenu->AddItem(new MenuItemAction("Probe Open Ends (see log)", DominoCheat::ProbeOpenEnds));
		g_mainMenu->AddItem(new MenuItemAction("Probe Best Move (see log)", DominoCheat::ProbeBestMove));
		g_mainMenu->AddItem(new MenuItemAction("Probe Tile Ownership (see log)", DominoCheat::ProbeTilePropOwnership));
		g_mainMenu->AddItem(new MenuItemAction("Probe Domino Skin (see log)", DominoCheat::ProbeDominoSkin));
		g_mainMenu->AddItem(new MenuItemAction("Dump Full Stack JSONL", DominoCheat::DumpFullStackJsonl));
		g_mainMenu->AddItem(new MenuItemAction("Reload Config (see log)", Config::Reload));
		g_menuController.RegisterMenu(g_mainMenu);
	}
#endif
}

void ScriptMain()
{
	Log::Write("DominoCheat started");

	// Config is loaded from DllMain now, not here -- see main.cpp.

#ifdef _DEBUG
	BuildMenu();
#else
	// No menu in Release to flip this from, so start already polling.
	DominoCheat::SetEnabled(true);
#endif

	while (true)
	{
#ifdef _DEBUG
		if (!g_menuController.HasActiveMenu() && MenuInput::MenuSwitchPressed())
			g_menuController.PushMenu(g_mainMenu);

		g_menuController.Update();
#endif
		DominoCheat::OnTick();

		WAIT(0);
	}
}
