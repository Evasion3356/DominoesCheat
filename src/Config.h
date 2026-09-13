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

		// Standalone move-advice readout (DrawMoveAdviceStatus()/
		// DrawMoveSafetyStatus(), the centered "Best Move"/"Winning
		// Move!" + SAFE/RISKY/VERY RISKY text) -- independent of
		// ShowPlayableDomino below, so either can be shown without the
		// other. Both are already gated in DrawOverlay() on it being
		// mySeat's own turn AND turnSubState==4 specifically (CONFIRMED
		// LIVE 2026-09-13 as the real decision window -- see
		// kTurnSubStateFieldOffset's own header comment).
		bool ShowAdvice = true;

		// World-space "PLAY THIS ONE"/"WINNING MOVE" text drawn directly
		// over the recommended physical tile (DrawWorldMarkerOnTile())
		// -- independent of ShowAdvice above.
		bool ShowPlayableDomino = true;

		// Overrides which language the real-font HUD (per-seat hand
		// blocks, move-advice readout, world-space tile markers -- see
		// Localization.h/.cpp) shows in -- "auto" (the default) matches
		// the game's own current UI language automatically via
		// LANGUAGE::_GET_CURRENT_LANGUAGE_ID(), no setup needed. Set to
		// one of en-US/fr-FR/de-DE/it-IT/es-ES/pt-BR/pl-PL/ru-RU/ko-KR/
		// zh-TW/ja-JP/es-MX/zh-CN (the exact codes that native itself
		// maps to) to force a specific language regardless of the game's
		// own UI language; anything else unrecognized (a typo, or this
		// default "auto") falls back to that same auto-detect behavior.
		// Ported from PokerCheat's/BlackjackCheat's identical Language
		// key.
		std::string Language = "auto";

