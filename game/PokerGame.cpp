// Runs preparation, streets, betting, and payout through one resumable hand
// state machine.

#include "game/PokerGame.hpp"
#include "betting/BettingInternal.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace poker {
PokerGame::PokerGame(int num_players, const GameConfig &config,
                     std::uint32_t seed, const FastHandEvaluator &evaluator)
    : config_(config), evaluator_(&evaluator), random_generator_(seed) {
  // Reserve space for current_bet + minimum_raise_size in int arithmetic.
  if (num_players < 2 || num_players > MAX_PLAYERS ||
      config.starting_chips <= 0 || config.small_blind <= 0 ||
      config.big_blind < config.small_blind ||
      config.starting_chips >
          std::numeric_limits<int>::max() / 2 / num_players ||
      config.big_blind > std::numeric_limits<int>::max() / 2) {
    throw std::invalid_argument("Invalid poker player count or configuration");
  }
  table_init(table_, num_players, config.starting_chips);
}

// Setup does not transfer ownership of the supplied providers or observer.
void PokerGame::set_random_seed(std::uint32_t seed) {
  random_generator_.seed(seed);
}
void PokerGame::set_blinds(int small_blind, int big_blind) {
  if (hand_active_)
    throw std::logic_error("Change blinds only between hands");
  // Match the engine constructor's blind rules and integer-arithmetic bounds.
  // Validate both values before updating either, so rejected changes are
  // atomic.
  if (small_blind <= 0 || big_blind < small_blind ||
      big_blind > std::numeric_limits<int>::max() / 2)
    throw std::invalid_argument("Invalid blind amounts");
  config_.small_blind = small_blind;
  config_.big_blind = big_blind;
}
void PokerGame::set_decision_provider(int id, DecisionProvider &provider) {
  if (id < 0 || id >= table_.num_players)
    throw std::out_of_range("Invalid provider seat");
  if (provider.requires_action_history() && !history_enabled_)
    enable_history(true);
  decision_providers_[id] = &provider;
}
void PokerGame::set_observer(GameObserver *observer) { observer_ = observer; }
bool PokerGame::match_finished() const {
  // All-in cash is in the pot, so zero cash during a hand is not match
  // completion.
  return !hand_active_ && count_players_with_chips(table_) <= 1;
}
int PokerGame::winner_id() const {
  if (!match_finished() || count_players_with_chips(table_) != 1)
    return -1;
  for (int id = 0; id < table_.num_players; ++id)
    if (table_.players[id].cash > 0)
      return id;
  return -1;
}

void PokerGame::reset(std::uint32_t seed) {
  std::array<int, MAX_PLAYERS> stacks{};
  std::fill_n(stacks.begin(), table_.num_players, config_.starting_chips);
  reset_episode(std::span(stacks.data(), table_.num_players), 0, seed);
}
void PokerGame::reset_episode(std::span<const int> stacks, int dealer_id,
                              std::uint32_t seed) {
  // Validate completely before discarding any current hand. Reset explicitly
  // abandons that hand, as needed by independent training environments.
  if (stacks.size() != static_cast<std::size_t>(table_.num_players) ||
      dealer_id < 0 || dealer_id >= table_.num_players) {
    throw std::invalid_argument(
        "Episode seats or dealer do not match the table");
  }
  std::int64_t total = 0;
  int funded = 0;
  for (int stack : stacks) {
    if (stack < 0)
      throw std::invalid_argument("Negative episode stack");
    total += stack;
    funded += stack > 0;
  }
  if (funded < 2 || stacks[dealer_id] == 0 ||
      total > std::numeric_limits<int>::max() / 2) {
    throw std::invalid_argument(
        "Episode needs two funded seats and a funded dealer");
  }
  // Preserve vector capacity and borrowed dependencies while resetting state.
  for (int id = 0; id < table_.num_players; ++id) {
    table_.players[id].cash = stacks[id];
    table_.players[id].hand.fill(Card{});
    table_.players[id].reset_for_new_hand();
  }
  table_.dealer_id = dealer_id;
  table_.community_cards.fill(Card{});
  table_.community_card_count = 0;
  table_.deck.reset();
  assign_blinds(table_);
  pot_manager_.reset_for_new_hand();
  betting_state_reset(betting_state_, 0, config_.big_blind);
  current_stage_ = PokerStage::PreFlop;
  random_generator_.seed(seed);
  hand_number_ = 0;
  hand_active_ = false;
  acting_player_ = after_player_ = -1;
  current_legal_ = {};
  payouts_.fill(0);
  rewards_.fill(0);
  starting_stacks_.fill(0);
  history_.clear();
  decisions_applied_ = 0;
}
void PokerGame::enable_history(bool enabled, std::size_t reserve_actions) {
  if (hand_active_)
    throw std::logic_error("Configure history between hands");
  if (!enabled)
    for (auto *provider : decision_providers_)
      if (provider && provider->requires_action_history())
        throw std::logic_error("A registered provider requires public history");
  if (enabled)
    history_.reserve(reserve_actions);
  history_enabled_ = enabled;
  history_.clear();
}

