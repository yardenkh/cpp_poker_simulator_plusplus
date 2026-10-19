// Independent small poker game for validating regret-minimization trainers.
#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace poker::research::kuhn {

// Declaration order is strength order; showdown compares these enum values.
enum class Rank { Jack, Queen, King };
// Distinct names avoid overloading "pass" as both check and fold, or "bet"
// as both opening bet and call. Only two of these are legal at a decision.
enum class Action { Check, Bet, Fold, Call };
using Deal = std::array<Rank, 2>; // Player 0's card, then player 1's card.
using Payoffs = std::array<int, 2>; // Net profit, including the ante.

// An information set groups full states the player cannot distinguish. For
// example, holding Q at the start gives the same key against hidden J or K.
// A CFR trainer must share one strategy for those states, rather than learning
// separate actions based on the opponent's actual hidden card.
struct InformationSet {
  // Seat choosing now, not the last player who acted. -1 means unset.
  int player_id = -1;
  // Only this seat's private card. Jack is a default value, not an undealt flag.
  Rank card = Rank::Jack;
  // At most three actions: check, bet, then fold/call. Only the first
  // history_size entries have been played; remaining entries are padding.
  std::array<Action, 3> history{};
  std::size_t history_size = 0;

  // Compare fields, not raw struct bytes. Games keep unused history entries
  // value-initialized so padding actions cannot split an information set.
  bool operator==(const InformationSet &) const = default;
};

// Two players ante one chip and receive distinct cards from J/Q/K. Player 0
// acts first. Check or bet one chip; a bet permits fold/call only. No raises.
// Higher card wins a showdown. A fresh game starts at chance, before dealing.
//
// Owns only value state: copy before exploring a branch, assign to restore.
// No external pointers, internal RNG, callbacks or Hold'em dependencies.
class KuhnGame {
public:
  // Six equally likely ordered deals. Enumerate for exact CFR, or sample
  // uniformly for a sampled trainer. Chance is separate from player actions.
  // The returned array has static lifetime and must not be modified.
  static const std::array<Deal, 6> &chance_outcomes();
  // Probability of EACH listed outcome, not the probability of a private card.
  static constexpr double chance_probability() { return 1.0 / 6.0; }

  // True only before deal(). Neither player has a decision at that point.
  bool is_chance_node() const { return !dealt_; }
  // Install one ordered chance outcome. Throws logic_error on a second deal,
  // or invalid_argument for duplicate/invalid cards, without changing state.
  // Assign a fresh KuhnGame to start another independent hand.
  void deal(const Deal &cards);
  // A hand ends on check-check, bet-fold/call, or check-bet-fold/call.
  bool is_terminal() const;
  // -1 at chance/terminal states; otherwise 0 or 1.
  int acting_player() const;
  // Empty at chance/terminal states. Otherwise Check/Bet when no bet is owed,
  // or Fold/Call facing a bet. Stable ordering supports trainer action indices.
  // This span points to immutable static storage, not mutable game state.
  std::span<const Action> legal_actions() const;
  // Append one legal public action and implicitly advance the turn. Throws
  // invalid_argument for illegal/unknown actions or actions outside a decision.
  void apply_action(Action action);
  // Return an owned strategy key; safe to keep after the game changes. Throws
  // logic_error unless a player decision is pending. Excludes opponent cards.
  InformationSet information_set() const;
  // Borrowed public history; valid until mutation or destruction.
  std::span<const Action> history() const;
  // Throws logic_error unless terminal. Positive means profit, negative means
  // loss: +/-1 after a fold or two checks, +/-2 after a bet and call. These
  // values already subtract the winner's own contributions, including ante.
  Payoffs payoffs() const;

private:
  // Explicitly distinguishes chance from a dealt game. Default cards_ contains
  // placeholders and must not be read as an actual deal until this is true.
  bool dealt_ = false;
  // Full simulator knowledge. Access to this array is never part of a key.
  Deal cards_{};
  // The public history is the entire betting state: no pot or stack simulation
  // is needed because stakes and the maximum number of actions are fixed.
  std::array<Action, 3> history_{};
  // Always 0..3; only apply_action() can grow it, after validating legality.
  std::size_t history_size_ = 0;
};

} // namespace poker::research::kuhn
