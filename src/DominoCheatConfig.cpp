#include "Config.h"

// The library's side of Config: the live values and their option table. The
// INI loading (Reload) is the ASI's, in Config.cpp.
namespace DominoCheat::Config
{
	Values& Mutable()
	{
		static Values values;
		return values;
	}

	const Values& Get()
	{
		return Mutable();
	}

	std::span<const Option> Options()
	{
		using enum Option::Kind;
		Values& v = Mutable();
		static const Option options[] = {
			{ "showopponenthands", "HUD", "Show Opponent Hands", "Shows every opponent's tiles.", Bool, &v.ShowOpponentHands },
			{ "showboneyard", "HUD", "Show Boneyard", "Shows the undrawn tiles in draw order (fewer than 4 players).", Bool, &v.ShowBoneyard },
			{ "showadvice", "HUD", "Show Advice", "On your turn, the best move and how safe it is.", Bool, &v.ShowAdvice },
			{ "showplayabledomino", "HUD", "Show Playable Domino", "Marks the recommended tile and where to play it on the table.", Bool, &v.ShowPlayableDomino },
			{ "advisorwallclockbudgetms", "Advisor", "Search Time (ms)", "How long the move search may think per decision; 0 is unlimited.", Int, &v.AdvisorWallClockBudgetMs, 0.0f, 30000.0f, 50.0f },
#ifdef _DEBUG
			{ "panelx", "HUD Layout", "Panel X", "Text panel position.", Float, &v.PanelX, 0.0f, 1.0f, 0.005f },
			{ "panely", "HUD Layout", "Panel Y", "Text panel position.", Float, &v.PanelY, 0.0f, 1.0f, 0.005f },
			{ "textscale", "HUD Layout", "Text Scale", "Text panel scale.", Float, &v.TextScale, 0.1f, 1.0f, 0.01f },
			{ "titletextscale", "HUD Layout", "Title Text Scale", "Text panel title scale.", Float, &v.TitleTextScale, 0.1f, 1.0f, 0.01f },
			{ "opponenthandbasex", "HUD Layout", "Opponent Hands X", "Opponent tiles, first row.", Float, &v.OpponentHandBaseX, 0.0f, 1.0f, 0.001f },
			{ "opponenthandbasey", "HUD Layout", "Opponent Hands Y", "Opponent tiles, first row.", Float, &v.OpponentHandBaseY, 0.0f, 1.0f, 0.001f },
			{ "opponenthandstepy", "HUD Layout", "Opponent Hands Row Step", "Vertical step between seat rows.", Float, &v.OpponentHandStepY, -0.5f, 0.5f, 0.001f },
			{ "opponenttileiconspacingx", "HUD Layout", "Opponent Tile Spacing", "Space between tiles.", Float, &v.OpponentTileIconSpacingX, 0.0f, 0.2f, 0.001f },
			{ "opponenttileiconwidth", "HUD Layout", "Opponent Tile Width", "Tile icon size.", Float, &v.OpponentTileIconWidth, 0.0f, 0.2f, 0.001f },
			{ "opponenttileiconheight", "HUD Layout", "Opponent Tile Height", "Tile icon size.", Float, &v.OpponentTileIconHeight, 0.0f, 0.2f, 0.001f },
			{ "worldmarkeroffsetx", "HUD Layout", "Marker Offset X", "Table marker offset.", Float, &v.WorldMarkerOffsetX, -0.2f, 0.2f, 0.001f },
			{ "worldmarkeroffsety", "HUD Layout", "Marker Offset Y", "Table marker offset.", Float, &v.WorldMarkerOffsetY, -0.2f, 0.2f, 0.001f },
			{ "worldmarkerfontsize", "HUD Layout", "Marker Font Size", "Table marker text size.", Float, &v.WorldMarkerFontSize, 6.0f, 80.0f, 1.0f },
			{ "boneyardx", "HUD Layout", "Boneyard X", "Boneyard strip position.", Float, &v.BoneyardX, 0.0f, 1.0f, 0.001f },
			{ "boneyardy", "HUD Layout", "Boneyard Y", "Boneyard strip position.", Float, &v.BoneyardY, 0.0f, 1.0f, 0.001f },
			{ "boneyardtileiconlabeloffsetx", "HUD Layout", "Boneyard Label Offset", "Boneyard label offset.", Float, &v.BoneyardTileIconLabelOffsetX, -0.2f, 0.2f, 0.001f },
			{ "boneyardtileiconspacingx", "HUD Layout", "Boneyard Tile Spacing", "Space between boneyard tiles.", Float, &v.BoneyardTileIconSpacingX, 0.0f, 0.2f, 0.001f },
			{ "boneyardtileiconwidth", "HUD Layout", "Boneyard Tile Width", "Boneyard tile size.", Float, &v.BoneyardTileIconWidth, 0.0f, 0.2f, 0.001f },
			{ "boneyardtileiconheight", "HUD Layout", "Boneyard Tile Height", "Boneyard tile size.", Float, &v.BoneyardTileIconHeight, 0.0f, 0.2f, 0.001f },
			{ "moveadvicex", "HUD Layout", "Advice X", "Move advice position.", Float, &v.MoveAdviceX, 0.0f, 1.0f, 0.005f },
			{ "moveadvicey", "HUD Layout", "Advice Y", "Move advice position.", Float, &v.MoveAdviceY, 0.0f, 1.0f, 0.005f },
#endif
		};
		return options;
	}
}
