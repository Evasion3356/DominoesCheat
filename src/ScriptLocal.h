/*
	Chainable script-local field/array accessor, adapted from HorseMenu's
	own `game/rdr/ScriptLocal.hpp`/`ScriptGlobal.hpp`
	(D:\Backup\Stuff\RDR2 Shit\HorseMenu\src\game\rdr\) -- ported here
	because DominoCheat.cpp's original approach (hand-flattening every
	nested `.f_N` field access into one big constexpr absolute slot
	number, and manually re-deriving "+1 for the array's header word" by
	guessing per field) was exactly the failure mode that produced two of
	this project's three live-confirmed bugs: the boneyard and ped-array
	header words were both guessed wrong before live testing corrected
	them (see DominoCheat.cpp's file header comment).

	The key idea, taken directly from HorseMenu's own design (see e.g.
	`HorseMenu\src\game\features\self\TpToCamp.cpp`'s
	`PlayerList.At(i, 27).At(9)`): a script-local chain mirrors the
	decompile's OWN field-access syntax one step at a time, and the rule
	for which At() overload to use is MECHANICAL, not guessed:
	  - The decompile shows a plain nested field, `something.f_N` (no
	    brackets) -> use the single-argument `At(fieldOffset)`. No header
	    word is ever implied.
	  - The decompile shows bracket indexing, `something[i]` -> use the
	    two-argument `At(index, elementStride)`. This is exactly where
	    every SCR_ARRAY in this project family (RDR-Classes\script\
	    types.hpp's SCR_ARRAY -- a leading size/count word before the
	    element data) has turned out to have a header word so far --
	    seats, hand tiles, and the ped-tracking array all confirmed this
	    live. The "+1" is applied HERE, automatically, every time,
	    instead of being a fact a human has to remember (and periodically
	    forget/guess wrong) at each individual call site.

	This does NOT eliminate every kind of guesswork this project still
	needs live testing for -- it specifically only removes "does this
	particular bracket-indexed array have a header word" as an open
	question, since bracket syntax IS that question's answer by
	construction. It does NOT know a struct's own total size (kSeatStride
	below, e.g.) just from one field access -- that's a separate fact
	still derived from a live dump the way it was for BlackjackCheat's own
	stride corrections. And it does NOT help with a field accessed only
	through a raw pointer/native call rather than the VM's own `[i]`
	opcode (dominoes_sp's boneyard bitfield, read via
	`MISC::_IS_BIT_FLAG_SET(panParam0, bit)` rather than `panParam0[i]`)
	-- no bracket syntax exists there to read the answer off of, so that
	one still needs its header-word question settled by a live dump
	regardless of which accessor style is used to express the result
	afterward.
*/

#pragma once

#include "GamePointers.h"

#include <cstdint>

class ScriptLocal
{
	rage::scrThread* m_Thread;
	std::uint32_t m_Index;

public:
	constexpr ScriptLocal(rage::scrThread* thread, std::uint32_t index) :
		m_Thread(thread), m_Index(index)
	{
	}

	// Plain nested-struct field access -- use when the decompile shows
	// `.f_N` (no brackets) at this step. No header word implied.
	constexpr ScriptLocal At(std::uint32_t fieldOffset) const
	{
		return ScriptLocal(m_Thread, m_Index + fieldOffset);
	}

	// Array-element access -- use when the decompile shows `something[i]`
	// (brackets) at this step. Automatically skips the array's own
	// header/count word (the leading size word every SCR_ARRAY in this
	// project family has turned out to have -- see this file's header
	// comment) before advancing by `index * elementStride`.
	constexpr ScriptLocal At(std::uint32_t index, std::uint32_t elementStride) const
	{
		return ScriptLocal(m_Thread, m_Index + 1 + index * elementStride);
	}

	std::uint32_t Index() const { return m_Index; }

	// alignas(8) int -- the real value lives in the low 4 bytes of the
	// 8-byte slot (same convention every ReadInt()-style helper in this
	// project family already relies on).
	std::int32_t AsInt32() const
	{
		void* raw = GamePointers::ReadScriptLocal(m_Thread, m_Index);
		return static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(raw));
	}
};
