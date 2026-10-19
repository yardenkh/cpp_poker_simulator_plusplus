// Implements chance selection, the complete betting state machine and terminal
// utilities. Strategy learning belongs in the future trainer, not this module.
#include "research/games/kuhn/KuhnGame.hpp"
#include <algorithm>
#include <stdexcept>

namespace poker::research::kuhn {
namespace {
// Ordered assignments matter: (J,Q) gives the first actor J, while (Q,J)
// gives that actor Q. Each has probability (1/3)*(1/2) = 1/6. The remaining
// card is determined by this pair and need not be stored.
constexpr std::array<Deal, 6> deals{{
    {Rank::Jack, Rank::Queen}, {Rank::Jack, Rank::King},
    {Rank::Queen, Rank::Jack}, {Rank::Queen, Rank::King},
    {Rank::King, Rank::Jack}, {Rank::King, Rank::Queen},
}};
// Shared immutable lists avoid allocating a vector on every trainer visit.
constexpr std::array<Action, 2> opening_actions{Action::Check, Action::Bet};
constexpr std::array<Action, 2> response_actions{Action::Fold, Action::Call};

bool valid_rank(Rank rank) {
  // An enum can still contain an invalid value after an explicit cast or data
  // load, so validate the boundary rather than relying only on the type.
  return rank == Rank::Jack || rank == Rank::Queen || rank == Rank::King;
}
} // namespace

const std::array<Deal, 6> &KuhnGame::chance_outcomes() { return deals; }

void KuhnGame::deal(const Deal &cards) {
  // Check everything before assigning, so a rejected deal can be retried.
  if (dealt_)
    throw std::logic_error("Kuhn cards have already been dealt");
  if (!valid_rank(cards[0]) || !valid_rank(cards[1]) || cards[0] == cards[1])
    throw std::invalid_argument("Kuhn deal needs two distinct J/Q/K cards");
  cards_ = cards;
  dealt_ = true;
}

bool KuhnGame::is_terminal() const {
  if (!dealt_ || history_size_ < 2)
    return false;
  // apply_action() guarantees legal histories, so at length two there are only
  // four possibilities: check-check, check-bet, bet-fold, bet-call. Only
  // check-bet continues; either response to it is terminal at length three.
  return history_size_ == 3 || history_[0] == Action::Bet ||
         history_[1] == Action::Check;
}

int KuhnGame::acting_player() const {
  if (!dealt_ || is_terminal())
    return -1;
  // Turns alternate: P0 opens, P1 responds, and P0 responds again only after
  // check-bet. There are no skipped players or further betting rounds.
  return static_cast<int>(history_size_ % 2);
}

std::span<const Action> KuhnGame::legal_actions() const {
  // Returning no actions at chance is deliberate: deal() handles chance and
  // apply_action() handles player choices, keeping the two roles separate.
  if (acting_player() < 0)
    return {};
  if (history_size_ > 0 && history_[history_size_ - 1] == Action::Bet)
    return response_actions;
  return opening_actions;
}

void KuhnGame::apply_action(Action action) {
  const auto legal = legal_actions();
  if (std::find(legal.begin(), legal.end(), action) == legal.end())
    throw std::invalid_argument("Illegal Kuhn action for the current state");
  // A legal response ends the hand by length three. Once terminal, legal is
  // empty, so this append cannot run past the fixed history buffer.
  history_[history_size_++] = action;
}

InformationSet KuhnGame::information_set() const {
  const int player = acting_player();
  if (player < 0)
    throw std::logic_error("Kuhn information set needs a pending decision");
  // Copy the public history and ONLY the acting player's card. The complete
  // deal remains in the simulator even when a trainer enumerates hidden worlds.
  return {player, cards_[player], history_, history_size_};
}

std::span<const Action> KuhnGame::history() const {
  // Hide unused buffer entries; they are not extra checks in the betting line.
  return {history_.data(), history_size_};
}

Payoffs KuhnGame::payoffs() const {
  if (!is_terminal())
    throw std::logic_error("Kuhn payoffs require a terminal state");
  int winner;
  int profit;
  if (history_[history_size_ - 1] == Action::Fold) {
    // Action index zero belongs to P0, index one to P1, and index two to P0.
    // Identify the folder from the LAST index, not the next actor (-1 here).
    const int folding_player = static_cast<int>((history_size_ - 1) % 2);
    winner = 1 - folding_player;
    profit = 1; // Unmatched bet is returned; only the opponent's ante is won.
  } else {
    // Cards are distinct and ordered by strength, so showdown cannot tie.
    // Check-check has a two-chip pot and bet-call a four-chip pot: the winner
    // nets one or two chips after subtracting their own ante and possible bet.
    winner = cards_[0] > cards_[1] ? 0 : 1;
    profit = history_[history_size_ - 1] == Action::Call ? 2 : 1;
  }
  // Always return seat order, irrespective of who bet or made the last move.
  return winner == 0 ? Payoffs{profit, -profit} : Payoffs{-profit, profit};
}

} // namespace poker::research::kuhn
