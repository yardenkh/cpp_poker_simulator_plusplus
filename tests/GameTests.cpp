// Scripted complete-game regressions covering action order, runouts, side pots,
// and conservation.

#include "game/PokerGame.hpp"
#include "decision/AIDecisionProvider.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace poker;
namespace {
// Throw instead of assert so Release builds also execute every check.
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
// Deterministic strategies force complete hands along specific paths.
struct Strategy : DecisionProvider {
  enum Mode { Passive, Fold, AllIn, Invalid } mode = Passive;
  PlayerDecision choose_action(const PlayerView &v) override {
    if (mode == Invalid)
      return {ActionType::RaiseTo, -1};
    if (mode == Fold)
      return {ActionType::Fold, 0};
    if (mode == AllIn && v.legal_actions.can_all_in)
      return {ActionType::AllIn, 0};
    return {v.legal_actions.can_check ? ActionType::Check : ActionType::Call,
            0};
  }
};
struct Recorder : GameObserver {
  int hands = 0, finished = 0, showdowns = 0, max_pots = 0;
  int expected_chips = 0;
  std::vector<int> preflop, flop, dealers;
  std::vector<std::array<int, MAX_PLAYERS>> results;
  void on_hand_started(int, const Table &t, const PotManager &) override {
    ++hands;
    dealers.push_back(t.dealer_id);
    for (int id = 0; id < t.num_players; ++id)
      require(t.players[id].cards_in_hand == (t.players[id].cash > 0 ? 2 : 0),
              "eliminated seat was dealt cards");
  }
  void on_player_action(int id, const PlayerDecision &, const Table &t,
                        const PotManager &p,
                        const BettingRoundState &) override {
    if (t.community_card_count == 0)
      preflop.push_back(id);
    if (t.community_card_count == 3)
      flop.push_back(id);
    // Chip supply stays constant while actions move cash into the pot.
    int chips = p.total_pot_amount();
    for (int i = 0; i < t.num_players; ++i)
      chips += t.players[i].cash;
    require(chips == expected_chips, "chips lost during betting");
  }
  void on_showdown(const Table &t, const PotManager &p,
                   const std::array<HandScore, MAX_PLAYERS> &scores) override {
    ++showdowns;
    max_pots = std::max(max_pots, p.num_pots());
    require(t.community_card_count == 5, "incomplete showdown board");
    int players = 0;
    for (int id = 0; id < t.num_players; ++id) {
      if (t.players[id].cards_in_hand == 2)
        ++players;
      if (t.players[id].in_hand)
        require(scores[id] == evaluate_player_hand(t, id),
                "incorrect player score");
    }
    require(t.deck.cards_dealt() == players * 2 + 8,
            "burn/deal count incorrect");
    require(p.pot(0).amount > 0, "showdown pots not built before callback");
  }
  void on_hand_finished(const Table &t,
                        const std::array<int, MAX_PLAYERS> &payouts) override {
    ++finished;
    results.push_back(payouts);
    int chips = 0;
    for (int id = 0; id < t.num_players; ++id)
      chips += t.players[id].cash;
    require(chips == expected_chips, "chips lost at payout");
  }
};
// Set up borrowed providers and a seeded game with an observer.
void setup(PokerGame &g, Strategy &s, Recorder &r, int seats, int chips) {
  for (int id = 0; id < seats; ++id)
    g.set_decision_provider(id, s);
  r.expected_chips = seats * chips;
  g.set_observer(&r);
  g.set_random_seed(42);
}
void test_games() {
  Strategy passive, fold, allin, invalid;
  fold.mode = Strategy::Fold;
  allin.mode = Strategy::AllIn;
  invalid.mode = Strategy::Invalid;
  // Everyone folds: the big blind gets paid without dealing the board.
  {
    PokerGame g(3, {500, 10, 20});
    Recorder r;
    setup(g, fold, r, 3, 500);
    g.play_one_hand();
    require(r.finished == 1 && r.showdowns == 0, "uncontested finish failed");
    require(g.table().community_card_count == 0,
            "uncontested hand dealt board");
  }
  // Heads-up dealer acts first pre-flop; big blind acts first post-flop.
  {
    PokerGame g(2, {500, 10, 20});
    Recorder r;
    setup(g, passive, r, 2, 500);
    g.play_one_hand();
    require(r.showdowns == 1, "normal showdown missing");
    require(r.preflop == std::vector<int>({0, 1}),
            "heads-up preflop order wrong");
    require(r.flop == std::vector<int>({1, 0}),
            "heads-up postflop order wrong");
    g.play_one_hand();
    require(r.dealers == std::vector<int>({0, 1}), "dealer rotated twice");
  }
  // All-in players require no later actions but all streets still burn/deal.
  {
    PokerGame g(3, {100, 10, 20});
    Recorder r;
    setup(g, allin, r, 3, 100);
    g.play_one_hand();
    require(r.showdowns == 1 && r.flop.empty(), "all-in runout failed");
  }
  // Several hands generate unequal stacks; the next all-in showdown needs side
  // pots.
  {
    PokerGame g(3, {500, 10, 20});
    Recorder r;
    setup(g, passive, r, 3, 500);
    g.run_match(3);
    for (int id = 0; id < 3; ++id)
      g.set_decision_provider(id, allin);
    g.play_one_hand();
    require(r.max_pots > 1, "side pots not constructed");
  }
  // Fixed seeds and deterministic choices reproduce cards, winners and payouts.
  {
    PokerGame a(3, {500, 10, 20}), b(3, {500, 10, 20});
    Recorder ra, rb;
    setup(a, passive, ra, 3, 500);
    setup(b, passive, rb, 3, 500);
    a.run_match(20);
    b.run_match(20);
    require(ra.results == rb.results && ra.finished == 20,
            "seeded results differ");
    require(a.table().community_cards == b.table().community_cards,
            "seeded board differs");
  }
  // Tiny blind stacks run a whole match and future hands skip bankrupt seats.
  {
    PokerGame g(3, {1, 1, 2});
    Recorder r;
    setup(g, passive, r, 3, 1);
    g.run_match();
    require(g.match_finished() && g.winner_id() >= 0, "match did not finish");
    int finished = r.finished;
    g.play_one_hand();
    require(r.finished == finished, "finished match played another hand");
  }
  // Headless operation uses no observer and can use the simple AI directly.
  {
    PokerGame g(3, {500, 10, 20});
    AIDecisionProvider ai;
    for (int id = 0; id < 3; ++id)
      g.set_decision_provider(id, ai);
    g.set_random_seed(7);
    g.run_match(10);
  }
  // Invalid setup and illegal provider actions fail explicitly.
  bool rejected = false;
  try {
    PokerGame g(1);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "invalid seats accepted");
  rejected = false;
  try {
    PokerGame g(2, {100, 20, 10});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "invalid blinds accepted");
  rejected = false;
  try {
    PokerGame g(2);
    g.play_one_hand();
  } catch (const std::logic_error &) {
    rejected = true;
  }
  require(rejected, "missing provider accepted");
  {
    PokerGame g(2, {500, 10, 20});
    Recorder r;
    setup(g, invalid, r, 2, 500);
    rejected = false;
    try {
      g.play_one_hand();
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    require(rejected && r.finished == 0, "illegal provider action accepted");
  }
}
} // namespace
int main() {
  try {
    test_games();
    std::cout << "All game tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
