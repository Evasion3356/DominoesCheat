#include "Localization.h"
#include "Config.h"
#include "Log.h"
#include "script.h" // LANGUAGE::_GET_CURRENT_LANGUAGE_ID() (natives.h, via script.h)

#include <string>
#include <string_view>

namespace
{
	constexpr int kLanguageCount = static_cast<int>(Localization::Language::Count);

	// "Best Move" / "Winning Move!" -- the standalone move-advice
	// headline (see DrawMoveAdviceStatus() in DominoCheat.cpp). LLM-
	// assisted, not source-verified against a dominoes glossary the way
	// BlackjackCheat's HIT/STAND/DOUBLE/SPLIT table was (there's no
	// standard "dominoes HUD command" vocabulary to check against) --
	// fix a row directly here if a wording turns out to be wrong.
	constexpr std::string_view kBestMoveLabels[kLanguageCount] =
	{
		"Best Move",                  // en-US
		"Meilleur Coup",              // fr-FR
		"Bester Zug",                 // de-DE
		"Mossa Migliore",             // it-IT
		"Mejor Jugada",               // es-ES
		"Melhor Jogada",              // pt-BR
		"Najlepszy Ruch",             // pl-PL
		"Лучший Ход",                 // ru-RU
		"최고의 수",                    // ko-KR
		"最佳出牌",                     // zh-TW
		"ベストムーブ",                 // ja-JP
		"Mejor Jugada",               // es-MX
		"最佳出牌",                     // zh-CN
	};

	constexpr std::string_view kWinningMoveLabels[kLanguageCount] =
	{
		"Winning Move!",              // en-US
		"Coup Gagnant !",             // fr-FR
		"Gewinnzug!",                 // de-DE
		"Mossa Vincente!",            // it-IT
		"¡Jugada Ganadora!",          // es-ES
		"Jogada Vencedora!",          // pt-BR
		"Zwycięski Ruch!",            // pl-PL
		"Победный Ход!",              // ru-RU
		"승리하는 수!",                 // ko-KR
		"獲勝出牌！",                   // zh-TW
		"勝利の一手！",                 // ja-JP
		"¡Jugada Ganadora!",          // es-MX
		"获胜出牌！",                   // zh-CN
	};

	// "TWO SPOTS FIT!" -- appended to the move-advice line when the
	// recommended tile also legally matches a different open end (see
	// Localization.h's AmbiguousEndWarning() header comment). LLM-assisted
	// like the other short exclamatory labels here -- fix a row directly
	// if a wording is off for a given language.
	constexpr std::string_view kAmbiguousEndWarnings[kLanguageCount] =
	{
		"TWO SPOTS FIT!",             // en-US
		"DEUX EMPLACEMENTS !",        // fr-FR
		"ZWEI STELLEN PASSEN!",       // de-DE
		"DUE POSTI POSSIBILI!",       // it-IT
		"¡ENCAJA EN DOS SITIOS!",     // es-ES
		"CABE EM DOIS LUGARES!",      // pt-BR
		"PASUJE W DWÓCH MIEJSCACH!",  // pl-PL
		"ПОДХОДИТ В ДВУХ МЕСТАХ!",    // ru-RU
		"두 곳에 맞습니다!",              // ko-KR
		"兩處都能放！",                  // zh-TW
		"二か所に置けます！",              // ja-JP
		"¡ENCAJA EN DOS SITIOS!",     // es-MX
		"两处都能放！",                  // zh-CN
	};

	// SAFE / RISKY / VERY RISKY -- this mod's own invented blocking-safety
	// qualifier (see Localization.h's ClassifyBlockingSafety() header
	// comment), same category as BlackjackCheat's BET LOW/MEDIUM/HIGH
	// table -- no real glossary to verify against, grammar/tone pass
	// only.
	constexpr int kBlockingSafetyLabelCount = 3;
	constexpr std::string_view kBlockingSafetyLabels[kLanguageCount][kBlockingSafetyLabelCount] =
	{
		{ "Safe", "Risky", "Very Risky" },                 // en-US
		{ "Sûr", "Risqué", "Très Risqué" },                // fr-FR
		{ "Sicher", "Riskant", "Sehr Riskant" },           // de-DE
		{ "Sicura", "Rischiosa", "Molto Rischiosa" },      // it-IT
		{ "Seguro", "Arriesgado", "Muy Arriesgado" },      // es-ES
		{ "Seguro", "Arriscado", "Muito Arriscado" },      // pt-BR
		{ "Bezpieczny", "Ryzykowny", "Bardzo Ryzykowny" }, // pl-PL
		{ "Безопасно", "Рискованно", "Очень Рискованно" }, // ru-RU
		{ "안전", "위험", "매우 위험" },                        // ko-KR
		{ "安全", "有風險", "非常危險" },                       // zh-TW
		{ "安全", "リスクあり", "非常に危険" },                  // ja-JP
		{ "Seguro", "Arriesgado", "Muy Arriesgado" },      // es-MX
		{ "安全", "有风险", "非常危险" },                       // zh-CN
	};

