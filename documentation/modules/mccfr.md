# MCCFR reference trainer (milestones A–C)

The current implementation trains **eight-player, 25-BB, no-limp preflop push/fold**.
It is the end-to-end correctness slice for the larger implementation plan, not a
full-game trained opponent. Regular `poker_cli --game` still uses the uniform AI.

## Build and test (Ubuntu/WSL)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

## Start a run

```bash
./build/poker_train_mccfr --traversals 10000 --out runs/pushfold \
  --seed 1 --checkpoint-every 1000
```

One traversal updates one seat; eight traversals form a sweep. Fresh deals use
separate deterministic seed namespaces for the deck and action sampling. Button
position rotates per sweep. Limits default to 200,000 entries, a conservative
256-MiB estimated table budget and one million nodes per traversal.

A fresh run refuses to overwrite a directory containing `latest.mccfr`.

## Resume

```bash
./build/poker_train_mccfr --resume runs/pushfold/latest.mccfr \
  --traversals 10000 --out runs/pushfold
```

The traversal count is **additional** on resume. The seed comes from the checkpoint;
do not pass `--seed`. Capacity can be raised explicitly using `--max-entries`,
`--table-mib` and `--max-nodes`. If a traversal hits a limit, training stops, saves
committed progress and retains that traversal ID. Resume retries its same deal;
it never skips expensive states and continues silently.

## Outputs

- `latest.mccfr`: resumable regrets, simple averages, counters, configuration and
  next traversal ID. Checkpoint format v2 persists independent weighting flags;
  the loader also accepts the initial unweighted v1 format.
- `current.policy`: regret-matched probabilities at the committed boundary.
- `average.policy`: cyclic-simple sampled averages. This is **not** claimed to be
  the ideal multiplayer own-reach average. Rows without average data use uniform
  legal probabilities; the training summary reports their count.
- `metrics.csv`: committed traversal/node/terminal/entry counts, estimated table
  bytes and elapsed wall time for the current invocation.
- `manifest.txt`: rules/schema, seed and reference implementation label.
- `.previous` files: the preceding complete artifact generation after replacement.

The Linux save path writes a temporary file, flushes, preserves the previous
complete file, renames and flushes the directory. One writer per destination is
required. Checksum, length, numeric values and schema are checked on load.

This prototype serializes to memory and uses a sparse hash map with owned keys
and fixed numeric arrays, rather than production compact arenas. Memory accounting
is an estimate, not an RSS limit. Worker parallelism, large out-of-core tables,
streaming checkpoints and long-run disk/RAM budgets remain future work. The loader
currently limits a single artifact to 2 GiB. Prefer the Linux filesystem for runs.

## Independent response evaluation

```bash
./build/poker_evaluate_mccfr runs/pushfold/current.policy 10000 20000 123
./build/poker_evaluate_mccfr runs/pushfold/average.policy 10000 20000 123
```

Arguments are policy, selection hands per seat, independent scoring hands per
seat and evaluation seed. The tool samples complete deals forward with frozen
opponents. It estimates both actions at each target observation using selection
data, freezes a response, then scores it on disjoint paired data.

Each player acts at most once under these rules. Response rows with fewer than
four selection visits and unseen response rows retain the original policy. The
CSV reports selected/rare rows, uncovered target decisions, missing-policy
fallback counts and approximate 95% intervals. Fallback counts are lookups across
both selection branches and scoring, not a percentage of independent hands.

The sum of estimated gains is **not NashConv/exploitability**, can be negative
through sampling and response-selection error, and says nothing about unrestricted
Hold'em. Report coverage with the estimates. No champion promotion happens here.
The evaluator CLI currently uses the default 25-BB unweighted manifest; alternate
configurations/weighting experiments use the C++ evaluation API with matching
configuration until corresponding CLI options are added.

## Code responsibilities

- `view/PlayerObservation.hpp`: owned view plus borrowed public history.
- `DecisionProvider::choose_observation`: backward-compatible public-context hook;
  history-aware providers request automatic engine history recording.
- `decision/AIActionDistribution.*`: shared concrete actions and stable tokens;
  the normal AI's uniform probabilities and raise behavior are preserved.
- `policy/Observation.*`: restricted tokens and canonical content-based keys,
  relative positions and all 169 starting-hand classes. No hidden opponent cards.
- `policy/FrozenPolicy.*`: runtime lookup and checksummed portable artifacts.
- `policy/TrainedAIDecisionProvider.*`: restricted observation-driven inference.
  It deliberately rejects view-only calls and unsupported policy schemas.
- `research/mccfr/ExternalSamplingTrainer.hpp`: one generic serial traversal used
  for Kuhn, the three-player multistreet fixture and actual eight-player Hold'em.
- `research/mccfr/StrategyTable.*`: ordinary regret matching, transactional deltas,
  simple averages, zero-data fallback and capacity preflight.
- `research/mccfr/TrainingConfig.*`: ordinary reference configuration; independent
  linear regret/average weight switches are available through the C++ API, tested
  and checkpointed, but off in these CLI reference runs.
- `research/mccfr/HoldemTrainingGame.*`: real-engine push/fold adapter with ordinary
  full snapshots/history for correctness. Compact snapshots are deferred.
- `research/mccfr/Checkpoint.*`: resumable learning state and compatibility checks.
- `research/mccfr/Evaluation.*`: independent restricted response probe.
- `research/games/leduc/`: explicitly specified three-player, two-round test game,
  not a claim of compatibility with every published Leduc benchmark.
- `tests/MCCFRTests.cpp`: expected regret checks, own-reach averaging oracle,
  seeded learning, multistreet/side-pot invariants, export and recovery tests.

## Where to continue

See the status table near the top of
`MCCFR_EIGHT_PLAYER_IMPLEMENTATION_PLAN.md`. Continue at **milestone D**: full-game
postflop encoder/codebooks and representative capacity/branching measurements.
Do not use push/fold artifacts as full-game policies or turn on pruning before
its estimator and action-recovery tests are implemented.
