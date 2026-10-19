# Poker simulation and research guide

The game uses the shared fast lookup evaluator. Synchronous play, batched inference,
and externally supplied actions run the same betting and hand transitions. Engine
code does not print or write files. Interactive play uses the separate terminal UI.

## Interactive poker

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/poker_cli
```

In a terminal, the display refreshes around the current hand: players sit in a
circle around the public board and pot, with your seat at the bottom. Each seat
shows its stack, round contribution, and latest action (or WAIT before acting).
Your private cards and the current stage remain visible. The highlighted YOUR
TURN area lists only available actions.
Type `fold`, `check`, `call`, `raise 500`, or `allin`; shortcuts are `f`, `k`, `c`,
`r 500`, and `a`. A raise amount is the total contribution for this round.
Each hand ends with a result screen showing payouts, net gains/losses, and final
stacks. The default CLI now runs a tournament and also shows blind levels,
eliminations, finishes and prizes. Press Enter after unfinished hands; the final
hand shows final standings without another pause. Redirected input/output remains
plain and requires no extra Enter. Use `./build/poker_cli --game` for the original
fixed-blind match. Headless simulation tools do not use this UI.

CLI prizes use abstract units: a pool of 1,000 units per player pays 70/30%
heads-up or 60/30/10% to the top three with more players. No entry fee is deducted.

## Build and verify

Run these commands inside WSL from the project directory:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
cmake --build build-release --target poker_simulation_benchmark poker_validation -j
./build-release/poker_simulation_benchmark 2000 1
./build-release/poker_simulation_benchmark 2000 4
./build-release/poker_validation
```

The benchmark arguments are episode count and worker count. It reports **actual**
completed hands, median/range of three trials, decisions/second, and result checksum.
Different policies have different hand lengths; do not equate evaluator calls with
completed hands. Observer/history are disabled in this benchmark. Loading the
shared evaluator occurs before timing; episode reset and worker startup are timed.

Release IPO/LTO is enabled when supported. Disable it with `-DPOKER_ENABLE_IPO=OFF`
for comparison. Debug builds are for diagnosis rather than throughput estimates.

## Existing playable-game API

```cpp
// Providers are borrowed and must outlive the game that uses them.
poker::AIDecisionProvider policy;
poker::PokerGame game(6, poker::GameConfig{}, 42);
for (int seat = 0; seat < 6; ++seat)
  game.set_decision_provider(seat, policy);
game.run_match(100); // Up to 100 additional hands, stopping if the match ends.
```

Construction is seeded explicitly (default seed 5489), avoiding operating-system
entropy calls for every new environment. Randomized applications can obtain their
experiment seed once outside the engine. `GameObserver` remains optional.

The default evaluator is an immutable process-wide singleton initialized safely
on first use. CMake embeds the source-tree table location. For deployment, load
`FastHandEvaluator` from your installed data path and pass it as the fourth
`PokerGame` constructor argument or the final runner/batch argument. It must
outlive those users. Never allocate/load its roughly 15 MiB tables per environment.
Packed public scores and observer score meanings are unchanged.

## Single-table tournaments

`Tournament` wraps `PokerGame` for a 2–8 player freezeout with no re-entry.
Stacks carry between hands. After each payout, new eliminations receive finishing
positions and prizes. Simultaneous eliminations rank by stacks **before blinds**;
equal stacks share a position interval and the combined prizes. For indivisible
integer prize units, any remainder goes to tied players in ascending seat-ID order.

```cpp
#include "tournament/Tournament.hpp"
#include "decision/AIDecisionProvider.hpp"

// Define money separately from playing chips. Here the integer prize units
// represent cents, so the three paid positions award $500, $300 and $200.
poker::TournamentConfig config;
config.starting_chips = 2500;
config.prizes = {50000, 30000, 20000};
// Each level lasts ten completed hands; the final level continues to the winner.
config.blind_levels = {{50, 100, 10}, {100, 200, 10}, {200, 400, 10}};

// Providers are borrowed. This simple policy is stateless and can serve all seats.
poker::AIDecisionProvider policy;
poker::Tournament tournament(8, config, 42);
for (int id = 0; id < 8; ++id)
  tournament.set_decision_provider(id, policy);
tournament.run(); // Continue until one player holds all the chips.
const auto &standings = tournament.result();
// players[id].first_place/last_place describe the player's finish or tied interval;
// players[id].prize is money, not the final stack of poker chips.
```

