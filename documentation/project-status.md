# Project status and roadmap

Status as of 2026-10-17 (commit `46c99e7`).

## Other documents in the repository

| File | Contents |
| --- | --- |
| `README.md` | Quick start, plus the author's personal notes and plans for a bot with behavioural profiles |
| `SIMULATION_GUIDE.md` | Detailed guide to the research APIs, rule compatibility notes, validation coverage, measured benchmark baseline |
| `AI_RESEARCH_PLAN.md` | "HoldemLab" mathematical roadmap: evaluation methodology, Monte Carlo equity, ranges, CFR on Kuhn/Leduc, abstractions, neural methods, search, multiplayer, bibliography |
| `PokerEngine_ImplementationPlan.md` | The original step-by-step plan used to build the engine (36 sections, config → main.cpp → tests) |
| `todo.md` | Near-term plan: opponent response model + ICM call/fold experiment (see below) |
| `todo` | Empty file |

## What exists

- Complete no-limit Hold'em engine for 2–8 seats with side pots, split pots,
  short all-ins and heads-up rules.
- Resumable state machine with provider-driven, stepped and batched control,
  snapshots and action history.
- Fast lookup-table evaluator validated against a reference evaluator.
- Single-table freezeout tournaments with rising blinds, tied finishes and
  prize splits.
- Reproducible multi-threaded simulation for cash episodes and tournaments.
- Analysis: Monte Carlo equity (random or range-restricted opponents) and exact
  ICM.
- Terminal play against a baseline bot.
- Seven CTest suites plus optional validation and benchmarks.

## Known limits

- The only bot, `AIDecisionProvider`, ignores its cards and samples legal actions and stack-based raise sizes uniformly.
- No public action history is fed into `PlayerView`; strategies that want it
  must use `PokerGame::action_history()` or track it themselves.
- Betting rules are simplified (see [rules.md](rules.md)): short-raise
  reopening, short-blind targets.
- No antes, rake, re-entry, multi-table tournaments, or time-based levels.
- Tournament play does not use ICM internally.
- Equity ignores future betting and supports only unweighted ranges.

## Planned next steps (from `todo.md` and `README.md`)

1. Give the AI public action information (who did what, amounts) plus
   tournament context, never opponents' hidden cards.
2. Build simple configurable opponent behaviours (cautious, aggressive,
   calling station) that return action **probabilities**.
3. Test that the response model predicts those behaviours correctly.
4. First ICM decision experiment: facing an all-in with ≥ 3 players left,
   compare call vs. fold by sampling opponent cards, settling with the engine,
   and scoring with ICM (or the actual prize if eliminated).
5. Extend to raises and continued play using the response model; evaluate
   against several fixed opponent styles.

The author's longer-term idea (README) is a **behavioural profile** per player
— attributes such as aggression and bluff frequency — that a bot both has and
estimates for opponents from observed actions. Transformers, genetic
algorithms and large learning systems are deliberately postponed until the
simpler approach can be measured.
