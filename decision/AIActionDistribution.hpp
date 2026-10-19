// Build one menu of complete legal decisions: each raise amount is its own
// entry.
#pragma once
#include "view/PlayerView.hpp"
#include <array>
#include <cstddef>
#include <random>
#include <span>

namespace poker {
// Stable meaning independent of the position of a candidate in a filtered menu.
enum class ActionToken : std::uint8_t {
  Fold,
  Check,
  Call,
  MinRaise,
  Stack25,
  Stack40,
  Stack60,
  Stack80,
  AllIn,
  Shove // restricted push/fold only; separate schema from the full-game menu
};
struct ActionProbability {
  // Includes the exact round total when action == RaiseTo; otherwise zero.
  PlayerDecision decision;
  double probability = 0;
  ActionToken token = ActionToken::Fold;
};
struct AIActionDistribution {
  // Capacity: fold, check, call, all-in, and five raises. Only [0, count) is
  // used. Fixed storage keeps the decision path free of heap allocations.
  std::array<ActionProbability, 9> actions{};
  std::size_t count = 0;
};
/** Receives a player view and four configured payment fractions; returns only
 * complete candidates and stable ordinal sizing tokens (no probabilities).
 * The schema/manifest records the configured fractions: tokens name slots.
 * Used identically by trainers, learned providers and the uniform wrapper. */
AIActionDistribution generate_ai_candidates(
    const PlayerView &view,
    const std::array<double, 4> &fractions = {0.25, 0.40, 0.60, 0.80});
/**
 * Receives: the acting player's snapshot and four payment/current-stack
 * fractions. Returns: a menu of distinct legal decisions, each with probability
 * 1/count. Usage: choose_action builds this menu and samples once. Future
 * trainers can use the same concrete decisions with learned weights instead of
 * uniform ones. Order: fold, check, call, minimum raise, configured raises,
 * all-in (if legal). Raise fractions include the call portion; amounts are
 * rounded down and illegal or duplicate raises are omitted. Full-stack raises
 * use AllIn exclusively. Call and AllIn remain distinct legal types even when
 * they pay the same amount. Throws out_of_range for an invalid seat and
 * invalid_argument for fractions outside (0,1), nonfinite fractions, or a
 * snapshot with no legal decisions.
 */
AIActionDistribution make_uniform_ai_distribution(
    const PlayerView &view,
    const std::array<double, 4> &fractions = {0.25, 0.40, 0.60, 0.80});
/**
 * Receives: a borrowed probability span and mutable random generator.
 * Returns: one index sampled according to the probabilities; consumes RNG
 * state. Usage: choose_action maps that index directly to a complete
 * PlayerDecision. Supports learned nonuniform weights too. Throws
 * invalid_argument before drawing for empty, negative, nonfinite, or
 * unnormalized input (sum tolerance 1e-9). Zero-weight entries are never
 * selected; the span is not modified or retained.
 */
std::size_t sample_probability_index(std::span<const double> probabilities,
                                     std::mt19937_64 &rng);
} // namespace poker