void PokerGame::start_new_hand() {
  if (hand_number_ == std::numeric_limits<int>::max())
    throw std::overflow_error(
        "Hand counter capacity exceeded; reset the episode");
  ++hand_number_;
  hand_active_ = true;
  history_.clear();
  payouts_.fill(0);
  rewards_.fill(0);
  pot_manager_.reset_for_new_hand();
  // Resetting before shuffle makes episode replay independent of prior decks.
  table_.deck.reset();
  table_.deck.shuffle(random_generator_);
  table_.community_cards.fill(Card{});
  table_.community_card_count = 0;
  for (int id = 0; id < table_.num_players; ++id) {
    starting_stacks_[id] = table_.players[id].cash;
    table_.players[id].reset_for_new_hand();
  }
  assign_blinds(table_);
  deal_hole_cards(table_);
  current_stage_ = PokerStage::PreFlop;
  if (observer_)
    observer_->on_hand_started(hand_number_, table_, pot_manager_);
}
void PokerGame::post_blinds() {
  // Preserve this engine's documented rule: the actual larger payment sets
  // the opening target, and the configured big blind sets the minimum raise.
  const int small = pot_manager_.contribute(table_, table_.small_blind_id,
                                            config_.small_blind);
  const int big =
      pot_manager_.contribute(table_, table_.big_blind_id, config_.big_blind);
  betting_state_reset(betting_state_, std::max(small, big), config_.big_blind);
}
void PokerGame::enter_stage(PokerStage stage) {
  current_stage_ = stage;
  acting_player_ = -1;
  after_player_ =
      stage == PokerStage::PreFlop ? table_.big_blind_id : table_.dealer_id;
  if (observer_)
    observer_->on_stage_started(stage, table_, pot_manager_);
}

bool PokerGame::begin_hand() {
  if (hand_active_)
    throw std::logic_error("A hand is already in progress");
  if (match_finished())
    return false;
  start_new_hand();
  post_blinds();
  enter_stage(PokerStage::PreFlop);
  return true;
}
bool PokerGame::advance_to_decision() {
  // Stay at a pending decision until apply_action consumes it. With no actor,
  // repeatedly complete automatic streets until a decision or payout is
  // reached.
  if (!hand_active_)
    return false;
  if (acting_player_ >= 0)
    return true;
  while (hand_active_) {
    // This search replaces separate round-complete and next-player scans.
    acting_player_ = poker::next_player_needing_action(
        table_, pot_manager_, betting_state_, after_player_);
    if (acting_player_ >= 0) {
      current_legal_ = get_legal_actions(table_, pot_manager_, betting_state_,
                                         acting_player_);
      return true;
    }
    // With no decisions left, finish the street and run out any all-in board.
    if (current_stage_ == PokerStage::River) {
      run_showdown();
      return false;
    }
    PokerStage next;
    if (current_stage_ == PokerStage::PreFlop) {
      deal_flop(table_);
      next = PokerStage::Flop;
    } else if (current_stage_ == PokerStage::Flop) {
      deal_turn(table_);
      next = PokerStage::Turn;
    } else {
      deal_river(table_);
      next = PokerStage::River;
    }
    pot_manager_.reset_round_contributions();
    betting_state_reset(betting_state_, 0, config_.big_blind);
    enter_stage(next);
  }
  return false;
}

