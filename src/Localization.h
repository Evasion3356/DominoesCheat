/*
	Localizes the handful of strings DominoCheat's real-font Release HUD
	draws on screen -- the standalone move-advice readout (BestMoveLabel()/
	WinningMoveLabel(), see DrawMoveAdviceStatus() in DominoCheat.cpp), its
	blocking-safety qualifier (BlockingSafetyLabel() -- SAFE/RISKY/VERY
	RISKY is this mod's OWN invented HUD concept, same category as
	BlackjackCheat's BET LOW/MEDIUM/HIGH, not a real dominoes term to
	verify against a glossary), the world-space tile markers
	(PlayThisTileMarker()/WinningTileMarker(), see DrawWorldMarkerOnTile()),
	each opponent-hand row's header word (SeatWord(), see
	DrawOpponentHandStatus()), and the boneyard readout's label
	(BoneyardWord(), see DrawBoneyardStatus()). Everything else this mod draws (the
	Debug-only raw text panel, DrawLine()/DrawPanel() in DominoCheat.cpp)
	is #ifdef _DEBUG-only -- a dev diagnostic surface, never shown to an
	end user -- and stays English-only; not worth translating. Ported
	from PokerCheat's/BlackjackCheat's own Localization.h/.cpp -- same
	approach, different (smaller) string set, since most of what this mod
	shows is language-agnostic tile notation ("[3|5]") rather than
	sentences.

	Language is auto-detected from the game's own current UI language via
	LANGUAGE::_GET_CURRENT_LANGUAGE_ID() (see Localization.cpp), so a
	player sees this mod's HUD in whatever language they already have
	RDR2 itself set to, with no config needed. DominoCheat.ini's [General]
	Language key can override that per Config.h's header comment on
	Config::Values::Language, for anyone who wants the HUD in a different
	language than their game UI.

	Translations beyond English are LLM-assisted, not yet reviewed by a
	native speaker per language -- if a wording is wrong for a given
	language, fix the corresponding row in Localization.cpp's tables
	directly, no other file needs to change.

	$Font5 (this mod's text pipeline as of this session -- see
	DominoCheat.cpp's BgText()/ExtraNatives.h's UIDEBUG header comment) was
	already confirmed to render all 13 of these languages correctly,
	including CJK, by PokerCheat's own Session 20/21 font testing on this
	exact game build (1491.50) -- see PokerCheat's docs/PITFALLS.md. All
	three mods use the identical UIDEBUG::_BG_DISPLAY_TEXT/$Font5 pipeline,
	so that finding carries over verbatim; no separate font test tool was
	built here.
*/

#pragma once

#include <cstdint>

namespace Localization
{
	// Matches LANGUAGE::_GET_CURRENT_LANGUAGE_ID()'s own return value
	// mapping exactly -- same enum PokerCheat's/BlackjackCheat's own
	// Localization.h use (confirmed against rdr3-nativedb-data/
	// natives.json's comment on native hash 0xDB917DA5C6835FCC), these are
	// the 13 languages RDR2 itself ships with, not an arbitrary list.
	enum class Language : std::int32_t
	{
		English = 0,             // en-US
		French = 1,               // fr-FR
		German = 2,               // de-DE
		Italian = 3,              // it-IT
		Spanish = 4,              // es-ES
		PortugueseBrazilian = 5,  // pt-BR
		Polish = 6,               // pl-PL
		Russian = 7,              // ru-RU
		Korean = 8,               // ko-KR
		ChineseTraditional = 9,   // zh-TW
		Japanese = 10,            // ja-JP
		SpanishMexican = 11,      // es-MX
		ChineseSimplified = 12,   // zh-CN

		Count = 13
	};

	// Re-resolves the active language from DominoCheat.ini's [General]
	// Language override (Config::Get().Language) if set to anything other
	// than "auto", else from the game's own current UI language. MUST be
	// called from within ScriptHookRDR2's script fiber (i.e. from
	// OnTick() or a menu action running inside ScriptMain's loop), never
	// from DllMain -- unlike Config::Reload(), this calls a real game
	// native (LANGUAGE::_GET_CURRENT_LANGUAGE_ID()) and natives aren't
	// safe to invoke outside the registered script thread's own
	// cooperative fiber. Current() below lazily calls this on first use
	// instead, the same "g_loaded" pattern Config::Get() already uses, so
	// nothing needs to call this explicitly except the Debug F12 menu's
	// "Reload Config" item (to re-pick the language immediately after an
	// ini edit, without waiting for the next natural call) -- see
	// script.cpp.
	void Refresh();

	// Returns the cached language, resolving it via Refresh() on first
	// call if nothing has resolved it yet.
	Language Current();

	// Headline for a legal-but-not-winning move recommendation. Uses
	// Current() for the language.
	const char* BestMoveLabel();

	// Headline when the recommended move empties your hand outright.
	// Uses Current() for the language.
	const char* WinningMoveLabel();

	// This mod's own invented "how safely can opponents answer this"
	// qualifier -- Safe (DetermineBestMove()'s opponentRespondCount == 0,
	// i.e. no opponent tile can answer the resulting open end), Risky
	// (1-2 such tiles), VeryRisky (3+). Same category as BlackjackCheat's
	// BET LOW/MEDIUM/HIGH -- not a real dominoes term, just a threshold
	// this mod picked; adjust ClassifyBlockingSafety()'s cutoffs directly
	// if they read wrong in practice.
	// Since the 2026-09-13 decision-engine pass, DrawMoveSafetyStatus()
	// keys these labels on the SEARCH VERDICT rather than the 1-ply
	// respond count: Safe = the recommended line wins the round/game
	// under the model, Risky = undecided within the search budget or a
	// scoreless tie, Very Risky = every line loses (the advice is the
	// least-bad one). ClassifyBlockingSafety() below is the original
	// count-based mapping, kept for reference.
	enum class BlockingSafety { Safe, Risky, VeryRisky };
	BlockingSafety ClassifyBlockingSafety(std::int32_t opponentRespondCount);
	const char* BlockingSafetyLabel(BlockingSafety safety);

	// World-space text drawn directly over the recommended physical tile
	// (see DrawWorldMarkerOnTile()) -- distinct from BestMoveLabel()/
	// WinningMoveLabel() above (those head the standalone centered
	// readout; these sit on the 3D tile itself, so they're phrased as a
	// short imperative/exclamation instead of a label).
	const char* PlayThisTileMarker();
	const char* WinningTileMarker();

	// Per-seat hand row header word (DrawOpponentHandStatus()) -- "Seat",
	// paired at the call site with the seat number as a plain digit, the
	// same language-agnostic-numeral convention this file's tile
	// notation ("[3|5]") already relies on. Opponent-only (see
	// DrawOpponentHandStatus()'s own comment for why your own seat is
	// never shown this way) so there's no "(you)" variant to localize.
	const char* SeatWord();

	// Boneyard readout label (DrawBoneyardStatus()).
	const char* BoneyardWord();

	// Short language code ("en-US", "fr-FR", ...) for a given language --
	// purely for logging, not used by anything Release-facing.
	const char* LanguageCode(Language lang);
}
