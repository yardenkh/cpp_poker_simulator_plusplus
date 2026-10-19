// Runtime-only representation shared by the trainer and frozen-policy reader.
#pragma once
#include "decision/AIActionDistribution.hpp"
#include "view/PlayerObservation.hpp"
#include <string>
namespace poker::policy {
struct DecisionNode {
  std::string key; // Canonical bytes, never an insertion-order dictionary ID.
  AIActionDistribution candidates;
};
/** Receives a player's public observation. Returns a canonical preflop
 * push/fold node with only Fold/Shove/Call tokens. Used by training AND
 * deployment. Rejects postflop, incomplete cards or unsupported histories; no
 * hidden cards are accepted. The engine settles uncalled shoves and all-in
 * runouts itself. */
DecisionNode push_fold_node(const PlayerObservation &observation);
/** Receives two private cards; returns a suit-invariant class in [0,169).
 * Pairs lie on the diagonal; suited and offsuit rank combinations use opposite
 * matrix triangles. Used only before any public board has been dealt. */
int preflop_class(const std::array<Card, 2> &cards);
} // namespace poker::policy