PlayerView PokerGame::current_view() const {
  if (acting_player_ < 0 || !hand_active_)
    throw std::logic_error("No pending decision");
  return make_player_view(table_, pot_manager_, betting_state_, current_stage_,
                          acting_player_, current_legal_);
}
PlayerObservation PokerGame::current_observation() const {
  return {current_view(), action_history()};
}
void PokerGame::apply_action(const PlayerDecision &decision) {
  // The cached legal actions were computed for this exact pending state.
  // No public mutable table/pot reference can invalidate them between calls.
  if (!hand_active_ || acting_player_ < 0)
    throw std::logic_error("No pending decision");
  if (!validate_decision(current_legal_, decision))
    throw std::invalid_argument("Cannot execute an illegal poker decision");
  const int id = acting_player_;
  const int before = table_.players[id].cash;
  // Grow history BEFORE mutation so an allocation failure leaves the decision
  // pending.
  if (history_enabled_)
    history_.push_back({current_stage_, id, decision, 0, 0});
  detail::apply_validated_decision(table_, pot_manager_, betting_state_, id,
                                   decision);
  ++decisions_applied_;
  if (history_enabled_) {
    history_.back().paid = before - table_.players[id].cash;
    history_.back().current_bet = betting_state_.current_bet;
  }
  acting_player_ = -1;
  after_player_ = id;
  if (observer_)
    observer_->on_player_action(id, decision, table_, pot_manager_,
                                betting_state_);
  const int winner = find_only_remaining_player(table_);
  if (winner >= 0)
    finish_uncontested_hand(winner);
}
void PokerGame::finish_uncontested_hand(int winner) {
  payouts_.fill(0);
  payouts_[winner] = pot_manager_.award_entire_pot(table_, winner);
  finish_hand();
}
void PokerGame::run_showdown() {
  current_stage_ = PokerStage::Showdown;
  if (observer_)
    observer_->on_stage_started(current_stage_, table_, pot_manager_);
  std::array<HandScore, MAX_PLAYERS> scores{};
  const auto &board = table_.community_cards;
  for (int id = 0; id < table_.num_players; ++id) {
    if (!table_.players[id].in_hand)
      continue;
    const auto &hand = table_.players[id].hand;
    // The deck guarantees seven distinct, valid cards. Convert the dense score
    // to the existing public representation so observers and payouts agree.
    scores[id] = evaluator_->packed_score(evaluator_->evaluate(
        hand[0], hand[1], board[0], board[1], board[2], board[3], board[4]));
  }
  pot_manager_.build_pots(table_);
  if (observer_)
    observer_->on_showdown(table_, pot_manager_, scores);
  payouts_ = pot_manager_.distribute_pots(table_, scores);
  finish_hand();
}
void PokerGame::finish_hand() {
  // Report final chips before moving the button to the next hand. The pot has
  // already been emptied by the payout function that called this helper.
  for (int id = 0; id < table_.num_players; ++id)
    rewards_[id] = table_.players[id].cash - starting_stacks_[id];
  hand_active_ = false;
  acting_player_ = -1;
  current_legal_ = {};
  if (observer_)
    observer_->on_hand_finished(table_, payouts_);
  if (!match_finished())
    rotate_dealer(table_);
}
void PokerGame::play_one_hand() {
  if (!hand_active_ && match_finished())
    return;
  // The synchronous API requires providers; the externally stepped API does
  // not.
  for (int id = 0; id < table_.num_players; ++id)
    if (table_.players[id].cash > 0 &&
        (!hand_active_ ||
         (table_.players[id].in_hand && !table_.players[id].all_in)) &&
        !decision_providers_[id])
      throw std::logic_error("A funded player has no decision provider");
  if (!hand_active_)
    begin_hand();
  while (advance_to_decision())
    apply_action(
        decision_providers_[acting_player_]->choose_observation(current_observation()));
}
void PokerGame::run_match(int max_hands) {
  int played = 0;
  while (!match_finished() && (max_hands <= 0 || played < max_hands)) {
    play_one_hand();
    if (max_hands > 0)
      ++played;
  }
}
PokerGame::Snapshot PokerGame::snapshot() const {
  // Capture execution position as well as cards: restoring only the visible
  // board would not restore pending turns, contribution layers, or future
  // deals.
  Snapshot s;
  s.config_ = config_;
  s.table_ = table_;
  s.pots_ = pot_manager_;
  s.betting_ = betting_state_;
  s.rng_ = random_generator_;
  s.stage_ = current_stage_;
  s.hand_number_ = hand_number_;
  s.actor_ = acting_player_;
  s.after_ = after_player_;
  s.active_ = hand_active_;
  s.history_enabled_ = history_enabled_;
  s.legal_ = current_legal_;
  s.payouts_ = payouts_;
  s.rewards_ = rewards_;
  s.starting_stacks_ = starting_stacks_;
  s.decisions_ = decisions_applied_;
  s.history_ = history_;
  return s;
}
void PokerGame::restore(const Snapshot &s) {
  // Snapshots can only be created by this class, not populated with unchecked
  // external cards. Copy the only allocating member first for exception safety.
  history_ = s.history_;
  config_ = s.config_;
  table_ = s.table_;
  pot_manager_ = s.pots_;
  betting_state_ = s.betting_;
  random_generator_ = s.rng_;
  current_stage_ = s.stage_;
  hand_number_ = s.hand_number_;
  acting_player_ = s.actor_;
  after_player_ = s.after_;
  hand_active_ = s.active_;
  history_enabled_ = s.history_enabled_;
  current_legal_ = s.legal_;
  payouts_ = s.payouts_;
  rewards_ = s.rewards_;
  starting_stacks_ = s.starting_stacks_;
  decisions_applied_ = s.decisions_;
}
} // namespace poker
