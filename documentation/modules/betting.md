# Betting

Files: `betting/Betting.hpp`, `betting/Betting.cpp`,
`betting/BettingInternal.hpp`

The betting module answers three questions: *what may this player do?*, *what
happens when they do it?*, and *who acts next?* It holds no chips itself;
`PotManager` records payments and `Table` holds player status.

## Types

```cpp
enum class PokerStage { PreFlop, Flop, Turn, River, Showdown };
std::string poker_stage_to_string(PokerStage);   // throws on unknown value

enum class ActionType { Fold, Check, Call, RaiseTo, AllIn };

struct PlayerDecision {
  ActionType action = ActionType::Fold;
  int raise_to = 0;     // RaiseTo only: TOTAL for this round, not the increment
};

struct LegalActions {
  bool can_fold, can_check, can_call, can_raise, can_all_in;
  int amount_to_call;   // extra chips needed to match current_bet
  int min_raise_to;     // current_bet + minimum_raise_size
  int max_raise_to;     // round contribution + remaining cash
};

struct BettingRoundState {
  int current_bet = 0;              // per-player round target
  int minimum_raise_size = 0;       // size of the last full raise
  std::array<bool, MAX_PLAYERS> acted_since_last_full_raise{};
};
```

## Functions

| Function | Purpose |
| --- | --- |
| `betting_state_reset(state, bet, min_raise)` | Start a round; clear acted flags |
| `amount_to_call(pots, state, id)` | `max(0, current_bet − round contribution)` |
| `get_legal_actions(table, pots, state, id)` | Compute `LegalActions`; all false for invalid, folded, all-in or broke seats, or when there is nothing to decide |
| `validate_decision(table, pots, state, id, d)` | Check a decision against freshly computed legal actions |
| `validate_decision(actions, d)` | Check against already-computed actions (used by `PokerGame`, `BatchSimulator`) |
| `execute_decision(table, pots, state, id, d)` | Validate, then apply; throws `std::invalid_argument` without changing state if illegal |
| `player_needs_action(...)` | Does this seat need a turn now? |
| `betting_round_complete(...)` | True if no seat needs a turn |
| `next_player_needing_action(..., after)` | Next seat after `after` (wrapping, start seat checked last) that needs a turn, or −1 |

`detail::apply_validated_decision` (in `BettingInternal.hpp`) is the single
mutation path. It is only called after validation against the same state.

## Key semantics

- **Raise rights.** `can_raise` requires the player's acted flag to be false,
  an opponent able to respond, and enough chips for a full raise.
  `can_all_in` is always available when the all-in is only a call; as a raise it
  needs open rights and a responding opponent, but may be below the minimum.
- **Full vs. short raise.** After any action, if the player's round
  contribution exceeds the old `current_bet`, the bet goes up. If the increase
  is at least `minimum_raise_size` it is a full raise: the minimum raise size
  becomes that increase and every acted flag is cleared. Otherwise only the
  target changes.
- The acting player's flag is set **after** any reset, so the raiser does not
  need to act again unless re-raised.
- A player who owes chips always needs action, even if their flag is set (for
  example after a short all-in on top of their call).

See [rules.md](../rules.md) for worked examples.
