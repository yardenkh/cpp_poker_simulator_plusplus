// Interface used by human input and AI policies to return a requested betting
// action.

#ifndef POKER_DECISION_PROVIDER_HPP
#define POKER_DECISION_PROVIDER_HPP

// PlayerView supplies the information available to the player and the
// PlayerDecision type returned by their decision provider.
#include "view/PlayerObservation.hpp"

namespace poker {

// Common interface for human input and AI strategies. The game asks a provider
// for a decision, then validates and executes it through the betting functions.
// Providers receive a snapshot rather than access to the full table or deck.
class DecisionProvider {
public:
  // Allow a derived provider to be destroyed safely through this interface.
  virtual ~DecisionProvider() = default;

  // Every provider implements this function to choose one action from the view.
  // = 0 makes this an abstract interface: use a derived provider to play.
  // Choosing an action does not itself change the game or move chips.
  virtual PlayerDecision choose_action(const PlayerView &view) = 0;

  // Receives a borrowed public observation; returns the requested action.
  // Existing human/baseline providers need only the view, so default forwarding
  // preserves their API. History-aware providers override this entry point.
  virtual PlayerDecision choose_observation(const PlayerObservation &observation) {
    return choose_action(observation.view);
  }
  // Receives nothing; returns whether the game must record public actions.
  virtual bool requires_action_history() const { return false; }

  // Simulation workers call this at each independent episode. Stochastic or
  // stateful strategies override it to seed their RNG and clear episode state.
  // Stateless providers need no work. Model parameters may remain loaded.
  virtual void reset_for_episode(std::uint64_t /*seed*/) {}
};

} // namespace poker

#endif // POKER_DECISION_PROVIDER_HPP
