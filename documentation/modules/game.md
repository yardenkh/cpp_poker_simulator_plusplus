# Game orchestration

Files: `game/PokerGame.hpp`, `game/PokerGame.cpp`, `game/GameObserver.hpp`

`PokerGame` owns the table, pots, betting state and RNG for one match and runs
each hand as a **resumable state machine**. The same transitions serve
synchronous play, externally stepped play and batched inference.

## Construction and setup

```cpp
PokerGame(int num_players,
          const GameConfig& config = GameConfig{},
          std::uint32_t seed = 5489u,
          const FastHandEvaluator& evaluator = default_fast_hand_evaluator());
```

Throws `std::invalid_argument` for a bad player count or config.

| Method | Notes |
| --- | --- |
| `set_random_seed(seed)` | Reseeds the deck RNG |
| `set_blinds(sb, bb)` | Between hands only (`std::logic_error` during a hand); validated atomically |
| `set_decision_provider(id, provider&)` | Borrowed; must outlive use |
| `set_observer(GameObserver*)` | Borrowed; `nullptr` disables events |

## Synchronous API

| Method | Notes |
| --- | --- |
| `play_one_hand()` | Plays (or resumes) one hand using providers. Throws `std::logic_error` if a funded seat that must act has no provider. |
| `run_match(max_hands = 0)` | Repeats until one seat has chips, or `max_hands` hands |
| `match_finished()` | No hand in progress and ≤ 1 funded seat |
| `winner_id()` | The last funded seat, or −1 |
| `table()` | Read-only `Table` |

If a provider throws mid-hand, the hand stays pending; calling
`play_one_hand()` again resumes it.

## Externally stepped API

No providers are needed:

```cpp
game.begin_hand();                       // false if match finished; throws if a hand is active
while (game.advance_to_decision()) {     // false when the hand has paid out
  poker::PlayerView v = game.current_view();
  game.apply_action(choose(v));          // validated against cached legal actions
}
auto rewards = game.last_rewards();
```

- `advance_to_decision()` deals streets automatically (including all-in
  run-outs) and runs the showdown. Repeated calls at a pending decision are
  cheap and do nothing.
- `acting_player()`, `hand_in_progress()` expose the state.
- `apply_action` throws `std::logic_error` if no decision is pending and
  `std::invalid_argument` if illegal (state unchanged).

## Results and history

| Method | Meaning |
| --- | --- |
| `last_payouts()` | Gross chips received last hand (may include returned unmatched chips) |
| `last_rewards()` | Net change from before the blinds; sums to zero — use this for learning |
| `decisions_applied()` | Count of actions applied since the last reset |
| `enable_history(bool, reserve = 128)` | Between hands only; off by default (no allocation) |
| `action_history()` | `span<const ActionRecord>` for the current/last hand |

```cpp
struct ActionRecord {
  PokerStage stage; int player_id; PlayerDecision decision;
  int paid;          // chips actually moved by this action
  int current_bet;   // round target after the action
};
```

## Episodes, snapshots and branching

| Method | Notes |
| --- | --- |
| `reset(seed)` | Equal configured stacks, button on seat 0, new seed; abandons any hand |
| `reset_episode(stacks, dealer_id, seed)` | Arbitrary stacks (≥ 2 funded, funded dealer) |
| `snapshot()` | Opaque copy: table, deck, RNG, pots, betting, stage, pending actor, history, counters |
| `restore(snapshot)` | Returns to that exact point; emits no callbacks |

Snapshots exclude the evaluator, observer, providers and any strategy state.
Restoring the same snapshot and applying the same actions reproduces the same
cards — this is how you branch for search or counterfactual rollouts.

## Hand lifecycle (internal)

1. `start_new_hand` — increment hand number, reset pots, `deck.reset()` +
   `shuffle`, record starting stacks, reset players, assign blinds, deal hole
   cards, `on_hand_started`.
2. `post_blinds` — contribute both blinds; target = larger actual payment,
   minimum raise = configured big blind.
3. `enter_stage` — set where the next-actor search begins (after BB pre-flop,
   after dealer later); `on_stage_started`.
4. Actions via `apply_action`; if only one player remains,
   `finish_uncontested_hand`.
5. When a street completes: deal next street, reset round contributions and
   betting state. After the river: `run_showdown` (evaluate, `build_pots`,
   `on_showdown`, `distribute_pots`).
6. `finish_hand` — compute rewards, clear pending state, `on_hand_finished`,
   rotate the dealer if the match continues.

## GameObserver

Optional, trusted listener (sees the full `Table`, including hidden cards).
All callbacks have empty default implementations.

| Callback | When |
| --- | --- |
| `on_hand_started(hand_number, table, pots)` | After dealing, before blinds |
| `on_stage_started(stage, table, pots)` | Start of each street and of Showdown |
| `on_player_action(id, decision, table, pots, betting)` | After an action is applied |
| `on_showdown(table, pots, scores)` | Pots built, before payout |
| `on_hand_finished(table, payouts)` | After payout |

Observers should not throw. Strategies must not be given observer access if
you want honest information hiding.
