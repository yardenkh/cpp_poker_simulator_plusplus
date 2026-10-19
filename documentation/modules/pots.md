# Pots

Files: `pot/Pot.hpp`, `pot/PotManager.hpp`, `pot/PotManager.cpp`

## Pot

```cpp
struct Pot {
  int amount = 0;                                  // includes folded players' chips
  std::array<bool, MAX_PLAYERS> eligible_players{}; // permission to win this layer
};
```

## PotManager

Tracks every chip a player puts in, in two time frames — the whole hand and the
current betting round — and later turns those records into pot layers and
payouts.

| Method | Behaviour |
| --- | --- |
| `reset_for_new_hand()` | Clears hand totals, round totals and built pots |
| `reset_round_contributions()` | New street: clears round totals only |
| `contribute(table, id, amount)` | Moves `min(cash, amount)` from the player; sets `all_in` if that empties them; returns the amount actually paid; non-positive requests pay nothing |
| `total_pot_amount()` | Sum of all hand contributions (works before building pots) |
| `total_contribution(id)` / `round_contribution(id)` | Per-player records |
| `build_pots(table)` | Builds main + side pot layers (below) |
| `distribute_pots(table, scores)` | Pays every layer to its best eligible hands; returns winnings per seat; then resets |
| `award_entire_pot(table, winner)` | Uncontested win: pays the full total to one seat, then resets; throws `std::out_of_range` for an invalid seat |
| `num_pots()` / `pot(i)` | Inspect built layers (`pot` throws `std::out_of_range`) |

### Building layers

1. Copy and sort the per-player hand totals.
2. For each distinct non-zero level, every player who contributed at least that
   much adds `(level − previous level)` to the layer; those still in the hand
   are eligible.

### Distributing

For each layer: find the highest `HandScore` among eligible players, split
equally among ties, then hand out remainder chips one at a time starting from
the first seat after the dealer. Cash is only credited after **all** layers are
computed, so an error (`std::logic_error` for a non-empty pot with no eligible
player) leaves stacks untouched.

`distribute_pots` takes packed `HandScore`s (`std::uint32_t`, larger is
better). `PokerGame` converts the fast evaluator's dense scores with
`packed_score()` before calling it.
