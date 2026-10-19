# Hand evaluation

Files: `evaluation/HandEvaluator.*`, `evaluation/FastHandEvaluator.*`,
`evaluation/FastHandTableFormat.hpp`, `data/hand_tables.bin`,
`tools/GenerateFastHandTables.cpp`

There are two evaluators. The **reference** evaluator is simple and obviously
correct; the **fast** evaluator is what the engine uses. Tests and
`poker_validation` check that they always agree.

## Reference evaluator (`HandEvaluator.hpp`)

```cpp
enum class HandRank { HighCard, OnePair, TwoPair, ThreeOfAKind, Straight,
                      Flush, FullHouse, FourOfAKind, StraightFlush };
using HandScore = std::uint32_t;   // larger = stronger
```

`HandScore` packs the category and up to five tie-break ranks, four bits each:

```
bits: [category:4][r1:4][r2:4][r3:4][r4:4][r5:4]
```

so plain integer comparison orders hands correctly.

| Function | Purpose |
| --- | --- |
| `evaluate_five_cards(array<Card,5>)` | Score exactly five cards (handles the A-2-3-4-5 wheel) |
| `evaluate_seven_cards(array<Card,7>)` | Best of all 21 five-card subsets |
| `evaluate_player_hand(table, id)` | Player's two cards + full board; throws for invalid seats or incomplete cards |
| `hand_rank_from_score`, `hand_rank_to_string`, `hand_score_to_string` | Decoding / display |

## Fast evaluator (`FastHandEvaluator.hpp`)

```cpp
using DenseHandScore = std::uint16_t;  // 1..7462, larger = stronger; 0 = invalid
```

The 7,462 values are the distinct five-card equivalence classes, in strength
order. `FastHandEvaluator::hand_rank(score)` maps ranges to `DenseHandRank`:

| Category | Dense score range |
| --- | --- |
| High card | 1 – 1,277 |
| One pair | 1,278 – 4,137 |
| Two pair | 4,138 – 4,995 |
| Three of a kind | 4,996 – 5,853 |
| Straight | 5,854 – 5,863 |
| Flush | 5,864 – 7,140 |
| Full house | 7,141 – 7,296 |
| Four of a kind | 7,297 – 7,452 |
| Straight flush | 7,453 – 7,462 |

### API

| Method | Notes |
| --- | --- |
| `FastHandEvaluator(path)` | Loads and validates the table file; throws `std::runtime_error` on missing / truncated / incompatible files. Non-copyable, movable. |
| `evaluate_unsafe(c0..c6)` | Hot path. Seven valid, distinct cards required; no checks |
| `evaluate(array<Card,7>)`, `evaluate(c0..c6)`, `evaluate_seven_cards(...)` | Same, convenience forms |
| `packed_score(dense)` | Convert to the reference `HandScore` format |
| `static hand_rank(dense)` | Category |
| `default_fast_hand_evaluator()` | Process-wide lazily loaded instance (thread-safe), path from CMake's `POKER_DEFAULT_HAND_TABLE`, else `data/hand_tables.bin` |

### How it works

For each card the evaluator:

- adds `kRankHash[rank]` to a running sum, and
- sets bit `rank` in that card's suit mask.

If any suit mask has ≥ 5 bits, the answer is `flush_table_[mask]` (8,192
entries; handles straight flushes too). Otherwise it is
`non_flush_table_[sum]`. The rank-hash weights are chosen so every legal
seven-card rank multiset has a unique sum; the largest is 7,825,759, so the
non-flush table has 7,825,760 `uint16_t` entries (~15 MiB).

### Table file format (`FastHandTableFormat.hpp`)

```
TableFileHeader {
  char     magic[8] = "PKREVAL1";
  uint32_t version = 1;
  uint32_t endian_marker = 0x01020304;
  uint32_t non_flush_entries = 7,825,760;
  uint32_t flush_entries = 8,192;
  uint32_t packed_score_entries = 7,463;
}
uint16_t non_flush[7,825,760]
uint16_t flush[8,192]
uint32_t packed_score[7,463]
```

Raw native-endian bytes; the endian marker detects mismatches. Changing the
layout requires bumping `kTableVersion`.

### Regenerating the table

Normally never needed. If the format changes:

```bash
cmake --build build --target regenerate_hand_tables
cmake --build build --target run_random_validation   # verify against reference
```
