/*
	Lightweight INI-backed config, same inipp-based approach and Debug-only-
	panel-tuning convention as PokerCheat/BlackjackCheat's own Config.h --
	see either project's header comment for the full inipp-vs-mINI backstory
	(why this doesn't need a worker thread, why it opens the file via a wide
	path resolved from this DLL's own module handle).

	Toggles here are intentionally minimal for this mod's first pass: the
	struct layout DominoCheat.cpp's file header comment documents is a
	STATIC TRACE ONLY (not yet live-confirmed against a running game, same
	starting point BlackjackCheat had before its own Session 6), and no
	board/turn-state struct has been traced at all yet -- so there's no
	"best move" advice to gate a toggle on yet, unlike BlackjackCheat's
	betting-advice/hit-stand-double toggles. Once the board is traced, add
	toggles here the same way those did.
*/

#pragma once

#include <string>

namespace Config
{
	struct Values
	{
		// Reveal every occupied opponent seat's real hand (the actual
		// "cheat" -- dominoes are normally played with hidden opponent
		// tiles, same hidden-information premise poker's hole cards and
		// blackjack's dealer hole card have).
		bool ShowOpponentHands = true;

		// Reveal the undrawn boneyard tiles remaining after the deal, in
		// draw order -- only meaningful when fewer than 4 seats are
		// occupied (a full 4-player game consumes the entire 28-tile set
		// at deal time, see DominoCheat.cpp's file header comment).
		bool ShowBoneyardPrediction = true;

#ifdef _DEBUG
		// HUD text panel position/scale -- Debug-only dev-tuning values,
		// same convention as PokerCheat/BlackjackCheat's own PanelX/Y/
		// TextScale/TitleTextScale (not yet calibrated against a real
		// screen for this mod -- these are placeholder starting points).
		float PanelX = 0.015f;
		float PanelY = 0.30f;
		float TextScale = 0.32f;
		float TitleTextScale = 0.38f;
#endif
	};

	// Returns the current config, triggering the very first load
	// automatically on first call.
	const Values& Get();

	// Re-reads DominoCheat.ini from disk, replacing the cached values.
	// Wired to the F12 menu's "Reload Config" item; also called once,
	// eagerly, from DllMain (see main.cpp).
	void Reload();
}
