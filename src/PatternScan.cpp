// Vendored unchanged from PokerCheat/BlackjackCheat's own PatternScan.cpp --
// generic AOB scanner, no dominoes-specific content. See PatternScan.h for
// the pattern syntax and GamePointers.cpp for the one signature this project
// uses it for.

#include "PatternScan.h"

#include <windows.h>
#include <vector>
#include <string>
#include <cstring>

namespace
{
	struct ParsedPattern
	{
		std::vector<std::uint8_t> bytes;
		std::vector<bool> mask; // true = must match, false = wildcard
	};

	ParsedPattern Parse(std::string_view pattern)
	{
		ParsedPattern parsed;

		size_t i = 0;
		while (i < pattern.size())
		{
			while (i < pattern.size() && pattern[i] == ' ')
				i++;
			if (i >= pattern.size())
				break;

			if (pattern[i] == '?')
			{
				parsed.bytes.push_back(0);
				parsed.mask.push_back(false);
				i++;
				// tolerate "??" as a single wildcard token
				if (i < pattern.size() && pattern[i] == '?')
					i++;
			}
			else
			{
				parsed.bytes.push_back(static_cast<std::uint8_t>(std::stoul(std::string(pattern.substr(i, 2)), nullptr, 16)));
				parsed.mask.push_back(true);
				i += 2;
			}
		}

		return parsed;
	}
}

namespace PatternScan
{
	std::optional<std::uintptr_t> FindInMainModule(std::string_view pattern)
	{
		auto base = reinterpret_cast<std::uint8_t*>(GetModuleHandle(nullptr));
		if (!base)
			return std::nullopt;

		auto dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		auto ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dosHeader->e_lfanew);
		std::size_t imageSize = ntHeaders->OptionalHeader.SizeOfImage;

		ParsedPattern parsed = Parse(pattern);
		if (parsed.bytes.empty())
			return std::nullopt;

		std::size_t patternLen = parsed.bytes.size();
		if (imageSize < patternLen)
			return std::nullopt;

		// Anchor on the first non-wildcard byte and use memchr to jump
		// straight to each candidate occurrence of it instead of testing
		// every byte offset in the image by hand -- see PokerCheat's own
		// PatternScan.cpp header comment for the hitch this fixed.
		std::size_t firstConcrete = 0;
		while (firstConcrete < patternLen && !parsed.mask[firstConcrete])
			firstConcrete++;

		if (firstConcrete == patternLen)
			return std::nullopt;

		std::uint8_t* searchStart = base + firstConcrete;
		std::size_t remaining = imageSize - firstConcrete;

		for (;;)
		{
			if (remaining < patternLen - firstConcrete)
				break;

			std::size_t searchableForAnchor = remaining - (patternLen - firstConcrete - 1);
			void* found = memchr(searchStart, parsed.bytes[firstConcrete], searchableForAnchor);
			if (!found)
				break;

			std::uint8_t* candidateAnchor = static_cast<std::uint8_t*>(found);
			std::uint8_t* candidateStart = candidateAnchor - firstConcrete;

			bool matched = true;
			for (std::size_t j = 0; j < patternLen; j++)
			{
				if (parsed.mask[j] && candidateStart[j] != parsed.bytes[j])
				{
					matched = false;
					break;
				}
			}

			if (matched)
				return reinterpret_cast<std::uintptr_t>(candidateStart);

			std::size_t advanced = static_cast<std::size_t>(candidateAnchor - searchStart) + 1;
			searchStart += advanced;
			remaining -= advanced;
		}

		return std::nullopt;
	}

	std::uintptr_t ResolveRip(std::uintptr_t matchAddress, int operandOffset)
	{
		auto operandAddr = matchAddress + operandOffset;
		std::int32_t displacement = *reinterpret_cast<std::int32_t*>(operandAddr);
		return operandAddr + sizeof(std::int32_t) + displacement;
	}
}
