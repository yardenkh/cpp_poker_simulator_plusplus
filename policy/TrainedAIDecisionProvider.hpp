// A history-aware provider for RESTRICTED eight-player push/fold policies only.
#pragma once
#include "decision/DecisionProvider.hpp"
#include "policy/FrozenPolicy.hpp"
namespace poker::policy {
class TrainedAIDecisionProvider final : public DecisionProvider {
public:
  /** Receives a frozen policy (must outlive provider) and seed; returns a
   * provider. Used by the restricted evaluator. This artifact is not a
   * full-game blueprint. */
  TrainedAIDecisionProvider(const FrozenPolicy &policy, std::uint64_t seed = 1);
  // View-only calls cannot reconstruct public history and fail explicitly.
  PlayerDecision choose_action(const PlayerView &) override;
  // Receives public history/view; returns one legal concrete sampled decision.
  PlayerDecision
  choose_observation(const PlayerObservation &observation) override;
  bool requires_action_history() const override { return true; }
  // Receives episode seed; returns void, restarting only the random sequence.
  void reset_for_episode(std::uint64_t seed) override { rng_.seed(seed); }
  std::uint64_t fallbacks() const { return fallbacks_; }

private:
  const FrozenPolicy &policy_;
  std::mt19937_64 rng_;
  std::uint64_t fallbacks_ = 0;
};
} // namespace poker::policy
