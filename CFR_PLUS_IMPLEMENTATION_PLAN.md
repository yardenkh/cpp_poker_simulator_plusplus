# CFR+ implementation plan for this engine

> Historical plan: the conversation subsequently moved to external-sampling
> MCCFR with a bounded abstract table, without a separate full-tree CFR/CFR+
> trainer prerequisite. See [MCCFR_PROJECT_HANDOFF.md](MCCFR_PROJECT_HANDOFF.md)
> for the current direction, implementation status and unresolved decisions.

Prepared 17 October 2026. This is an implementation plan; no trainer has been
implemented as part of this review.

## Goal and initial scope

The eventual deliverable is a playable strategy for **each supported player
count, 2 through 8**, with training, saved policies, and evaluation for every
count. Heads-up is the first correctness milestone, not the final product.

Build one C++ training framework with separate versioned strategy artifacts for
each player count. Retain the existing no-limit Hold'em rules. Start with
independent, equal-stack hands and chip-profit rewards; tournament prize utility
is a later, separate objective. The existing `AI_RESEARCH_PLAN.md` recommends
100 big blinds (blinds 1/2, stacks 200); use that as the eventual initial full-hand
configuration. Use shorter stacks and restricted river games for early tests.

Full-game, full-tree CFR+ at that depth is not a practical first implementation.
First validate the algorithm exactly, then solve bounded Hold'em games using
the engine, then add the sampling and abstractions needed for full-hand training.
Record the actual algorithm and game solved in every artifact.

For two-player zero-sum games with perfect recall, use the standard averaged
strategy equilibrium framework. For 3–8 players, the same training machinery is
an empirical strategy-learning approach: no claim that CFR+ converges to Nash
equilibrium. Multiplayer strength needs its own experiments at every count.

## What the code already provides

| Component inspected | Reuse | Gap relevant to CFR+ |
| --- | --- | --- |
| `game/PokerGame.hpp` and `.cpp` | `begin_hand`, `advance_to_decision`, `current_view`, `apply_action` | Chance events are automatic, not explicit enumerable branches |
| `PokerGame::snapshot/restore` | Branch and replay the exact game state, including deck, RNG, betting, rewards, history | Copies state/history; contains actual hidden cards; not an information-set key |
| `PokerGame::last_rewards` | Terminal net chips, including blinds, zero-sum | Normalize by big blind; never use gross `last_payouts` as utility |
| `view/PlayerView.hpp` and `.cpp` | Restricted own-card/public-state observation and legal actions | No complete betting history in the strategy input |
| `PokerGame::action_history` | Public action records when enabled | Disabled by default; not delivered to `DecisionProvider` |
| `decision/DecisionProvider.hpp` | Plug a frozen learned policy into play; seed its action RNG | Only receives `PlayerView`; no history/context argument |
| `betting/Betting.cpp` | Legal action validation, total-round raise targets, short all-ins | Need a bounded action generator that respects this implementation |
| `deck/Deck.hpp`, `table/Table.cpp` | Dealing, burns, exact card ownership | No validated explicit deal/setup API |
| `evaluation/FastHandEvaluator.hpp` | Shared immutable showdown lookup tables | An evaluator ranks hands; it does not compute strategic action values |
| `analysis/Analysis.hpp` | Equity tools and test opponents later | Equity and ICM are not substitutes for CFR terminal utility |
| `simulation/Simulation.cpp` | Reproducible seeds and independent match evaluation | Its episode runner and batcher are not recursive CFR trainers |
| `tests/ResearchTests.cpp` | Snapshot replay, branching, stepping, chip conservation, betting edges | Add information-set, chance, trainer, and policy integration checks |
| `CMakeLists.txt` | C++20 engine library and CTest pattern | Add a solver library, trainer tool, and focused solver tests |

Preserve the documented short-blind/reopening/odd-chip rules initially. Version
them in checkpoints. Adding casino-standard alternatives would be separate work.
`GameObserver` receives the full table; it is trusted infrastructure, not a safe
policy input. `Snapshot` also contains hidden information. Never serialize either
as the player's observation.

## Design

### 1. Training configuration and player count

`TrainingConfig` records player count, initial stacks, blinds, root distribution,
rules version, action abstraction, card abstraction, averaging schedule, seed,
budgets, and algorithm variant. Require 2–8 participants. Validate configuration
before allocating tables. Share implementation across counts, not learned tables.

Differentiate these concepts explicitly:

