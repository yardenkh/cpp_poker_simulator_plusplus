// Connects one-stage probabilistic selection to the engine provider interface.
// Flow: PlayerView -> complete legal decisions -> sample one decision
// -> return PlayerDecision. The engine separately validates and executes it.
// This baseline ignores hand strength and assigns uniform probabilities.
// Four configurable payment/stack fractions supplement the minimum legal raise;
// constructor validation rejects nonfinite values and values outside (0,1).
// Keep a separate provider per seat/worker: RNG state changes on each decision
// and must not be shared across concurrently running simulation threads.
// reset_for_episode restarts randomness but preserves the sizing configuration.
// The same seed and sequence of views reproduce decisions on the same runtime.
// The default seed is fixed; the CLI and simulators supply separate seat seeds.
// choose_action does not retain the view or change the game. Invalid input
// errors propagate rather than being silently replaced with a fallback action.
#pragma once
#include "decision/DecisionProvider.hpp"
#include "decision/AIActionDistribution.hpp"


namespace poker {
// Uniform baseline, independent of card strength. Each seat owns its RNG.
class AIDecisionProvider final : public DecisionProvider {
public:
/**
 * Receives: seed for this provider's random generator and four fractions of
 * the player's current stack to consider alongside the minimum legal raise.
 * Returns: no value (constructor); initializes a ready-to-use AI provider.
 * Usage: CLI/simulation setup creates one provider per AI seat and registers
 * it with set_decision_provider. Invalid fractions throw invalid_argument.
 */
  explicit AIDecisionProvider(std::uint64_t seed = 5489u,
      std::array<double, 4> raise_fractions = {0.25, 0.40, 0.60, 0.80});
/**
 * Receives: a read-only PlayerView for the acting player at the current turn.
 * Returns: one sampled PlayerDecision, with raise_to populated only for RaiseTo.
 * Usage: PokerGame calls this through DecisionProvider, then validates and
 * executes the result. Samples one complete decision, including its raise size
 * if applicable. Advances the RNG without changing the game state.
 */
  PlayerDecision choose_action(const PlayerView &view) override;
/**
 * Receives: the seed assigned to this seat for a new independent episode.
 * Returns: nothing (void); resets RNG state and preserves raise preferences.
 * Usage: simulation runners call this before each episode. Reusing a seed
 * with the same sequence of input views reproduces choices on the same runtime.
 */
  void reset_for_episode(std::uint64_t seed) override;
private:
  std::mt19937_64 rng_;
  std::array<double, 4> raise_fractions_;
};
} // namespace poker
