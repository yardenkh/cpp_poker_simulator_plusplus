# Tournaments

Files: `tournament/Tournament.*`, `tournament/TournamentConfig.hpp`,
`tournament/TournamentObserver.hpp`, `tournament/TournamentResult.hpp`

A single-table **freezeout**: equal starting stacks, no re-entry, blinds that
rise by hand count, and prize payouts by finishing place. `Tournament` wraps a
`PokerGame` and adds no betting rules of its own.

## Configuration

```cpp
struct BlindLevel { int small_blind = 50; int big_blind = 100; int hands_per_level = 10; };

struct TournamentConfig {
  int starting_chips = 2500;
  std::vector<BlindLevel> blind_levels{{50,100,10},{100,200,10},{200,400,10},
                                       {400,800,10},{800,1600,10}};
  std::vector<std::int64_t> prizes;   // [0] = 1st place; you must supply this
};
```

Validation (throws `std::invalid_argument`):

- 2..8 players, positive stack.
- At least one blind level; each with `0 < small < big`, non-decreasing across
  levels, `hands_per_level > 0`.
- `1 ≤ prizes.size() ≤ players`; prizes non-negative, non-increasing, total
  fits in `int64`.

Prize units are caller-defined (e.g. cents) and are unrelated to chips.

## API

| Method | Notes |
| --- | --- |
| `Tournament(n, config, seed = 5489, evaluator = default)` | |
| `set_decision_provider`, `set_observer` | Forwarded to the inner `PokerGame` |
| `set_tournament_observer(TournamentObserver*)` | Level/finish events |
| `play_one_hand()` | Plays or resumes one hand; returns false if finished |
| `run(max_hands = 0)` | Until a winner, or pause after `max_hands` |
| `reset(seed)` | New tournament, same config and borrowed dependencies |
| `finished()`, `winner_id()`, `result()`, `table()` | State |
| `blind_level_index()`, `blind_level()` | Current level |
| `decisions_applied()` | From the inner game |

Read `result()` after `play_one_hand()` returns — not inside
`GameObserver::on_hand_finished`, because standings are updated afterwards.

## Blind progression

Before each hand: if the current level has run `hands_per_level` hands and
another level exists, advance. The last level continues indefinitely.

## Eliminations, ties and prizes

After each hand, every seat that had chips before the blinds and has zero now
is eliminated.

- Players eliminated in the same hand are ordered by their **stack before the
  blinds** (larger stack finishes higher).
- Players with equal pre-hand stacks **tie**: they occupy a range of places
  (e.g. 2nd–3rd) and split the combined prizes of those places. Any
  indivisible remainder goes to lower seat IDs first.
- The last survivor gets 1st place and `prizes[0]`.
- Places beyond `prizes.size()` pay zero.

## Results

```cpp
struct TournamentPlayerResult {
  int first_place = 0, last_place = 0;   // equal for a normal finish; 0 = still playing
  std::int64_t prize = 0;
  std::uint64_t eliminated_on_hand = 0;  // 0 for winner / active
};
struct TournamentResult {
  int num_players, players_remaining;
  std::uint64_t hands_played;
  int winner_id = -1;
  std::array<TournamentPlayerResult, MAX_PLAYERS> players;  // indexed by original seat
};
```

## TournamentObserver

| Callback | When |
| --- | --- |
| `on_tournament_started(players, config)` | Before the first hand |
| `on_blind_level_started(level, blinds, completed_hands)` | At start and at every level change |
| `on_tournament_hand_finished(table, result)` | After standings/prizes are settled for a hand |

## Not implemented

Re-entry, multi-table balancing, antes, time-based levels. ICM is available
separately in [Analysis](analysis.md) but the tournament itself does not use
it.