- Hand-start participants: chooses the player-count strategy artifact.
- Live players: excludes folds; changes during a hand.
- Players able to act: excludes all-ins too.
- Physical seat IDs: the engine's stable array indices.

A six-player hand that becomes heads-up after four folds stays in its six-player
strategy. A later hand beginning with five funded players uses five-player
coverage. If compacting seats for lookup, retain a tested mapping to engine seats
and button-relative positions. Changing player count must not erase betting
history or side-pot eligibility.

Use vector terminal utilities from the start: one entry per seat. For learner i,
counterfactual reach is chance reach times the product of *all other players'*
action reaches. Do not negate a value merely because the next actor changes;
that shortcut is invalid for multiplayer games.

### 2. Information-set keys and strategy context

Introduce a public-history/context type in a neutral header. Move or adapt
`ActionRecord` there to avoid a game/decision include cycle. The key contains:

- Rules/configuration identity and hand-start player count.
- Acting seat/position, initial stacks, current stacks and contribution state.
- Own hole cards; public cards with street reveal chronology preserved.
- Full public betting history, with actors, stages and exact numeric bet amounts.
- Canonical ordered candidate actions, or an assertion that these are identical
  for every history sharing the key.

Exclude opponents' private cards, burns, undealt cards, shuffle seed, scenario
index, snapshot identity, and training iteration. Keep exact cards initially.
Do not bucket by equity alone. Later suit canonicalization must transform all
observed cards consistently; do not sort all five board cards and lose which
card appeared on the turn versus river. Validate perfect recall after abstraction.

During training, build context directly from a paused engine and its history.
For synchronous play, add a backward-compatible context-aware provider method
whose default forwards to existing `choose_action(view)`. Have the game dispatch
that method and enable history when a provider declares it needs history. Existing
simple/human providers continue to work. Missing required context is an explicit
configuration error, not silently a different strategy.

Borrowed history spans are valid only during the decision call. Policy lookup
stores owned keys; it must not retain spans across actions or snapshot restores.

### 3. Legal action abstraction

One shared `ActionAbstraction` generates candidates during training and play:
fold when facing a bet, check or call, minimum raise, half-pot raise, pot raise,
and all-in, when legal. Excluding a free fold is an intentional action restriction.

For a pot fraction q, before integer rounding define:

    P = current pot, C = actual additional call payment, c = own round contribution
    raise_to = c + C + q * (P + C)

Use integer/rational arithmetic with a documented rounding rule and safe
intermediates. If raising is legal, clamp full-raise targets to
`[min_raise_to, max_raise_to]`, validate every decision, preserve legal short
all-ins separately, and deduplicate equivalent transitions (e.g. all-in call
versus call, maximum full raise versus all-in). Stable ordering gives stable
action indices. The action set must depend only on the learner's information.

Do not introduce a silent raise cap to control recursion. A cap changes the
abstract game and must be specified as part of that game if later used.

### 4. CFR+ state and exact update schedule

`RegretTable` maps owned information-set keys to ordered action IDs, nonnegative
regret-like accumulators Q, and average-strategy sums. Start with `double` values.
Use full-key equality, not an unchecked hash-only identity. Choose uniform legal
probabilities if all accumulators are zero.

For each outer iteration, run one learner pass for each seat in a fixed order:

1. Freeze strategies for the complete learner pass, including all chance worlds.
2. Recursively evaluate every candidate action, including zero-probability ones.
3. Accumulate counterfactual regret deltas across every history in an information
   set, including all hidden-card assignments in the declared chance model.
4. Only after that aggregation, set `Q[I,a] = max(0, Q[I,a] + delta[I,a])` for
   the learner's information sets. Clipping each hidden-history contribution
   separately would produce a different algorithm.
5. Recompute the learner's strategy before the next player's pass.

Conditional action utilities returned by recursion use the learner's terminal
net profit. Compute `node_value = sum(strategy[a] * child_value[a])` and weight
the action-minus-node difference by chance and opponents' reach. Apply chance
probabilities exactly once. Choose one utility convention throughout; do not
mix already counterfactually weighted values with conditional child utilities.

Specify averaging at a precise boundary: accumulate the learner's pre-update
policy once per outer iteration, before its pass update, with weight
`max(iteration - delay, 0) * own_reach`. Visit each information set once for this
purpose, regardless of how many hidden histories share it; perfect recall makes
own reach identical there. Default delay is zero. Retain averaged and current
policies for comparison, using the average as the default export. Use a documented
uniform fallback where average mass is zero.

Implement ordinary CFR as a reference variant in the same framework. No clipping
for that variant and uniform iteration weighting. Keep its variant flag visible
in outputs and checkpoints.