`run(100)` plays at most 100 additional hands and can be called again to resume;
`play_one_hand()` advances by one complete hand. `blind_level_index()` identifies
the most recently begun hand's level (zero before play). Progression is based on
completed hands rather than elapsed time, preserving seeded reproducibility.

Configuration must provide a nonempty, nonnegative, descending prize list with
no more entries than players; unpaid lower places implicitly receive zero. The
nonempty blind schedule must have positive durations and nondecreasing blind
amounts, with a positive small blind smaller than the big blind. Chips and the
total prize pool are validated against their integer storage limits.

Attach an optional `GameObserver` through `set_observer`. It receives ordinary
engine events; standings update after `play_one_hand()` returns. Providers and
observers must outlive use; observers should not throw. Provider errors propagate,
and replacing/fixing a provider lets the pending hand resume without applying
blinds twice. The wrapper exposes only a const table, preventing callers from
resetting its underlying game and invalidating tournament standings.

Attach a `TournamentObserver` using `set_tournament_observer` before play starts.
Its hand-finish event sees settled standings, unlike the earlier engine event.
`TerminalUI` implements both observer interfaces; attach it through both setters
for hand events and tournament results. It pauses once, after both are displayed.
`reset(seed)` reuses the wrapper/providers, clearing ranks, prizes, levels and
counters. Reset external policy state separately. Hand-level short-blind/reopening
semantics remain those documented below. ICM is not implemented yet.

## Tournament ICM analysis

`Analysis::calculate_icm` converts settled survivor stacks and remaining prizes
into expected prize values under the Independent Chip Model. It is static: no
hand-evaluator lookup table or Analysis object needs to be loaded for this method.

```cpp
#include "analysis/Analysis.hpp"

// Keep a separate seat-ID mapping if you compact these from a tournament table.
const std::array<int, 3> stacks{5000, 3000, 2000};
// Prize values are integer units (for example cents), independent of poker chips.
const std::array<std::int64_t, 3> prizes{500, 300, 200};
const auto values = poker::Analysis::calculate_icm(stacks, prizes);
// values[0], values[1], values[2] correspond to these three input stacks:
// approximately 383.928571, 327.5, 288.571429 prize units.
```

Provide 1..8 positive survivor stacks and a descending, nonnegative prize list.
Omitted finishing positions pay zero; an empty list returns zero values. Entries
beyond the supplied player count in the fixed output array are zero. Already paid
elimination prizes must be handled separately; zero mid-hand cash does not prove
elimination, so first settle actual/hypothetical pots before building these inputs.

The exact model calculation uses at most 256 subset states, without random samples
or heap allocations. Floating-point rounding remains, especially for large integer
prize values. ICM does not account for skill, position, future blind pressure, or
entry fees. It supplies a stack-value tool, not an action selector. Future AI
rollouts should compute ICM for each settled outcome before averaging its value.

## Parallel tournament simulations

```sh
cmake --build build --target poker_tournament_simulation -j
./build/poker_tournament_simulation 10000 4
```

Arguments specify complete tournaments and worker count (zero selects hardware
concurrency). This executable uses six simple AI seats, the default blind schedule,
and prizes `{3000, 1800, 1200}` in abstract units. It reports actual hands,
decisions, throughput, wins, prize totals per seat, and a reproducible checksum.

For custom experiments:

