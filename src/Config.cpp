// Same inipp-based load/save pattern as PokerCheat/BlackjackCheat's own
// Config.cpp -- see Config.h's header comment for the backstory. Trimmed to
// this mod's much smaller toggle set (see Config.h).

#include "Config.h"
#include "Log.h"

#include "..\external\inipp\inipp\inipp.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <string>
#include <exception>

namespace
{
	using Section = inipp::Ini<char>::Section;

	Config::Values g_values;
	bool g_loaded = false;

	const std::wstring& ResolveIniPath()
	{
		static const std::wstring path = []() -> std::wstring
		{
			HMODULE hModule = nullptr;
			GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&ResolveIniPath),
				&hModule);

			wchar_t modulePath[MAX_PATH] = {};
			GetModuleFileNameW(hModule, modulePath, MAX_PATH);

			wchar_t drive[_MAX_DRIVE], dir[_MAX_DIR];
			_wsplitpath_s(modulePath, drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);

			return std::wstring(drive) + dir + L"DominoCheat.ini";
		}();

		return path;
	}

	std::string NarrowPath(const std::wstring& wide)
	{
		if (wide.empty())
			return {};

		int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (size <= 0)
			return {};

		std::string narrow(static_cast<std::size_t>(size - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, narrow.data(), size, nullptr, nullptr);
		return narrow;
	}

	template <typename T>
	T GetOr(const Section& sec, const char* key, T def)
	{
		inipp::get_value(sec, key, def);
		return def;
	}

#ifdef _DEBUG
	void SetFloat(Section& sec, const char* key, float value)
	{
		std::ostringstream oss;
		oss << value;
		sec[key] = oss.str();
	}
#endif

	void SetBool(Section& sec, const char* key, bool value)
	{
		sec[key] = value ? "true" : "false";
	}

	void ReloadImpl()
	{
		inipp::Ini<char> ini;
		{
			std::ifstream is(ResolveIniPath());
			if (is)
				ini.parse(is);
		}

		Config::Values defaults;
		auto& general = ini.sections["General"];
		auto& advisor = ini.sections["Advisor"];
		g_values.AdvisorWallClockBudgetMs = Config::ClampWallClockBudgetMs(
			GetOr(advisor, "WallClockBudget", defaults.AdvisorWallClockBudgetMs));
		advisor["WallClockBudget"] = std::to_string(g_values.AdvisorWallClockBudgetMs);

		g_values.ShowOpponentHands = GetOr(general, "ShowOpponentHands", defaults.ShowOpponentHands);
		if (general.find("ShowBoneyard") != general.end())
		{
			g_values.ShowBoneyard = GetOr(general, "ShowBoneyard", defaults.ShowBoneyard);
		}
		else
		{
			// Preserve the user's setting from releases that called this a
			// prediction, then write it back using the accurate key below.
			g_values.ShowBoneyard = GetOr(general, "ShowBoneyardPrediction", defaults.ShowBoneyard);
		}
		general.erase("ShowBoneyardPrediction");
		g_values.ShowAdvice = GetOr(general, "ShowAdvice", defaults.ShowAdvice);
		g_values.ShowPlayableDomino = GetOr(general, "ShowPlayableDomino", defaults.ShowPlayableDomino);
		g_values.Language = GetOr(general, "Language", defaults.Language);

		SetBool(general, "ShowOpponentHands", g_values.ShowOpponentHands);
		SetBool(general, "ShowBoneyard", g_values.ShowBoneyard);
		SetBool(general, "ShowAdvice", g_values.ShowAdvice);
		SetBool(general, "ShowPlayableDomino", g_values.ShowPlayableDomino);
		general["Language"] = g_values.Language;

#ifdef _DEBUG
		auto& hud = ini.sections["HUD"];
		g_values.PanelX = GetOr(hud, "PanelX", defaults.PanelX);
		g_values.PanelY = GetOr(hud, "PanelY", defaults.PanelY);
		g_values.TextScale = GetOr(hud, "TextScale", defaults.TextScale);
		g_values.TitleTextScale = GetOr(hud, "TitleTextScale", defaults.TitleTextScale);
		g_values.OpponentHandBaseX = GetOr(hud, "OpponentHandBaseX", defaults.OpponentHandBaseX);
		g_values.OpponentHandBaseY = GetOr(hud, "OpponentHandBaseY", defaults.OpponentHandBaseY);
		g_values.OpponentHandStepY = GetOr(hud, "OpponentHandStepY", defaults.OpponentHandStepY);
		g_values.OpponentTileIconSpacingX = GetOr(hud, "OpponentTileIconSpacingX", defaults.OpponentTileIconSpacingX);
		g_values.OpponentTileIconWidth = GetOr(hud, "OpponentTileIconWidth", defaults.OpponentTileIconWidth);
		g_values.OpponentTileIconHeight = GetOr(hud, "OpponentTileIconHeight", defaults.OpponentTileIconHeight);
		g_values.WorldMarkerOffsetX = GetOr(hud, "WorldMarkerOffsetX", defaults.WorldMarkerOffsetX);
		g_values.WorldMarkerOffsetY = GetOr(hud, "WorldMarkerOffsetY", defaults.WorldMarkerOffsetY);
		g_values.WorldMarkerFontSize = GetOr(hud, "WorldMarkerFontSize", defaults.WorldMarkerFontSize);
		g_values.BoneyardX = GetOr(hud, "BoneyardX", defaults.BoneyardX);
		g_values.BoneyardY = GetOr(hud, "BoneyardY", defaults.BoneyardY);
		g_values.BoneyardTileIconLabelOffsetX = GetOr(hud, "BoneyardTileIconLabelOffsetX", defaults.BoneyardTileIconLabelOffsetX);
		g_values.BoneyardTileIconSpacingX = GetOr(hud, "BoneyardTileIconSpacingX", defaults.BoneyardTileIconSpacingX);
		g_values.BoneyardTileIconWidth = GetOr(hud, "BoneyardTileIconWidth", defaults.BoneyardTileIconWidth);
		g_values.BoneyardTileIconHeight = GetOr(hud, "BoneyardTileIconHeight", defaults.BoneyardTileIconHeight);
		g_values.MoveAdviceX = GetOr(hud, "MoveAdviceX", defaults.MoveAdviceX);
		g_values.MoveAdviceY = GetOr(hud, "MoveAdviceY", defaults.MoveAdviceY);
		SetFloat(hud, "PanelX", g_values.PanelX);
		SetFloat(hud, "PanelY", g_values.PanelY);
		SetFloat(hud, "TextScale", g_values.TextScale);
		SetFloat(hud, "TitleTextScale", g_values.TitleTextScale);
		SetFloat(hud, "OpponentHandBaseX", g_values.OpponentHandBaseX);
		SetFloat(hud, "OpponentHandBaseY", g_values.OpponentHandBaseY);
		SetFloat(hud, "OpponentHandStepY", g_values.OpponentHandStepY);
		SetFloat(hud, "OpponentTileIconSpacingX", g_values.OpponentTileIconSpacingX);
		SetFloat(hud, "OpponentTileIconWidth", g_values.OpponentTileIconWidth);
		SetFloat(hud, "OpponentTileIconHeight", g_values.OpponentTileIconHeight);
		SetFloat(hud, "WorldMarkerOffsetX", g_values.WorldMarkerOffsetX);
		SetFloat(hud, "WorldMarkerOffsetY", g_values.WorldMarkerOffsetY);
		SetFloat(hud, "WorldMarkerFontSize", g_values.WorldMarkerFontSize);
		SetFloat(hud, "BoneyardX", g_values.BoneyardX);
		SetFloat(hud, "BoneyardY", g_values.BoneyardY);
		SetFloat(hud, "BoneyardTileIconLabelOffsetX", g_values.BoneyardTileIconLabelOffsetX);
		SetFloat(hud, "BoneyardTileIconSpacingX", g_values.BoneyardTileIconSpacingX);
		SetFloat(hud, "BoneyardTileIconWidth", g_values.BoneyardTileIconWidth);
		SetFloat(hud, "BoneyardTileIconHeight", g_values.BoneyardTileIconHeight);
		SetFloat(hud, "MoveAdviceX", g_values.MoveAdviceX);
		SetFloat(hud, "MoveAdviceY", g_values.MoveAdviceY);
#endif

		{
			std::ofstream os(ResolveIniPath(), std::ios::trunc);
			if (os)
				ini.generate(os);
			else
				Log::Write("Config::Reload -- failed to open {} for writing", NarrowPath(ResolveIniPath()));
		}

		Log::Write("Config::Reload -- loaded from {} (ShowOpponentHands={} ShowBoneyard={} AdvisorWallClockBudgetMs={})",
			NarrowPath(ResolveIniPath()), g_values.ShowOpponentHands, g_values.ShowBoneyard,
			g_values.AdvisorWallClockBudgetMs);
	}
}

namespace Config
{
	void Reload()
	{
		try
		{
			ReloadImpl();
		}
		catch (const std::exception& e)
		{
			Log::Write("Config::Reload -- std::exception: {} -- keeping previous config values", e.what());
		}
		catch (...)
		{
			Log::Write("Config::Reload -- unknown non-std exception -- keeping previous config values");
		}

		g_loaded = true;
	}

	const Values& Get()
	{
		if (!g_loaded)
			Reload();

		return g_values;
	}
}
