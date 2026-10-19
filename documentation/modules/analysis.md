# Analysis: equity and ICM

Files: `analysis/Analysis.hpp`, `analysis/Analysis.cpp`

Read-only tools for decision-making. They never change a live game and are
never called automatically by the engine; a bot calls them.

## Construction

```cpp
poker::Analysis analysis;                         // borrows default_fast_hand_evaluator()
poker::Analysis analysis(my_evaluator);           // must outlive the Analysis object
```

## Showdown equity against random hands

```cpp
double calculate_equity(const PlayerView& view, int sample_count,
                        std::uint32_t seed = 5489u) const;
```

Estimates the player's **average share of the pot** if every remaining player
reaches showdown:

- each sample deals random hole cards to every opponent still in the hand
  (including all-in players; folded players are excluded) and completes the
  board;
- winning alone counts 1.0, an *n*-way tie counts 1/*n*, losing counts 0.

So 0.5 means "50 % pot share including ties", not "wins half the time".

- Returns 1.0 immediately if every opponent has folded.
- Uses its own RNG seeded by `seed`: same inputs + seed → same result.
- Throws `std::invalid_argument` for a non-positive sample count, bad player
  ID, invalid/duplicate known cards, or a player who is not in the hand.
- No game copies and no per-sample heap allocation.

## Equity against opponent ranges

```cpp
struct OpponentRange { int player_id; std::vector<std::array<Card,2>> hands; };

double calculate_equity(const PlayerView& view, int sample_count, std::uint32_t seed,
                        std::span<const OpponentRange> opponent_ranges) const;
```

- Each listed opponent's hand is drawn uniformly from its (deduplicated,
  known-card-filtered) set; unlisted opponents get random cards. Empty input
  falls back to the basic method.
- Joint assignments that overlap are rejected and redrawn as a whole, so every
  legal combination is equally likely (no bias toward whoever is drawn first).
- Throws `std::invalid_argument` for invalid ranges or when no legal joint
  assignment exists; `std::runtime_error` if compatibility checking exceeds
  1,000,000 visits or a sample needs more than 10,000 proposals.

Ranges are unweighted: every listed combo is equally likely.

## Independent Chip Model

```cpp
static std::array<double, MAX_PLAYERS>
calculate_icm(std::span<const int> stacks, std::span<const std::int64_t> prizes);
```

Converts tournament chip stacks into expected prize value. First place goes to
each player with probability proportional to their stack; that player is
removed and the rule repeats for second place, and so on.

- `stacks`: 1..8 positive stacks of surviving players (settled, not mid-hand).
- `prizes`: remaining prizes, descending, non-negative; may be shorter than the
  stacks (unpaid places are 0); empty means all zero.
- Result `[i]` belongs to `stacks[i]`. Track your own mapping back to seat IDs.
- Exact under the model (subset dynamic programming, ≤ 256 states, no
  allocation). Static: no evaluator needed.
- Throws `std::invalid_argument` for invalid stacks/prizes or prize overflow.

## Limits

Neither tool models future betting, side pots, weighted ranges, skill or blind
pressure, and neither chooses an action. They are inputs for a strategy.
