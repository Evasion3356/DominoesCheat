/*
	Natives missing from (or mistyped in) the stock 2019 ScriptHookRDR2 SDK's
	natives.h. Vendored unchanged from PokerCheat/BlackjackCheat's own
	ExtraNatives.h -- see either project's own header comment for why
	UI::DRAW_TEXT/SET_TEXT_COLOR_RGBA are nullsub on game build 1491.50 and
	this UIDEBUG pair is the confirmed working replacement (including for
	Scaleform-style rich text tags in a LITERAL_STRING).

	Include this after natives.h/types.h (script.h does that ordering).
*/

#pragma once

namespace UIDEBUG
{
	static void _BG_DISPLAY_TEXT(char* text, float x, float y) { invoke<Void>(0x16794E044C9EFB58, text, x, y); }
	static void _BG_SET_TEXT_SCALE(float scaleX, float scaleY) { invoke<Void>(0xA1253A3C870B6843, scaleX, scaleY); }
	static void _BG_SET_TEXT_COLOR(int red, int green, int blue, int alpha) { invoke<Void>(0x16FA5CE47F184F1E, red, green, blue, alpha); }
}

namespace MINIGAME
{
	// Undocumented native dominoes_sp itself calls (dominoes_sp.ysc.c's
	// own func_347, line ~14691: `MINIGAME::_0x3AE451860F03CA8A(anParam0, anParam1)`)
	// to find which tiles in a hand are currently playable against the
	// board's open end(s) -- a read-only QUERY, distinct from the
	// separate placement-COMMIT native (DOMINOES_FUNCTION,
	// `_0x012027C28F421F46`) that actually mutates board state. Pass the
	// raw script-local address of a seat's hand array field as `hand`
	// (exactly what the script itself passes: `&(seat.f_4)`), and the
	// address of a caller-owned scratch buffer as `outCandidates` --
	// DominoCheat.cpp's FindPlayableTiles() builds that buffer to match
	// the exact layout the script's own consumers (func_352/func_353)
	// expect: an 8-bytes-per-"int" SCR_ARRAY (matching this whole
	// project's established script-local convention), header word = 15
	// (the fixed capacity every real call site in the decompile uses),
	// followed by 15 elements of 5 words each. Returns the number of
	// candidates actually filled in (element 0 of each = a hand index).
	// See DominoCheat.cpp's file header comment for the full derivation
	// and why this one, unlike the placement-commit native, didn't need
	// IDA-level reversing: it's a pure read-only call, and the script's
	// own two consumer functions already spell out the exact I/O
	// contract. CONFIRMED LIVE 2026-09-13 against a real board.
	static int _FIND_PLAYABLE_HAND_TILES(Any* hand, Any* outCandidates) { return invoke<int>(0x3AE451860F03CA8A, hand, outCandidates); }
}
