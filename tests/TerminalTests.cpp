// Script terminal input and verify it uses engine rules without leaking cards.
#include "decision/HumanDecisionProvider.hpp"
#include "decision/AIDecisionProvider.hpp"
#include "game/PokerGame.hpp"
#include "tournament/Tournament.hpp"
#include "ui/TerminalUI.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace poker;
namespace {
// Throwing checks remain active in optimized Release builds.
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
PlayerView example_view() {
  PlayerView view;
  view.player_id = 0;
  view.num_players = 2;
  view.player_stacks[0] = 1000;
  view.players_in_hand[0] = view.players_in_hand[1] = true;
  view.legal_actions = {true, true, true, true, true, 100, 200, 1000};
  return view;
}
void commands() {
  const auto view = example_view();
  // Each supported command reaches the provider with the requested action.
  for (const auto &entry : {std::pair{"fold", ActionType::Fold},
                            std::pair{"check", ActionType::Check},
                            std::pair{"call", ActionType::Call},
                            std::pair{"raise 500", ActionType::RaiseTo},
                            std::pair{"allin", ActionType::AllIn}}) {
    std::istringstream input(std::string(entry.first) + "\n");
    std::ostringstream output;
    TerminalUI ui(input, output);
    HumanDecisionProvider human(ui);
    const auto action = human.choose_action(view);
    check(action.action == entry.second, "command mapped to wrong action");
    check(action.raise_to == (entry.second == ActionType::RaiseTo ? 500 : 0),
          "raise command did not preserve total");
  }
  // Malformed syntax and out-of-range targets must retry, not reach execution.
  std::istringstream input(
      "\nunknown\nraise\nraise 99999999999999999999\n"
      "raise 500 extra\ncall extra\nraise 199\nraise 1001\nraise 200\n");
  std::ostringstream output;
  TerminalUI ui(input, output);
  check(ui.request_decision(view).raise_to == 200,
        "invalid input was accepted");
  auto restricted = view;
  restricted.legal_actions.can_check = false;
  restricted.legal_actions.can_raise = false;
  restricted.legal_actions.can_all_in = false;
  std::istringstream retry("check\nraise 500\nallin\ncall\n");
  TerminalUI restricted_ui(retry, output);
  check(restricted_ui.request_decision(restricted).action == ActionType::Call,
        "unavailable action accepted");
  // A closed stream terminates with a clear error instead of looping.
  std::istringstream closed;
  TerminalUI closed_ui(closed, output);
  bool threw = false;
  try {
    closed_ui.request_decision(view);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  check(threw, "closed input did not stop");
}
void events_and_play() {
  // Distinct private cards make accidental disclosure easy to detect.
  Table table;
  table_init(table, 2, 500);
  table.players[0].receive_card({Face::Ace, Suit::Spades});
  table.players[1].receive_card({Face::King, Suit::Hearts});
  PotManager pots;
  std::istringstream input("fold\n");
  std::ostringstream output;
  TerminalUI ui(input, output);
  ui.on_hand_started(1, table, pots);
  ui.on_stage_started(PokerStage::PreFlop, table, pots);
  check(output.str().find(card_to_string(table.players[0].hand[0])) ==
                std::string::npos &&
            output.str().find(card_to_string(table.players[1].hand[0])) ==
                std::string::npos,
        "public callback disclosed private cards");
  auto view = example_view();
  view.hole_cards[0] = table.players[0].hand[0];
  view.hole_card_count = 1;
  ui.print_player_view(view);
  check(output.str().find(card_to_string(table.players[0].hand[0])) !=
                std::string::npos &&
            output.str().find(card_to_string(table.players[1].hand[0])) ==
                std::string::npos,
        "player view revealed wrong cards");
  table.players[1].in_hand = false;
  ui.on_showdown(table, pots, {});
  check(output.str().find(card_to_string(table.players[1].hand[0])) ==
            std::string::npos,
        "folded cards disclosed");

  // Exercise real provider/observer wiring through a complete uncontested hand.
  HumanDecisionProvider human(ui);
  AIDecisionProvider ai;
  PokerGame game(2, {500, 10, 20}, 71);
  game.set_decision_provider(0, human);
  game.set_decision_provider(1, ai);
  game.set_observer(&ui);
  game.play_one_hand();
  check(!game.hand_in_progress(), "human hand did not complete");
  check(game.table().players[0].cash + game.table().players[1].cash == 1000,
        "interactive hand lost chips");
  check(output.str().find("Hand finished") != std::string::npos,
        "payout callback did not display results");

  // Force a real showdown so board and hand-description callbacks also run.
  std::ostringstream showdown_output;
  TerminalUI showdown_ui(input, showdown_output);
  PokerGame showdown(2, {500, 10, 20}, 72);
  struct PassiveProvider : DecisionProvider {
    PlayerDecision choose_action(const PlayerView &v) override {
      return {v.legal_actions.can_check ? ActionType::Check : ActionType::Call, 0};
    }
  } passive;
  showdown.set_decision_provider(0, passive);
  showdown.set_decision_provider(1, passive);
  showdown.set_observer(&showdown_ui);
  showdown.play_one_hand();
  check(showdown_output.str().find("Main pot:") != std::string::npos,
        "showdown callback missing");
}

void dashboard_play() {
  // Force interactive behavior on streams: one action, then one result-screen
  // acknowledgement. The trailing line proves the pause consumes only Enter.
  std::istringstream input("f\n\nnext\n");
  std::ostringstream output;
  TerminalUI ui(input, output, true);
  HumanDecisionProvider human(ui);
  AIDecisionProvider ai;
  PokerGame game(2, {500, 10, 20}, 71);
  game.set_decision_provider(0, human);
  game.set_decision_provider(1, ai);
  game.set_observer(&ui);
  game.play_one_hand();
  const auto screen = output.str();
  check(screen.find("\033[2J\033[H") != std::string::npos,
        "interactive display did not refresh");
  check(screen.find("+-----+") != std::string::npos &&
            screen.find("[PreFlop]") != std::string::npos &&
            screen.find("YOUR ACTION >") != std::string::npos,
        "dashboard is missing cards, stage or action prompt");
  check(screen.find("FOLD") != std::string::npos,
        "human fold is not shown at the seat on the result screen");
  check(screen.find("YOUR LAST MOVE") == std::string::npos &&
            screen.find("RECENT ACTIONS") == std::string::npos &&
            screen.find("SEATS:") == std::string::npos,
        "removed history or legend still appears");
  check(screen.find("HAND RESULT") != std::string::npos &&
            screen.find("Press ENTER") != std::string::npos,
        "hand results were not paused for review");
  std::string remaining;
  std::getline(input, remaining);
  check(remaining == "next", "result pause consumed too much input");
  // Dashboard updates must never disclose the opponent's dealt cards before
  // showdown, including on an uncontested hand's result screen.
  for (int c = 0; c < game.table().players[1].cards_in_hand; ++c)
    check(screen.find(card_to_string(game.table().players[1].hand[c])) ==
              std::string::npos,
          "dashboard revealed opponent private cards");

  // Invalid input stays next to a freshly redrawn action area.
  std::istringstream retry("raise 199\nr 200\n");
  std::ostringstream feedback;
  TerminalUI retry_ui(retry, feedback, true);
  check(retry_ui.request_decision(example_view()).raise_to == 200,
        "raise shortcut or retry failed");
  check(feedback.str().find(" ! That action is not available") !=
            std::string::npos,
        "invalid-action feedback missing from dashboard");
}

void circular_seats() {
  // Exercise all supported table sizes and rotate the human to another seat.
  // Each seat appears once, and the human stays below the shared board.
  for (int count = 2; count <= MAX_PLAYERS; ++count) {
    auto view = example_view();
    view.num_players = count;
    view.player_id = count - 1;
    for (int id = 0; id < count; ++id) {
      view.player_stacks[id] = 1800;
      view.players_in_hand[id] = true;
    }
    std::istringstream input;
    std::ostringstream output;
    TerminalUI ui(input, output, true);
    Table table;
    table_init(table, count, 1800);
    PotManager pots;
    ui.on_hand_started(1, table, pots);
    ui.on_player_action(0, {ActionType::Fold, 0}, table, pots, {});
    view.players_in_hand[0] = false;
    // A fold from a previous street remains visible rather than becoming OUT.
    ui.on_stage_started(PokerStage::Turn, table, pots);
    ui.print_player_view(view);
    const auto screen = output.str();
    for (int id = 0; id < count; ++id) {
      const auto name = "P" + std::to_string(id);
      const auto position = screen.find(name);
      check(position != std::string::npos &&
                screen.find(name, position + name.size()) == std::string::npos,
            "circular table omitted or duplicated a seat");
    }
    check(screen.find("> YOU P") > screen.find("[ ..]"),
          "human is not seated below the board");
    check(screen.find("1800 / 0 FOLD") != std::string::npos,
          "fold action did not persist across streets");
    check(screen.find(" IN") == std::string::npos &&
              screen.find(" OUT") == std::string::npos,
          "generic IN/OUT status is still rendered");
  }
}
void tournament_display() {
  // A complete tournament uses the same hand renderer but adds a single
  // post-ranking pause. No human input is needed with these passive providers.
  std::istringstream input(std::string(32, '\n'));
  std::ostringstream output;
  TerminalUI ui(input, output, true);
  AIDecisionProvider ai;
  TournamentConfig config;
  config.starting_chips = 100;
  config.blind_levels = {{5, 10, 1}, {1000, 2000, 1}};
  config.prizes = {700, 300};
  Tournament tournament(2, config, 71);
  tournament.set_decision_provider(0, ai);
  tournament.set_decision_provider(1, ai);
  tournament.set_observer(&ui);
  tournament.set_tournament_observer(&ui);
  tournament.play_one_hand();
  const auto first_screen = output.str();
  check(first_screen.find("TOURNAMENT STARTED") != std::string::npos &&
            first_screen.find("Level 1 | Blinds 5/10") != std::string::npos &&
            first_screen.find("Prizes (units): 1: 700 | 2: 300") !=
                std::string::npos,
        "Tournament configuration is missing from the UI");
  const auto pause = first_screen.find("Press ENTER");
  check(pause != std::string::npos &&
            first_screen.find("Press ENTER", pause + 1) == std::string::npos,
        "Tournament UI paused twice for one hand");
  tournament.run(20);
  check(tournament.finished(), "UI test tournament did not finish");
  const auto final_screen = output.str();
  check(final_screen.find("ELIMINATED") != std::string::npos &&
            final_screen.find("FINAL TOURNAMENT RESULTS") !=
                std::string::npos &&
            final_screen.find("Prize 700 units") != std::string::npos &&
            final_screen.find("Prize 300 units") != std::string::npos,
        "Tournament finish or prize displays are missing");
}
} // namespace
int main() {
  try {
    commands();
    events_and_play();
    dashboard_play();
    circular_seats();
    tournament_display();
    std::cout << "Terminal tests passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
