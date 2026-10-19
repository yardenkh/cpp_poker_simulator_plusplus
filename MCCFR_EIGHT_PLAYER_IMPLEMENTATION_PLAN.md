# Eight-player MCCFR: efficient training and implementation plan

Updated 19 October 2026. The serial milestones A–C reference slice is implemented and tested. Full-game eight-player training and milestones D–G remain future work.


### Implementation status — verified 19 October 2026

This is the exact continuation point. Implementation details and commands are in
[the MCCFR module guide](documentation/modules/mccfr.md).

| Milestone | Status | What is actually available |
|---|---|---|
| A — shared foundation | Complete for the A–C slice | Shared complete candidates with stable tokens; public observation/history provider hook; engine-managed history requirements; separate `poker_policy` and `poker_mccfr` targets. The currently deployed regular-game uniform AI is preserved. |
| B — serial reference learning | Complete | One generic external-sampling traversal instantiated for Kuhn, an enumerable three-player multistreet Leduc variant and real Hold'em; ordinary regret matching; expected-update tests; exact tiny-game own-reach averaging oracle; independent linear regret/average switches in the C++ configuration, off in reference runs and persisted in checkpoints. |
| C — eight-player restricted pipeline | Complete reference pilot | No-limp equal-stack push/fold through the actual engine; current/cyclic-simple average export; independent response selection/scoring with coverage and rare-state fallback; deterministic save/resume and fresh-process key/action/probability checks. Not a full-game trained policy. |
| D — full-game feasibility | Not started | Postflop card codebooks, full-game keys, menu comparisons, p95/p99 traversal/coverage/RSS profiling and compact storage decisions are next. |
| E — full eight-player 25-BB learning | Not started | No full-game blueprint, pruning or benchmarked weighting variants yet. |
| F — long-run hardening | Partial infrastructure only | Checksummed versioned artifacts, Linux flush/rename, previous generation, corruption/interruption tests and restart-on-failed-ID work. Streaming/bounded-memory saves, broad filesystem failure testing, champion promotion and long-run budgets are not complete. |
| G — optimization/expansion | Not started | No parallel batches, production arenas, compact snapshots/apply-undo, out-of-core table, deeper-stack or neural/search extensions. |

**Verification completed:** all 11 CTest suites pass, including the new MCCFR and
fresh-process policy tests. The MCCFR test binary also passes address and
undefined-behavior sanitizers. CLI node-limit stop/resume produces byte-identical
checkpoint and current-policy files to uninterrupted training. Initial v1
unweighted checkpoints remain readable by the v2 loader.

**Measured restricted reference run:** seed 1, 25 BB, ordinary regret matching,
cyclic-simple averaging, budgets 1,000 / 5,000 / 20,000 traversals. Later budgets
resume the prior checkpoint. Artifacts are under the ignored
`build/mccfr-validation-1000/`, `build/mccfr-validation-5000/` and
`build/mccfr-validation-20000/` directories.

| Traversals | Entries | Nodes | Estimated table bytes | Zero-average rows (uniform average export) |
|---:|---:|---:|---:|---:|
| 1,000 | 7,546 | 13,477 | 4,436,756 | 5,901 |
| 5,000 | 20,819 | 67,366 | 12,460,554 | 14,816 |
| 20,000 | 35,495 | 269,555 | 21,442,674 | 20,204 |

Independent evaluation uses seed 123, 10,000 selection and 20,000 scoring hands
per seat. The aggregate estimated deviation gains below are sums over eight
seats, in BB/hand, with approximate marginal 95% intervals. They are not exact
exploitability, certified best responses or confidence intervals on cross-budget
improvement. Detailed coverage/fallback counts are in each evaluation CSV.

| Traversals | Current export: sum of estimated gains | Simple-average export: sum of estimated gains |
|---:|---|---|
| 1,000 | 1.2713 [0.7013, 1.8413] | 2.0698 [1.4923, 2.6474] |
| 5,000 | 0.4913 [-0.0430, 1.0256] | 1.2434 [0.6831, 1.8038] |
| 20,000 | 0.4493 [-0.0528, 0.9513] | 0.4704 [-0.0558, 0.9965] |

**Current limitations:** the reference table uses owned canonical keys and
per-entry fixed arrays, not compact arenas; memory accounting is estimated rather
than a hard process-RSS cap. Checkpointing builds an in-memory serialized payload
and has a 2-GiB per-artifact loader limit. Training retains ordinary engine history
and full snapshots. The tools target restricted 25-BB play; the normal CLI does
not load these artifacts. Linear switches are programmatic, not CLI options.
There is no automatic champion promotion or proof of strong full-game play.

**Next coding task:** milestone D. Build/version a full-game player-visible
postflop encoder and representative branching/capacity pilot, reusing the shared
candidate/observation interfaces. Measure key growth, weighted coverage, actual
RSS and traversal tails before selecting an abstraction or starting long training.
Keep restricted policies and full-game keys/artifacts in separate schemas.
## 1. Objective and recommended architecture

Train an eight-player no-limit Hold'em strategy using external-sampling MCCFR, a bounded abstract table, durable checkpoints and independent evaluation. Optimize playing strength per unit of time and memory, rather than maximizing table size or hands simulated.

```text
PlayerView + public history -> abstraction key -> strategy table
                                                    |
                                   regret-matched complete actions
                                                    |
                                   external-sampling traversal
                                                    |
                             RAM updates -> disk checkpoints
                                                    |
                       current/averaged policies -> evaluation -> promotion
```

