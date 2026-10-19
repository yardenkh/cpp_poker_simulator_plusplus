# Simulation

Files: `simulation/Simulation.*`, `simulation/TournamentSimulation.*`,
`simulation/CardSampler.hpp`, `simulation/Observation.hpp`

Tools for running many games quickly and reproducibly, and for driving games
from a model in batches.

## Seeds

```cpp
std::uint64_t simulation_seed(std::uint64_t master, std::uint64_t episode);
```

SplitMix64 mixing. Episode `e` of an experiment always gets the same seed
regardless of worker count; each seat's provider is reset with
`simulation_seed(episode_seed, seat)`. Not cryptographic.

## Parallel cash-game episodes

```cpp
struct SimulationOptions {
  std::uint64_t episodes = 1000;
  int players = 6;
  GameConfig config{};
  int hands_per_episode = 1;   // <= 0: play each match to completion
  unsigned workers = 1;        // 0: hardware_concurrency
  std::uint64_t seed = 42;
  std::uint64_t first_episode = 0;   // run large experiments in chunks
};
struct EpisodeResult    { std::uint64_t hands, decisions; std::array<int,8> final_stacks; };
struct SimulationResult { std::uint64_t hands, decisions, checksum; };

using ProviderFactory = std::function<std::unique_ptr<DecisionProvider>()>;

SimulationResult run_simulations(const SimulationOptions&, const ProviderFactory&,
                                 std::span<EpisodeResult> output = {},
                                 const FastHandEvaluator& = default_fast_hand_evaluator());
```

- The factory is called serially, `workers × players` times. Each worker owns
  its providers and one `PokerGame`.
- Episodes are strided across workers (worker *w* runs *w*, *w+W*, …), so no
  lock is needed and output row *i* always belongs to episode
  `first_episode + i`.
- Each episode: `game.reset(seed)`, `reset_for_episode` on every provider, play
  up to `hands_per_episode` hands.
- `checksum` sums a hash of each episode's result; equal checksums across
  worker counts are a quick determinism check.
- Exceptions in workers stop the others and are rethrown after joining; discard
  partial output.

## Parallel tournaments

```cpp
struct TournamentSimulationOptions {
  std::uint64_t episodes = 1000; int players = 6;
  TournamentConfig config;      // prizes required
  unsigned workers = 1; std::uint64_t seed = 42; std::uint64_t first_episode = 0;
};
struct TournamentEpisodeResult  { TournamentResult standings; std::uint64_t decisions; std::array<int,8> final_stacks; };
struct TournamentSimulationResult {
  std::uint64_t tournaments, hands, decisions, checksum;
  std::array<std::int64_t, 8> total_prizes;
  std::array<std::uint64_t, 8> wins;
};
using TournamentProviderFactory = std::function<std::unique_ptr<DecisionProvider>(int seat)>;

TournamentSimulationResult run_tournament_simulations(
    const TournamentSimulationOptions&, const TournamentProviderFactory&,
    std::span<TournamentEpisodeResult> output = {}, const FastHandEvaluator& = …);
```

The factory receives the seat number, so different strategies can sit at
different seats. Every episode runs to a winner. The config is validated even
for zero episodes, and aggregate prize / counter overflow is checked.

## BatchSimulator — batched model inference

Keeps many paused games and hands back one decision per game at a time.

```cpp
poker::BatchSimulator batch(/*count=*/1024, /*players=*/6);
for (;;) {
  auto views = batch.collect();          // span<const PlayerView>, one per waiting game
  if (views.empty()) break;              // all games finished
  std::vector<poker::PlayerDecision> actions = model(views);
  batch.apply(actions);                  // whole batch validated before any game changes
  // for finished games: read batch.game(id).last_rewards(), then batch.reset(id, seed)
}
```

- `game_ids()` maps row *r* of `collect()` to a game index.
- `collect()` called twice without `apply()` returns the same pending batch.
- Finished games drop out of `collect()` until `reset(id, seed)` (which is
  refused while a batch is pending).

## CardSampler

```cpp
CardSampler sampler(known_cards);       // throws on invalid/duplicate known cards
sampler.sample(std::span<Card> out, rng);
sampler.remaining_cards();
```

Excludes known cards once, then draws without replacement using a partial
Fisher–Yates shuffle with unbiased bounded distributions. No allocation.

## Observation encoding

`encode_observations(span<const PlayerView>, span<EncodedObservation>, chip_scale = 1)`
turns views into a fixed float vector (layout version 1) plus a 5-entry legal
mask:

| Indices | Content |
| --- | --- |
| 0–5 | player id, seat count, stage, dealer, SB seat, BB seat |
| 6–10 | pot, current bet, to call, min raise-to, max raise-to (÷ chip_scale) |
| 11–12 | hole card count, board card count |
| 13–19 | own hole cards then board as card IDs 0..51, −1 if undealt |
| 20–59 | per seat × 5: cash, round paid, total paid (scaled), in_hand, all_in |

`legal_mask` order: Fold, Check, Call, RaiseTo, AllIn. IDs are categorical —
embed or one-hot them rather than treating them as magnitudes.
