#include "research/mccfr/Evaluation.hpp"
#include <cmath>
#include <stdexcept>
namespace poker::mccfr {
namespace {
// Receives current game, policy and RNG; returns sampled action index and
// counts missing-policy fallbacks. Does not advance the game itself.
std::size_t choose(const HoldemTrainingGame &game,
                   const policy::FrozenPolicy &policy, std::mt19937_64 &rng,
                   std::uint64_t &fallbacks) {
  auto node = game.node();
  bool fallback = false;
  auto row = policy.lookup(node, &fallback);
  fallbacks += fallback;
  return sample_probability_index({row.probabilities.data(), row.count}, rng);
}
// Receives a game and target; returns void once target acts or the hand ends.
void to_target(HoldemTrainingGame &game, int target,
               const policy::FrozenPolicy &policy, std::mt19937_64 &rng,
               std::uint64_t &fallbacks) {
  while (game.actor() >= 0 && game.actor() != target)
    game.apply(choose(game, policy, rng, fallbacks));
}
// Receives a post-target game; returns settled target reward using frozen
// others.
double finish(HoldemTrainingGame &game, int target,
              const policy::FrozenPolicy &policy, std::mt19937_64 rng,
              std::uint64_t &fallbacks) {
  while (game.actor() >= 0) {
    if (game.actor() == target)
      throw std::logic_error("Restricted player acted twice");
    game.apply(choose(game, policy, rng, fallbacks));
  }
  return game.utility(target);
}
struct Values {
  std::array<double, 9> sums{};
  std::uint64_t count = 0;
  std::size_t actions = 0;
};
} // namespace
SeatEvaluation evaluate_response(const policy::FrozenPolicy &policy,
                                 const TrainingConfig &config, int seat,
                                 std::uint64_t selection_hands,
                                 std::uint64_t scoring_hands,
                                 std::uint64_t seed,
                                 std::uint64_t minimum_visits) {
  if (seat < 0 || seat >= 8 || !selection_hands || scoring_hands < 2 ||
      !minimum_visits)
    throw std::invalid_argument("Invalid restricted evaluation budget/seat");
  auto prefix = config.manifest() + ";export=";
  if (policy.manifest != prefix + "current" &&
      policy.manifest != prefix + "simple-average")
    throw std::invalid_argument(
        "Policy does not match evaluation rules/schema");
  SeatEvaluation report;
  std::unordered_map<std::string, Values> values;
  TrainingConfig deals = config;
  deals.seed = split_seed(seed ^ 0x53454c454354ull, seat);
  for (std::uint64_t n = 0; n < selection_hands; ++n) {
    HoldemTrainingGame game(deals, n);
    std::mt19937_64 rng(split_seed(deals.seed, n));
    to_target(game, seat, policy, rng, report.policy_fallbacks);
    if (game.actor() < 0)
      continue;
    auto node = game.node();
    auto saved = game.snapshot();
    auto &v = values[node.key];
    v.actions = node.candidates.count;
    ++v.count;
    for (std::size_t i = 0; i < v.actions; ++i) {
      game.restore(saved);
      game.apply(i);
      v.sums[i] += finish(game, seat, policy, rng, report.policy_fallbacks);
    }
  }
  std::unordered_map<std::string, std::size_t> response;
  for (const auto &[key, v] : values) {
    if (v.count < minimum_visits) {
      ++report.rare_rows;
      continue;
    }
    std::size_t best = 0;
    for (std::size_t i = 1; i < v.actions; ++i)
      if (v.sums[i] > v.sums[best])
        best = i;
    response.emplace(key, best);
  }
  report.selected_rows = response.size();
  // Scoring has a disjoint seed namespace; selecting and scoring never share
  // data.
  deals.seed = split_seed(seed ^ 0x53434f5245ull, seat);
  double sum = 0, sum2 = 0;
  for (std::uint64_t n = 0; n < scoring_hands; ++n) {
    HoldemTrainingGame game(deals, n);
    std::mt19937_64 rng(split_seed(deals.seed, n));
    to_target(game, seat, policy, rng, report.policy_fallbacks);
    double gain = 0;
    if (game.actor() >= 0) {
      auto node = game.node();
      auto saved = game.snapshot();
      // Separate own-action draw keeps continuation random numbers paired.
      std::mt19937_64 own_rng(split_seed(deals.seed ^ 0x4f574eull, n));
      auto baseline = choose(game, policy, own_rng, report.policy_fallbacks);
      auto it = response.find(node.key);
      if (it == response.end())
        ++report.uncovered_target_decisions;
      auto alternative = it == response.end() ? baseline : it->second;
      if (alternative != baseline) {
        game.apply(baseline);
        double original =
            finish(game, seat, policy, rng, report.policy_fallbacks);
        game.restore(saved);
        game.apply(alternative);
        gain =
            finish(game, seat, policy, rng, report.policy_fallbacks) - original;
      }
    }
    sum += gain;
    sum2 += gain * gain;
  }
  report.scored_hands = scoring_hands;
  report.gain_bb_per_hand = sum / scoring_hands;
  double variance = (sum2 - sum * sum / scoring_hands) / (scoring_hands - 1);
  report.standard_error = std::sqrt(std::max(0.0, variance) / scoring_hands);
  return report;
}
} // namespace poker::mccfr
