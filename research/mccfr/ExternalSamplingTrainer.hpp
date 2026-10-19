// Same serial traversal template is instantiated for toy and real Hold'em
// games.
#pragma once
#include "research/mccfr/StrategyTable.hpp"
#include <cmath>
#include <random>
#include <stdexcept>
namespace poker::mccfr {
struct TraversalStats {
  std::uint64_t nodes = 0, terminals = 0;
};
class ExternalSamplingTrainer {
public:
  TrainingConfig config;
  StrategyTable table;
  std::uint64_t next_traversal = 0, total_nodes = 0, total_terminals = 0;
  // Receives validated configuration; returns an empty, reproducible trainer.
  explicit ExternalSamplingTrainer(TrainingConfig settings = {})
      : config(std::move(settings)) {
    config.validate();
  }
  /** Receives a freshly sampled game at the next actor/terminal boundary.
   * Returns traversal stats after committing regret and cyclic-simple average
   * updates. Opponents sample once; learner branches over every complete
   * action. Game protocol: actor(), node(), snapshot/restore(), apply(index),
   * utility(i). The logical ID advances ONLY on success: restarting retries
   * failed deals. Any limit/numeric/layout error leaves committed progress
   * unchanged. */
  template <class Game> TraversalStats step(Game &game) {
    int learner = static_cast<int>(next_traversal % config.players);
    std::mt19937_64 rng(
        split_seed(config.seed ^ 0x414354494f4eull, next_traversal));
    StrategyRows deltas;
    TraversalStats stats;
    traverse(game, learner, rng, deltas, stats);
    // Optional linear weighting scales contributions, not the regret-matching
    // formula. Regret and averaging schedules are separate and persisted.
    const double weight =
        static_cast<double>(next_traversal / config.players + 1);
    for (auto &[key, delta] : deltas) {
      (void)key;
      for (std::size_t i = 0; i < delta.count; ++i) {
        if (config.linear_regret_weights)
          delta.regrets[i] *= weight;
        if (config.linear_average_weights)
          delta.average[i] *= weight;
      }
    }
    table.commit(deltas, config);
    ++next_traversal;
    total_nodes += stats.nodes;
    total_terminals += stats.terminals;
    return stats;
  }

private:
  /** Receives current branch, learner, RNG and traversal-local buffers; returns
   * estimated learner utility. Reads only committed regrets; local deltas never
   * affect sibling strategies. Exceptions abort this traversal, never skip it.
   */
  template <class Game>
  double traverse(Game &game, int learner, std::mt19937_64 &rng,
                  StrategyRows &deltas, TraversalStats &stats) {
    if (++stats.nodes > config.max_nodes_per_traversal)
      throw std::runtime_error(
          "Traversal node limit: run stopped; retry same logical ID");
    int actor = game.actor();
    if (actor < 0) {
      ++stats.terminals;
      return game.utility(learner);
    }
    auto node = game.node();
    auto entry = table.lookup(node);
    auto strategy = regret_match(entry);
    auto [it, inserted] = deltas.try_emplace(node.key, empty_entry(node));
    if (!inserted) {
      if (it->second.count != entry.count)
        throw std::logic_error("Local action layout mismatch");
      for (std::size_t i = 0; i < entry.count; ++i)
        if (it->second.tokens[i] != entry.tokens[i])
          throw std::logic_error("Local token mismatch");
    }
    // Conservative local-buffer bound; no unchecked growth inside branching.
    if (deltas.size() > config.max_entries ||
        deltas.size() * (sizeof(StrategyEntry) + 512) > config.max_table_bytes)
      throw std::runtime_error("Traversal buffer limit: run stopped");
    ++it->second.visits;
    // Cyclic SIMPLE averaging: only next seat's nodes receive strategy sums.
    // This is explicitly not claimed to be the ideal multiplayer own-reach
    // mean.
    if (actor == (learner + 1) % config.players)
      for (std::size_t i = 0; i < entry.count; ++i)
        it->second.average[i] += strategy[i];
    if (actor != learner) {
      auto selected =
          sample_probability_index({strategy.data(), entry.count}, rng);
      game.apply(selected);
      return traverse(game, learner, rng, deltas, stats);
    }
    auto saved = game.snapshot();
    std::array<double, 9> values{};
    double expected = 0;
    for (std::size_t i = 0; i < entry.count; ++i) {
      game.restore(saved);
      game.apply(i);
      values[i] = traverse(game, learner, rng, deltas, stats);
      expected += strategy[i] * values[i];
    }
    game.restore(saved);
    // Re-find by key after recursion: unordered-map rehash invalidates
    // iterators.
    auto &delta = deltas.at(node.key);
    for (std::size_t i = 0; i < entry.count; ++i)
      delta.regrets[i] += values[i] - expected;
    if (!std::isfinite(expected))
      throw std::runtime_error("Nonfinite traversal utility");
    return expected;
  }
};
} // namespace poker::mccfr
