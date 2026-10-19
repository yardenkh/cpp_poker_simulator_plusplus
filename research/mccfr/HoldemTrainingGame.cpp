#include "research/mccfr/HoldemTrainingGame.hpp"
#include <stdexcept>
namespace poker::mccfr {
HoldemTrainingGame::HoldemTrainingGame(const TrainingConfig &config,
                                       std::uint64_t traversal)
    : engine_(8, {config.starting_chips, config.small_blind, config.big_blind}),
      big_blind_(config.big_blind) {
  config.validate();
  if (config.players != 8 || config.schema != TrainingConfig{}.schema)
    throw std::invalid_argument("Hold'em adapter supports only pushfold8-v1");
  std::array<int, 8> stacks;
  stacks.fill(config.starting_chips);
  auto seed = split_seed(config.seed ^ 0x4445414cull, traversal);
  engine_.enable_history(true, 8);
  engine_.reset_episode(stacks, static_cast<int>((traversal / 8) % 8),
                        static_cast<std::uint32_t>(seed));
  engine_.begin_hand();
  engine_.advance_to_decision();
}
void HoldemTrainingGame::apply(std::size_t index) {
  auto choices = node().candidates;
  if (index >= choices.count)
    throw std::out_of_range("Restricted action index");
  engine_.apply_action(choices.actions[index].decision);
  engine_.advance_to_decision();
  // All nonterminal decisions must remain preflop under no-limp rules.
  if (engine_.acting_player() >= 0 &&
      engine_.current_view().stage != PokerStage::PreFlop)
    throw std::logic_error("Unexpected postflop decision in push/fold");
}
double HoldemTrainingGame::utility(int seat) const {
  if (actor() >= 0 || engine_.hand_in_progress() || seat < 0 || seat >= 8)
    throw std::logic_error("Utility requires terminal game and valid seat");
  return static_cast<double>(engine_.last_rewards()[seat]) / big_blind_;
}
} // namespace poker::mccfr