	// "PLAY THIS ONE!" / "WINNING MOVE!" -- world-space text drawn
	// directly over the recommended physical tile (DrawWorldMarkerOnTile()).
	constexpr std::string_view kPlayThisTileMarkers[kLanguageCount] =
	{
		"PLAY THIS ONE!",             // en-US
		"JOUEZ CELUI-CI !",           // fr-FR
		"SPIELE DIESEN!",             // de-DE
		"GIOCA QUESTA!",              // it-IT
		"¡JUEGA ÉSTA!",               // es-ES
		"JOGUE ESTA!",                // pt-BR
		"ZAGRAJ TĘ!",                 // pl-PL
		"СЫГРАЙ ЭТУ!",                // ru-RU
		"이거 내세요!",                 // ko-KR
		"打這張！",                     // zh-TW
		"これを出せ！",                 // ja-JP
		"¡JUEGA ÉSTA!",               // es-MX
		"打这张！",                     // zh-CN
	};

	constexpr std::string_view kWinningTileMarkers[kLanguageCount] =
	{
		"WINNING MOVE!",              // en-US
		"COUP GAGNANT !",             // fr-FR
		"GEWINNZUG!",                 // de-DE
		"MOSSA VINCENTE!",            // it-IT
		"¡JUGADA GANADORA!",          // es-ES
		"JOGADA VENCEDORA!",          // pt-BR
		"ZWYCIĘSKI RUCH!",            // pl-PL
		"ПОБЕДНЫЙ ХОД!",              // ru-RU
		"승리하는 수!",                 // ko-KR
		"獲勝出牌！",                   // zh-TW
		"勝利の一手！",                 // ja-JP
		"¡JUGADA GANADORA!",          // es-MX
		"获胜出牌！",                   // zh-CN
	};

	// "PLAY HERE!" -- world-space text drawn over the physical BOARD
	// POSITION to place the recommended tile on (see BoardTracker in
	// DominoCheat.cpp), distinct from kPlayThisTileMarkers above (which
	// marks the tile in your hand, not where it goes on the table).
	constexpr std::string_view kPlayHereMarkers[kLanguageCount] =
	{
		"PLAY HERE!",                 // en-US
		"JOUEZ ICI !",                // fr-FR
		"HIER SPIELEN!",              // de-DE
		"GIOCA QUI!",                 // it-IT
		"¡JUEGA AQUÍ!",               // es-ES
		"JOGUE AQUI!",                // pt-BR
		"ZAGRAJ TUTAJ!",              // pl-PL
		"СЫГРАЙ ЗДЕСЬ!",              // ru-RU
		"여기 내세요!",                  // ko-KR
		"打在這裡！",                    // zh-TW
		"ここに出せ！",                  // ja-JP
		"¡JUEGA AQUÍ!",               // es-MX
		"打在这里！",                    // zh-CN
	};

	// "Boneyard" -- DrawBoneyardStatus()'s own label.
	constexpr std::string_view kBoneyardWords[kLanguageCount] =
	{
		"Boneyard",                   // en-US
		"Pioche",                     // fr-FR
		"Talon",                      // de-DE
		"Tallone",                    // it-IT
		"Boliche",                    // es-ES -- real dominoes term for the draw pile (confirmed common usage, e.g. domino.rules sites; regional variants exist but this is the most widely used)
		"Monte",                      // pt-BR
		"Rezerwa",                    // pl-PL
		"Базар",                      // ru-RU -- common informal Russian dominoes term for the draw pile
		"보유타일",                     // ko-KR
		"備用骨牌",                     // zh-TW
		"山札",                        // ja-JP
		"Boliche",                    // es-MX
		"备用骨牌",                     // zh-CN
	};

	Localization::Language g_current = Localization::Language::English;
	bool g_resolved = false; // true once Refresh() has actually run at least once

	Localization::Language ClampLanguage(std::int32_t raw)
	{
		if (raw < 0 || raw >= kLanguageCount)
			return Localization::Language::English;
		return static_cast<Localization::Language>(raw);
	}

