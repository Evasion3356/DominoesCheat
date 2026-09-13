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

		g_values.ShowOpponentHands = GetOr(general, "ShowOpponentHands", defaults.ShowOpponentHands);
		g_values.ShowBoneyardPrediction = GetOr(general, "ShowBoneyardPrediction", defaults.ShowBoneyardPrediction);

		SetBool(general, "ShowOpponentHands", g_values.ShowOpponentHands);
		SetBool(general, "ShowBoneyardPrediction", g_values.ShowBoneyardPrediction);

#ifdef _DEBUG
		auto& hud = ini.sections["HUD"];
		g_values.PanelX = GetOr(hud, "PanelX", defaults.PanelX);
		g_values.PanelY = GetOr(hud, "PanelY", defaults.PanelY);
		g_values.TextScale = GetOr(hud, "TextScale", defaults.TextScale);
		g_values.TitleTextScale = GetOr(hud, "TitleTextScale", defaults.TitleTextScale);
		SetFloat(hud, "PanelX", g_values.PanelX);
		SetFloat(hud, "PanelY", g_values.PanelY);
		SetFloat(hud, "TextScale", g_values.TextScale);
		SetFloat(hud, "TitleTextScale", g_values.TitleTextScale);
#endif

		{
			std::ofstream os(ResolveIniPath(), std::ios::trunc);
			if (os)
				ini.generate(os);
			else
				Log::Write("Config::Reload -- failed to open {} for writing", NarrowPath(ResolveIniPath()));
		}

		Log::Write("Config::Reload -- loaded from {} (ShowOpponentHands={} ShowBoneyardPrediction={})",
			NarrowPath(ResolveIniPath()), g_values.ShowOpponentHands, g_values.ShowBoneyardPrediction);
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
