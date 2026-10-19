// Sparse reference storage. Compact production arenas are a later measured
// step.
#pragma once
#include "policy/FrozenPolicy.hpp"
#include "research/mccfr/TrainingConfig.hpp"
namespace poker::mccfr {
struct StrategyEntry {
  std::size_t count = 0;
  std::array<ActionToken, 9> tokens{};
  std::array<double, 9> regrets{}, average{};
  std::uint64_t visits = 0;
};
using StrategyRows = std::unordered_map<std::string, StrategyEntry>;
/** Receives the candidate node; returns a zero-initialized compatible entry.
 * Used for unseen information sets without mutating the committed table. */
StrategyEntry empty_entry(const policy::DecisionNode &node);
/** Receives an entry; returns regret-matched current probabilities. Retains
 * negative cumulative regrets in storage; uniform only when none are positive.
 */
std::array<double, 9> regret_match(const StrategyEntry &entry);
class StrategyTable {
public:
  StrategyRows rows;
  /** Receives a node; returns an owned snapshot of its committed entry or
   * zeros. Rejects token/layout mismatches rather than reusing unrelated action
   * data. */
  StrategyEntry lookup(const policy::DecisionNode &node) const;
  /** Receives traversal-local deltas/config; returns void after one atomic
   * logical commit. Preflights capacity and finite numeric values first. A
   * limit failure leaves committed regrets and traversal progress unchanged. */
  void commit(const StrategyRows &deltas, const TrainingConfig &config);
  /** Receives a manifest and export kind; returns independent frozen rows.
   * Zero-average-data entries deliberately use uniform legal weights and are
   * counted through zero_average_rows. No training state is modified. */
  policy::FrozenPolicy
  export_policy(const std::string &manifest, bool average,
                std::size_t *zero_average_rows = nullptr) const;
  // Receives nothing; returns conservative estimated storage, not process RSS.
  std::size_t estimated_bytes() const;
  // Used after loading rows; returns void and rebuilds the cached byte
  // estimate.
  void recount_memory();

private:
  std::size_t row_bytes_ = 0;
};
} // namespace poker::mccfr
