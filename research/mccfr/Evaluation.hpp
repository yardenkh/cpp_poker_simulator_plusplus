// Independent response selection/scoring for the restricted eight-player game.
#pragma once
#include "policy/FrozenPolicy.hpp"
#include "research/mccfr/HoldemTrainingGame.hpp"
namespace poker::mccfr {
struct SeatEvaluation {
  double gain_bb_per_hand = 0, standard_error = 0;
  std::size_t selected_rows = 0, rare_rows = 0;
  std::uint64_t scored_hands = 0, uncovered_target_decisions = 0,
                policy_fallbacks = 0;
};
/** Receives a frozen restricted policy, matching config, target seat,
 * independent selection/scoring budgets and seed. Returns paired gain/error and
 * coverage. Root sampling forwards opponents with their frozen policies,
 * conditioning holdings on observed actions without guessing uniform
 * post-history ranges. Each seat acts at most once; explore all its actions in
 * selection data only. Freeze those choices before scoring on disjoint deals.
 * Rare/unseen response rows retain the original policy; the result is an
 * estimated deviation gain, NOT exact exploitability or a guarantee of
 * nonnegative finite-sample gain. */
SeatEvaluation evaluate_response(const policy::FrozenPolicy &policy,
                                 const TrainingConfig &config, int seat,
                                 std::uint64_t selection_hands,
                                 std::uint64_t scoring_hands,
                                 std::uint64_t seed,
                                 std::uint64_t minimum_visits = 4);
} // namespace poker::mccfr
