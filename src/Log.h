/*
	Minimal file logger backed by spdlog (see external/spdlog), vendored
	unchanged (aside from the logger name/filename) from PokerCheat/
	BlackjackCheat's own Log.h -- see either project's own header comment
	for the full backstory (header-only mode, why {}-style fmt placeholders
	instead of printf %-style, why this stays synchronous in both Debug and
	Release). Writes DominoCheat.log next to the .asi.
*/

#pragma once

#define SPDLOG_HEADER_ONLY
#include "..\external\spdlog\include\spdlog\spdlog.h"
#include "..\external\spdlog\include\spdlog\sinks\basic_file_sink.h"

#include <memory>

namespace Log
{
	namespace detail
	{
		inline std::shared_ptr<spdlog::logger> CreateLogger()
		{
			try
			{
				auto logger = spdlog::basic_logger_mt("DominoCheat", "DominoCheat.log");
				logger->set_pattern("[%H:%M:%S.%e] %v");
				logger->flush_on(spdlog::level::info);
				return logger;
			}
			catch (const spdlog::spdlog_ex&)
			{
				return nullptr;
			}
		}

		inline const std::shared_ptr<spdlog::logger>& GetLogger()
		{
			static std::shared_ptr<spdlog::logger> logger = CreateLogger();
			return logger;
		}
	}

	template <typename... Args>
	void Write(spdlog::format_string_t<Args...> fmt, Args&&... args)
	{
		if (const auto& logger = detail::GetLogger())
			logger->info(fmt, std::forward<Args>(args)...);
	}
}