### 5. Engine traversal and chance handling

`HoldemAdapter` owns a `PokerGame` with no observers/providers attached during
training and history enabled. At a decision, save a snapshot, restore it before
each candidate, apply that candidate, advance, and recurse. Restore caller state
on return, including terminal branches. Stop after one hand and read
`last_rewards()[learner] / big_blind` even when the hand ends directly in
`apply_action` through folds. Never start a second hand during a traversal.

Add a narrow validated explicit-deal facility: a `DealPlan` specifying a full
physical deck permutation, installed at hand start instead of shuffling. Keep
ordinary seeded play as the default. Validate unique/in-range cards, ensure the
same hole-deal/burn/board order, include setup in replay tests, and expose no
mutable full-table setter to policies. Invalid setup must leave the game intact.

This facility supports exact river roots: declare a public board, a fixed legal
public action prefix and initial stacks, enumerate compatible private-hand
assignments with declared joint weights, build each deck, and replay the prefix
to the same public river state using real engine actions. Validate all roots have
matching public state. The probability model is the explicitly supplied range
model; it is not automatically the posterior of a full-game equilibrium.

A complete deck fixes one possible world. Evaluating many branches on one such
deck is not full-tree CFR over real Hold'em chance outcomes. Repeatedly selecting
decks is chance sampling; a finite fixed deck list solves only that empirical
chance game. Never use deck/scenario identity in policy lookup.

Exact full-game chance traversal would require an additional public chance
enumeration layer. Defer that scale: deterministic deals plus exact finite river
roots suffice for the first engine solver. A river strategy is local to its
declared board/ranges/history, not a globally safe full-game resolver.

### 6. Scaling to full hands and 2–8 players

Start exact engine integration on a small heads-up river root. Generalize the
same adapter to small 3-player roots immediately afterward to exercise vector
utilities, multiple opponents' reach, folds, unequal commitments and side pots.
Smoke-test traversal/configuration for every count through 8 before large runs.

Before preflop-to-river training, add bounded card abstraction and measure table
growth and tree size for every count. A small action set alone does not make the
full card tree manageable. Expand only within explicit memory/node/time budgets.
Budget exhaustion returns a clear incomplete result; do not pretend an arbitrary
depth cutoff with zero payoff is CFR+ terminal utility.

Add external-sampling MCCFR as a separately named, validated baseline for larger
games. Sample chance and all opponents, enumerate the learner's actions, and
rotate the learner across every seat. Derive sampling and average-policy updates
for the chosen n-player algorithm; do not copy a two-player averaging shortcut.
Test estimator means against exact tiny-tree updates. Clipping noisy samples is
nonlinear, so a sampled CFR+ hybrid requires its own specification and benchmarks.
Do not label it identical to the paper's full-tree CFR+.

Maintain one strategy profile per player-count/configuration, covering every
position. Compare exact CFR+, sampled baselines and any later hybrid by elapsed
time, memory and held-out strength. For counts where full-tree training is
infeasible, keep the exact solver as the reference and use the validated scalable
method for the playable strategy. Neural regret approximation is optional if
measured table memory becomes the limiting factor.

Optimize after profiling: compact node IDs, preallocated traversal storage,
cached legal action lists, then state-copy or apply/undo improvements. Fixed-point
compression and distributed subgames are later projects. Parallel learner passes
must use a consistent frozen strategy and combine deltas before clipping; do not
race on a shared regret table. Reuse seed splitting, not the simulation runner as
a training algorithm.

### 7. Playable policies, checkpoints and unsupported situations

Save resumable training state and compact read-only average-policy exports.
Checkpoint after complete outer iterations initially. Persist algorithm/update
schedule, player count, rules, stack/blind coverage, root model, action/card
versions, tables, iteration, averaging state and relevant RNG state. Use an
explicit format, not raw C++ struct bytes. Write atomically and validate loads.

`CFRDecisionProvider` samples a frozen policy with its own episode-seeded RNG.
`StrategyRegistry` routes hand-start counts 2–8 to compatible artifacts. It
refuses incompatible configuration in strict evaluation. Track coverage misses.

Actual opponents can use bet sizes absent from training. First integration uses
opponents restricted to the same abstraction. Before ordinary CLI deployment,
specify and test numeric bet translation or local resolving. A documented legal
fallback may allow unsupported states to continue, but log it and do not report
fallback actions as trained coverage. Later stack variation/tournament use also
needs appropriate training coverage and seat mapping.

