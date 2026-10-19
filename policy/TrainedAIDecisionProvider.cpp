#include "policy/TrainedAIDecisionProvider.hpp"
#include <stdexcept>
namespace poker::policy {
// Receives a frozen policy and seed; returns initialized inference state.
// Runtime has no trainer dependency, so validate the supported schema directly.
TrainedAIDecisionProvider::TrainedAIDecisionProvider(const FrozenPolicy &policy,
                                                     std::uint64_t seed)
    : policy_(policy), rng_(seed) {
  if (!policy.manifest.starts_with("pushfold8-v1;key=PF8-v1;average=cyclic-"
                                   "simple;regret=ordinary;players=8;stack=") ||
      (!policy.manifest.ends_with(";export=current") &&
       !policy.manifest.ends_with(";export=simple-average")))
    throw std::invalid_argument(
        "Provider requires a supported pushfold8-v1 policy");
}
PlayerDecision TrainedAIDecisionProvider::choose_action(const PlayerView &) {
  throw std::logic_error(
      "Learned push/fold policy requires PlayerObservation history");
}
PlayerDecision
TrainedAIDecisionProvider::choose_observation(const PlayerObservation &o) {
  auto node = push_fold_node(o);
  bool fallback = false;
  auto row = policy_.lookup(node, &fallback);
  fallbacks_ += fallback;
  auto index =
      sample_probability_index({row.probabilities.data(), row.count}, rng_);
  return node.candidates.actions[index].decision;
}
} // namespace poker::policy
