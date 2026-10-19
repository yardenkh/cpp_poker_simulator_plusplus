#include "decision/AIDecisionProvider.hpp"
#include "game/PokerGame.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace poker;
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
int main() {
  try {
    PlayerView v;
    v.num_players = 2; v.player_id = 0;
    v.player_stacks[0] = 1000;
    v.round_contributions[0] = 50;
    v.current_bet = 150;
    v.legal_actions = {true, false, true, true, true, 100, 250, 1050};
    auto d = make_uniform_ai_distribution(v);
    check(d.count == 8, "combined candidate count");
    check(d.actions[2].decision.raise_to == 250, "minimum raise total");
    check(d.actions[3].decision.raise_to == 300, "fraction includes call and contribution");
    for (std::size_t i = 0; i < d.count; ++i) {
      check(d.actions[i].probability == 0.125, "uniform complete decisions");
      check(validate_decision(v.legal_actions, d.actions[i].decision), "illegal candidate");
    }
    AIDecisionProvider ai(123), replay(123);
    std::array<int, 9> counts{};
    for (int n = 0; n < 100000; ++n) {
      auto a = ai.choose_action(v), b = replay.choose_action(v);
      check(a.action == b.action && a.raise_to == b.raise_to, "replay mismatch");
      check(validate_decision(v.legal_actions, a), "illegal sample");
      bool found = false;
      for (std::size_t i = 0; i < d.count; ++i)
        if (a.action == d.actions[i].decision.action &&
            a.raise_to == d.actions[i].decision.raise_to) {
          ++counts[i]; found = true; break;
        }
      check(found, "sample not in combined menu");
    }
    // Every concrete choice, including each individual raise, gets 1/8.
    for (std::size_t i = 0; i < d.count; ++i)
      check(std::abs(counts[i] - 12500) < 600, "complete decision frequency");
    ai.reset_for_episode(7); auto first = ai.choose_action(v);
    ai.reset_for_episode(7); auto again = ai.choose_action(v);
    check(first.action == again.action && first.raise_to == again.raise_to, "reset seed");
    v.legal_actions.can_raise = false;
    d = make_uniform_ai_distribution(v);
    check(d.count == 3, "closed raise");
    for (std::size_t i = 0; i < d.count; ++i)
      check(std::abs(d.actions[i].probability - 1.0/3) < 1e-12, "redistribution");
    for (int i = 0; i < 1000; ++i)
      check(ai.choose_action(v).action != ActionType::RaiseTo, "closed raise sampled");
    // Full raises require the whole stack: only AllIn represents this payment.
    v.legal_actions.can_raise = true;
    v.legal_actions.min_raise_to = v.legal_actions.max_raise_to;
    check(make_uniform_ai_distribution(v).count == 3, "duplicate all-in raise");
    // Short all-in remains available without full raise rights.
    v.legal_actions.can_raise = false;
    v.legal_actions.min_raise_to = 2000;
    check(make_uniform_ai_distribution(v).count == 3, "short all-in lost");
    v.player_stacks[0] = 10; v.round_contributions[0] = 0;
    v.legal_actions = {true, true, false, true, true, 0, 2, 10};
    auto small = make_uniform_ai_distribution(v, {0.25, 0.25, 0.25, 0.25});
    check(small.count == 4 && small.actions[2].decision.raise_to == 2,
          "deduplication of minimum and repeated fractions");
    // Cover minimum payments around stack boundaries and existing contributions.
    for (int stack = 1; stack <= 30; ++stack) {
      for (int contribution : {0, 7}) {
        for (int minimum = 1; minimum <= 40; ++minimum) {
          v.player_stacks[0] = stack;
          v.round_contributions[0] = contribution;
          v.legal_actions = {true, false, true, true, true, 1,
                             contribution + minimum, contribution + stack};
          const auto menu = make_uniform_ai_distribution(v);
          int raise_count = 0;
          for (std::size_t i = 0; i < menu.count; ++i) {
            const auto decision = menu.actions[i].decision;
            check(validate_decision(v.legal_actions, decision), "boundary legality");
            if (decision.action != ActionType::RaiseTo) continue;
            ++raise_count;
            check(decision.raise_to < contribution + stack, "duplicate all-in");
            for (std::size_t j = 0; j < i; ++j)
              check(menu.actions[j].decision.action != ActionType::RaiseTo ||
                    menu.actions[j].decision.raise_to != decision.raise_to,
                    "duplicate raise total");
          }
          check((raise_count > 0) == (minimum < stack), "minimum raise availability");
          check(raise_count <= 5, "too many raises");
        }
      }
    }
    // Configuration and empty/invalid observations must fail explicitly.
    for (double fraction : {0.0, 1.0, -0.1, std::numeric_limits<double>::infinity()}) {
      bool rejected = false;
      try { make_uniform_ai_distribution(v, {fraction, .4, .6, .8}); }
      catch (const std::invalid_argument &) { rejected = true; }
      check(rejected, "invalid fraction accepted");
    }
    v.legal_actions = {};
    bool rejected_empty = false;
    try { make_uniform_ai_distribution(v); }
    catch (const std::invalid_argument &) { rejected_empty = true; }
    check(rejected_empty, "empty menu accepted");
    v.player_id = -1;
    bool rejected_seat = false;
    try { make_uniform_ai_distribution(v); }
    catch (const std::out_of_range &) { rejected_seat = true; }
    check(rejected_seat, "invalid seat accepted");
    std::mt19937_64 rng(9);
    std::array<double, 3> certain{0, 1, 0};
    for (int i = 0; i < 1000; ++i)
      check(sample_probability_index(certain, rng) == 1, "zero probability sampled");
    for (auto invalid : {std::array<double, 3>{0, 0, 0},
                         std::array<double, 3>{-0.1, 1, 0.1},
                         std::array<double, 3>{0, std::numeric_limits<double>::quiet_NaN(), 1}}) {
      bool rejected = false;
      try { sample_probability_index(invalid, rng); }
      catch (const std::invalid_argument &) { rejected = true; }
      check(rejected, "invalid probabilities accepted");
    }
    // Exercise actual engine legality over changing streets, stacks and rights.
    for (int seed = 0; seed < 20; ++seed) {
      PokerGame game(4, {1000, 10, 20}, seed);
      std::array<AIDecisionProvider, 4> providers;
      for (int seat = 0; seat < 4; ++seat) {
        providers[seat].reset_for_episode(seed * 4 + seat);
        game.set_decision_provider(seat, providers[seat]);
      }
      game.run_match(50);
      int total = 0;
      for (int seat = 0; seat < 4; ++seat) total += game.table().players[seat].cash;
      check(total == 4000, "AI games lost chips");
    }
    std::cout << "AI tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n'; return 1;
  }
}
