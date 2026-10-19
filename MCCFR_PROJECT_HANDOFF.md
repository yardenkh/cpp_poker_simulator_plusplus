# Poker AI: current direction and conversation handoff

Last updated: **17 October 2026**, Asia/Jerusalem.
that's the file i should read while continuing this project.

This document captures where the discussion stopped. Read it before proposing
the next implementation step. It distinguishes agreed direction, existing code,
proposed architecture, and decisions we have not made yet.

## 1. What we want to build

Build a poker strategy-learning system for the existing **C++ no-limit Texas
Hold'em engine**, eventually supporting **every player count from 2 through 8**.
The user wants strategies as close to Nash equilibrium as practical, rather than
an agent that merely beats the existing simple AI.

The current direction is **external-sampling MCCFR with a bounded abstract
strategy table**. Do not assume an exhaustive table of exact game situations.
Neural approximation and local solving are possible later extensions, not
requirements already chosen for the first implementation.

Important user preferences established in the conversation:

- Focus development on the actual 2–8-player Hold'em goal.
- Do not build a separate full-tree CFR trainer just as a prerequisite.
- Kuhn poker can be a small correctness test for the same MCCFR implementation;
  learning a strong Kuhn strategy is not a product goal or a large project phase.
- Keep research/test games in dedicated directories, separate from engine rules.
- Explain concepts and architecture before turning them into a large file list.
- Add useful comments explaining rules, assumptions, state ownership and tests.
- Storage matters. Some space is acceptable, but the actual RAM/disk budget is
  still unknown. Do not promise that a particular abstraction fits or is strong.
- A sparse exact-state table that mostly falls back to random play would not
  satisfy the user's goal.

## 2. The most recent discussion: what abstraction actually means

The user now understands why grouping situations may make a table feasible.
Their latest technical questions were:

> How do we group hands well when suits, ranks, draws and other information can
> matter together? How do we avoid losing too much strategy quality? Are we
> storing a tree, or something else?

We explained that a deterministic **encoder maps the player's information to an
abstract information-set key**. Inputs that produce the same key share one
learned distribution. We generally do not train separate exact snapshots first
and then average their completed policies together.

### Lossless grouping: suit symmetry

Suit labels can be renamed consistently across a complete observed card history:

    Own cards A-spades K-spades, flop Q-spades 7-spades 2-diamonds
    Own cards A-hearts K-hearts, flop Q-hearts 7-hearts 2-diamonds

With identical non-card context, these are strategically equivalent. But changing
only the hole-card suits while leaving the board unchanged is not equivalent:
it can remove the flush draw.

Proposed first reduction: canonical suit representations. Preserve relationships
between own cards and board cards, and preserve which cards appeared on each
street. Never sort all five board cards together and erase turn/river chronology.
There are at most 24 global suit-name permutations; this reduction alone does
not make full no-limit Hold'em tractable.

Preflop has 169 own starting-hand classes up to suit symmetry (pairs, suited and
offsuit rank combinations). That is not 169 total strategy entries: positions,
opponent count, stacks and betting histories still distinguish decisions.

### Approximate grouping: strategically similar hands

When exact symmetry is insufficient, map similar card situations to buckets.
This introduces approximation; a solution in the abstract game is not
automatically an equilibrium of unrestricted real poker.

Do not group solely by a single equity percentage. A made hand and a draw can
have similar current average equity but very different future value and actions.
Candidate features discussed include:

- Current strength against a declared reference distribution of opponent hands.
- The distribution of possible future hand strengths over legal future boards.
- Flush/straight possibilities and whether improvements make the strongest hand.
- Blockers that remove important opponent holdings.
- Context relevant to different player counts.

These are candidate features, not an agreed final feature list or a proof that
all important strategic differences are captured. Reference ranges must be stated;
equity against uniform hands is not equity against every continuing range.

Proposed construction process:

1. Generate representative own-hand/public-board combinations.
2. Compute features using hypothetical compatible opponent cards and runouts.
3. Cluster similar feature descriptions under a selected distance/metric.
4. Assign bucket IDs and save the encoder/codebook.
5. Use exactly that versioned mapping in both training and play.

Build street-appropriate groupings; the river has no future board cards, while
earlier streets do. Sampling for features must never inspect the live simulator's
actual hidden opponent cards, folded cards or undealt deck.

