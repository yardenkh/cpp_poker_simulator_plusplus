// Configuration for the serial correctness slice; no full-game abstraction yet.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace poker::mccfr {
struct TrainingConfig {
  std::uint64_t seed = 1;
  int players = 8;
  // Independent experimental switches, off for all reference runs. Linear
  // weight is the 1-based sweep number, shared by all seats in a logical sweep.
  bool linear_regret_weights = false, linear_average_weights = false;
  int starting_chips = 2500, small_blind = 50, big_blind = 100;
  std::size_t max_entries = 200000;
  std::size_t max_table_bytes = 256ull << 20;
  std::uint64_t max_nodes_per_traversal = 1000000;
  // Includes game/action/key/averaging versions; checkpoint comparisons are
  // exact.
  std::string schema =
      "pushfold8-v1;key=PF8-v1;average=cyclic-simple;regret=ordinary";
  /** Receives nothing; returns a stable rules/algorithm manifest (not
   * capacities). Limits can be increased on resume without changing the learned
   * game. */
  std::string manifest() const;
  // Receives nothing; returns void, rejecting invalid/unsafe runtime settings.
  void validate() const;
};
// Receives a master seed and logical identifier; returns a deterministic mixed
// seed. Independent namespaces/counters make resume independent of RNG history.
std::uint64_t split_seed(std::uint64_t master, std::uint64_t identifier);
} // namespace poker::mccfr
