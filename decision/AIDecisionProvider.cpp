// Orchestrate probability sampling; the engine retains control of chip movement.
#include "decision/AIDecisionProvider.hpp"
#include <cmath>
#include <stdexcept>

namespace poker {
/**
 * Receives: seed for this provider's random generator and four fractions of
 * the player's current stack to consider alongside the minimum legal raise.
 * Returns: no value (constructor); initializes a ready-to-use AI provider.
 * Usage: CLI/simulation setup creates one provider per AI seat and registers
 * it with set_decision_provider. Invalid fractions throw invalid_argument.
 */
AIDecisionProvider::AIDecisionProvider(std::uint64_t seed,
                                     std::array<double, 4> raise_fractions)
    : rng_(seed), raise_fractions_(raise_fractions) {
  // Fail at construction for invalid settings rather than during the first hand.
  for (double fraction : raise_fractions_)
    if (!std::isfinite(fraction) || fraction <= 0 || fraction >= 1)
      throw std::invalid_argument("Raise fractions must be between zero and one");
}
// Independent episodes restart the sequence without changing raise preferences.
/**
 * Receives: the seed assigned to this seat for a new independent episode.
 * Returns: nothing (void); resets RNG state and preserves raise preferences.
 * Usage: simulation runners call this before each episode. Reusing a seed
 * with the same sequence of input views reproduces choices on the same runtime.
 */
void AIDecisionProvider::reset_for_episode(std::uint64_t seed) { rng_.seed(seed); }
/**
 * Receives: the acting player's read-only snapshot.
 * Returns: one complete PlayerDecision sampled from the legal menu.
 * Usage: PokerGame calls this through DecisionProvider, then validates/executes
 * the result. A single draw chooses both action and amount. No game mutation.
 */
PlayerDecision AIDecisionProvider::choose_action(const PlayerView &view) {
  // Rebuild each turn because stacks, contributions and legal rights can change.
  const auto distribution = make_uniform_ai_distribution(view, raise_fractions_);
  // Copy only active probabilities into a stack buffer for the generic sampler.
  std::array<double, 9> weights{};
  for (std::size_t i = 0; i < distribution.count; ++i)
    weights[i] = distribution.actions[i].probability;
  const auto index = sample_probability_index(
      {weights.data(), distribution.count}, rng_);
  return distribution.actions[index].decision;
}
} // namespace poker