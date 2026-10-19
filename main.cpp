// CLI setup chooses the tournament wrapper or an ordinary match, never poker
// rules.
#include "decision/HumanDecisionProvider.hpp"
#include "decision/AIDecisionProvider.hpp"
#include "game/PokerGame.hpp"
#include "tournament/Tournament.hpp"
#include "ui/TerminalUI.hpp"
#include <exception>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
int main(int argc, char **argv) {
  try {
    // Tournament play is the default. --game retains ordinary fixed-blind play.
    const bool regular_game = argc == 2 && std::string(argv[1]) == "--game";
    if (argc > 1 && !regular_game) {
      std::cout
          << "Usage: poker_cli [--game]\nDefault: single-table tournament.\n";
      return argc == 2 && std::string(argv[1]) == "--help" ? 0 : 1;
    }
    int players = 0;
    for (;;) {
      std::cout << "Number of players (2.." << poker::MAX_PLAYERS
                << "): " << std::flush;
      std::string line, extra;
      if (!std::getline(std::cin, line))
        return 0;
      std::istringstream input(line);
      if ((input >> players) && !(input >> extra) && players >= 2 &&
          players <= poker::MAX_PLAYERS)
        break;
      std::cout << "Please enter a whole number from 2 to "
                << poker::MAX_PLAYERS << ".\n";
    }
    // Borrowed providers/listeners are declared first so they outlive the game.
    poker::TerminalUI ui;
    poker::HumanDecisionProvider human(ui);
    std::array<poker::AIDecisionProvider, poker::MAX_PLAYERS> ais;
    const auto seed =
        std::random_device{}(); // Obtain entropy once for interactive play.
    for (int id = 1; id < players; ++id)
      ais[id].reset_for_episode((static_cast<std::uint64_t>(seed) << 32) + id);
    std::cout << "You are player 0.\n";
    if (regular_game) {
      poker::PokerGame game(players, poker::GameConfig{}, seed);
      game.set_decision_provider(0, human);
      for (int id = 1; id < players; ++id)
        game.set_decision_provider(id, ais[id]);
      game.set_observer(&ui);
      game.run_match();
      std::cout << "Match winner: player " << game.winner_id() << '\n';
    } else {
      poker::TournamentConfig config;
      // A simple demonstration prize structure in abstract units, separate from
      // betting chips. Heads-up pays 70/30%; larger tables pay 60/30/10% to
      // top 3.
      const std::int64_t pool = players * 1000;
      config.prizes =
          players == 2
              ? std::vector<std::int64_t>{pool * 70 / 100, pool * 30 / 100}
              : std::vector<std::int64_t>{pool * 60 / 100, pool * 30 / 100,
                                          pool * 10 / 100};
      poker::Tournament tournament(players, config, seed);
      tournament.set_decision_provider(0, human);
      for (int id = 1; id < players; ++id)
        tournament.set_decision_provider(id, ais[id]);
      tournament.set_observer(&ui); // Cards, bets and chip payouts.
      tournament.set_tournament_observer(
          &ui); // Levels, finishes and prize payouts.
      tournament.run();
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