	// DominoCheat.ini's [General] Language override -- "auto" (the
	// default) defers to the game's own current UI language; anything
	// else must match one of these exact codes, the same ones
	// LANGUAGE::_GET_CURRENT_LANGUAGE_ID()'s own return-value mapping
	// uses. Unrecognized text (a typo, or "auto" itself) falls back to
	// English via Refresh()'s caller. Ported verbatim from PokerCheat's/
	// BlackjackCheat's own TryParseOverride().
	bool TryParseOverride(const std::string& code, Localization::Language& out)
	{
		if (code == "en-US") { out = Localization::Language::English; return true; }
		if (code == "fr-FR") { out = Localization::Language::French; return true; }
		if (code == "de-DE") { out = Localization::Language::German; return true; }
		if (code == "it-IT") { out = Localization::Language::Italian; return true; }
		if (code == "es-ES") { out = Localization::Language::Spanish; return true; }
		if (code == "pt-BR") { out = Localization::Language::PortugueseBrazilian; return true; }
		if (code == "pl-PL") { out = Localization::Language::Polish; return true; }
		if (code == "ru-RU") { out = Localization::Language::Russian; return true; }
		if (code == "ko-KR") { out = Localization::Language::Korean; return true; }
		if (code == "zh-TW") { out = Localization::Language::ChineseTraditional; return true; }
		if (code == "ja-JP") { out = Localization::Language::Japanese; return true; }
		if (code == "es-MX") { out = Localization::Language::SpanishMexican; return true; }
		if (code == "zh-CN") { out = Localization::Language::ChineseSimplified; return true; }
		return false;
	}
}

namespace Localization
{
	void Refresh()
	{
		const std::string& languageOverride = Config::Get().Language;

		Language resolved;
		if (TryParseOverride(languageOverride, resolved))
		{
			g_current = resolved;
		}
		else
		{
			// Covers "auto" (the documented default) and any typo'd
			// override alike -- both should fall back to the game's own
			// current language rather than silently forcing English.
			std::int32_t raw = LANGUAGE::_GET_CURRENT_LANGUAGE_ID();
			g_current = ClampLanguage(raw);
		}

		g_resolved = true;
		Log::Write("Localization::Refresh -> language index {} (ini override='{}')", static_cast<int>(g_current), languageOverride);
	}

	Language Current()
	{
		if (!g_resolved)
			Refresh();

		return g_current;
	}

	std::string_view BestMoveLabel() { return kBestMoveLabels[static_cast<int>(Current())]; }
	std::string_view WinningMoveLabel() { return kWinningMoveLabels[static_cast<int>(Current())]; }
	std::string_view AmbiguousEndWarning() { return kAmbiguousEndWarnings[static_cast<int>(Current())]; }

	BlockingSafety ClassifyBlockingSafety(std::int32_t opponentRespondCount)
	{
		if (opponentRespondCount <= 0)
			return BlockingSafety::Safe;
		if (opponentRespondCount <= 2)
			return BlockingSafety::Risky;
		return BlockingSafety::VeryRisky;
	}

	std::string_view BlockingSafetyLabel(BlockingSafety safety)
	{
		return kBlockingSafetyLabels[static_cast<int>(Current())][static_cast<int>(safety)];
	}

	std::string_view PlayThisTileMarker() { return kPlayThisTileMarkers[static_cast<int>(Current())]; }
	std::string_view WinningTileMarker() { return kWinningTileMarkers[static_cast<int>(Current())]; }
	std::string_view PlayHereMarker() { return kPlayHereMarkers[static_cast<int>(Current())]; }

	std::string_view BoneyardWord() { return kBoneyardWords[static_cast<int>(Current())]; }

	std::string_view LanguageCode(Language lang)
	{
		switch (lang)
		{
			case Language::English: return "en-US";
			case Language::French: return "fr-FR";
			case Language::German: return "de-DE";
			case Language::Italian: return "it-IT";
			case Language::Spanish: return "es-ES";
			case Language::PortugueseBrazilian: return "pt-BR";
			case Language::Polish: return "pl-PL";
			case Language::Russian: return "ru-RU";
			case Language::Korean: return "ko-KR";
			case Language::ChineseTraditional: return "zh-TW";
			case Language::Japanese: return "ja-JP";
			case Language::SpanishMexican: return "es-MX";
			case Language::ChineseSimplified: return "zh-CN";
			default: return "?";
		}
	}
}
