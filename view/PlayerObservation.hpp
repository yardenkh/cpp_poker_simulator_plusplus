// Public actions and the current player's observation. No hidden table/deck
// data.
#pragma once
#include "view/PlayerView.hpp"
#include <span>
namespace poker {
struct ActionRecord {
  PokerStage stage{};
  int player_id = -1;
  PlayerDecision decision{};
  int paid = 0;
  int current_bet = 0;
};
struct PlayerObservation {
  PlayerView view;
  // Borrowed until the game next mutates. Providers must not retain this span.
  std::span<const ActionRecord> history;
};
} // namespace poker
