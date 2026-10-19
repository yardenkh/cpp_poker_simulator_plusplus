// Exhaust every deal and legal betting history. Checks stay active in Release.
#include "research/games/kuhn/KuhnGame.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace poker::research::kuhn;

namespace {
// Unlike assert(), this keeps validation active in optimized Release builds.
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

// Only the expected exception counts as success. A different exception escapes
// and fails the test, so an unrelated error cannot hide a broken precondition.
template <class Exception, class Function> void rejects(Function function) {
  try {
    function();
  } catch (const Exception &) {
    return;
  }
  throw std::runtime_error("Expected exception was not thrown");
}

// Verify ordering too: a trainer associates probability slots with these IDs.
void check_actions(const KuhnGame &game, Action first, Action second) {
  const auto actions = game.legal_actions();
  check(actions.size() == 2 && actions[0] == first && actions[1] == second,
        "Incorrect legal actions or action order");
}

// Cover chance setup, its normalized distribution, and errors that must not
// change the state. A caller should be able to recover after an invalid input.
void test_chance_and_rejections() {
  KuhnGame game;
  check(game.is_chance_node() && !game.is_terminal(), "Missing initial chance");
  check(game.acting_player() == -1 && game.legal_actions().empty(),
        "Player can act before the deal");
  rejects<std::logic_error>([&] { game.information_set(); });
  rejects<std::logic_error>([&] { game.payoffs(); });
  rejects<std::invalid_argument>([&] { game.apply_action(Action::Check); });
  rejects<std::invalid_argument>([&] { game.deal({Rank::Jack, Rank::Jack}); });
  rejects<std::invalid_argument>([&] {
    game.deal({static_cast<Rank>(99), Rank::King});
  });
  check(game.is_chance_node() && game.history().empty(),
        "Rejected operation changed chance state");
  double probability = 0;
  const auto &deals = KuhnGame::chance_outcomes();
  for (std::size_t i = 0; i < deals.size(); ++i) {
    check(deals[i][0] != deals[i][1], "Deal contains duplicate cards");
    for (std::size_t j = 0; j < i; ++j)
      check(deals[i] != deals[j], "Duplicate chance outcome");
    probability += KuhnGame::chance_probability();
  }
  check(deals.size() == 6 && std::abs(probability - 1.0) < 1e-12,
        "Incomplete chance distribution");
  // Repeat the rejection checks after chance, when a real decision is pending.
  game.deal({Rank::Jack, Rank::King});
  const auto original = game.information_set();
  rejects<std::logic_error>([&] { game.deal({Rank::King, Rank::Jack}); });
  rejects<std::invalid_argument>([&] { game.apply_action(Action::Call); });
  rejects<std::invalid_argument>([&] { game.apply_action(Action::Fold); });
  rejects<std::invalid_argument>([&] { game.apply_action(static_cast<Action>(99)); });
  check(game.information_set() == original, "Rejected operation changed a decision");
  rejects<std::logic_error>([&] { game.payoffs(); });
}

// These are all five terminal betting paths, listed independently of the game
// implementation. Repeating them for all six deals checks 30 distinct leaves.
void test_terminal_paths() {
  const std::vector<std::vector<Action>> paths{
      {Action::Check, Action::Check},
      {Action::Bet, Action::Fold},
      {Action::Bet, Action::Call},
      {Action::Check, Action::Bet, Action::Fold},
      {Action::Check, Action::Bet, Action::Call}};
  for (const auto &deal : KuhnGame::chance_outcomes()) {
    for (std::size_t path = 0; path < paths.size(); ++path) {
      KuhnGame game;
      game.deal(deal);
      for (std::size_t turn = 0; turn < paths[path].size(); ++turn) {
        check(game.acting_player() == static_cast<int>(turn % 2),
              "Incorrect turn order");
        check(!game.is_terminal(), "Hand ended before its response");
        game.apply_action(paths[path][turn]);
      }
      const int showdown_sign = deal[0] > deal[1] ? 1 : -1;
      // In path order: checked showdown, P1 folds, called showdown, P0 folds,
      // called showdown after a check. Folds ignore the actual card strengths.
      const std::array<int, 5> expected{
          showdown_sign, 1, 2 * showdown_sign, -1, 2 * showdown_sign};
      check(game.is_terminal() && !game.is_chance_node(), "Hand did not finish");
      check(game.acting_player() == -1 && game.legal_actions().empty(),
            "Terminal state permits decisions");
      const auto result = game.payoffs();
      check(result[0] == expected[path] && result[1] == -expected[path],
            "Incorrect net payoff");
      for (Action action : {Action::Check, Action::Bet, Action::Fold, Action::Call})
        rejects<std::invalid_argument>([&] { game.apply_action(action); });
      rejects<std::logic_error>([&] { game.information_set(); });
      check(game.payoffs() == result && game.history().size() == paths[path].size(),
            "Rejected terminal action changed the outcome");
    }
  }
}

// Count full-state visits per strategy key. Each key should occur twice, once
// for each possible hidden opponent card, rather than splitting into two keys.
struct Visit {
  InformationSet key;
  int histories = 0;
};

// Exhaust the action tree by value, without selecting a strategy or doing CFR.
// nodes includes decision and terminal states, but excludes the initial chance
// node because this function starts after deal(). leaves counts terminal states.
void walk(KuhnGame game, int &nodes, int &leaves, std::vector<Visit> &infosets) {
  ++nodes;
  if (game.is_terminal()) {
    ++leaves;
    const auto result = game.payoffs();
    check(result[0] + result[1] == 0, "Non-zero-sum payoff");
    return;
  }
  const auto key = game.information_set();
  // Equality must use only the strategy-visible information. Searching a small
  // vector keeps this reference check independent of any future trainer hash.
  const auto it = std::find_if(infosets.begin(), infosets.end(),
                               [&](const Visit &v) { return v.key == key; });
  if (it == infosets.end())
    infosets.push_back({key, 1});
  else
    ++it->histories;
  const auto history = game.history();
  const bool facing_bet = !history.empty() && history.back() == Action::Bet;
  check_actions(game, facing_bet ? Action::Fold : Action::Check,
                facing_bet ? Action::Call : Action::Bet);
  // Every decision rejects the two inapplicable actions without mutation.
  for (Action action : {Action::Check, Action::Bet, Action::Fold, Action::Call}) {
    const auto legal = game.legal_actions();
    if (std::find(legal.begin(), legal.end(), action) == legal.end()) {
      rejects<std::invalid_argument>([&] { game.apply_action(action); });
      check(game.information_set() == key, "Invalid action mutated the game");
    }
  }
  for (const auto action : game.legal_actions()) {
    // Each child owns its state; sibling exploration must leave the parent
    // available to try the other legal action from exactly the same decision.
    auto branch = game;
    branch.apply_action(action);
    walk(branch, nodes, leaves, infosets);
    check(game.information_set() == key, "A branch mutated its parent");
  }
}

// Check the complete tree, then explicitly vary hidden cards and restore a
// saved decision. These properties are prerequisites for correct CFR traversal.
void test_full_tree_and_information_sets() {
  int nodes = 0, leaves = 0;
  std::vector<Visit> infosets;
  for (const auto &deal : KuhnGame::chance_outcomes()) {
    KuhnGame game;
    game.deal(deal);
    walk(game, nodes, leaves, infosets);
  }
  // Each deal has four decision nodes and five leaves: 6*(4+5) = 54 states.
  // P0 has start and check-bet decisions for each card; P1 has after-check and
  // after-bet decisions for each card: 2 players * 2 histories * 3 cards = 12.
  check(nodes == 54 && leaves == 30, "Incorrect complete game tree");
  check(infosets.size() == 12, "Information sets merge history or leak cards");
  for (const auto &visit : infosets)
    check(visit.histories == 2, "Hidden opponent cards split an information set");

  // P0 holds Q in both worlds. Its strategy must ignore whether P1 holds J/K.
  KuhnGame a, b;
  a.deal({Rank::Queen, Rank::Jack});
  b.deal({Rank::Queen, Rank::King});
  const auto saved = a;
  check(a.information_set() == b.information_set(), "Opponent card leaked");
  a.apply_action(Action::Check);
  b.apply_action(Action::Check);
  // Now P1 acts and sees a DIFFERENT own card in each world; keys must differ.
  check(!(a.information_set() == b.information_set()), "Own card absent from key");
  a.apply_action(Action::Bet);
  b.apply_action(Action::Bet);
  // Back at P0, both observations match again, but differ from P0's start key.
  check(a.information_set() == b.information_set(), "Hidden card leaked after bet");
  check(!(a.information_set() == saved.information_set()), "History absent from key");
  // Assignment restores hidden cards as well as the public turn/history. The
  // resulting Q-versus-J showdown confirms that the original deal survived.
  a = saved;
  check(a.history().empty() && a.acting_player() == 0,
        "Copy restore did not restore the decision");
  a.apply_action(Action::Bet);
  a.apply_action(Action::Call);
  check(a.payoffs() == Payoffs{2, -2}, "Copy restore lost private cards");
}
} // namespace

int main() {
  try {
    // Stop at the first failed invariant and let CTest report the diagnostic.
    test_chance_and_rejections();
    test_terminal_paths();
    test_full_tree_and_information_sets();
    std::cout << "All Kuhn poker tests passed (6 deals, 30 terminal histories, "
                 "12 information sets)\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
