// Tiny three-player, two-round Leduc variant for the SAME generic MCCFR
// trainer. Six cards (two each J/Q/K), ante 1, one bet per round, sizes 1 then
// 2, no raises. A board-rank pair beats an unpaired hand; equal ranks split.
// This explicit variant is a test fixture, not a claim of matching a published
// Leduc benchmark.
#pragma once
#include "policy/Observation.hpp"
namespace poker::research {
class LeducGame {
public:
  // Receives a seed; returns a sampled private deal and hidden future board.
  explicit LeducGame(std::uint64_t seed);
  int actor() const { return actor_; }
  // Receives nothing; returns only actor-visible rank/board and public history.
  policy::DecisionNode node() const;
  // Receives current candidate index; returns void after one legal transition.
  void apply(std::size_t index);
  // Small fixture state copies are intentional; no callbacks or shared RNG.
  LeducGame snapshot() const { return *this; }
  void restore(const LeducGame &state) { *this = state; }
  // Receives seat; returns terminal net profit, rejecting live-hand requests.
  double utility(int seat) const;
  int street() const { return street_; }

private:
  std::array<int, 3> ranks_{}, paid_{1, 1, 1}, round_{};
  std::array<bool, 3> live_{true, true, true}, acted_{};
  int board_ = 0, street_ = 0, actor_ = 0, bet_ = 0;
  std::string history_;
};
} // namespace poker::research