#ifdef _DEBUG
		// HUD text panel position/scale -- Debug-only dev-tuning values,
		// same convention as PokerCheat/BlackjackCheat's own PanelX/Y/
		// TextScale/TitleTextScale (not yet calibrated against a real
		// screen for this mod -- these are placeholder starting points).
		// Backs the RAW debug text panel only (DrawLine()'s own
		// UI::DRAW_TEXT pipeline, nullsub in Release -- see
		// DominoCheat.cpp's BgText() header comment).
		float PanelX = 0.015f;
		float PanelY = 0.30f;
		float TextScale = 0.32f;
		float TitleTextScale = 0.38f;

		// Real-font opponent-hand readout (DrawOpponentHandStatus(), see
		// DominoCheat.cpp) -- one row per occupied OPPONENT seat (never
		// your own -- you already see your own hand as real physical
		// tiles), positioned via the same dense-relative-seat-offset
		// technique PokerCheat's own DrawSeatCardIcons() uses to sit
		// "next to" each opponent's on-screen name/stack panel instead of
		// a fixed corner list keyed by raw seat index (see
		// ComputeDenseRowForSeat()'s own header comment for the full
		// rationale, and DominoCheat.cpp's file header comment for why
		// this replaced the original absolute-seat-index stack). Release
		// bakes in the same starting numbers as constexpr, same
		// convention as Poker/BlackjackCheat's own Release HUD constants.
		// PLACEHOLDER, NOT calibrated against dominoes_sp's own table/
		// camera at all -- copied from PokerCheat's OWN pre-calibration
		// starting numbers as a reasonable starting point, not this
		// mod's own derived values; retune live via Reload Config once a
		// real session shows where dominoes_sp's opponent seats actually
		// sit on screen (this mod has never run a
		// DrawCalibrationGrid()-style pass the way PokerCheat's were
		// derived, and a 4-seat table's layout may not even resemble
		// poker's 6-seat one).
		float OpponentHandBaseX = 0.18f;
		float OpponentHandBaseY = 0.83f;
		float OpponentHandStepY = -0.0915f;

		// Real 2D tile-icon sizing/spacing (DrawOpponentHandStatus(),
		// same function as above) -- drawn via GRAPHICS::DRAW_SPRITE
		// against the game's own real "dominos_set_N" texture dictionary
		// (CONFIRMED to exist -- see BuildDominoTileTextureName()'s own
		// header comment for the decompile + asset-manifest evidence
		// trail), replacing the plain FormatTile() text this row used to
		// draw. LabelOffsetX is how far right of the "Seat N" text label
		// the icon strip starts. CONFIRMED LIVE (2026-09-13, user-tuned
		// via Reload Config against a real table) -- Width/Height in
		// particular are notably smaller/more square than PokerCheat's
		// own portrait-card sizing this started from, confirming a
		// domino tile face really is a different shape than a playing
		// card (see this session's own header-comment note).
		float OpponentTileIconLabelOffsetX = 0.035f;
		float OpponentTileIconSpacingX = 0.015f;
		float OpponentTileIconWidth = 0.015f;
		float OpponentTileIconHeight = 0.045f;

		// World-space marker text (DrawWorldMarkerOnTile(), "PLAY THIS
		// ONE"/"WINNING MOVE") -- screen-space offset from the tile
		// prop's own projected coordinate, added because the $Font5/
		// UIDEBUG pipeline has no SET_TEXT_CENTRE equivalent (see that
		// function's own header comment). CONFIRMED LIVE (2026-09-13,
		// user-tuned via Reload Config against a real table) -- the
		// original hardcoded -0.06f/0 nudge sat over the tile's LEFT
		// side rather than centered; -0.03f/0 centers it correctly.
		// FontSize is the same $Font5 SIZE units DrawBgText() takes
		// everywhere else in this file (stored as float here only for
		// GetOr/SetFloat's uniform float plumbing -- truncated to int at
		// the actual draw call), still an untouched placeholder.
		float WorldMarkerOffsetX = -0.03f;
		float WorldMarkerOffsetY = 0.0f;
		float WorldMarkerFontSize = 26.0f;

		// Boneyard readout position (DrawBoneyardStatus()) -- no longer
		// anchored below the opponent-hand list (that list's rows now
		// scatter to per-seat calibrated positions instead of stacking
		// top-to-bottom), so this needs its own fixed spot. Same
		// placeholder-not-calibrated caveat as everything else on this
		// page.
		float BoneyardX = 0.02f;
		float BoneyardY = 0.30f;

		// Real 2D tile-icon sizing/spacing for the boneyard row (same
		// DrawBoneyardStatus(), 2026-09-13 -- replaced the plain
		// FormatTile() text list with the same real "dominos_set_N"
		// sprites DrawOpponentHandStatus() already draws, per user
		// request). Defaults started as OpponentTileIcon*'s own CONFIRMED
		// LIVE values above (same sprite asset, same aspect ratio) as a
		// jumping-off point. LabelOffsetX is how far right of the
		// "Boneyard (N):" text label the icon strip starts --
		// CONFIRMED LIVE (2026-09-13, user-tuned via Reload Config
		// against a real table): 0.035 sat the icon strip too close to
		// the label text, 0.055 clears it. SpacingX/Width/Height not yet
		// independently retuned for this row -- still the
		// OpponentTileIcon* starting values.
		float BoneyardTileIconLabelOffsetX = 0.055f;
		float BoneyardTileIconSpacingX = 0.015f;
		float BoneyardTileIconWidth = 0.015f;
		float BoneyardTileIconHeight = 0.045f;

		// Standalone move-advice readout position (DrawMoveAdviceStatus()/
		// DrawMoveSafetyStatus()) -- same "rough screen-center starting
		// point, not calibrated against anything" caveat as PokerCheat's
		// own WinPredictionX/Y / BlackjackCheat's AdviceX/Y, whose exact
		// values (0.48/0.5) this reuses as a starting point for
		// consistency across the mod family.
		float MoveAdviceX = 0.48f;
		float MoveAdviceY = 0.5f;
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
