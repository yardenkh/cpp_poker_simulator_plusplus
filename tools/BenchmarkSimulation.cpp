// Measures complete-game throughput under several policies, reporting actual
// hands and decisions.

#include "decision/AIDecisionProvider.hpp"
#include "simulation/Simulation.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

namespace {
using namespace poker;
// Deterministic workload policies isolate engine costs from model inference.
class Policy : public DecisionProvider {
public:
  explicit Policy(int mode) : mode_(mode) {}
  PlayerDecision choose_action(const PlayerView &v) override {
    const auto &a = v.legal_actions;
    if (mode_ == 1)
      return {ActionType::Fold, 0};
    if (mode_ == 2 && a.can_all_in)
      return {ActionType::AllIn, 0};
    if (mode_ == 3 && a.can_raise)
      return {ActionType::RaiseTo, a.min_raise_to};
    return {a.can_check ? ActionType::Check : ActionType::Call, 0};
  }

private:
  int mode_;
};
void measure(const char *name, const SimulationOptions &options,
             const ProviderFactory &factory) {
  std::array<double, 3> rates{};
  SimulationResult result{};
  for (std::size_t trial = 0; trial < rates.size(); ++trial) {
    const auto start = std::chrono::steady_clock::now();
    result = run_simulations(options, factory);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    rates[trial] = result.hands / seconds;
  }
  std::sort(rates.begin(), rates.end());
  const double decisions_per_hand =
      result.hands ? double(result.decisions) / result.hands : 0;
  std::cout << name << " seats=" << options.players
            << " workers=" << options.workers
            << " actual_hands=" << result.hands
            << " median_hands/sec=" << rates[1] << " range=" << rates.front()
            << ".." << rates.back()
            << " decisions/sec=" << rates[1] * decisions_per_hand
            << " checksum=" << result.checksum << '\n';
}
} // namespace
int main(int argc, char **argv) {
  try {
    const auto episodes = argc > 1 ? std::stoull(argv[1]) : 200;
    const unsigned workers =
        argc > 2 ? static_cast<unsigned>(std::stoul(argv[2])) : 1;
    // Load shared tables before timing; retain a result checksum against
    // dead-code removal.
    (void)poker::default_fast_hand_evaluator();
#ifdef NDEBUG
    std::cout << "Release benchmark, observer=null, history=off, three trials "
                 "per workload\n";
#else
    std::cout
        << "Debug benchmark: rebuild in Release for throughput measurements\n";
#endif
    for (int seats : {2, 6}) {
      SimulationOptions options;
      options.episodes = episodes;
      options.workers = workers;
      options.players = seats;
      options.config = {1000000, 50, 100};
      options.hands_per_episode = 100;
      measure("passive/showdown", options,
              [] { return std::make_unique<Policy>(0); });
      measure("simple", options,
              [] { return std::make_unique<AIDecisionProvider>(); });
      measure("fold-heavy", options,
              [] { return std::make_unique<Policy>(1); });
      // Smaller stacks exercise repeated raises, side pots and short matches.
      options.config = {500, 10, 20};
      options.episodes *= 10;
      options.hands_per_episode = 10;
      measure("raises", options, [] { return std::make_unique<Policy>(3); });
      measure("all-in", options, [] { return std::make_unique<Policy>(2); });
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