We have not selected the clustering algorithm, distance metric, number of buckets,
or whether/how buckets depend on the public board and number of players.
Distribution-aware clustering is a research starting point, not a settled design.

### Card abstraction is only part of the key

An illustrative key is:

    rules/configuration version
    hand-start player count
    acting position/seat mapping
    street and remembered public information
    abstract card representation
    public betting history
    relevant stacks/contributions
    stable legal candidate-action identity

This is a conceptual list, not a finalized struct. Card bucket 42 must not mean
the same action distribution in every betting situation. The surrounding context
still matters. Public-board information can be represented in different places
in the abstraction, but must not accidentally disappear from the strategy input.

Initially preserve public betting history and fix starting stack configurations.
Additional stack/history compression must be deliberate and evaluated. If the
representation forgets earlier abstract observations, it can introduce imperfect
recall; do not keep asserting perfect-recall guarantees without checking.

More detail does not automatically produce stronger finite-budget results:
it increases storage and spreads training over more entries. The goal is to
preserve distinctions that affect decisions within the available resources.

## 3. Do we save a tree?

The complete game tree is a conceptual description of all continuations. We do
not intend to allocate every complete hidden-card node in that tree.

Three separate representations were explained:

| Representation             | Role                                                    | Lifetime                        |
| -------------------------- | ------------------------------------------------------- | ------------------------------- |
| Engine game state/snapshot | Cards, stacks, pot, legal moves, current branch         | During traversal and branching  |
| Information-set key        | Player-visible situation after the chosen abstraction   | Used for learning/policy lookup |
| Strategy entry             | Ordered actions, regrets, average-strategy accumulators | Persistent training data        |

The initial storage can be a dictionary from keys to entries. Later, compact
IDs and contiguous numeric arrays can replace long keys/allocations. A public
betting tree could be stored separately to avoid duplicating action-history
strings across entries. That is optional; it is not the complete private-state
game tree.

New exact situations can map to existing abstract entries. However, having a
defined bucket encoder does not prove that every context-plus-bucket entry has
been trained. We still need coverage checks, and a policy for unsupported states
and unusual opponent bet sizes. Uniform initialization during training must not
be presented as learned deployment coverage.

## 4. Proposed component responsibilities

### Existing engine

Owns cards/deck, players, stacks, pots, betting rules, public action records,
turn order and payouts. Keep one implementation of those rules.

### Hold'em adapter and encoder

Connects engine operations to the learner. Supplies current actor, terminal
status, legal candidate actions, snapshot/restore and utility. Builds strategy
keys from player-visible information, not raw snapshots.

The adapter can inspect complete state for legitimate simulation/evaluation,
but action probabilities must not depend on invisible opponent cards or deck.

### MCCFR trainer

Owns training configuration, iterations, sampling RNG, traversal logic and
learning state. It can own engine instances; this does not mean duplicating
their internal card or betting rules.

Player count is specified in configuration and recorded in saved policies.
The engine remains authoritative for the actual actor/state. Use one shared
implementation that handles all supported player counts, with compatible
strategy artifacts evaluated separately for each count/configuration.

### Strategy table and playing policy

Each entry has a variable-length ordered action list, accumulated regrets and
average-strategy sums. A generic trainer learns the number of actions from the
adapter; it does not hard-code fold/call/raise or assume three outputs.

The playing policy loads a frozen average strategy, encodes the current player
information, retrieves probabilities and samples a legal action. Training need
not run while playing unless online solving is intentionally added.

## 5. External-sampling MCCFR, conceptually

For one learner seat:

1. Start a sampled hand using the real engine.
2. At another player's decision, sample one action from that player's strategy.
3. At the learner's decision, explore every candidate action, restoring the
   engine state before each alternative.
4. At terminal hands, return the learner's net profit in big blinds.
5. Compare sampled action values with the current strategy's weighted value and
   accumulate regret updates at the learner's abstract information sets.
6. Repeat with each seat as learner; maintain an appropriately sampled average
   strategy for the chosen multiplayer algorithm.

Ordinary regret matching uses positive accumulated regrets to form probabilities,
or uniform legal probabilities when there are no positive regrets. Do not silently
add CFR+ clipping to sampled updates and claim the full-tree CFR+ results.

The exact update boundary, sampling schedule, weighting and averaging method
are still design work. Multiplayer averaging cannot blindly copy a two-player
shortcut. Check the chosen estimator against hand-calculated small examples.

