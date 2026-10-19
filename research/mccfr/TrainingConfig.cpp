#include "research/mccfr/TrainingConfig.hpp"
#include <limits>
#include <stdexcept>
namespace poker::mccfr {
std::string TrainingConfig::manifest() const {
  return schema + ";players=" + std::to_string(players) +
         ";stack=" + std::to_string(starting_chips) +
         ";sb=" + std::to_string(small_blind) +
         ";bb=" + std::to_string(big_blind) +
         (linear_regret_weights ? ";regret-weight=linear-sweep" : "") +
         (linear_average_weights ? ";average-weight=linear-sweep" : "");
}
void TrainingConfig::validate() const {
  if (players < 2 || players > 8 || starting_chips <= 0 || small_blind <= 0 ||
      big_blind < small_blind ||
      starting_chips > std::numeric_limits<int>::max() / 2 / players ||
      big_blind > std::numeric_limits<int>::max() / 2 || !max_entries ||
      max_table_bytes < 1024 || !max_nodes_per_traversal || schema.empty())
    throw std::invalid_argument("Invalid training configuration");
}
std::uint64_t split_seed(std::uint64_t master, std::uint64_t identifier) {
  auto z = master + 0x9e3779b97f4a7c15ull * (identifier + 1);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}
} // namespace poker::mccfr