```cpp
#include "simulation/TournamentSimulation.hpp"
#include "decision/AIDecisionProvider.hpp"

poker::TournamentSimulationOptions options;
options.episodes = 1000;
options.players = 6;
options.workers = 4;
options.seed = 42;
options.config.prizes = {3000, 1800, 1200};
// Keep per-tournament finishes, prizes, elimination hands, and final stacks.
std::vector<poker::TournamentEpisodeResult> rows(options.episodes);
const auto totals = poker::run_tournament_simulations(
    options,
    [](int seat) {
      // The seat argument allows mixed algorithm lineups. This example uses
      // one stateless policy type, with a private provider object per seat.
      (void)seat;
      return std::make_unique<poker::AIDecisionProvider>();
    },
    rows);
```

Factory calls are serial; providers receive seat-specific `reset_for_episode`
seeds. Construct equivalent initial policies for each seat regardless of worker
count, and clear episode-local state on reset. Share only immutable model data;
RNGs/beliefs belong to each provider. One episode always runs until a winner.

`first_episode` preserves scenario IDs across chunks; output rows are local to
each chunk. Seeded results are scheduling-independent on the same runtime.
Prize totals are gross rewards, not net profit after entry fees. Worker errors
are rethrown after threads join; discard partially filled output on failure.
The original `run_simulations` and hand benchmark remain available for chip-based
experiments.

## Externally supplied actions

```cpp
poker::PokerGame game(2, poker::GameConfig{}, 42);
game.begin_hand(); // No DecisionProvider is needed for external control.
while (game.advance_to_decision()) {
  const poker::PlayerView view = game.current_view();
  // Replace this example with human input, a policy, or a model prediction.
  const poker::PlayerDecision action{
      view.legal_actions.can_check ? poker::ActionType::Check : poker::ActionType::Call, 0};
  game.apply_action(action);
}
const auto rewards = game.last_rewards(); // Final cash minus cash before blinds.
```

`advance_to_decision()` deals automatic streets, including burns during all-in
runouts. It returns false at hand completion and does not begin another hand.
Repeated calls at the same pending decision do not recompute legality. Invalid
choices throw before mutation. The public standalone `execute_decision` still
validates from current state; the engine uses its own cached legal-action result.
Never call `detail::apply_validated_decision` from strategy or UI code.

A game belongs to one thread at a time. Observers must not reenter/mutate the game
or throw exceptions from callbacks. Strategies receive `PlayerView`, which never
includes other players' private cards or the deck. Observers and state snapshots
are privileged engine tools, not model observations.

## Independent episodes and branching

`reset(seed)` restores configured equal stacks while preserving attached providers,
observer, evaluator, and allocated history capacity. `reset_episode(stacks, dealer,
seed)` accepts explicit nonnegative stacks, including eliminated seats. The dealer
must have chips and at least two seats must be funded. Reset explicitly abandons
any current hand. It does not reset external strategy state.

`auto saved = game.snapshot()` captures cards/deck position, RNG, pots, betting,
pending actor/legal actions, configuration, payouts/rewards, and optional history.
`game.restore(saved)` resumes that exact state without emitting events. A snapshot
is opaque and cannot be filled with unchecked card data. It can also be restored
into a separate game for a branch. That game keeps its own evaluator/providers/
observer. Snapshot or reseed the strategy's RNG, recurrent hidden state, and other
mutable policy data separately. Snapshots are in-process objects, not a portable
serialized format. They intentionally copy state only when requested; apply/undo
is not necessary for the normal simulation path.

## Parallel experiments

```cpp
poker::SimulationOptions options;
options.episodes = 100000;
options.players = 6;
options.hands_per_episode = 1; // <= 0 runs each match to completion.
options.workers = 4;          // 0 chooses hardware_concurrency.
options.seed = 42;
auto result = poker::run_simulations(options, [] {
  // Factory runs serially and creates one provider per seat per worker.
  return std::make_unique<poker::AIDecisionProvider>();
});
```

