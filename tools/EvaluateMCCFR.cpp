// Frozen-policy response probes; never changes the training table or champion.
#include "research/mccfr/Evaluation.hpp"
#include <charconv>
#include <cmath>
#include <iostream>
using namespace poker;
int main(int argc, char **argv) {
  try {
    if (argc < 2 || argc > 5 || std::string(argv[1]) == "--help") {
      std::cout << "poker_evaluate_mccfr POLICY [selection_hands=2000] "
                   "[scoring_hands=5000] [seed=123]\n"
                   "Eight-player push/fold estimated unilateral improvements; "
                   "not exploitability.\n";
      return argc < 2 ? 1 : 0;
    }
    // Receives optional position/default; returns a fully parsed unsigned
    // integer.
    const auto number = [&](int pos, std::uint64_t fallback) {
      if (argc <= pos)
        return fallback;
      std::string text = argv[pos];
      std::uint64_t n;
      auto r = std::from_chars(text.data(), text.data() + text.size(), n);
      if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid evaluation count");
      return n;
    };
    auto policy = policy::load_policy(argv[1]);
    mccfr::TrainingConfig config;
    auto selection = number(2, 2000), scoring = number(3, 5000),
         seed = number(4, 123);
    double sum = 0, variance = 0;
    std::cout << "seat,gain_BB_per_hand,ci95_low,ci95_high,selected_rows,rare_"
                 "rows,uncovered_target_decisions,policy_fallbacks\n";
    for (int seat = 0; seat < 8; ++seat) {
      auto r = mccfr::evaluate_response(policy, config, seat, selection,
                                        scoring, seed);
      sum += r.gain_bb_per_hand;
      variance += r.standard_error * r.standard_error;
      std::cout << seat << ',' << r.gain_bb_per_hand << ','
                << r.gain_bb_per_hand - 1.96 * r.standard_error << ','
                << r.gain_bb_per_hand + 1.96 * r.standard_error << ','
                << r.selected_rows << ',' << r.rare_rows << ','
                << r.uncovered_target_decisions << ',' << r.policy_fallbacks
                << '\n';
    }
    std::cout << "Sum of estimated gains: " << sum
              << " BB/hand; approximate 95% interval ["
              << sum - 1.96 * std::sqrt(variance) << ", "
              << sum + 1.96 * std::sqrt(variance) << "]\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
