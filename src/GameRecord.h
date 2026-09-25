/*
	GameRecord.h -- the game log's JSONL line format, shared by the mod
	(which writes DominoCheat_games.jsonl next to DominoCheat.log, Debug
	build only -- see DominoCheat.cpp's GameRecorder section) and
	tests/DominoHandEvalTests.cpp (which replays lines copied from it into
	tests/fixtures/games.jsonl). No game dependency. Ported from
	BlackjackCheat's RoundRecord.h (same flat-line writer and reader).

	Three line types, each a flat JSON object on its own line so any single
	line can be copied into the fixture file as a self-contained test case.
	Tiles are flat pip pairs: [low,high,low,high,...].

	  {"type":"decision", ...}  one per move I made while advice was shown.
	      The full search input (WriteState() below: mySeat, rules,
	      occupied, hand0..hand3, ends, boneyard in draw order, scores,
	      pointsTarget, rootBonus -- 8 per hand tile: ends 0-6, opening --,
	      passStreak), the advice showing when I
	      played ("rec", "recEnd", "recResult", "recOutcome", "recPoints",
	      "recExact", "recDepth"), what I played ("played"), "followed",
	      and when checkable, "postEnds" (open ends after the move) and
	      "wrongEnd" (the recommended tile went on the other end).
	  {"type":"npcMove", ...}   one per opponent play the scripted-AI model
	      predicted: rules, seat, that seat's hand, the open ends, the
	      game's own candidate list in native order (candHand, candPlaced,
	      candEndTotal, candGrid = f1,f2,f3 triples), the model's pick
	      ("predicted") and the real play ("played", "match").
	  {"type":"round", ...}     one per finished round, written at the next
	      deal: rules, mySeat, occupied, each seat's final hand (final0..3),
	      scoresBefore/scoresAfter, pointsTarget, buyIn, "gameOver" (the
	      scores reset), and this round's decision/prediction tallies.

	To turn a line into a test: copy it into tests/fixtures/games.jsonl and
	add "expectTile":[low,high] (decision: what FindBestMove() must
	recommend, optionally "expectEnd"; npcMove: what SelectCandidate()
	must pick -- usually a copy of "played"). Lines with no expect* key
	are skipped.

	The reader only understands this file's own flat format (a top-level
	key's int, bool, string or int array) -- not general JSON.
*/

#pragma once

