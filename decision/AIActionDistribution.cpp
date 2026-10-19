// Shared candidate generation is separate from baseline probability assignment.
#include "decision/AIActionDistribution.hpp"
#include <cmath>
#include <stdexcept>

namespace poker {
/**
 * Receives: the current player's view and configured payment/stack fractions.
 * Returns: complete legal candidates with zero weights and stable action
 * tokens. Usage: trainers assign learned weights; the baseline wrapper assigns
 * uniform weights.
 */
AIActionDistribution
generate_ai_candidates(const PlayerView &view,
                       const std::array<double, 4> &fractions) {
  if (view.num_players < 1 || view.num_players > MAX_PLAYERS ||
      view.player_id < 0 || view.player_id >= view.num_players)
    throw std::out_of_range("AI view does not identify an active seat");
  for (double fraction : fractions)
    if (!std::isfinite(fraction) || fraction <= 0 || fraction >= 1)
      throw std::invalid_argument(
          "Raise fractions must be between zero and one");

  AIActionDistribution result;
  const auto &legal = view.legal_actions;
  // Receives availability and a complete decision; returns void. Append legal
  // ordinary actions into result. Probabilities are assigned after counting.
  const auto add = [&](bool allowed, PlayerDecision decision,
                       ActionToken token) {
    if (allowed)
      result.actions[result.count++] = {decision, 0, token};
  };
  add(legal.can_fold, {ActionType::Fold, 0}, ActionToken::Fold);
  add(legal.can_check, {ActionType::Check, 0}, ActionToken::Check);
  add(legal.can_call, {ActionType::Call, 0}, ActionToken::Call);

  if (legal.can_raise) {
    const int stack = view.player_stacks[view.player_id];
    const int contributed = view.round_contributions[view.player_id];
    // Receives additional chips paid NOW, including the call; returns void.
    // Used for minimum/fraction proposals. Reject invalid and duplicate amounts
    // before appending a complete RaiseTo decision to the same action menu.
    const auto add_raise = [&](long long payment, ActionToken token) {
      // Wide intermediates avoid overflow before checking the engine limits.
      const long long total = static_cast<long long>(contributed) + payment;
      if (payment <= 0 || payment >= stack || total < legal.min_raise_to ||
          total > legal.max_raise_to)
        return;
      for (std::size_t i = 0; i < result.count; ++i)
        if (result.actions[i].decision.action == ActionType::RaiseTo &&
            result.actions[i].decision.raise_to == total)
          return;
      add(true, {ActionType::RaiseTo, static_cast<int>(total)}, token);
    };
    // Always offer the minimum non-all-in raise when affordable, even if all
    // configured fractions fall below it. Full-stack payments use AllIn below.
    add_raise(static_cast<long long>(legal.min_raise_to) - contributed,
              ActionToken::MinRaise);
    // Example: 50 already paid, 1000 remaining, fraction .25 -> pay 250 now,
    // RaiseTo 300. Existing contributions are not included in the stack
    // fraction.
    for (std::size_t i = 0; i < fractions.size(); ++i)
      add_raise(
          static_cast<long long>(std::floor(fractions[i] * stack)),
          static_cast<ActionToken>(static_cast<int>(ActionToken::Stack25) + i));
  }
  // Short all-ins and all-in calls can be legal even without full raise rights.
  add(legal.can_all_in, {ActionType::AllIn, 0}, ActionToken::AllIn);
  if (!result.count)
    throw std::invalid_argument("AI has no legal actions");
  return result;
}

// Receives the same input as candidate generation; returns uniform weights over
// the active complete decisions. Existing normal-game behavior is unchanged.
AIActionDistribution
make_uniform_ai_distribution(const PlayerView &view,
                             const std::array<double, 4> &fractions) {
  auto result = generate_ai_candidates(view, fractions);
  for (std::size_t i = 0; i < result.count; ++i)
    result.actions[i].probability = 1.0 / result.count;
  return result;
}
/**
 * Receives normalized probabilities and an RNG; returns one sampled index.
 * Used once by choose_action for the combined menu, including all raise sizes.
 */
std::size_t sample_probability_index(std::span<const double> probabilities,
                                     std::mt19937_64 &rng) {
  // Reject invalid strategy output before consuming any RNG state. Tolerate
  // small normalization error from floating-point probability calculations.
  double sum = 0;
  for (double p : probabilities) {
    if (!std::isfinite(p) || p < 0)
      throw std::invalid_argument("Invalid AI probability");
    sum += p;
  }
  if (probabilities.empty() || !std::isfinite(sum) || std::abs(sum - 1) > 1e-9)
    throw std::invalid_argument("AI probabilities must sum to one");
  // Draw in [0, sum). Consecutive intervals have widths p0, p1, ... .
  // Scaling by the accepted sum also handles tiny normalization errors.
  const double draw = std::generate_canonical<double, 53>(rng) * sum;
  double cumulative = 0;
  // Strict comparison skips zero-width intervals. Preserve a positive entry
  // for the floating-point fallback so trailing zero weights stay impossible.
  std::size_t last_positive = 0;
  for (std::size_t i = 0; i < probabilities.size(); ++i) {
    if (probabilities[i] > 0)
      last_positive = i;
    cumulative += probabilities[i];
    if (draw < cumulative)
      return i;
  }
  return last_positive; // Floating-point accumulation at the upper boundary.
}
} // namespace poker
