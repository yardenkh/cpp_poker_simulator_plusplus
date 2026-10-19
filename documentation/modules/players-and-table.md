# Players and table

Files: `player/Player.hpp`, `player/Player.cpp`, `table/Table.hpp`,
`table/Table.cpp`

## Player

```cpp
enum class PlayerType { Human, AI };

struct Player {
  std::array<Card, 2> hand{};
  int  cards_in_hand = 0;
  int  cash = 0;            // chips not yet in the pot
  PlayerType type = PlayerType::AI;
  bool in_hand = false;     // false after folding (or if unfunded)
  bool all_in = false;      // committed every chip; still in the hand
  void reset_for_new_hand();
  void receive_card(const Card&);
  bool is_eliminated() const;
};
```

- `reset_for_new_hand()` clears cards and `all_in`, and sets
  `in_hand = cash > 0`. Cash and type persist.
- `receive_card` throws `std::runtime_error` on a third card.
- `is_eliminated()` is `cash <= 0`. Only meaningful between hands: an all-in
  player has zero cash mid-hand but may still win.
- `type` is informational; who actually decides is set by
  `PokerGame::set_decision_provider`.

## Table

```cpp
struct Table {
  int num_players = 0;
  std::array<Player, MAX_PLAYERS> players{};
  Deck deck;
  std::array<Card, 5> community_cards{};
  int community_card_count = 0;
  int dealer_id = -1, small_blind_id = -1, big_blind_id = -1;
};
```

`Table` is plain data; all behaviour is in free functions.

### Setup and counting

| Function | Purpose |
| --- | --- |
| `table_init(table, n, chips)` | Equal stacks, button on seat 0, initial blind seats |
| `count_players_with_chips` | Funded seats (match is over when ≤ 1) |
| `count_players_in_hand` | Not folded (includes all-in) |
| `count_players_able_to_act` | In hand, not all-in, has chips |
| `find_only_remaining_player` | Seat ID if exactly one player is in the hand, else −1 |

### Seat searches

`next_player_with_chips`, `next_player_in_hand`, `next_player_able_to_act`
each search clockwise **after** the given seat, wrap around, exclude the start
seat, and return −1 if nothing matches. (The betting search,
`next_player_needing_action`, differs: it checks the start seat last.)

### Button and blinds

- `rotate_dealer` moves the button to the next funded seat.
- `assign_blinds` uses heads-up positions (dealer = small blind) whenever the
  table has two seats **or** only two funded seats remain; otherwise the next
  two funded seats after the button.
- `table_prepare_new_hand` is a convenience helper that shuffles, resets
  players, rotates and assigns blinds. `PokerGame` does **not** use it — it
  rotates after payout instead. Don't mix both paths.

### Dealing

`deal_hole_cards` (one card each, twice, to players in the hand), `deal_flop`,
`deal_turn`, `deal_river` (each burns one card first).
