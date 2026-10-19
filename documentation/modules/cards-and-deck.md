# Cards, deck and configuration

Files: `config/PokerConfig.hpp`, `card/Card.hpp`, `card/Card.cpp`,
`deck/Deck.hpp`, `deck/Deck.cpp`

## Configuration (`config/PokerConfig.hpp`)

Fixed capacities size every array in the engine:

| Constant | Value |
| --- | --- |
| `MAX_PLAYERS` | 8 |
| `DECK_SIZE` | 52 |
| `HOLE_CARDS_PER_PLAYER` | 2 |
| `MAX_COMMUNITY_CARDS` | 5 |
| `DEFAULT_STARTING_CHIPS` | 2500 |
| `DEFAULT_SMALL_BLIND` | 50 |
| `DEFAULT_BIG_BLIND` | 100 |

```cpp
struct GameConfig {
  int starting_chips = DEFAULT_STARTING_CHIPS;
  int small_blind    = DEFAULT_SMALL_BLIND;
  int big_blind      = DEFAULT_BIG_BLIND;
};
```

`PokerGame` validates a `GameConfig` on construction: 2..8 players, positive
stacks and small blind, `big_blind ≥ small_blind`, and bounds that keep chip
arithmetic inside `int`.

## Card (`card/Card.hpp`)

```cpp
enum class Suit : std::uint8_t { Hearts, Diamonds, Clubs, Spades };   // 0..3
enum class Face : std::uint8_t { Two = 2, …, Ace = 14 };
struct Card { Face face = Face::Two; Suit suit = Suit::Clubs; };      // 2 bytes
```

- A default `Card` is the two of clubs, **not** an "unknown" card. Arrays of
  cards are always paired with a count (`cards_in_hand`,
  `community_card_count`) that says how many entries are real.
- `face_to_string`, `suit_to_string`, `card_to_string` give short labels
  (`"A"`, `"H"`, `"AH"`); unknown enum values give `""`.
- `operator==` / `!=` compare physical identity, not hand strength.
- Where a numeric card ID is needed (sampler, observation encoding) the
  convention is `suit * 13 + (face − 2)`, range 0..51.

## Deck (`deck/Deck.hpp`)

A fixed `std::array<Card, 52>` plus a `next_card_index_`. Cards are never
removed; dealing just advances the index.

| Method | Behaviour |
| --- | --- |
| `Deck()` | Builds an ordered 52-card deck |
| `reset()` | Restores ordered state, index 0 |
| `shuffle(std::mt19937&)` | `std::shuffle` the whole deck, index 0 |
| `deal_card()` | Returns the next card; throws `std::runtime_error` if empty |
| `burn_card()` | Skips the next card; throws if empty |
| `cards_remaining()` / `cards_dealt()` | Counters |

`PokerGame` always calls `reset()` before `shuffle()` so that a hand's deal
depends only on the RNG state, not on the previous hand's deck order. This is
what makes snapshots and episode replay exact.