## Implementation order and completion checks

| Step | Concrete deliverable | Completion check |
| --- | --- | --- |
| 1 | Solver config, utility vectors, action generator, public context and key | 2–8 counts represented; valid/deduplicated actions; hidden-card changes do not alter keys/probabilities; distinct remembered histories remain distinct |
| 2 | Exact generic CFR/CFR+ core with a small Kuhn adapter | Hand-computed deltas and clipping agree; alternating schedule verified; independent best response; average exploitability below 0.005 ante units within a recorded budget |
| 3 | Leduc adapter and small multiplayer fixtures | Card/chance weighting checked independently; multiplayer utilities/reach verified; no two-player sign shortcuts; progress recorded without claiming multiplayer Nash convergence |
| 4 | Explicit deal setup, engine adapter, tiny exact river roots | Snapshot branch isolation, prefix replay, reward conservation, legal bet generation and exact infoset-respecting best responses agree |
| 5 | Checkpoint/export plus engine policy integration | Resume equals uninterrupted training on the same runtime; frozen policy seeded replay; human/simple providers still work; strict configuration and coverage checks |
| 6 | All-count integration suite and sampled baseline | 2–8 traversal and policy routing work; sampling estimators checked; each count has independent evaluation reports |
| 7 | Full-hand abstract training for counts 2–8 | Separate artifacts with all positions and declared stack coverage; predeclared per-count budgets; held-out population and vulnerability results; no undeclared fallbacks |
| 8 | Larger stack/card/action coverage and performance work | Each expansion has measured cost/benefit and retains correctness checks; player counts do not silently share incompatible policies |

First coding slice: steps 1–2, including the multiplayer-capable data structures.
First solver using real Hold'em engine transitions: step 4. First end-to-end
playable learned-policy integration: step 5. A strategy for every requested
player count is not complete until step 7 produces and evaluates all seven
artifacts; passing heads-up tests alone does not satisfy that goal.

## Evaluation and test placement

Add `poker_solver` linking `poker_engine`, an explicit `poker_train` tool, and
CTest targets for solver math, engine adapter, and learned-policy integration.
Suggested modules: `research/cfr/`, `research/games/`, `research/holdem/`,
`decision/CFRDecisionProvider.*`, `tools/TrainCFR.cpp`, and dedicated tests.

Exact best responses must choose one action per information set, not different
actions after looking at hidden cards. For small two-player games report
NashConv/2. For tiny multiplayer games compute unilateral improvement for each
seat and sum as NashConv, with its definition stated; do not treat this as the
two-player security guarantee. On full Hold'em, use restricted/learned best
responses as vulnerability probes, not exact exploitability measurements.

Evaluate each count separately using frozen checkpoints, balanced seat/button
rotations, shared deal scenarios where valid, independent policy RNGs, confidence
intervals and held-out seeds/opponents. Multiplayer lineups should include one
candidate among reference opponents and multiple candidate copies among diverse
opponents. Self-play zero-sum accounting alone says nothing about strength. Six-
player success does not establish eight-player strength.

Required regression cases include identical public histories across different
hidden deals, same final pot with different histories, turn/river chronology,
short all-in calls/raises, no reopened raise rights, duplicate action targets,
off-tree bets, zero-reach states, terminal folds, side pots, ties/odd chips,
history-enabled snapshot restores, and all hand-start counts 2–8. Existing
engine/research tests remain required when their interfaces are changed.

No tests were run for this planning-only change. Engine observations above came
from source review, not a claim of a fresh successful build.

## References

- Existing project direction: `AI_RESEARCH_PLAN.md`, especially sections 2–4,
  10–11 and 15–17; current rules in `documentation/rules.md` and
  `SIMULATION_GUIDE.md`.
- [Heads-up Limit Hold'em Poker is Solved](https://johanson.ca/publications/poker/2015-science-hulhe/2015-science-hulhe.pdf).
- [CFR+ algorithm](https://jeskola.net/cfrp_mt.pdf).
- [Detailed CFR+ companion paper](https://poker.cs.ualberta.ca/publications/2015-ijcai-cfrplus.pdf).
- [Revisiting CFR+ and Alternating Updates](https://arxiv.org/abs/1810.11542),
  which supplies corrected proofs for alternating-update analysis.
- [Superhuman AI for multiplayer poker](https://noambrown.github.io/papers/19-Science-Superhuman.pdf),
  relevant to multiplayer limitations and empirical evaluation, not a claim that
  implementing CFR+ reproduces Pluribus.