Each worker reuses one game and its private providers. The shared evaluator is
read-only. Override `DecisionProvider::reset_for_episode(uint64_t seed)` in a
stateful/randomized policy to clear episode state and seed its RNG. The runner
uses stable episode IDs and separate per-seat seeds, so changing worker count
preserves seeded results for policies that honor this contract. Reproducibility
is within the same toolchain/library and rules version: standard shuffle/bounded
random algorithms need not produce identical sequences across C++ libraries.

An optional caller-owned `span<EpisodeResult>` stores results by episode ID. With
an empty span, only aggregates are retained. To bound dataset memory, run chunks
and advance `options.first_episode` by the preceding chunk size; do not write one record per action
from each worker. Worker exceptions are rethrown after joining; output may be
partially filled when an exception occurs and should be discarded for that run.

Shared model parameters can be immutable; any shared mutable model/inference
state must be synchronized by the application. No Python, GPU, or ML-framework
calls are placed inside the engine. The supplied simple AI is a throughput
placeholder, not a useful poker-strength baseline.

## Batched model inference

`BatchSimulator(count, seats, config, seed)` begins one hand per environment.
`collect()` returns a contiguous span of `PlayerView`; `game_ids()` maps rows to
environments. Supply one `PlayerDecision` per row to `apply()`. The whole batch is
checked before any game changes. Repeated collect calls return the pending batch.
After apply, old spans are invalidated and must not be retained.

Keep output action/tensor buffers outside the loop and reuse their capacity. The
optional `encode_observations` helper fills caller-owned `EncodedObservation`
buffers with documented numeric features and a legal-action mask. This versioned
example encoding is separate from poker rules: replace it or embed categorical
card/seat IDs as your model requires. RaiseTo still needs a numeric target within
the supplied range. No opponent private cards enter the encoder.

Completed environments remain completed until `reset(id, seed)`. Read
`batch.game(id).last_rewards()` first. Reset only after consuming any pending
batch. If all games finished, collect returns an empty span. A caller can record
observation/action/reward transitions in contiguous buffers and flush them in
chunks, keeping model training and storage out of the game loop.

## History and rollout sampling

### Simple showdown equity

`Analysis` estimates the requesting player's share of one hypothetical pot,
assuming every opponent still in the hand reaches showdown with uniformly random
cards. Folded players are excluded; all-in players remain opponents. The live
game is never changed, and each sample uses reusable arrays rather than a table
copy. A second overload accepts explicit opponent ranges; side pots and future
betting are not included yet.

```cpp
#include "analysis/Analysis.hpp"

// Construct once and reuse; Analysis borrows the shared fast lookup evaluator.
poker::Analysis analysis;
// Obtain the view while the game is waiting for a decision.
const auto view = game.current_view();
// Average 10,000 sampled pot shares with an explicit reproducible seed.
const double equity = analysis.calculate_equity(view, 10000, 42);
```

The result is between 0 and 1: 0.5 means an estimated 50% pot share, including
ties, rather than necessarily a 50% outright win rate. The method rejects invalid
observations and nonpositive sample counts. Its default seed is 5489; explicitly
vary the seed when independent repeated estimates are desired.

To restrict a particular opponent, supply equally likely physical two-card
combinations. Opponents omitted from the list remain uniformly random:

```cpp
// Assume seat 1 holds either Ah Ac or Kh Kc, each equally likely before blockers.
const std::array<poker::OpponentRange, 1> ranges{{
    {1, {{poker::Card{poker::Face::Ace, poker::Suit::Hearts},
          poker::Card{poker::Face::Ace, poker::Suit::Clubs}},
         {poker::Card{poker::Face::King, poker::Suit::Hearts},
          poker::Card{poker::Face::King, poker::Suit::Clubs}}}}
}};
// These are hypothetical assumptions; no actual opponent cards are read.
const double ranged_equity = analysis.calculate_equity(view, 10000, 42, ranges);
```

