# DominoCheat Changelog

## [Unreleased]

### Changed

- All Fives and All Threes advice is much stronger. The mod now follows
  the exact board (every end, which ends are doubles, and the spinner), so
  the advisor counts every scoring play for the rest of the round, yours
  and the opponents', and expects each opponent to make the move the
  game's AI actually makes instead of the worst possible one.
- The "PLAY HERE" marker now points at the end the advice chose. When a
  tile fit two ends it could point at the other one, and on All Fives it
  now picks the spot that scores.

### Fixed

- No advice on some turns: the check for which numbers are open could
  miss an end next to a double.
- The first move of a round was evaluated as if every number were open.
- On All Fives/All Threes, a tile that fits two ends could be credited
  with the points only the other end scores.

### Added

- `[Advisor] WallClockBudget=0` now means unlimited: the move advisor keeps
  refining its advice until the position is solved or your turn ends,
  instead of stopping at a fixed time. It runs on its own thread, so the
  game never waits on it, but it keeps one CPU core busy while you think.
  Previously 0 was raised to the 50 ms minimum.

## [1.2.0] - 2026-09-23

### Fixed

- Move advice is no longer built on the wrong board when the mod misses
  one of the open ends (seen right after a double is played onto an open
  end). A missed double's end is now added automatically; any other
  disagreement with the game's own list of playable tiles shows no advice
  for that turn instead of wrong advice.
- The game can no longer hang on exit while the move advisor is working,
  and ejecting the mod no longer stalls for half a second.
- The log is no longer silently lost when the game folder isn't writable.
- If the game folder can't be written (e.g. a `C:\Program Files` install,
  or a read-only/locked log file), the log now goes to
  `%LOCALAPPDATA%\RDR2ASIMods\DominoCheat.log` instead, and its first line
  names the path that couldn't be used.
- Loads reliably even when an ASI loader injects the mod before RDR2 has
  finished unpacking itself. Startup work moved out of the DLL's load
  callback, and a failed memory scan is retried instead of leaving the mod
  inactive for the whole session.
- Settings work in a game folder that can't be written, too:
  `DominoCheat.ini` is then saved to `%LOCALAPPDATA%\RDR2ASIMods\DominoCheat.ini`
  (starting from the game folder's copy, if there is one) instead of the
  mod being stuck on default settings.

### Changed

- The Release HUD no longer allocates memory every frame.

## [1.1.0] - 2026-09-17

### Added

- Real 3D "PLAY HERE!" world-space marker showing exactly where the
  recommended tile should be placed on the physical board, replacing the
  earlier prop-tracking approach with a deterministic formula ported from
  the game's own ghost-preview code.
- On-screen/log warning when the recommended tile legally fits more than
  one open end, so an ambiguous placement doesn't read as a single
  obvious spot.
- A post-move check that flags when the tile was actually played to a
  different end than recommended, even when the tile itself matched.

### Fixed

- Opponent move-prediction timing: predictions are now captured before an
  opponent's move commits instead of a tick where it had already
  committed.
- "PLAY HERE!" marker no longer fails to appear on boards where the same
  pip value has more than one open end.
- "PLAY HERE!" marker no longer floats above or sits below the physical
  table; it now uses a real board tile's own height instead of an
  unreliable base coordinate.
- Debug marker trace logging no longer spams the log; it is off by
  default and wall-clock throttled when enabled.

### Removed

- The observational board-tracking mechanism and six disabled diagnostic
  menu items, both superseded by the deterministic board marker and no
  longer needed.

## [1.0.0] - 2026-09-13

Initial release of DominoCheat, a ScriptHookRDR2 ASI mod that exposes
dominoes_sp game state and provides an in-game dominoes advisor.

### Added

- Reveal the real hands of occupied opponent seats.
- Display the remaining boneyard tiles in draw order.
- Identify the player's seat and show whose turn it is.
- Mark the player's currently legal tiles.
- Recommend a move during the player's decision window.
- Highlight the recommended physical domino in the 3D game world.
- Detect immediate winning moves when the search can establish one.
- Use a background advisor worker so move evaluation does not block the
  game thread.
- Use a configurable runtime budget for the advisor, defaulting to 1000 ms.
  The budget is clamped to a safe range of 50-30000 ms.
- Model known hands, boneyard draws, scoring rules, accumulated scores, and
  the table's points target where the available game state is valid.
- Support Block, Draw, All Threes, All Fives, and unknown rule modes with
  conservative fallbacks where the board state cannot be modeled exactly.
- Use the game's own domino tile sprites for opponent-hand and boneyard HUD
  rows.
- Localize the release HUD for the 13 languages supported by RDR2.
- Add independent configuration toggles for:
  - Opponent hands
  - Boneyard
  - Move advice
  - Recommended-domino world marker
- Add a Debug F12 menu with probing and diagnostic tools for live game
  investigation.
- Add isolated unit coverage for tile decoding, search behavior, AI policy,
  runtime-budget behavior, and asynchronous advisor publication.

### Configuration

`DominoCheat.ini` is created next to the ASI. The main release options are:

```ini
[General]
ShowOpponentHands=true
ShowBoneyard=true
ShowAdvice=true
ShowPlayableDomino=true
Language=auto

[Advisor]
WallClockBudget=1000
```

`Language=auto` follows the game's current UI language. The advisor budget is
in milliseconds and is clamped to a safe range. In Debug builds, additional
HUD positioning and sizing values are available.

### Requirements

- Red Dead Redemption 2 single-player, build 1491.50.
- ScriptHookRDR2 matching the installed game build.
- A working ASI loader.

### Known limitations

- This is a single-player dominoes_sp mod; it does not support Red Dead
  Online.
- The advisor uses a distinct-open-pip board abstraction. It does not yet
  fully model exact board topology when both ends share the same pip value.
- Exact placement-side advice is not exposed; the mod recommends a tile, not
  every possible physical end on which that tile may be played.
- Scoring-mode opponent behavior and some board-dependent scoring details use
  conservative modeling when the native placement data is not available.
- The advisor is an aid, not a guarantee. Opponent choices, game state
  changes, and model limitations can still produce a losing recommendation.
- The Debug F12 diagnostics are intended for development and troubleshooting,
  not normal release use.
