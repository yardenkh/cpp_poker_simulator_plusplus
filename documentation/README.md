# poker simulator++ — Documentation Library

This library documents the repository **as of 2026-10-17**, at commit
`46c99e7` ("Enhance README with personal notes and project plans") on `main`.
It describes the code that exists at that point; if the code has moved on,
trust the source over these pages.

The project is a C++20 Texas Hold'em engine (2–8 players, no-limit) built for
three uses: interactive terminal play, large reproducible simulations, and
poker-AI research.

## Contents

| Page | What it covers |
| --- | --- |
| [Getting started](getting-started.md) | Requirements, building, running the CLI, running tests, every CMake target |
| [Architecture](architecture.md) | Directory layout, layers and dependencies, how a hand flows through the engine, design principles |
| [Game rules as implemented](rules.md) | Blinds, action order, raises, short all-ins, side pots, odd chips, tournaments |
| [Tools and tests](tools-and-tests.md) | Command-line tools, benchmarks, the lookup-table generator, test suites |
| [Project status and roadmap](project-status.md) | Other docs in the repo, what is done, known limits, planned AI work |
| [Glossary](glossary.md) | Poker and project terms used across the code |

### Module reference

One page per source directory, covering public types, functions and contracts.

| Module | Directories | Page |
| --- | --- | --- |
| Cards and deck | `card/`, `deck/`, `config/` | [modules/cards-and-deck.md](modules/cards-and-deck.md) |
| Players and table | `player/`, `table/` | [modules/players-and-table.md](modules/players-and-table.md) |
| Pots | `pot/` | [modules/pots.md](modules/pots.md) |
| Betting | `betting/` | [modules/betting.md](modules/betting.md) |
| Views and decisions | `view/`, `decision/` | [modules/views-and-decisions.md](modules/views-and-decisions.md) |
| Game orchestration | `game/` | [modules/game.md](modules/game.md) |
| Hand evaluation | `evaluation/`, `data/` | [modules/evaluation.md](modules/evaluation.md) |
| Tournaments | `tournament/` | [modules/tournament.md](modules/tournament.md) |
| Simulation | `simulation/` | [modules/simulation.md](modules/simulation.md) |
| Analysis (equity, ICM) | `analysis/` | [modules/analysis.md](modules/analysis.md) |
| Terminal UI | `ui/`, `main.cpp` | [modules/terminal-ui.md](modules/terminal-ui.md) |

## Suggested reading order

1. [Getting started](getting-started.md) to build and play a hand.
2. [Architecture](architecture.md) for the big picture.
3. [Game](modules/game.md), then [Betting](modules/betting.md) and
   [Pots](modules/pots.md): this is the rules core.
4. [Simulation](modules/simulation.md) and [Analysis](modules/analysis.md) if
   you want to build a bot.

## Snapshot facts

- Language / build: C++20, CMake ≥ 3.16, Threads. Optional IPO in Release.
- Size: about 60 tracked files, ~8,000 lines of C++ (including tests and tools).
- Namespace: everything lives in `namespace poker` (plus `poker::detail` and
  `poker::fast_eval` for internals).
- Lookup data: `data/hand_tables.bin` (~15 MB), pre-generated and committed.