Known-card conflicts are removed, and duplicate/reversed combinations count
once. Rejection sampling conditions all supplied ranges on collision-free joint
assignments. A range must identify a unique opponent still in the hand. Empty or
impossible ranges throw `std::invalid_argument`; excessive search/sampling throws
`std::runtime_error`, never a partial estimate. The documented limits are
1,000,000 compatibility-search hand visits and 10,000 proposals per sample.

Call `enable_history(true)` between hands to record stage, actor, action, chips
paid, and resulting bet. `action_history()` is a read-only span valid until the
next mutation/reset. The vector reuses capacity and grows for unusually long
hands; history is off by default. Hole cards are never written to action history.

`CardSampler(known_cards)` validates and excludes known cards. `sample(output,rng)`
uses a partial Fisher-Yates shuffle to draw only the missing cards, without
replacement and without allocations. Each call starts a new rollout from the same
known-card exclusion set. It does not consume the pool across calls. Duplicate or
invalid known cards and oversized requests throw. Sampling uses bounded uniform
random distributions instead of modulo reduction.

## Rules and compatibility

This optimization preserves the project's existing betting contract. In particular:

- The opening pre-flop target is the largest **actual** blind payment, even if a
  blind is short. The minimum raise size is the configured big blind.
- A short all-in raises the call target but does not reopen a previous actor's
  raise rights. Multiple short all-ins do not accumulate reopening rights in
  this simplified model. Only a full raise resets the acted flags.
- Unmatched top contributions are returned through the single-eligible-player
  pot at payout; payout totals may therefore include returned unmatched chips.
  Use net `last_rewards()` for training rather than gross payout amounts.
- The dealer rotates after a hand, skipping eliminated seats. There is no rake.

These rules are regression-tested, but they are not a claim of compatibility with
every casino/tournament rule set. Choose and version the research rules before
comparing learned strategies with external poker environments. Changing reopening
or short-blind semantics should be an explicit rules change with dedicated tests.

Card rank and suit now use one byte each (Card is two bytes on supported builds).
Public enum values and text formatting are preserved. Rebuild callers rather than
mixing old object files, and never persist raw C++ struct bytes as a dataset schema.

## Validation coverage

CTest includes the original evaluator/table tests, scripted full games, stepped
versus synchronous equivalence, snapshot branching/replay, batched/single-game
agreement, one-worker/four-worker stochastic determinism, sampler exclusions,
short blind/raise and odd-chip/side-pot regressions, and randomized legal games
with chip conservation and fast/reference score checks. `poker_validation` adds
one million unbiased random seven-card comparisons against the reference scorer.

## Measured baseline after these changes

Measured in WSL with GCC 13.3, Release + IPO, no observer/history, on this machine
on 2026-10-11. Values below are medians of three trials from
`poker_simulation_benchmark 2000 1` and `poker_simulation_benchmark 2000 4`.
The passive and simple workloads completed 200,000 hands per trial. The raise and
all-in workloads often eliminate players immediately and report fewer actual hands.

| Workload | 1 worker, hands/sec | 4 workers, hands/sec |
| --- | ---: | ---: |
| Heads-up passive/showdown | 1,366,670 | 4,362,550 |
| Six-player passive/showdown | 594,392 | 1,825,680 |
| Six-player simple AI | 622,060 | 1,733,870 |
| Six-player fold-heavy | 2,440,300 | 5,778,120 |
| Six-player minimum raises | 282,841 | 854,572 |
| Six-player all-in | 457,441 | 1,285,400 |

Checksums matched across worker counts for every workload. The earlier engine
measured about 53,000-56,000 six-player showdown-heavy hands/sec in a shorter
20,000-hand benchmark. That is a useful approximate baseline, not an identical
benchmark protocol. These measurements do not include model inference, training,
or dataset writes, and timing depends on hardware load and policy behavior.

All three CTest suites passed in Debug, Release, and an AddressSanitizer plus
UndefinedBehaviorSanitizer build. One million random seven-card comparisons also
matched the reference evaluator. The randomized game tests check conservation,
valid card dealing, and showdown scores across two, three, six and eight seats.
