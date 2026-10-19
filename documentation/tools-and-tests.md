# Tools and tests

## Tools (`tools/`)

None of these are built by default; build them by target name.

### `poker_tournament_simulation` — `tools/TournamentSimulation.cpp`

```bash
./build/poker_tournament_simulation [episodes] [workers]   # defaults 1000, 1
```

Runs complete six-seat tournaments with `AIDecisionProvider` at every
seat, prizes `{3000, 1800, 1200}` units, default blind schedule. Prints
tournaments, hands, decisions, seconds, tournaments/second, checksum, and per
seat wins and total prize. Rejects non-numeric or extra arguments.

### `poker_simulation_benchmark` — `tools/BenchmarkSimulation.cpp`

```bash
./build/poker_simulation_benchmark [episodes] [workers]    # defaults 200, 1
```

End-to-end throughput, three trials per workload, median reported. Workloads at
2 and 6 seats: passive/showdown (check-call), simple AI, fold-heavy (deep
stacks, 100 hands/episode), then minimum-raise and all-in policies (short
stacks 500 with 10/20 blinds, 10× episodes, 10 hands each). Build in Release for
meaningful numbers. Reference figures are in `SIMULATION_GUIDE.md`
(e.g. ~594k six-player showdown hands/sec on one worker, ~1.8M on four).

### `poker_benchmark` / `run_poker_benchmark` — `tools/BenchmarkFastEvaluator.cpp`

Times the fast lookup evaluator against the reference evaluator on the same
pre-generated hands.

### `poker_validation` / `run_random_validation` — `tools/RandomValidation.cpp`

Compares fast vs. reference scores on 1,000,000 reproducible random seven-card
hands.

### `generate_hand_tables` / `regenerate_hand_tables` — `tools/GenerateFastHandTables.cpp`

Writes `data/hand_tables.bin`. Only needed if the table format changes. See
[modules/evaluation.md](modules/evaluation.md).

## Tests (`tests/`)

All registered with CTest; run with `ctest --test-dir build --output-on-failure`.
The tests are plain executables (no external framework).

| Executable | Source | Covers |
| --- | --- | --- |
| `poker_tests` | `PokerTests.cpp` | Card formatting, deck, player, five/seven-card reference evaluator, fast evaluator, table helpers |
| `poker_game_tests` | `GameTests.cpp` | Scripted full hands: action order, all-in run-outs, side pots, chip conservation, randomized legal games at 2/3/6/8 seats |
| `poker_research_tests` | `ResearchTests.cpp` | Stepped vs. synchronous equivalence, snapshot branching/replay, batching, card sampler, observation encoding, 1-vs-4-worker determinism |
| `poker_analysis_tests` | `AnalysisTests.cpp` | Seeded equity, invalid observations, exact river equity, ranges, ICM |
| `poker_tournament_tests` | `TournamentTests.cpp` | Blind progression, tied and unequal finishes, prize splits, config validation and retry after a provider error, reproducibility |
| `poker_tournament_simulation_tests` | `TournamentSimulationTests.cpp` | Parallel tournament runs match direct seeded runs |
| `poker_terminal_tests` | `TerminalTests.cpp` | Scripted terminal input, no leakage of hidden cards |

`poker_tests` runs with its executable directory as the working directory so it
can find its copy of `hand_tables.bin`.

According to `SIMULATION_GUIDE.md`, the suites have been run in Debug, Release
and an ASan + UBSan build.