Use the designated learner's terminal utility throughout recursion. In multiplayer,
negating a value just because the next actor changes is incorrect. Keep sampling
RNG separate from engine snapshots so restoring a branch does not accidentally
rewind the trainer's random choices.

MCCFR reduces work per traversal; it does not by itself bound table growth or
make the entire 2–8-player state space small.

## 6. Candidate bet sizes and real-game compatibility

The engine has integer-chip no-limit betting, potentially many legal amounts.
External sampling explores all candidate learner actions, so a bounded action
abstraction is expected. The illustrative menu was fold, check/call, minimum
raise, half-pot raise, pot raise and all-in, where legal.

The final menu is not selected. It needs legal target calculation, stable ordering,
and deduplication when two labels cause the same transition. In this engine,
RaiseTo means total chips contributed in the current round, not additional chips.

Each abstract information set must have a consistent candidate action identity.
Do not merge contexts with incompatible action sets unnoticed. Real opponent bet
amounts outside the menu require explicit translation or solving. Keep actual
chips and engine legality correct even if policy lookup uses an abstraction.

## 7. Player counts and equilibrium expectations

Distinguish hand-start participants, players still in the hand, players able to
act, and physical seat IDs. A six-player hand that reaches two live players after
folds remains part of the six-player strategy; do not discard earlier history and
switch to a separately trained heads-up starting-game policy.

The user's objective is near-Nash play. Be precise about what can be supported:

- A Nash equilibrium is a strategy profile for all seats with no profitable
  unilateral deviation, holding other strategies fixed.
- Standard equilibrium convergence arguments apply to two-player zero-sum
  perfect-recall games under their algorithmic assumptions.
- Standard MCCFR does not supply the same Nash convergence guarantee for
  3–8-player poker. Pluribus demonstrated strong six-player play, not a general
  multiplayer equilibrium certificate.
- Small exploitability in an abstract game is not proof of small exploitability
  in unrestricted Hold'em.
- Neural generalization and local solving do not automatically remove these
  limitations.

Use independent exact checks where tractable. For larger games, measure frozen
strategy performance across diverse opponents and use adversaries to probe
unilateral improvements. Such probes do not certify the absence of other exploits.

## 8. Resources and alternatives discussed

The user first rejected a vast exact-state lookup table, then preferred a bounded
abstract table after understanding that one entry can represent many situations.
Do not revert to "neural first" as an already agreed requirement.

Illustrative capacity calculation: five 32-bit action probabilities use 20 bytes
per entry; five 64-bit regrets plus five 64-bit average accumulators use 80 bytes,
before keys, action IDs, indexing and allocator overhead. A planning assumption
of 128 bytes per compact training entry gives about 15.6 million entries in 2 GB,
62.5 million in 8 GB, or 125 million in 16 GB (decimal units). These are arithmetic
examples, not measured implementation sizes or predictions of strategy quality.

Actual CPU, RAM, GPU and disk budgets are unknown. The engine defaults are
2,500 chips with blinds 50/100 (25 BB); the older research plan proposed a 100 BB
configuration. Training configuration is not yet agreed. Chip denomination and
stack depth change the size of no-limit games substantially.

Possible later roles for neural models: approximate regret/average strategy or
continuation values. Training requires sample memories and computation in
addition to model files. Possible later roles for local solving: refine a learned
strategy and handle the current public situation with consistent hand ranges.
Avoid independently solving only the actual hand from scratch using fixed weak
opponents and calling the result equilibrium play.

## 9. What is already implemented and verified

Added and commented:

    research/games/kuhn/KuhnGame.hpp
    research/games/kuhn/KuhnGame.cpp
    research/games/kuhn/KuhnGameTests.cpp
    research/games/kuhn/README.md

`CMakeLists.txt` registers the independent `poker_kuhn` library and
`poker_kuhn_tests`. No Hold'em evaluation table is needed by this toy module.

The module exposes six ordered chance deals; legal check/bet/fold/call actions;
owned player-visible information sets; terminal net payoffs; and small copyable
state for branching/restoration. It contains no trainer.

Verification already completed in this conversation:

- Release project build succeeded.
- All 8 CTest suites passed after adding the module.
- Comments/documentation were then expanded; the Kuhn target rebuilt and its
  test passed again.
- Tests cover all six deals, 30 terminal histories, 12 information sets, legal
  actions, invalid-operation rejection, hidden-card grouping and branch replay.