#include "DominoHandEval.h"
#include "DominoAiPolicy.h"
#include "DominoSearch.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace GameRecord
{
	using DominoHandEval::Tile;

	class JsonLine
	{
	public:
		JsonLine& Add(std::string_view key, std::int64_t value)
		{
			Key(key);
			m_out << value;
			return *this;
		}

		JsonLine& Add(std::string_view key, bool value)
		{
			Key(key);
			m_out << (value ? "true" : "false");
			return *this;
		}

		JsonLine& Add(std::string_view key, std::string_view value)
		{
			Key(key);
			Quoted(value);
			return *this;
		}

		JsonLine& Add(std::string_view key, const char* value)
		{
			return Add(key, std::string_view(value));
		}

		JsonLine& Add(std::string_view key, const std::int32_t* values, int count)
		{
			Key(key);
			m_out << '[';
			for (int i = 0; i < count; i++)
				m_out << (i ? "," : "") << values[i];
			m_out << ']';
			return *this;
		}

		JsonLine& Add(std::string_view key, const std::vector<std::int32_t>& values)
		{
			return Add(key, values.data(), static_cast<int>(values.size()));
		}

		JsonLine& AddTiles(std::string_view key, const Tile* tiles, int count)
		{
			Key(key);
			m_out << '[';
			for (int i = 0; i < count; i++)
				m_out << (i ? "," : "") << tiles[i].low << ',' << tiles[i].high;
			m_out << ']';
			return *this;
		}

		JsonLine& AddTile(std::string_view key, const Tile& tile)
		{
			return AddTiles(key, &tile, 1);
		}

		std::string Str() const
		{
			return m_out.str() + "}";
		}

	private:
		void Key(std::string_view key)
		{
			m_out << (m_first ? "{" : ",");
			m_first = false;
			Quoted(key);
			m_out << ':';
		}

		void Quoted(std::string_view text)
		{
			m_out << '"';
			for (char c : text)
			{
				if (c == '"' || c == '\\')
					m_out << '\\';
				m_out << c;
			}
			m_out << '"';
		}

		std::ostringstream m_out;
		bool m_first = true;
	};

	// Position just past `"key":` (skipping spaces), or npos. Only matches
	// a key, not the same text inside a string value, since a key is
	// always directly preceded by '{' or ','.
	inline std::size_t FindValue(std::string_view line, std::string_view key)
	{
		const std::string needle = "\"" + std::string(key) + "\"";
		for (std::size_t pos = line.find(needle); pos != std::string_view::npos; pos = line.find(needle, pos + 1))
		{
			std::size_t before = pos;
			while (before > 0 && line[before - 1] == ' ')
				before--;
			if (before == 0 || (line[before - 1] != '{' && line[before - 1] != ','))
				continue;

			std::size_t after = pos + needle.size();
			while (after < line.size() && line[after] == ' ')
				after++;
			if (after >= line.size() || line[after] != ':')
				continue;
			after++;
			while (after < line.size() && line[after] == ' ')
				after++;
			return after;
		}
		return std::string_view::npos;
	}

	inline bool ParseInt(std::string_view line, std::size_t& pos, std::int32_t& out)
	{
		bool negative = pos < line.size() && line[pos] == '-';
		if (negative)
			pos++;
		if (pos >= line.size() || line[pos] < '0' || line[pos] > '9')
			return false;
		std::int64_t value = 0;
		while (pos < line.size() && line[pos] >= '0' && line[pos] <= '9')
			value = value * 10 + (line[pos++] - '0');
		out = static_cast<std::int32_t>(negative ? -value : value);
		return true;
	}

	inline bool GetInt(std::string_view line, std::string_view key, std::int32_t& out)
	{
		std::size_t pos = FindValue(line, key);
		return pos != std::string_view::npos && ParseInt(line, pos, out);
	}

	inline bool GetBool(std::string_view line, std::string_view key, bool& out)
	{
		std::size_t pos = FindValue(line, key);
		if (pos == std::string_view::npos)
			return false;
		if (line.substr(pos, 4) == "true")
		{
			out = true;
			return true;
		}
		if (line.substr(pos, 5) == "false")
		{
			out = false;
			return true;
		}
		return false;
	}

	inline bool GetString(std::string_view line, std::string_view key, std::string& out)
	{
		std::size_t pos = FindValue(line, key);
		if (pos == std::string_view::npos || line[pos] != '"')
			return false;
		out.clear();
		for (pos++; pos < line.size(); pos++)
		{
			if (line[pos] == '"')
				return true;
			if (line[pos] == '\\' && pos + 1 < line.size())
				pos++;
			out += line[pos];
		}
		return false;
	}

	inline bool GetIntArray(std::string_view line, std::string_view key, std::vector<std::int32_t>& out)
	{
		std::size_t pos = FindValue(line, key);
		if (pos == std::string_view::npos || line[pos] != '[')
			return false;
		out.clear();
		pos++;
		while (pos < line.size())
		{
			while (pos < line.size() && (line[pos] == ' ' || line[pos] == ','))
				pos++;
			if (pos < line.size() && line[pos] == ']')
				return true;
			std::int32_t value = 0;
			if (!ParseInt(line, pos, value))
				return false;
			out.push_back(value);
		}
		return false;
	}

	// Flat [low,high,...] pairs; every tile must be a valid double-six tile.
	inline bool GetTiles(std::string_view line, std::string_view key, std::vector<Tile>& out)
	{
		std::vector<std::int32_t> flat;
		if (!GetIntArray(line, key, flat) || flat.size() % 2 != 0)
			return false;
		out.clear();
		for (std::size_t i = 0; i < flat.size(); i += 2)
		{
			Tile tile{ flat[i], flat[i + 1] };
			if (!tile.IsValid())
				return false;
			out.push_back(tile);
		}
		return true;
	}

	inline bool GetTile(std::string_view line, std::string_view key, Tile& out)
	{
		std::vector<Tile> tiles;
		if (!GetTiles(line, key, tiles) || tiles.size() != 1)
			return false;
		out = tiles[0];
		return true;
	}

	inline DominoAiPolicy::Rules RulesFromName(std::string_view name)
	{
		using DominoAiPolicy::Rules;
		for (Rules rules : { Rules::Block, Rules::Draw, Rules::AllThrees, Rules::AllFives })
			if (name == DominoAiPolicy::RulesName(rules))
				return rules;
		return Rules::Unknown;
	}

	inline constexpr std::string_view kHandKeys[DominoSearch::kMaxSeats] = { "hand0", "hand1", "hand2", "hand3" };

	// The exact board (DominoSearch::Board): "exactBoard", then when set
	// "board" (regular ends as [pip,dbl,...]) and "spinner" [pip, open long
	// sides, unplayed sides] ([-1,0,0] before any double), "placed".
	inline void WriteBoard(JsonLine& line, const DominoSearch::Board& board, bool exact)
	{
		line.Add("exactBoard", exact);
		if (!exact)
			return;
		std::vector<std::int32_t> ends;
		for (int e = 0; e < board.count; e++)
		{
			ends.push_back(board.ends[static_cast<std::size_t>(e)].pip);
			ends.push_back(board.ends[static_cast<std::size_t>(e)].dbl ? 1 : 0);
		}
		std::int32_t spinner[3] = { board.spinnerPip, board.spinnerLong, board.emptySides };
		line.Add("board", ends).Add("spinner", spinner, 3).Add("placed", board.placed);
	}

	// False if the line has no exact board (or a malformed one).
	inline bool ReadBoard(std::string_view line, DominoSearch::Board& board)
	{
		bool exact = false;
		if (!GetBool(line, "exactBoard", exact) || !exact)
			return false;
		std::vector<std::int32_t> ends, spinner;
		if (!GetIntArray(line, "board", ends) || ends.size() % 2 != 0 || ends.size() / 2 > board.ends.size() ||
			!GetIntArray(line, "spinner", spinner) || spinner.size() != 3)
			return false;
		board = DominoSearch::Board{};
		for (std::size_t i = 0; i < ends.size(); i += 2)
			board.ends[static_cast<std::size_t>(board.count++)] = { static_cast<std::int8_t>(ends[i]), ends[i + 1] != 0 };
		board.spinnerPip = static_cast<std::int8_t>(spinner[0]);
		board.spinnerLong = static_cast<std::int8_t>(spinner[1]);
		board.emptySides = static_cast<std::int8_t>(spinner[2]);
		GetBool(line, "placed", board.placed);
		return true;
	}

	// The search's whole input for mySeat's decision.
	inline void WriteState(JsonLine& line, const DominoSearch::GameState& state, int mySeat)
	{
		line.Add("mySeat", static_cast<std::int64_t>(mySeat));
		line.Add("rules", DominoAiPolicy::RulesName(state.rules));
		std::int32_t occupied[DominoSearch::kMaxSeats]{};
		for (int s = 0; s < DominoSearch::kMaxSeats; s++)
			occupied[s] = state.occupied[s] ? 1 : 0;
		line.Add("occupied", occupied, DominoSearch::kMaxSeats);
		for (int s = 0; s < DominoSearch::kMaxSeats; s++)
			line.AddTiles(kHandKeys[s], state.hands[s].data(), state.occupied[s] ? state.handCounts[s] : 0);
		line.Add("ends", state.ends.pips.data(), state.ends.count);
		int remaining = state.boneyardCount - state.boneyardNext;
		line.AddTiles("boneyard", state.boneyard.data() + state.boneyardNext, remaining > 0 ? remaining : 0);
		line.Add("scores", state.scores.data(), DominoSearch::kMaxSeats);
		line.Add("pointsTarget", static_cast<std::int64_t>(state.pointsTarget));
		// rootBonus: kRootBonusSlots entries per hand tile (ends 0-6, then
		// the opening move), tile after tile.
		int myCount = (mySeat >= 0 && mySeat < DominoSearch::kMaxSeats) ? state.handCounts[mySeat] : 0;
		std::vector<std::int32_t> bonus;
		for (int i = 0; i < myCount; i++)
			bonus.insert(bonus.end(), state.rootMoveBonusPoints[static_cast<std::size_t>(i)].begin(), state.rootMoveBonusPoints[static_cast<std::size_t>(i)].end());
		line.Add("rootBonus", bonus);
		line.Add("passStreak", static_cast<std::int64_t>(state.passStreak));
		WriteBoard(line, state.board, state.exactBoard);
	}

	// Inverse of WriteState(). turnSeat is set to mySeat (a decision line
	// is always mySeat's own turn).
	inline bool ReadState(std::string_view line, DominoSearch::GameState& state, int& mySeat)
	{
		state = DominoSearch::GameState{};
		std::int32_t seat = -1;
		std::string rules;
		std::vector<std::int32_t> occupied, ends, scores, rootBonus;
		if (!GetInt(line, "mySeat", seat) || seat < 0 || seat >= DominoSearch::kMaxSeats || !GetString(line, "rules", rules) ||
			!GetIntArray(line, "occupied", occupied) || occupied.size() != DominoSearch::kMaxSeats || !GetIntArray(line, "ends", ends) ||
			ends.size() > state.ends.pips.size())
			return false;
		mySeat = seat;
		state.rules = RulesFromName(rules);
		state.turnSeat = seat;

		for (int s = 0; s < DominoSearch::kMaxSeats; s++)
		{
			state.occupied[s] = occupied[static_cast<std::size_t>(s)] != 0;
			std::vector<Tile> hand;
			if (!GetTiles(line, kHandKeys[s], hand) || hand.size() > DominoSearch::kMaxHandTiles)
				return false;
			for (std::size_t i = 0; i < hand.size(); i++)
				state.hands[s][i] = hand[i];
			state.handCounts[s] = static_cast<int>(hand.size());
		}
		for (std::int32_t pip : ends)
			state.ends.pips[static_cast<std::size_t>(state.ends.count++)] = pip;

		std::vector<Tile> boneyard;
		if (GetTiles(line, "boneyard", boneyard))
		{
			if (boneyard.size() > state.boneyard.size())
				return false;
			for (const Tile& tile : boneyard)
				state.boneyard[static_cast<std::size_t>(state.boneyardCount++)] = tile;
		}
		if (GetIntArray(line, "scores", scores) && scores.size() == DominoSearch::kMaxSeats)
			for (int s = 0; s < DominoSearch::kMaxSeats; s++)
				state.scores[s] = scores[static_cast<std::size_t>(s)];
		GetInt(line, "pointsTarget", state.pointsTarget);
		if (GetIntArray(line, "rootBonus", rootBonus))
		{
			// Lines recorded before 2026-09-25 hold one value per hand tile
			// (any end); newer ones hold kRootBonusSlots per tile.
			const std::size_t perTile = rootBonus.size() == static_cast<std::size_t>(state.handCounts[seat]) ? 1 : DominoSearch::kRootBonusSlots;
			for (std::size_t i = 0; i < state.rootMoveBonusPoints.size() && (i + 1) * perTile <= rootBonus.size(); i++)
				for (int slot = 0; slot < DominoSearch::kRootBonusSlots; slot++)
					state.rootMoveBonusPoints[i][static_cast<std::size_t>(slot)] = rootBonus[i * perTile + (perTile == 1 ? 0 : static_cast<std::size_t>(slot))];
		}
		std::int32_t passStreak = 0;
		if (GetInt(line, "passStreak", passStreak))
			state.passStreak = passStreak;
		state.exactBoard = ReadBoard(line, state.board);
		return state.occupied[seat];
	}

	// The game's own candidate list, in native order (ties go to the LAST
	// valid candidate, so order is part of the data).
	inline void WriteCandidates(JsonLine& line, const DominoAiPolicy::Candidate* candidates, int count)
	{
		std::vector<std::int32_t> hand, placed, endTotal, grid;
		for (int i = 0; i < count; i++)
		{
			hand.push_back(candidates[i].handIndex);
			placed.push_back(candidates[i].hasPlacement ? 1 : 0);
			endTotal.push_back(candidates[i].resultingEndTotal);
			grid.push_back(candidates[i].gridF1);
			grid.push_back(candidates[i].gridF2);
			grid.push_back(candidates[i].gridOrientation);
		}
		line.Add("candHand", hand).Add("candPlaced", placed).Add("candEndTotal", endTotal).Add("candGrid", grid);
	}

	inline bool ReadCandidates(std::string_view line, std::vector<DominoAiPolicy::Candidate>& out)
	{
		std::vector<std::int32_t> hand, placed, endTotal, grid;
		if (!GetIntArray(line, "candHand", hand) || !GetIntArray(line, "candPlaced", placed) ||
			!GetIntArray(line, "candEndTotal", endTotal) || placed.size() != hand.size() || endTotal.size() != hand.size())
			return false;
		GetIntArray(line, "candGrid", grid);
		out.clear();
		for (std::size_t i = 0; i < hand.size(); i++)
		{
			DominoAiPolicy::Candidate c;
			c.handIndex = hand[i];
			c.hasPlacement = placed[i] != 0;
			c.resultingEndTotal = endTotal[i];
			if (grid.size() == hand.size() * 3)
			{
				c.gridF1 = grid[i * 3];
				c.gridF2 = grid[i * 3 + 1];
				c.gridOrientation = grid[i * 3 + 2];
			}
			out.push_back(c);
		}
		return true;
	}
}
