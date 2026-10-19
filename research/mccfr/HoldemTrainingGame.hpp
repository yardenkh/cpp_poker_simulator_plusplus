// Real engine adapter for the eight-player no-limp restricted training game.
#pragma once
#include "game/PokerGame.hpp"
#include "policy/Observation.hpp"
#include "research/mccfr/TrainingConfig.hpp"
namespace poker::mccfr {
class HoldemTrainingGame {
public:
  /** Receives training configuration and logical traversal ID; returns a dealt
   * independent eight-player hand. Button rotates per sweep. History is enabled
   * in this reference adapter so deployment and training share the SAME
   * records. Compact trainer-owned history is deferred until profiling
   * justifies it. */
  HoldemTrainingGame(const TrainingConfig &config, std::uint64_t traversal);
  // Returns pending actor or -1 after engine settlement; no hidden state
  // exposed.
  int actor() const { return engine_.acting_player(); }
  // Returns a public restricted information set and complete legal decisions.
  policy::DecisionNode node() const {
    return policy::push_fold_node(engine_.current_observation());
  }
  // Receives an index in the current node; returns void after engine
  // validation.
  void apply(std::size_t index);
  // Snapshot inputs/outputs follow the engine contract; no policy RNG is
  // stored.
  PokerGame::Snapshot snapshot() const { return engine_.snapshot(); }
  void restore(const PokerGame::Snapshot &state) { engine_.restore(state); }
  // Receives seat; returns settled net BB, rejecting nonterminal requests.
  double utility(int seat) const;
  // Used in tests to compare training and runtime observations, not for
  // policies.
  const PokerGame &engine() const { return engine_; }

private:
  PokerGame engine_;
  int big_blind_;
};
} // namespace poker::mccfr
