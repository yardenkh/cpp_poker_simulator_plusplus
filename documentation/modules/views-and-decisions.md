# Player views and decision providers

Files: `view/PlayerView.hpp`, `view/PlayerView.cpp`,
`decision/DecisionProvider.hpp`, `decision/AIDecisionProvider.*`,
`decision/HumanDecisionProvider.*`

## PlayerView

A value snapshot of everything **one** player may legally know when it is their
turn. It never contains opponents' hole cards or deck order.

| Field | Meaning |
| --- | --- |
| `player_id`, `num_players`, `stage` | Who, how many seats, which street |
| `hole_cards[2]`, `hole_card_count` | This player's cards only |
| `community_cards[5]`, `community_card_count` | Visible board (0/3/4/5) |
| `player_stacks[8]` | Cash not yet in the pot, per seat |
| `players_in_hand[8]`, `players_all_in[8]` | Per-seat status |
| `round_contributions[8]`, `total_contributions[8]` | Paid this street / this hand |
| `dealer_id`, `small_blind_id`, `big_blind_id` | Positions |
| `pot_amount` | All chips in all pots this hand |
| `current_bet` | Round target to match |
| `legal_actions` | `LegalActions` for this player |

Built by `make_player_view(table, pots, state, stage, id)` (throws
`std::out_of_range` / `std::invalid_argument` on bad input). An overload taking
precomputed `LegalActions` is used internally to avoid recomputation.

## DecisionProvider

```cpp
class DecisionProvider {
public:
  virtual ~DecisionProvider() = default;
  virtual PlayerDecision choose_action(const PlayerView& view) = 0;
  virtual void reset_for_episode(std::uint64_t seed) {}
};
```

- `choose_action` returns a request; the game validates it. An illegal choice
  causes `std::invalid_argument` from `apply_action`.
- `reset_for_episode` is called by simulation runners at the start of every
  episode with a deterministic per-seat seed. Stateful or random strategies
  should reseed their RNG and clear episode memory here.
- A provider may be attached to several seats in the same single-threaded
  game (the CLI uses separate instances), but simulation factories create
  a separate instance per seat per worker.

## AIDecisionProvider

The baseline uses one distribution over complete legal decisions. Fold, check,
call and all-in each have their own entry when legal; every distinct legal raise
size has its own entry too. Each entry receives probability 1 / entry count,
and one random draw selects the complete PlayerDecision.

Raise proposals are the minimum full raise and 25%, 40%, 60%, 80% of the current
remaining stack (configurable in the provider constructor). A fraction means
payment NOW / remaining stack, including the call portion. For example, with
50 contributed and 1000 remaining, 25% pays 250 and becomes RaiseTo 300.
Fractional payments are rounded down; illegal and duplicate raises are removed.
Full-stack raises use AllIn only. AllIn legality is checked independently, so
short all-ins remain available. Call and AllIn remain distinct legal types even
when their payments coincide.

With fold, call, five raises and all-in, each of eight entries gets 12.5%:
raising therefore has a combined probability of 62.5% under this baseline.
The policy does not evaluate cards or estimate value. Future training can assign
learned weights to these complete decisions instead of uniform probabilities.

`AIActionDistribution` stores up to nine entries in a fixed array, avoiding
per-decision heap allocation. Candidate generation lives in the same module;
there are no separate raise structures or raise-sizing files. The sampler checks
probability validity. `reset_for_episode(seed)` reproduces the RNG sequence on
the same runtime. The CLI and simulation runners isolate providers per seat.
Human decisions and engine validation are unchanged.

## HumanDecisionProvider

Adapter (in the `poker_terminal` library) that forwards `choose_action` to
`TerminalUI::request_decision`. The `TerminalUI` is borrowed and must outlive
it. See [terminal-ui.md](terminal-ui.md).

## Writing your own bot

```cpp
class MyBot : public poker::DecisionProvider {
  std::mt19937_64 rng_;
public:
  poker::PlayerDecision choose_action(const poker::PlayerView& v) override {
    const auto& a = v.legal_actions;
    if (a.can_raise) return {poker::ActionType::RaiseTo, a.min_raise_to};
    return {a.can_check ? poker::ActionType::Check : poker::ActionType::Call, 0};
  }
  void reset_for_episode(std::uint64_t seed) override { rng_.seed(seed); }
};
```

Combine with [Analysis](analysis.md) for equity- or ICM-aware decisions.
