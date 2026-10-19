# Getting started

## Requirements

- A C++20 compiler (GCC or Clang; MSVC is supported by the CMake file but the
  project is developed on Linux / Ubuntu under WSL).
- CMake 3.16 or newer.
- A threads library (`find_package(Threads REQUIRED)`).
- `data/hand_tables.bin` — already in the repository. You do **not** need to
  generate it.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`POKER_ENABLE_IPO` (default `ON`) turns on interprocedural optimization for
Release builds when the compiler supports it.

The engine library is compiled with `-Wall -Wextra -Wpedantic` (or `/W4` on
MSVC). The build copies `hand_tables.bin` next to each executable that needs
it; the engine also has the source-tree path baked in through the
`POKER_DEFAULT_HAND_TABLE` compile definition, so the working directory does
not matter for the default evaluator.

## Play in the terminal

```bash
./build/poker_cli          # single-table freezeout tournament (default)
./build/poker_cli --game   # ordinary fixed-blind match
./build/poker_cli --help
```

You choose 2–8 players and always sit in **seat 0**; the other seats are
`AIDecisionProvider` bots. Commands at your turn:

| Command | Short | Meaning |
| --- | --- | --- |
| `fold` | `f` | Leave the hand |
| `check` | `k` | Pass when nothing is owed |
| `call` | `c` | Match the current bet (partial call if short) |
| `raise N` | `r N` | Raise **to** a round total of N (not "by" N) |
| `allin` | `a` | Commit every remaining chip |

Tournament mode prizes (abstract units, pool = 1000 × players):
heads-up pays 70/30 %, three or more players pay 60/30/10 %.

## Run the tests

```bash
ctest --test-dir build --output-on-failure
```

See [Tools and tests](tools-and-tests.md) for what each suite covers.

## All CMake targets

| Target | Built by default | Purpose |
| --- | --- | --- |
| `poker_engine` | yes | Static library: all rules, evaluation, simulation, tournament, analysis |
| `poker_terminal` | yes | Library: `TerminalUI` + `HumanDecisionProvider` (links `poker_engine`) |
| `poker_cli` | yes | Interactive game (`main.cpp`) |
| `poker_tests` | yes | Foundational unit tests |
| `poker_game_tests` | yes | Scripted full-hand regressions |
| `poker_research_tests` | yes | Stepping, snapshots, batching, determinism |
| `poker_analysis_tests` | yes | Equity and ICM |
| `poker_tournament_tests` | yes | Tournament rules |
| `poker_tournament_simulation_tests` | yes | Parallel tournament determinism |
| `poker_terminal_tests` | yes | Terminal input/output |
| `poker_validation` / `run_random_validation` | no | 1,000,000-hand fast-vs-reference evaluator check |
| `poker_benchmark` / `run_poker_benchmark` | no | Evaluator speed benchmark |
| `poker_simulation_benchmark` | no | End-to-end hands/sec benchmark |
| `poker_tournament_simulation` | no | Headless tournament experiment runner |
| `generate_hand_tables` / `regenerate_hand_tables` | no | Rebuild `data/hand_tables.bin` |

Build a non-default target explicitly, for example:

```bash
cmake --build build --target poker_tournament_simulation -j
./build/poker_tournament_simulation 10000 4   # tournaments, worker threads
```

## Using the engine from your own code

Link against `poker_engine` and include headers relative to the project root:

```cpp
#include "game/PokerGame.hpp"
#include "decision/AIDecisionProvider.hpp"

poker::PokerGame game(6, poker::GameConfig{}, /*seed=*/123);
poker::AIDecisionProvider ai;
for (int seat = 0; seat < 6; ++seat) game.set_decision_provider(seat, ai);
game.run_match();
int winner = game.winner_id();
```

Next: [Architecture](architecture.md).
