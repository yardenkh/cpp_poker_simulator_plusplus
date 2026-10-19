// Runnable headless tournament experiment; no terminal observer or human input.
#include "simulation/TournamentSimulation.hpp"
#include "decision/AIDecisionProvider.hpp"
#include <charconv>
#include <chrono>
#include <iostream>
#include <string_view>
int main(int argc, char **argv) {
  try {
    if (argc > 3)
      throw std::invalid_argument(
          "Usage: poker_tournament_simulation [episodes] [workers]");
    poker::TournamentSimulationOptions options;
    // Parse whole nonnegative integers; reject extra text and numeric overflow.
    if (argc > 1) {
      std::string_view text(argv[1]);
      const auto parsed = std::from_chars(
          text.data(), text.data() + text.size(), options.episodes);
      if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid tournament count");
    }
    if (argc > 2) {
      std::string_view text(argv[2]);
      const auto parsed = std::from_chars(
          text.data(), text.data() + text.size(), options.workers);
      if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid worker count");
    }
    options.config.prizes = {3000, 1800,
                             1200}; // Abstract prize units; six seats.
    (void)
        poker::default_fast_hand_evaluator(); // Load lookup data before timing.
    const auto start = std::chrono::steady_clock::now();
    const auto result = poker::run_tournament_simulations(options, [](int) {
      return std::make_unique<poker::AIDecisionProvider>();
    });
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    std::cout << "Completed tournaments: " << result.tournaments
              << "\nHands: " << result.hands
              << "\nDecisions: " << result.decisions << "\nSeconds: " << seconds
              << "\nTournaments/second: " << result.tournaments / seconds
              << "\nChecksum: " << result.checksum << '\n';
    for (int id = 0; id < options.players; ++id)
      std::cout << "Player " << id << " | Wins: " << result.wins[id]
                << " | Total prize: " << result.total_prizes[id] << " units\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
