#include "policy/Observation.hpp"
#include <algorithm>
#include <stdexcept>
namespace poker::policy {
namespace {
// Receives a key and unsigned field; returns void, appending four LE bytes.
// Explicit bytes make keys portable across machines and insertion orders.
void field(std::string &key, unsigned value) {
  for (int i = 0; i < 4; ++i)
    key.push_back(static_cast<char>(value >> (8 * i)));
}
} // namespace
int preflop_class(const std::array<Card, 2> &cards) {
  int a = static_cast<int>(cards[0].face) - 2;
  int b = static_cast<int>(cards[1].face) - 2;
  if (a < 0 || a > 12 || b < 0 || b > 12 || cards[0] == cards[1] ||
      static_cast<int>(cards[0].suit) > 3 ||
      static_cast<int>(cards[1].suit) > 3)
    throw std::invalid_argument("Invalid private cards");
  int high = std::max(a, b), low = std::min(a, b);
  return cards[0].suit == cards[1].suit ? high * 13 + low : low * 13 + high;
}
DecisionNode push_fold_node(const PlayerObservation &o) {
  const auto &v = o.view;
  if (v.num_players != 8 || v.player_id < 0 || v.player_id >= 8 ||
      v.dealer_id < 0 || v.dealer_id >= 8 || v.hole_card_count != 2 ||
      v.stage != PokerStage::PreFlop || v.community_card_count != 0)
    throw std::invalid_argument(
        "Push/fold policy requires an eight-player preflop observation");
  bool shoved = false;
  for (const auto &record : o.history) {
    if (record.stage != PokerStage::PreFlop || record.player_id < 0 ||
        record.player_id >= 8)
      throw std::invalid_argument("Unsupported push/fold history");
    if (record.decision.action == ActionType::AllIn)
      shoved = true;
    else if (record.decision.action != ActionType::Fold &&
             !(shoved && record.decision.action == ActionType::Call))
      throw std::invalid_argument(
          "History is outside the no-limp push/fold schema");
  }
  DecisionNode node;
  auto &menu = node.candidates;
  // Receives an allowed flag, engine action and stable restricted token;
  // returns void. Append only legal concrete decisions; no weights assigned.
  const auto add = [&](bool allowed, ActionType action, ActionToken token) {
    if (allowed)
      menu.actions[menu.count++] = {{action, 0}, 0, token};
  };
  add(v.legal_actions.can_fold, ActionType::Fold, ActionToken::Fold);
  if (shoved)
    add(v.legal_actions.can_call, ActionType::Call, ActionToken::Call);
  else
    add(v.legal_actions.can_all_in, ActionType::AllIn, ActionToken::Shove);
  if (!menu.count)
    throw std::logic_error("No push/fold decisions at pending turn");
  auto &key = node.key;
  key = "PF8-v1";
  field(key, (v.player_id - v.dealer_id + 8) % 8);
  field(key, preflop_class(v.hole_cards));
  field(key, v.current_bet);
  field(key, v.pot_amount);
  // Rotate every public seat array consistently, not just the actor identity.
  for (int relative = 0; relative < 8; ++relative) {
    int seat = (v.dealer_id + relative) % 8;
    field(key, v.player_stacks[seat]);
    field(key, v.round_contributions[seat]);
    field(key, v.total_contributions[seat]);
    field(key, v.players_in_hand[seat] | (v.players_all_in[seat] << 1));
  }
  field(key, static_cast<unsigned>(o.history.size()));
  for (const auto &record : o.history) {
    field(key, (record.player_id - v.dealer_id + 8) % 8);
    field(key, static_cast<unsigned>(record.decision.action));
    field(key, record.paid);
    field(key, record.current_bet);
  }
  field(key, static_cast<unsigned>(menu.count));
  for (std::size_t i = 0; i < menu.count; ++i)
    field(key, static_cast<unsigned>(menu.actions[i].token));
  return node;
}
} // namespace poker::policy