The product target is eight-player poker from the first Hold'em experiment. Tiny games are correctness tests, not a heads-up product phase. A hand that starts with eight players remains in its eight-player context after opponents fold.

No finite-budget method can promise the best possible eight-player strategy. Standard two-player zero-sum Nash convergence guarantees do not extend unchanged to eight players or arbitrary imperfect-recall abstractions. Pluribus demonstrated strong six-player play, not a certificate for this design. [MCCFR](https://mlanctot.info/files/papers/nips09mccfr.pdf), [Pluribus](https://noambrown.github.io/papers/19-Science-Superhuman.pdf).

## 2. Existing foundations to reuse

- `game/PokerGame.hpp/.cpp`: episode reset, externally driven decisions, snapshots/restoration, public action history and terminal chip rewards.
- `view/PlayerView.hpp/.cpp`: player-visible cards and public context, excluding opponents' private cards and the deck.
- `decision/AIActionDistribution.hpp/.cpp`: one menu of complete legal decisions, with up to five distinct raises.
- `decision/AIDecisionProvider.hpp/.cpp`: seeded uniform baseline for testing and evaluation.
- `analysis/Analysis.hpp/.cpp`: tools for offline abstraction construction; not mandatory expensive calculations at every node.
- `research/games/kuhn/`: small independent imperfect-information test game.
- Simulation runners: existing examples of per-seat seeds and worker ownership.

Keep one implementation of poker rules. The learner controls engine transitions; it does not implement its own betting or payout rules.

## 3. Initial training game and experiment manifest

Start independent eight-player hands with fixed stacks and blinds; reset stacks after each hand and rotate the button. Train cash-game chip profit, not tournament prizes.

Proposed first setting: current engine defaults, 2,500 chips each and 50/100 blinds (25 BB), no rake or antes. This matches regular-game defaults. Stack depth remains configurable; 50 BB, 100 BB and heterogeneous stacks are separately identified later experiments, not silently mixed into this table.

```text
utility_i = (final_stack_i - stack_i_before_blinds) / big_blind
```

Always propagate the designated updating player's utility. Do not negate a value because the actor changed. All eight rewards sum to zero, but the problem is not a two-player zero-sum game.

Every run has a manifest recording rules, player count, stack distribution, blinds, seat conventions, action abstraction, card encoder/recall mode, deal-sharing scheme, averaging estimator, export type (current or averaged), weighting/pruning variant, update schedule, numeric format, seed scheme and code revision. Artifact compatibility depends on these fields.

### Early restricted eight-player integration mode

Before full-game training, run a small push/fold mode through the same real-engine adapter:

- Before any shove, allow only fold/shove; there is no limping. If everyone folds to the big blind, the big blind wins the blinds without acting. No unraised checkdown path exists; assert this rather than handling it. A small-blind-complete variant (with forced checkdown) would be a separately versioned rule set.
- Facing a shove, allow fold/call, including legal partial all-in calls.
- Run out the board and settle actual main/side pots with the engine.
- Start with fixed equal stacks and rotate all positions. Record restricted rules separately from full Hold'em.

The restricted mode has its own token set (Fold, Shove, Call) and action-layout IDs. Merging Call and AllIn into one token is intentional here and does not override section 4, where full-game Call and AllIn stay distinct. Restricted and full-game layouts never share keys or table entries.

This tests learning, lookup, save/resume, export and evaluation end to end. The 169 starting-hand classes are only part of the key: prior actions, positions and pot eligibility also matter. Measure runtime; do not promise near-convergence in minutes. Published push/fold charts are useful comparisons only for matching stacks, players, blinds/antes, payouts and calling/overcalling rules. Positional intuition is a sanity check, not a correctness oracle.

**Estimated unilateral improvement metric.** With all other seats fixed at a frozen policy, estimate how much one seat could gain by changing its own restricted-game strategy. Each information set (position, facing-action history, hand class) picks the action with the higher estimated EV. In push/fold each player acts at most once per hand, so every information set is a one-shot choice with no later own decisions. The response is therefore a separate choice at each information set, not a sequential tree computation.

- **Correct conditioning:** sample complete deals from the root, with chance from its true distribution, and play opponents forward with their frozen policies. Do not draw opponent hands uniformly after observing a history: shoves and calls reveal information, and forward sampling weights holdings by how likely each policy was to produce the observed actions.
- **No selection bias:** choose the response on one sample set, freeze it, then evaluate both the response and the original policy on independent paired samples. Choosing and scoring the best estimated EV on the same samples is optimistic.
- **Reporting:** report each seat's estimated gain (response value − policy value), the sum over seats and sampling intervals. Precompute or cache multiway all-in equities if needed.

Call the result an *estimated unilateral improvement*, not exploitability or NashConv. It becomes an exact restricted-game NashConv only if the response computation is exhaustive and validated against an exact small-game computation. Either way it is a restricted-game measure, not a full-game exploitability estimate.

## 4. Shared complete actions and stable identities

Expose candidate generation separately from uniform probability assignment within the existing action module. The baseline assigns uniform probabilities; training assigns regret-matched probabilities to the same menu.

The initial menu remains fold/check/call where legal, minimum raise, 25/40/60/80% stack raises where legal and distinct, and all-in. Fractions mean payment NOW / remaining stack, including the call amount. Existing round contributions are added to obtain `raise_to`.

Use stable tokens: Fold, Check, Call, MinRaise, Stack25, Stack40, Stack60, Stack80 and AllIn. Deduplicate raises in deterministic order: minimum first, then fraction order; retain the first token producing a concrete total. Record the resulting ordered action signature in the key or shared layout ID. Never interpret an array index as a permanent action identity.

All states sharing an abstract key must share the same ordered action-token list. Assert this at lookup. Call and AllIn remain distinct types even if their payment coincides, preserving current behavior. Full-stack raises use AllIn only.

### Branching and candidate-menu experiments

The array capacity is nine; the current valid menu has at most eight entries because check and call are mutually exclusive. Learner nodes branch over the full legal menu, so menu size matters. A product of action counts is a worst-case warning, not a throughput estimate: folds terminate branches and all-ins remove future decisions.

Preserve the user's stack-relative reference menu. At 25 BB, a 25% commitment can be approximately 6 BB, leaving gaps between minimum and fraction raises. Compare the reference with smaller/fewer stack fractions and street-dependent stack-fraction menus at equal time and memory. Include one pot-relative menu (for example roughly 2.5 BB preflop opens, 33%/75% pot postflop, all-in) as a measurement-only comparison. If every stack-fraction variant grows badly, this shows whether the sizing scheme or the menu size is the cause. It does not replace the requested design unless an explicit later decision says so. Limiting later raises changes the abstract game and must be versioned as such.

Measure median/p95/p99 nodes per traversal and entry growth before picking a production menu. Keep the regular-game baseline unchanged unless an explicit implementation step changes it.

## 5. Table abstraction

### Lossless reductions

Canonicalize suit names jointly across own cards and observed board history, preserving draw relationships and street chronology. Do not sort all board cards together and lose which arrived on the turn or river.

Rotate seat-indexed context consistently relative to the button. Preserve active/all-in masks, opponent order and contributions relevant to side pots. Arbitrary seat sorting is not a valid symmetry.

### Card buckets

Use the 169 suit-equivalent preflop hand classes with betting context still separate. Build versioned postflop codebooks offline from representative own-card/board observations.

Features should distinguish strength, improvement potential, draws, blockers and board texture. Equity against a stated reference range is one feature, not the complete abstraction. Include multiple relevant opponent counts/range models. Never obtain features from the simulator's actual hidden cards or future deck.

Pilot several modest resolutions, such as 64/128/256 buckets per postflop street. These are experimental settings, not estimates of total table size or sufficient strategy quality. Betting context combinations may dominate storage.

Save codebooks, feature definitions, seeds and hashes. Use identical encoding in training and play. Distribution-aware clustering is a research starting point, not a fixed requirement. [Card abstraction research](https://johanson.ca/publications/poker/2013-aamas-kmeans-abstraction/2013-aamas-kmeans-abstraction.pdf).

### Public history, stacks and recall

Use two explicit encoder modes:

- **Reference:** retain public abstract action history and the player's sequence of previous abstract observations/actions. Use this for mathematical tests and small diagnostic experiments with recall of the chosen abstract information.
- **Practical Hold'em pilot:** retain only the current-street private-card bucket, full abstract public betting history, relative seats, public stack/contribution context, active/all-in masks and action signature. Forgetting previous private buckets is intentional imperfect recall, recorded in the manifest.

The practical mode is the initial full-game capacity-pilot candidate. It trades memory for approximation; full public history can still grow too quickly. Exact current legality and public street chronology remain intact in both modes.

Initially use fixed starting stacks and deterministic in-menu actions. Intern history prefixes into shared compact IDs instead of storing long strings per entry. Do not redundantly store every numeric state field if history reconstructs it; account for history dictionaries in memory measurements.

**Portable keys.** A key's identity is defined by its content: a versioned canonical byte encoding of the abstract history, card bucket, seat context and action signature. Interned IDs assigned in insertion order are an in-process optimization only. They are never the persistent identity, because training and normal play intern histories in different orders. Checkpoints and frozen policies store either canonical key encodings or the full ID dictionary needed to map them back, together with the encoding version. A fresh process must be able to rebuild every key from a live observation without depending on training-time insertion order.

If capacity pilots show excessive growth, test compressed context in a new version: pot/BB and stack/pot bins, bounded history summaries and selective grouping. These introduce additional approximation beyond the practical mode and can further affect recall and the learning assumptions. Evaluate their strategic loss rather than presenting them as lossless compression.

Do not run weeks of training with nearly unique keys. Measure new-entry rate and revisits, then change abstraction if necessary. Sparse storage alone does not solve this problem.

### Unseen states and off-menu bets

Execute human/off-menu bet amounts exactly in the engine. A separate, versioned lookup translation may bin observed payment fractions or map to compatible abstract histories. Preserve legal action-layout compatibility and log translation error.

Use an explicit uniform legal fallback initially; count fallback usage in evaluation. A trained coarser fallback can follow later. A policy with many fallback decisions is not providing broad learned coverage. Test unusual bet sizes, stack patterns and side-pot states explicitly.

## 6. Compact table and explicit RAM limits

Recommended organization:

```text
verified compact key -> entry ID
entry metadata -> action-layout ID, numeric offset, visit counters
contiguous numeric arenas -> regrets and average-strategy weights
shared dictionaries -> history prefixes, action layouts, card codebooks
```

Avoid a heap-allocated vector/string/object for every information set. Use a sparse index and contiguous arrays. Hashes speed lookup but do not prove equality: compare canonical key data to resolve collisions rather than silently merging on one unchecked hash.

Use float64 in the correctness implementation. Test float32 stored arrays with float64 arithmetic later. Large cumulative float32 values can stop registering small updates. Numeric compression must pass drift and playing-quality tests; arbitrary per-entry rescaling changes future learning unless the update scale is handled consistently.

Illustration: eight actions (the current menu's maximum; see section 4) with two float64 arrays need 128 bytes per entry before metadata; float32 needs 64 bytes. Two GiB fits at most approximately 16.8 million float64 numeric payloads, but real capacity is lower after keys, index slack, dictionaries, buffers and allocator overhead. These are capacity examples, not quality predictions.

Hardware is unknown. Make budgets configurable; 2 GiB table RAM is only a pilot setting on a machine with sufficient available RAM. Measure actual peak process memory, not just array sizes. Reserve space for workers, traversal buffers, encoder cache, evaluation and checkpointing.

At the high-water mark, finish a consistent boundary, save and stop with a capacity report. Do not silently evict learned entries, stop creating selected branches or discard regrets to fit memory.

### Capacity and runtime stop/go gates

Use short doubling pilot batches with a fixed wall-time cap, first in restricted mode and then with full branching eight-player traversals. Measure nodes/traversal, terminal calls, bytes/new-entry and entries per million traversals. Ordinary rollout throughput does not predict learner traversal cost.

Proposed configurable diagnostic gates:

- Stop/checkpoint near 80% of the assigned table budget, with the remaining allowance reserved for in-flight work and saving. Reduce the threshold if measured worst-case buffers require more headroom.
- Project growth over the next planned block using recent rates and uncertainty. If the budget would be exceeded, revise abstraction or explicitly change the budget before proceeding.
- After warm-up, if more than half of lookups create new keys in two consecutive windows, investigate abstraction before long training. Repeat after policies change; this threshold is a diagnostic, not a convergence theorem.
- Report decision-frequency-weighted coverage alongside unweighted visit distributions. A median visit count alone can misrepresent rare-tail growth or missing important states.
- Initially budget approximately 5% wall time for saves and 10% for scheduled evaluation, adjusting to measured recovery needs and uncertainty.

There is no universal visit count that makes an information set learned. Track action count, update frequency and estimator variability rather than asserting convergence after K visits.

## 7. External-sampling learning algorithm

Start with ordinary cumulative regret matching:

```text
positive[a] = max(regret[I,a], 0)
strategy[a] = positive[a] / sum(positive)  if sum > 0
strategy[a] = 1 / action_count           otherwise
```

One sweep performs eight updating-seat traversals. During a traversal, read a consistent base strategy and collect local deltas; commit at its boundary. Policies evolve between traversals. This avoids changing the evaluated policy halfway through its own branch exploration.

Deal sharing is an explicit, manifest-recorded choice. Default: a fresh deal per traversal, seeded from a deterministic function of (run seed, sweep, updating seat), so resume and replay never depend on traversal order. Variant: one shared deal for all eight traversals of a sweep. This variant must freeze the strategy for the whole sweep: all eight traversals read the same base policy, accumulate deltas separately and commit together at the sweep boundary. Committing between traversals of a shared deal would make later traversals sample from strategies already updated on that same deal. The deal would then no longer be independent of the policy being evaluated, which needs separate analysis. The variant may change variance in either direction and costs more staleness; measure it at equal wall time rather than assuming a benefit. Within one traversal, sibling learner branches always share the sampled deal and runout.

```text
traverse(state, learner):
    advance engine until terminal or a player decision
    if terminal: return net_profit_in_BB[learner]

    key, candidates = encode_and_generate(state)
    strategy = regret_match(base_table[key])

    if actor != learner:
        sample one action from strategy
        apply action
        return traverse(state, learner)

    saved = snapshot(state)
    for each candidate a, including zero-probability candidates:
        restore(saved)
        apply a
        value[a] = traverse(state, learner)
    restore(saved)

    expected = sum(strategy[a] * value[a])
    delta_regret[I,a] += value[a] - expected
    return expected
```

This recurrence assumes opponents are sampled from their current strategy and chance from its true distribution. Their reach enters through sampling; do not multiply by opponent reach a second time. Changing behavior sampling requires derived importance corrections. [External sampling](https://mlanctot.info/files/papers/nips09mccfr.pdf).

The engine's shuffled deck can supply a sampled deal/runout shared across sibling actions. Verify correct chance marginals under alternative action histories. Hidden cards and future boards must never enter strategy keys. Keep trainer RNG separate from engine snapshots so restoring poker state does not rewind opponent-action sampling accidentally.

Drive training directly through the table and engine adapter, not mutable playing providers. Traversal resource limits (nodes, time, buffer bytes) are safety limits, not a sampling filter. Size them generously from pilot p99/max measurements. Hitting one is a run-stopping condition: discard the in-flight traversal, keep the last committed checkpoint, stop and report the configuration and the deal/history that triggered it. Never silently skip over-budget traversals and continue with fresh deals: that systematically excludes expensive situations such as multiway pots and long betting sequences, and biases learning. Never return an invented zero payoff either. Resumable traversal or a derived sampling scheme with importance corrections are possible later alternatives. Depth-limited value estimates would be a separate approximate algorithm.

## 8. Current and averaged policy extraction

Export both current and averaged policies as independently evaluated candidates:

- **Current:** regret matching over the committed current regrets.
- **Averaged:** normalized statistics from a named, versioned averaging rule.

Both require consistent table scans/exports and disk space. Neither automatically wins deployment. Pluribus is motivation for evaluating current-policy extraction, not evidence that a blueprint export alone reproduces its deployed postflop search.

Regret learning and policy averaging are separate. For a perfect-recall reference game, the ideal average is:

```text
S[I,a] = sum_t w[t] * own_reach_i^t(I) * strategy_i^t(I,a)
average[I,a] = S[I,a] / sum_b S[I,b]
```

Own reach includes only the player's prior actions. Raw sampled action counts are not automatically this average. Use w[t]=1 in reference tests; other weighting schedules need explicit definitions.

### Initial production experiment

Implement the inexpensive cyclic simple averaging rule as a clearly labeled experimental backend: in a traversal updating player i, accumulate the full current action distribution only at nodes of player (i+1) modulo the player count. Define initialization and the traversal-boundary policy version exactly, and check it against small enumerated examples. This is a sampled visitation-weighted rule, not a claim of equivalence to the ideal eight-player average. [OpenSpiel reference](https://raw.githubusercontent.com/google-deepmind/open_spiel/master/open_spiel/algorithms/external_sampling_mccfr.cc).

Keep an exact tiny-game own-reach averaging oracle for validating stronger claims. An imperfect-recall production abstraction requires its own empirical interpretation; it does not inherit perfect-recall guarantees.

### Deferred reference estimator

A history-importance-sampled averaging backend is optional verification/research work, not a prerequisite for first Hold'em learning. Its inclusion probabilities must cover relevant chance and behavior histories, its target must be derived explicitly, and its variance must be measured against the tiny-game oracle. Large inverse-history weights can make an otherwise correct estimator impractical. Log/scaled arithmetic addresses numerical range, not statistical variance. Clipping weights changes the estimator.

**Acceptance gate:** correctly implement and label the chosen averaging backend, test its own update convention, and evaluate its exported policy. An ideal multiplayer averaging implementation must not block early eight-player experiments. Keep current and averaged results separate in every report.

## 9. Disk persistence and recovery

### RAM-first working state

Update the working table in RAM. Save durable checkpoints to disk at committed boundaries. Avoid synchronous file writes or database transactions at every visited node.

Separate three artifacts:

| Artifact | Contents | Purpose |
|---|---|---|
| Training checkpoint | Regrets, average statistics, keys, progress, RNG/configuration | Resume learning |
| Frozen candidate policy | Current or averaged probabilities plus encoder/layout/variant metadata | Independent evaluation |
| Champion policy | A qualified deployment policy | Normal games |

Saving only probabilities is insufficient to resume cumulative learning.

### Checkpoint format and publication

A checkpoint must contain canonical keys, layout/history dictionaries, numeric arrays, counters/scales, logical update position, averaging progress, RNG states/seed schedules, rules/action/abstraction versions, numeric format, code revision and checksums. Store codebooks or content-addressed references to immutable copies.

Use a versioned binary format with explicit field encoding and endianness; do not dump compiler-specific structs or padding. Check bounds and checksums on load. Reject incompatible configurations instead of silently interpreting old regrets under new keys.

Initial safe save procedure:

1. Pause at a committed traversal/batch boundary.
2. Write a new temporary checkpoint directory on the destination filesystem.
3. Flush data/metadata using appropriate platform durability operations.
4. Verify sizes/checksums and publish its manifest atomically.
5. Retain the previous complete generation until the new one is readable.

A rename by itself does not guarantee crash durability. Failed saves or disk-full conditions must preserve the previous valid checkpoint. Incomplete directories are ignored on startup.

Start with stop-the-world saves to avoid duplicating the table in RAM. If pauses become material, add dirty-block snapshots or a committed batch log with sequence numbers. Never combine blocks from different logical versions into an apparently consistent save.

Pilot interval: roughly ten minutes and clean shutdown, adjusted to keep saving below approximately 5% of wall time. Retain two recovery generations, promoted policies and explicitly pinned milestones. Budget temporary output and recovery copies before starting a save; model export, reports and codebooks also occupy disk.

Policy exports omit regrets and training state. Quantization/compression is optional after probability-error and playing-performance tests. Lossy policy exports must not replace resumable numeric state.

### If a useful table exceeds RAM

First revisit abstraction and entry reuse. An enormous SSD table that is almost never revisited still learns poorly.

If measurements justify it, use paged disk-backed storage with a bounded RAM cache and batched dirty-page writes. Measure cache hit rate, page faults, bytes transferred and traversal throughput. Account for the index as well as numeric pages: an in-memory index can exhaust RAM even if values live on SSD.

Memory mapping is an access mechanism, not unlimited memory or a guarantee of fast random updates. Database caches also consume RAM. Introduce this complexity only after the RAM-first prototype identifies the actual bottleneck. [Storage/page-cache architecture](https://www.sqlite.org/arch.html).

## 10. Improvement, evaluation and promotion

Continue from the latest training checkpoint even if the newest evaluation score falls. Do not accept/reject ordinary regret updates according to whether a short game batch won. Training estimates and match results are noisy; learning need not improve monotonically.

Periodically export frozen current and averaged policies and evaluate them outside the training loop, identifying both variants in reports. Compare experiments at equal wall time and memory, not just equal traversal count.

All main evaluation lineups have eight players:

- One candidate against seven fixed opponents.
- Seven candidate copies against a probing opponent.
- Mixed historical/current policies and diverse fixed styles.

Include uniform, passive callers, tight policies, aggressive policies, off-menu sizing policies, previous champions and at least one competent configurable heuristic policy using declared equity assumptions, pot odds, position and legal stack commitments. A restricted push/fold policy is a baseline for its own rules, not automatically a competent full-game opponent. Pure identical self-play cannot measure absolute strength: combined profits sum to zero.

Rotate tested policies through all seats. Reuse deals in matched comparisons, treating the entire deal/rotation block as the statistical unit. Report paired differences, BB/100 and confidence intervals accounting for dependence; rotations from one deal are not independent observations.

Use separate development and promotion seeds. Frequent tuning uses development data; a selected candidate gets confirmation on fresh holdout blocks. Refresh holdout blocks after use. Repeatedly testing until a favorable result appears creates selection bias.

Proposed promotion gate:

- A predefined aggregate improvement clears a practical margin with uncertainty accounted for.
- Critical opponent families show no unacceptable regression.
- Coverage, off-menu fallback and latency meet declared limits.
- Correctness and artifact-compatibility tests pass.

If evidence is inconclusive, run a preplanned larger evaluation batch. Keep the current champion while training continues. Record failed candidates too. Retain a small diverse policy archive because multiplayer strength can be nontransitive.

Learned exploiters against frozen checkpoints probe weaknesses but are not exact exploitability certificates. Bring variance reduction forward when evaluation noise limits progress. Retain raw returns alongside every adjusted metric.

An all-in EV adjustment is appropriate only when the adjusted pot's entitlement is fixed and future cards can be integrated correctly. Handle side-pot eligibility, ties, refunds and mixed all-in/non-all-in players. Current showdown equity is not a generic unbiased replacement when later betting/folding can change entitlement. Actual simulator cards may support retrospective evaluation, never policy inputs; range-based estimates add assumptions. Test conditional expectations in enumerated cases. AIVAT is the more general option once its estimator is validated. [AIVAT](https://arxiv.org/abs/1612.06915).

### Execution environment and filesystem

Record Linux-visible RAM, relevant cgroup limits and explicit WSL settings; budget against actual available resources rather than host RAM alone. Leave headroom for host pressure and other applications. Do not automatically modify the user's `.wslconfig`.

Keep active artifacts on the Linux filesystem, for example `/home/yardenks/poker-runs/`, rather than `/mnt/c`. This repository already resides on the Linux side despite its Windows UNC access path. Use destination-appropriate flush/publication operations and interruption tests; do not assume identical durability across filesystems. [WSL settings](https://learn.microsoft.com/en-us/windows/wsl/wsl-config), [Filesystem guidance](https://learn.microsoft.com/en-us/windows/wsl/filesystems).

## 11. Time efficiency and parallel training

Optimize the measured bottleneck in this order:

1. Disable observers and engine history during training once equivalent trainer-owned history is tested; avoid per-node strings/heap allocations.
2. Reuse action arrays, traversal stacks and local-delta buffers.
3. Intern history/layout keys and bound encoder caches.
4. Profile encoder, hash lookup, snapshots, evaluator and averaging separately.
5. Benchmark snapshot copying early. Add a compact training snapshot when justified; consider apply/undo later with differential tests against full snapshot/restore.
6. Test numeric packing/float32 only after measuring precision loss.
7. Add multiple workers after deterministic serial correctness.

Use interned trainer-owned history IDs and restore their prefix alongside sibling branches. The current general snapshot owns a history vector and a shuffle RNG. A dedicated training snapshot can omit the shuffle RNG only after proving supported in-hand transitions never consume it. Still restore deck position, cards, players, pot layers, actor, legal state, contributions, rewards and counters. Keep the normal snapshot contract intact.

If apply/undo is added, use the existing rules path and verify randomized action sequences against full snapshots. Do not create a second betting implementation.

Do not run large equity simulations at every node. Feature construction belongs offline or in a bounded cache, with cheap lookup during training.

Parallel design: freeze a logical policy version for a bounded batch, run independent traversals with worker-local sparse deltas, reduce deltas deterministically, then commit. Keep seed namespaces separate for training, chance, averaging and evaluation. Bound every worker's buffers; do not clone the full strategy table per worker.

This is a batched learning variant with stale policies within a batch, not identical to sequential updates. Compare quality per wall time with the serial baseline. Smaller batches reduce staleness but increase synchronization. Sharded indexes and deterministic reductions come before unsynchronized shared floating-point writes.

Require reproducibility with a fixed worker configuration. Bitwise identity across arbitrary worker counts, compilers and scheduling is not promised. Record the full schedule needed to resume.

## 12. Algorithm upgrades, tested separately

Keep ordinary external sampling as the reference implementation. Add explicit weighting configuration early, default-off in reference tests. Compare a linear-weighted variant in the first full-game experiments. Define regret weighting, averaging weighting and discounting separately; they are not interchangeable flags.

Introduce pruning as an early measured optimization if full learner branching dominates. Before enabling it, specify eligibility, skipped-action update behavior, revisit schedule, any regret floor and action recovery. Test a game where a previously unattractive action becomes attractive. Never use zero as an unknown skipped payoff. A periodic unpruned traversal alone does not establish estimator correctness. Regret floors are named variants, not ordinary cumulative regret.

Change one factor at a time and compare equal-resource results. Pluribus motivates linear weighting and selective pruning, but its settings are not automatically suitable for this eight-player configuration. [Pluribus methods](https://noambrown.github.io/papers/19-Science-Superhuman.pdf).

| Upgrade | Intended benefit | Required checks |
|---|---|---|
| Baseline/control-variate MCCFR | Reduce sampled-value variance | Correct estimator, tiny-game expected values, variance per second |
| Discounted regrets/averages | Give later experience more influence | Exact schedule, sampled compatibility, equal-time evaluation |
| Regret-based pruning | Avoid repeatedly unpromising branches | Revisit/unprune rules and recovery of previously poor actions |
| Sample fewer learner actions | Reduce branching cost | Inclusion probabilities and importance corrections |
| Better buckets/context | Use memory on useful distinctions | Versioned encoder, coverage, held-out improvement |
| Revised raise menus | Better strategic coverage | Equal RAM/time comparison and off-menu tests |
| Neural approximation | Generalize beyond table capacity | Separate replay/training design, approximation and quality tests |
| Public-state local search | Refine important/off-menu decisions | Public belief ranges, continuation strategy and latency budget |

Do not add CFR+ clipping to sampled regrets and assume full-tree CFR+ results apply. Keep negative cumulative regrets in the initial ordinary implementation. Discounting must specify logical iteration, effects on unvisited entries and averaging weights. Lazy discounting must account for elapsed steps correctly, rather than discount only when an entry happens to be visited. [Discounted regret minimization](https://arxiv.org/abs/1809.04040).

Variance reduction is a strong early experiment once correctness is established. A baseline must enter a derived control-variate estimator; replacing terminal returns with an uncorrected heuristic changes the objective. [VR-MCCFR](https://arxiv.org/abs/1809.03057).

Neural methods and search remain optional later layers. They do not automatically solve multiplayer convergence, inadequate coverage or evaluation noise. [Deep CFR](https://proceedings.mlr.press/v97/brown19b.html).

## 13. Shared runtime and training file layout

Separate deployable policy functionality from mutable training machinery. Keep helpers internal when no shared API is needed. Function comments describe inputs, outputs, ownership, errors and usage; estimator comments also explain assumptions.

```text
poker_engine: existing rules, views and shared action generation
poker_policy -> poker_engine: encoders, frozen policy lookup, trained provider
poker_mccfr -> poker_engine + poker_policy: training and mutable statistics
CLI -> engine/terminal + optional poker_policy
```

Do not put the trained provider in the engine target if that creates a dependency cycle. Regular play must not require traversal or regret-table infrastructure.

| File(s) | Responsibility |
|---|---|
| Edit `decision/AIActionDistribution.hpp/.cpp` | Shared candidates/stable tokens, uniform wrapper and one-stage sampling |
| Add `policy/Observation.hpp` and minimal engine/provider integration | Explicit player-visible cards/context/history with documented lifetime |
| Add `policy/InformationSet.hpp/.cpp` | Relative seats, history IDs, keys and action-layout compatibility |
| Add `policy/Abstraction.hpp/.cpp` | Shared suit canonicalization, versioned codebook lookup and recall modes |
| Add `policy/FrozenPolicy.hpp/.cpp` | Read-only current/averaged policy artifacts, lookup and fallback metadata |
| Add `policy/TrainedAIDecisionProvider.hpp/.cpp` | Shared encoder/lookup, off-menu translation and legal decision sampling |
| Add `research/mccfr/TrainingConfig.hpp` | Eight-player rules, restricted/full modes, variants and resource budgets |
| Add `research/mccfr/StrategyTable.hpp/.cpp` | Compact mutable regrets/average weights, matching and capacity statistics |
| Add `research/mccfr/HoldemTrainingGame.hpp/.cpp` | Real-engine adapter, push/fold/full restrictions, snapshots and BB rewards |
| Add `research/mccfr/ExternalSamplingTrainer.hpp/.cpp` | Traversal, local deltas, update schedule and optional weighting/pruning |
| Add `research/mccfr/StrategyAveraging.hpp/.cpp` | Named simple averaging backend and test-oracle interfaces |
| Add `research/mccfr/Checkpoint.hpp/.cpp` | Resumable binary state, recovery and current/averaged policy export |
| Add `research/mccfr/AbstractionBuilder.hpp/.cpp` when needed | Training-only offline feature/codebook construction |
| Add tiny multiplayer multistreet game under `research/games/` | Enumerable three-player Leduc-like reference tests |
| Add `tools/BuildPokerAbstraction.cpp` | Offline codebook generation and manifests |
| Add `tools/TrainMCCFR.cpp` | Restricted/full training, start/resume, budgets and progress |
| Add `tools/EvaluateMCCFR.cpp` | Eight-player lineups, raw/adjusted returns and promotion reports |
| Add focused `tests/MCCFR*Tests.cpp` and policy tests | Encoder, reference mathematics, branching, extraction and recovery |
| Edit `CMakeLists.txt`, `main.cpp`, `.gitignore` | Library targets, optional learned policies and ignored run artifacts |

`PlayerView` currently lacks history. The explicit observation/context API must supply the same public history in training and deployment without granting access to hidden table/deck state. A small provider/context extension is acceptable. Test both call paths against identical engine states.

Use existing simulation infrastructure for evaluation and a configurable heuristic provider for a stronger baseline. Large artifacts live in configurable run directories, not committed source files.

## 14. Implementation milestones and acceptance gates

### A. Shared runtime foundation

Implement stable action tokens, deterministic deduplication, public observation/history and runtime/trainer dependency separation. Preserve the current stack-fraction reference menu.

Acceptance: identical training/play keys and candidates, legal short all-ins/side pots/closed raises, no hidden-card leakage and no dependency cycles. Canonical key encodings are identical when the same histories are interned in different orders.

### B. Serial reference learning on tiny games

Implement regret matching, frozen traversal reads, local deltas and restoration. Use existing Kuhn and an enumerable three-player multistreet game. Build exact expectation and own-reach averaging oracles as tests, not a separate solver product.

Acceptance: expected regret updates, utility signs, chance ordering, recall, legal branches and seeded replay pass. Define weighting switches, default-off in reference tests.

### C. Eight-player end-to-end restricted mode

Train push/fold through the real engine with precise rules from section 3. Include lightweight versioned save/resume, current-policy export, named simple averaging and basic eight-player evaluation.

Acceptance: chip/side-pot conservation, legal decisions, uninterrupted/resumed serial equivalence and usable exports. Export round-trip: export a policy, load it in a fresh process, rebuild the same observations from replayed engine states, and verify identical keys, action identities and probabilities. The section 3 estimated unilateral improvement is computed correctly (forward-sampled, with independent selection and evaluation samples), is reproducible from seeds and is reported with intervals for current and averaged exports at increasing budgets. A downward trend is expected but not guaranteed by eight-player MCCFR, especially for the current policy and simple averaging. It is not a pass condition; a persistent lack of improvement triggers investigation rather than automatically proving a bug. No fixed threshold or minute-scale convergence is promised, and nonmatching charts are not treated as oracles. Keep this integration step small.

### D. Full-game encoder and feasibility pilot

Build current-street card buckets with full abstract public history, compact storage and representative full-branching traversals. Compare stack-fraction menu variants plus the measurement-only pot-relative comparison menu from section 4. Benchmark snapshots immediately and use trainer-owned history.

Acceptance: memory/growth/runtime report, decision-weighted coverage, p95/p99 traversal cost and a versioned abstraction choice under section 6's stop/go rules. Repeat profiling as learning changes coverage. If keys remain mostly unique, revise before long runs.

### E. First full eight-player 25 BB experiments

Run increasing short budgets. Keep an ordinary reference; compare explicit linear weighting and current versus simple-averaged policies. Add pruning only after its recovery/estimator tests and measured need. Evaluate against competent heuristics, historical policies and diverse lineups.

Acceptance: reproducible reports, manageable capacity and correctly labeled exports, with quality uncertainty reported. Ideal multiplayer averaging is not a prerequisite; stronger averaging claims require stronger validation. Keep normal play usable.

### F. Long-run persistence and evaluation hardening

Before expensive runs, add complete crash-safe publication, checksum/compatibility rejection, bounded retention and independent champion promotion. Test disk-full/interrupted saves and destination filesystem recovery. Preserve lightweight recovery from milestone C rather than waiting until this stage for any checkpoint support.

Acceptance: last complete generation survives failure; serial resume matches the declared schedule; raw/adjusted metrics and holdout results are independently reproducible; run budgets include all processes and save buffers.

### G. Optimize and expand

Profile, then compare compact snapshots, numeric compression, validated pruning/variance reduction and parallel batches independently. Extend to deeper/heterogeneous stacks in versioned experiments. New abstractions require new runs or documented tested warm starts, not raw regret-table reuse.

## 15. Metrics and experiment discipline

Each run emits machine-readable metrics and a readable report:

- Sweeps, traversals, nodes, terminal evaluations, wall time and nodes/traversal.
- Nodes/second, median/p95/p99 nodes and time per traversal, and time split across encoder, table, snapshots, averaging and saving.
- Entry count, new-entry rate, revisit distribution and coverage by street/position.
- Peak process RAM, table/index/dictionary bytes, worker buffers and disk usage.
- Current/averaged export type, averaging update counts and estimator-specific diagnostics; effective sample sizes/extreme weights only for importance-sampled variants.
- Checkpoint time, bytes written, recoverable boundary and percentage overhead.
- Illegal-action/layout mismatches (must be zero), unsupported contexts and off-menu translation rates.
- Frozen current/averaged-policy raw and adjusted BB/100, intervals, results by seat/opponent family and promotion outcome.
- Hardware, code revision, manifest hashes and numeric/update variants.

Define experiment changes before running them. Keep seeds/configuration in reports and retain unsuccessful variants so apparent progress is not only selective reporting. Quality improvements should be reproducible across multiple independent training seeds when the budget permits.

## 16. Invariants and first concrete implementation batch

Use this checklist in tests and code review:

- Only player-visible information influences strategies; retrospective evaluator knowledge stays separate.
- Utilities, actor identity, pot eligibility and branch restoration are correct.
- Keys share compatible stable action layouts; hash collisions do not silently merge strategies.
- Persistent key identity is a canonical content encoding, never an insertion-order interned ID.
- No traversal is silently skipped or truncated; hitting a resource limit stops the run.
- Shared-deal sweeps read one frozen policy and commit once at the sweep boundary.
- Averaging, weighting, pruning and recall modes have explicit variant identifiers.
- Memory budgets and deployment fallbacks are measured; learned entries are not silently evicted.
- Checkpoints describe one committed state and recover after interrupted publication.
- Training and deployment share encoder/action schemas and public observations.
- Champion promotion uses independent evidence and never gates ordinary regret updates.

The next coding batch should expose shared complete actions/tokens, define the public observation contract and establish the `poker_policy`/`poker_mccfr` dependency split. Follow with the small serial reference tests and an eight-player restricted end-to-end learning result before building the full-game capacity pilot.

This order shortens the path to measurable eight-player learning while preserving a trusted baseline. It adopts a practical imperfect-recall pilot, current-policy evaluation and early resource gates without silently changing the requested stack-relative action design. Stronger abstractions, weighting/pruning variants and deployment policies earn their place through equal-budget tests.
