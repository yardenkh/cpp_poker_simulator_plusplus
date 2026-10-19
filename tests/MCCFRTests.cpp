// Correctness tests use throwing assertions so Release builds retain checks.
#include "policy/TrainedAIDecisionProvider.hpp"
#include "research/games/kuhn/KuhnGame.hpp"
#include "research/games/leduc/LeducGame.hpp"
#include "research/mccfr/Checkpoint.hpp"
#include "research/mccfr/Evaluation.hpp"
#include "research/mccfr/HoldemTrainingGame.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
using namespace poker;
using namespace poker::mccfr;
namespace {
// Receives a condition/message; returns void or throws a test failure.
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
// Receives an operation; returns void only if it fails explicitly.
template <class F> void rejects(F operation, const char *message) {
  bool failed = false;
  try {
    operation();
  } catch (const std::exception &) {
    failed = true;
  }
  check(failed, message);
}
// Existing Kuhn adapter: no duplicated game rules, same trainer instantiation.
struct KuhnAdapter {
  research::kuhn::KuhnGame game;
  explicit KuhnAdapter(int deal) { game.deal(game.chance_outcomes()[deal]); }
  int actor() const { return game.acting_player(); }
  policy::DecisionNode node() const {
    auto info = game.information_set();
    policy::DecisionNode node;
    node.key = "KUHN:" + std::to_string(info.player_id) + ":" +
               std::to_string(static_cast<int>(info.card)) + ":";
    for (auto a : game.history())
      node.key += static_cast<char>('a' + static_cast<int>(a));
    for (auto a : game.legal_actions())
      node.candidates.actions[node.candidates.count++] = {
          {ActionType::Check, 0}, 0, static_cast<ActionToken>(a)};
    return node;
  }
  void apply(std::size_t i) { game.apply_action(game.legal_actions()[i]); }
  KuhnAdapter snapshot() const { return *this; }
  void restore(const KuhnAdapter &s) { *this = s; }
  double utility(int i) const { return game.payoffs()[i]; }
};
// Receives Kuhn state/policy; returns EXACT expected seat-0 value with no
// sampling.
double kuhn_value(KuhnAdapter game, const policy::FrozenPolicy &policy) {
  if (game.actor() < 0)
    return game.utility(0);
  auto n = game.node();
  auto row = policy.lookup(n);
  double value = 0;
  for (std::size_t i = 0; i < row.count; ++i) {
    auto child = game;
    child.apply(i);
    value += row.probabilities[i] * kuhn_value(child, policy);
  }
  return value;
}
// Receives a tiny game/frozen profile and own-reach vector; returns void,
// accumulating the EXACT perfect-recall strategy numerator by full enumeration.
// This oracle is test-only: production exports use explicitly labeled simple
// averaging and are never asserted to equal this ideal multiplayer quantity.
void exact_average(
    KuhnAdapter game, const policy::FrozenPolicy &profile,
    std::array<double, 2> reach,
    std::unordered_map<std::string, std::array<double, 9>> &sums) {
  if (game.actor() < 0)
    return;
  int actor = game.actor();
  auto node = game.node();
  auto row = profile.lookup(node);
  for (std::size_t i = 0; i < row.count; ++i) {
    sums[node.key][i] += reach[actor] * row.probabilities[i];
    auto child = game;
    child.apply(i);
    auto next = reach;
    next[actor] *= row.probabilities[i];
    exact_average(child, profile, next, sums);
  }
}
// Receives a tiny multistreet game and seat; returns exact uniform continuation
// value by enumerating all player actions with its sampled deal held fixed.
double leduc_value(research::LeducGame game, int seat) {
  if (game.actor() < 0)
    return game.utility(seat);
  auto count = game.node().candidates.count;
  double sum = 0;
  for (std::size_t i = 0; i < count; ++i) {
    auto child = game;
    child.apply(i);
    sum += leduc_value(child, seat) / count;
  }
  return sum;
}
// Receives two committed trainers; returns void after exact row/progress
// equality.
void same(const ExternalSamplingTrainer &a, const ExternalSamplingTrainer &b) {
  check(a.next_traversal == b.next_traversal &&
            a.total_nodes == b.total_nodes &&
            a.table.rows.size() == b.table.rows.size(),
        "resume counters");
  for (const auto &[key, row] : a.table.rows) {
    const auto &other = b.table.rows.at(key);
    check(row.count == other.count && row.tokens == other.tokens &&
              row.regrets == other.regrets && row.average == other.average &&
              row.visits == other.visits,
          "resume numeric state");
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--read-policy") {
      auto policy = policy::load_policy(argv[2]);
      TrainingConfig c;
      HoldemTrainingGame game(c, 0);
      auto node = game.node();
      check(policy.rows.contains(node.key), "fresh-process key missing");
      auto row = policy.lookup(node);
      check(row.count == node.candidates.count, "fresh-process layout");
      ExternalSamplingTrainer reference(c);
      for (int i = 0; i < 160; ++i) {
        HoldemTrainingGame g(c, i);
        reference.step(g);
      }
      auto expected =
          reference.table.export_policy(c.manifest(), true).lookup(node);
      check(row.tokens == expected.tokens &&
                row.probabilities == expected.probabilities,
            "fresh-process canonical keys/actions/probabilities differ");
      std::cout << "Fresh-process policy read passed\n";
      return 0;
    }
    // Normal candidate generation is independent of uniform probability
    // assignment.
    TrainingConfig config;
    HoldemTrainingGame initial(config, 0);
    auto view = initial.engine().current_view();
    auto candidates = generate_ai_candidates(view);
    auto uniform = make_uniform_ai_distribution(view);
    check(candidates.count == uniform.count, "shared candidate count");
    for (std::size_t i = 0; i < candidates.count; ++i) {
      check(candidates.actions[i].probability == 0 &&
                candidates.actions[i].token == uniform.actions[i].token,
            "candidate token/weight");
      check(
          validate_decision(view.legal_actions, candidates.actions[i].decision),
          "candidate legality");
    }
    // Suit renaming and card order leave all 169-class keys unchanged.
    auto observation = initial.engine().current_observation();
    auto changed = observation;
    for (auto &card : changed.view.hole_cards)
      card.suit = static_cast<Suit>((static_cast<int>(card.suit) + 1) % 4);
    std::swap(changed.view.hole_cards[0], changed.view.hole_cards[1]);
    check(policy::push_fold_node(changed).key == initial.node().key,
          "suit/order invariance");
    // Consistent rotation of all public seat arrays preserves relative keys.
    auto rotated = observation;
    rotated.view.player_id = (view.player_id + 3) % 8;
    rotated.view.dealer_id = (view.dealer_id + 3) % 8;
    for (int i = 0; i < 8; ++i) {
      int to = (i + 3) % 8;
      rotated.view.player_stacks[to] = view.player_stacks[i];
      rotated.view.round_contributions[to] = view.round_contributions[i];
      rotated.view.total_contributions[to] = view.total_contributions[i];
      rotated.view.players_in_hand[to] = view.players_in_hand[i];
      rotated.view.players_all_in[to] = view.players_all_in[i];
    }
    check(policy::push_fold_node(rotated).key == initial.node().key,
          "relative seat rotation");
    // Different sampled hidden opponents/decks must not split the same own
    // class.
    std::unordered_map<int, std::string> class_keys;
    for (int i = 0; i < 500; ++i) {
      HoldemTrainingGame g(config, i * 64);
      auto o = g.engine().current_observation();
      int c = policy::preflop_class(o.view.hole_cards);
      auto [it, inserted] = class_keys.emplace(c, g.node().key);
      if (!inserted)
        check(it->second == g.node().key, "hidden cards leaked into key");
    }
    std::array<bool, 169> classes{};
    for (int a = 0; a < 52; ++a)
      for (int b = a + 1; b < 52; ++b) {
        std::array<Card, 2> cards{
            {{static_cast<Face>(a / 4 + 2), static_cast<Suit>(a % 4)},
             {static_cast<Face>(b / 4 + 2), static_cast<Suit>(b % 4)}}};
        classes[policy::preflop_class(cards)] = true;
      }
    for (bool used : classes)
      check(used, "missing preflop class");
    StrategyEntry test;
    test.count = 3;
    test.regrets = {-3, 2, 8};
    auto probs = regret_match(test);
    check(probs[0] == 0 && probs[1] == .2 && probs[2] == .8, "regret matching");
    // Exact first-traversal reference: Q vs J, learner checks or bets, opponent
    // uniform. Uniform continuation values .75 and 1.5 => delta [-.375,+.375].
    // Opponent is sampled, so average independent seeds and test that
    // expectation.
    double r0 = 0, r1 = 0;
    for (int seed = 0; seed < 4000; ++seed) {
      TrainingConfig c;
      c.players = 2;
      c.seed = seed;
      c.schema = "kuhn-test";
      ExternalSamplingTrainer t(c);
      KuhnAdapter game(2);
      auto key = game.node().key;
      t.step(game);
      r0 += t.table.rows.at(key).regrets[0];
      r1 += t.table.rows.at(key).regrets[1];
    }
    check(std::abs(r0 / 4000 + .375) < .04 && std::abs(r1 / 4000 - .375) < .04,
          "external sampling expected regrets");
    // Oracle distinguishes a reach-weighted average from a naive time average.
    KuhnAdapter qj(2);
    auto root = qj.node();
    auto response_game = qj;
    response_game.apply(0);
    response_game.apply(1);
    auto response = response_game.node();
    policy::FrozenPolicy profile1, profile2;
    policy::PolicyRow rootrow, responserow;
    rootrow.count = responserow.count = 2;
    for (int i = 0; i < 2; ++i) {
      rootrow.tokens[i] = root.candidates.actions[i].token;
      responserow.tokens[i] = response.candidates.actions[i].token;
    }
    rootrow.probabilities = {.2, .8};
    responserow.probabilities = {.1, .9};
    profile1.rows.emplace(root.key, rootrow);
    profile1.rows.emplace(response.key, responserow);
    rootrow.probabilities = {.8, .2};
    responserow.probabilities = {.9, .1};
    profile2.rows.emplace(root.key, rootrow);
    profile2.rows.emplace(response.key, responserow);
    std::unordered_map<std::string, std::array<double, 9>> oracle;
    for (int d = 0; d < 6; ++d) {
      exact_average(KuhnAdapter(d), profile1, {1, 1}, oracle);
      exact_average(KuhnAdapter(d), profile2, {1, 1}, oracle);
    }
    auto sums = oracle.at(response.key);
    check(std::abs(sums[1] / (sums[0] + sums[1]) - .26) < 1e-12,
          "own-reach average oracle");
    // Named linear schedules scale the SAME sampled traversal's contributions.
    TrainingConfig wc;
    wc.players = 2;
    wc.schema = "kuhn-test";
    ExternalSamplingTrainer unweighted(wc);
    wc.linear_regret_weights = true;
    wc.linear_average_weights = true;
    ExternalSamplingTrainer weighted(wc);
    unweighted.next_traversal = weighted.next_traversal = 10;
    KuhnAdapter ug(2), wg(2);
    unweighted.step(ug);
    weighted.step(wg);
    for (const auto &[k, row] : unweighted.table.rows) {
      const auto &w = weighted.table.rows.at(k);
      for (std::size_t i = 0; i < row.count; ++i)
        check(w.regrets[i] == 6 * row.regrets[i] &&
                  w.average[i] == 6 * row.average[i],
              "separate linear weighting schedules");
    }
    TrainingConfig kc;
    kc.players = 2;
    kc.schema = "kuhn-test";
    ExternalSamplingTrainer kt(kc);
    for (int i = 0; i < 30000; ++i) {
      KuhnAdapter game(static_cast<int>(split_seed(7, i) % 6));
      kt.step(game);
    }
    auto kp = kt.table.export_policy("kuhn-test", true);
    double value = 0;
    for (int i = 0; i < 6; ++i)
      value += kuhn_value(KuhnAdapter(i), kp) / 6;
    check(std::abs(value + 1.0 / 18) < .04, "Kuhn learned average game value");
    // Three-player multistreet state: future board is hidden until round two.
    research::LeducGame led(1);
    auto before = led.snapshot();
    auto key = led.node().key;
    led.apply(0);
    led.restore(before);
    check(led.node().key == key, "multistreet restore");
    led.apply(0);
    led.apply(0);
    led.apply(0);
    check(led.street() == 1, "public chance street");
    led.apply(0);
    led.apply(0);
    led.apply(0);
    check(led.actor() < 0 && std::abs(led.utility(0) + led.utility(1) +
                                      led.utility(2)) < 1e-12,
          "multiplayer rewards");
    TrainingConfig lc;
    lc.players = 3;
    lc.schema = "leduc-test";
    ExternalSamplingTrainer lt(lc);
    for (int i = 0; i < 500; ++i) {
      research::LeducGame game(split_seed(9, i));
      lt.step(game);
    }
    check(!lt.table.rows.empty(), "multiplayer trainer updates");
    research::LeducGame exact(1);
    auto exact_key = exact.node().key;
    auto action0 = exact, action1 = exact;
    action0.apply(0);
    action1.apply(1);
    double v0 = leduc_value(action0, 0), v1 = leduc_value(action1, 0),
           mean = (v0 + v1) / 2;
    double sampled = 0;
    for (int seed = 0; seed < 2000; ++seed) {
      auto cc = lc;
      cc.seed = seed;
      ExternalSamplingTrainer t(cc);
      research::LeducGame g(1);
      t.step(g);
      sampled += t.table.rows.at(exact_key).regrets[0];
    }
    check(std::abs(sampled / 2000 - (v0 - mean)) < .12,
          "multistreet expected regret");
    // Cyclic simple averaging accumulates the whole opponent distribution,
    // while the learner root gets no averaging weight in that traversal.
    auto cc = kc;
    ExternalSamplingTrainer cyclic(cc);
    KuhnAdapter cg(2);
    auto root_key = cg.node().key;
    auto after_check = cg;
    after_check.apply(0);
    auto opponent_key = after_check.node().key;
    cyclic.step(cg);
    check(cyclic.table.rows.at(root_key).average[0] == 0 &&
              cyclic.table.rows.at(opponent_key).average[0] == .5 &&
              cyclic.table.rows.at(opponent_key).average[1] == .5,
          "cyclic averaging rule");
    // Resource failures are transaction boundaries and MUST keep logical ID
    // zero.
    TrainingConfig tiny = config;
    tiny.max_nodes_per_traversal = 1;
    ExternalSamplingTrainer limited(tiny);
    HoldemTrainingGame fail(tiny, 0);
    rejects([&] { limited.step(fail); }, "node guard");
    check(limited.next_traversal == 0 && limited.table.rows.empty(),
          "failed traversal committed");
    tiny = config;
    tiny.max_entries = 1;
    ExternalSamplingTrainer capacity(tiny);
    HoldemTrainingGame too_many(tiny, 0);
    rejects([&] { capacity.step(too_many); }, "entry guard");
    check(capacity.next_traversal == 0 && capacity.table.rows.empty(),
          "capacity changed table");
    // Eight-player branching, chip conservation, and exact save/resume replay.
    ExternalSamplingTrainer uninterrupted(config), resumed(config);
    for (int i = 0; i < 160; ++i) {
      HoldemTrainingGame game(config, i);
      uninterrupted.step(game);
    }
    for (int i = 0; i < 80; ++i) {
      HoldemTrainingGame game(config, i);
      resumed.step(game);
    }
    auto dir = std::filesystem::temp_directory_path() / "poker-mccfr-tests";
    std::filesystem::create_directories(dir);
    save_checkpoint(resumed, dir / "state.mccfr");
    resumed = load_checkpoint(dir / "state.mccfr");
    for (int i = 80; i < 160; ++i) {
      HoldemTrainingGame game(config, i);
      resumed.step(game);
    }
    same(uninterrupted, resumed);
    auto variant = resumed;
    variant.config.linear_regret_weights = true;
    variant.config.linear_average_weights = true;
    save_checkpoint(variant, dir / "weighted.mccfr");
    auto loaded_variant = load_checkpoint(dir / "weighted.mccfr");
    check(loaded_variant.config.manifest() == variant.config.manifest(),
          "weighting metadata persisted");
    auto frozen = resumed.table.export_policy(config.manifest(), true);
    policy::save_policy(frozen, dir / "average.policy");
    auto loaded = policy::load_policy(dir / "average.policy");
    for (const auto &[k, row] : frozen.rows)
      check(row.probabilities == loaded.rows.at(k).probabilities,
            "policy roundtrip");
    // Artifact for CTest's separate process verification (keys rebuilt from
    // deal).
    policy::save_policy(frozen, "mccfr-fresh-process.policy");
    {
      std::ofstream out(dir / "broken", std::ios::binary);
      out << "truncated";
    }
    rejects([&] { load_checkpoint(dir / "broken"); }, "corrupt artifact");
    {
      std::ifstream in(dir / "average.policy", std::ios::binary);
      std::string bytes((std::istreambuf_iterator<char>(in)), {});
      bytes.back() ^= 1;
      std::ofstream out(dir / "bad-checksum.policy", std::ios::binary);
      out.write(bytes.data(), bytes.size());
    }
    rejects([&] { policy::load_policy(dir / "bad-checksum.policy"); },
            "bad checksum accepted");
    auto zero = empty_entry(initial.node());
    StrategyTable zeros;
    zeros.rows.emplace(initial.node().key, zero);
    std::size_t fallback = 0;
    auto zp = zeros.export_policy(config.manifest(), true, &fallback);
    check(fallback == 1 && zp.lookup(initial.node()).probabilities[0] == .5,
          "zero-average fallback");
    // No-limp all-fold path ends with BB award, never an unraised checkdown.
    HoldemTrainingGame folds(config, 5);
    while (folds.actor() >= 0)
      folds.apply(0);
    double sum = 0;
    for (int i = 0; i < 8; ++i)
      sum += folds.utility(i);
    check(sum == 0, "eight-player chip conservation");
    // Real unequal-stack all-in calls settle side pots through the same rules.
    PokerGame sidepots(8);
    std::array<int, 8> stacks{100, 200, 300, 400, 500, 600, 700, 800};
    sidepots.reset_episode(stacks, 4, 72);
    sidepots.enable_history(true);
    sidepots.begin_hand();
    while (sidepots.advance_to_decision()) {
      auto n = policy::push_fold_node(sidepots.current_observation());
      sidepots.apply_action(
          n.candidates.actions[n.candidates.count - 1].decision);
    }
    int net = 0;
    for (int reward : sidepots.last_rewards())
      net += reward;
    check(net == 0, "restricted side-pot conservation");
    policy::TrainedAIDecisionProvider provider(frozen);
    rejects([&] { provider.choose_action(view); }, "missing history accepted");
    auto decision =
        provider.choose_observation(initial.engine().current_observation());
    check(validate_decision(view.legal_actions, decision),
          "runtime inference legality");
    // Runtime providers automatically request history through PokerGame, and
    // the engine refuses to disable a public observation dependency afterward.
    PokerGame runtime(8);
    runtime.set_decision_provider(0, provider);
    rejects([&] { runtime.enable_history(false); },
            "required history disabled");
    for (int i = 1; i < 8; ++i)
      runtime.set_decision_provider(i, provider);
    runtime.play_one_hand();
    check(!runtime.hand_in_progress(), "runtime learned hand");
    // Missing selections deliberately retain baseline behavior: gain zero and
    // coverage reported, rather than calling rare sampled choices a best
    // response.
    auto eval = mccfr::evaluate_response(frozen, config, 0, 20, 50, 91, 1000);
    check(eval.selected_rows == 0 && eval.gain_bb_per_hand == 0,
          "rare response fallback");
    auto eval2 = mccfr::evaluate_response(frozen, config, 0, 20, 50, 91, 1000);
    check(eval.uncovered_target_decisions == eval2.uncovered_target_decisions &&
              eval.policy_fallbacks == eval2.policy_fallbacks,
          "evaluation reproducibility");
    rejects([&] { mccfr::evaluate_response(frozen, config, 8, 20, 50, 1); },
            "invalid evaluation seat");
    // A failed new save must leave the previous published file readable.
    policy::save_policy(frozen, dir / "protected.policy");
    std::filesystem::create_directory(dir / "protected.policy.tmp");
    rejects([&] { policy::save_policy(frozen, dir / "protected.policy"); },
            "publication failure");
    check(policy::load_policy(dir / "protected.policy").rows.size() ==
              frozen.rows.size(),
          "previous artifact lost");
    std::filesystem::remove(dir / "protected.policy.tmp");
    auto incompatible = resumed;
    incompatible.config.schema = "incompatible-v9";
    save_checkpoint(incompatible, dir / "incompatible.mccfr");
    rejects([&] { load_checkpoint(dir / "incompatible.mccfr"); },
            "incompatible schema accepted");
    std::cout << "MCCFR tests passed; Kuhn value " << value
              << "; restricted entries " << resumed.table.rows.size() << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