No MCCFR trainer, card abstraction encoder, trained strategy, neural model or
online solver has been implemented yet. The detailed 36-file list from earlier
in the conversation is not a binding current implementation checklist.

## 10. Existing engine interfaces useful for the next step

| Interface                                 | Role                                                         |
| ----------------------------------------- | ------------------------------------------------------------ |
| `PokerGame::reset_episode` / `begin_hand` | Set up independently sampled hands                           |
| `advance_to_decision` / `acting_player`   | Advance automatic events and identify the actor              |
| `current_view`                            | Own private cards and public state/legal actions             |
| `apply_action`                            | Execute through existing betting validation                  |
| `snapshot` / `restore`                    | Explore alternative learner actions                          |
| `enable_history` / `action_history`       | Full public betting sequence when enabled                    |
| `last_rewards`                            | Terminal net chips, including blinds; normalize by big blind |

`PlayerView` does not currently contain the complete public betting sequence.
`DecisionProvider::choose_action` receives only that view. A history/context
adapter is needed for history-aware policy lookup and eventual synchronous play.
`GameObserver` and full snapshots expose hidden state and are not policy inputs.

An existing shuffled deck already supplies sampled chance for MCCFR. An explicit
deal API might help future controlled tests or search, but it is not inherently
required to start sampled full-hand training. Do not carry it over from the
old full-tree plan as a mandatory first feature.

Rules currently include simplified short-blind/reopening behavior. Read
`documentation/rules.md` and `SIMULATION_GUIDE.md` before changing or comparing
rules. Use `last_rewards`, not gross `last_payouts`, for utility.

## 11. Where to resume the conversation

The next step is **designing and sizing the abstraction**, not immediately
generating all previously proposed trainer files. Resolve the smallest useful
set of decisions, using concrete hand examples:

1. Establish RAM/disk/compute budgets and the initial stack/blind configuration.
2. Specify lossless suit canonicalization, including street chronology.
3. Select an initial candidate-action menu and off-menu behavior for evaluation.
4. Decide which public context remains exact and how card buckets are built.
5. Choose initial per-street/player-count bucket budgets; estimate actual table
   growth with the betting contexts included, not just bucket count alone.
6. Specify the external-sampling update and multiplayer average-strategy rules.
7. Implement a small, commented vertical slice using the actual Hold'em engine.
8. Measure entry size, coverage, traversal cost and learning behavior before
   increasing the game/abstraction budget.

These are proposed next steps, not new user approvals or completed decisions.
Keep Kuhn as an optional shared-trainer correctness test and avoid a separate
full-tree CFR detour. Evaluate all counts independently as coverage expands.

Useful next discussion example: take two flop hands with similar equity but
different draw/blocker properties, show what the encoder preserves, and explain
which table entries they would share under two candidate bucket schemes.

## 12. Earlier plans and reading

`CFR_PLUS_IMPLEMENTATION_PLAN.md` is historical. It predates the decision to
focus directly on MCCFR without a separate full-tree CFR/CFR+ trainer. Its engine
review is useful, but its implementation order and some prerequisites are no
longer the current direction. `AI_RESEARCH_PLAN.md` is broader background, not a
record that all its milestones or optional methods were accepted.

Primary references already discussed:

- [MCCFR / external sampling](https://mlanctot.info/files/papers/nips09mccfr_techreport.pdf).
- [Distribution-aware poker abstractions](https://johanson.ca/publications/poker/2013-aamas-kmeans-abstraction/2013-aamas-kmeans-abstraction.pdf).
- [Pluribus: multiplayer poker, abstraction and search](https://noambrown.github.io/papers/19-Science-Superhuman.pdf).
- [Deep CFR: neural alternative, not the currently selected first representation](https://proceedings.mlr.press/v97/brown19b/brown19b.pdf).
- [DeepStack: continual solving with learned continuation values](https://poker.cs.ualberta.ca/publications/17science.pdf).
- [ReBeL: learning and search in two-player zero-sum games](https://arxiv.org/abs/2007.13544).
- [Published no-limit game-size counts](https://johanson.ca/publications/poker/2013-techreport-nl-size/2013-techreport-nl-size.pdf).

To restart in a new chat, say: **"Read MCCFR_PROJECT_HANDOFF.md in this repository
and continue from section 11. Explain the next design choice before implementing
it."**
