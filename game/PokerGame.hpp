// Game orchestration APIs: synchronous play, resumable decisions, episode
// resets, and snapshots.

#ifndef POKER_GAME_HPP
#define POKER_GAME_HPP

#include "decision/DecisionProvider.hpp"
#include "evaluation/FastHandEvaluator.hpp"
#include "game/GameObserver.hpp"
#include <random>
#include <span>
#include <vector>

namespace poker {
// One rules implementation serves synchronous play and externally chosen
// actions. No console output, neural-network dependency, or per-turn allocation
// is required. An instance belongs to one thread at a time.
class PokerGame {
public:
  // Explicit seeds avoid querying system entropy for every simulated game.
  // The injected evaluator must outlive this game; the default is process-wide.
  PokerGame(int num_players, const GameConfig &config = GameConfig{},
            std::uint32_t seed = 5489u,
            const FastHandEvaluator &evaluator = default_fast_hand_evaluator());
  void set_random_seed(std::uint32_t seed);
  // Update blind amounts between hands; an active hand keeps its original
  // rules. Throws logic_error during play or invalid_argument for unsafe blind
  // values.
  void set_blinds(int small_blind, int big_blind);
  // Borrowed providers/observer must outlive their use. nullptr disables
  // events.
  void set_decision_provider(int player_id, DecisionProvider &provider);
  void set_observer(GameObserver *observer);

  // Synchronous convenience API. A partially played hand is resumed, so human
  // or batched control and the provider-driven API use identical transitions.
  void play_one_hand();
  void run_match(int max_hands = 0);
  bool match_finished() const;
  int winner_id() const;
  const Table &table() const { return table_; }

  // External control: begin, advance to a decision, inspect, then apply it.
  // No providers are needed. advance returns false at hand completion and
  // never silently starts a new hand. Repeated calls at a decision are cheap.
  bool begin_hand();
  bool advance_to_decision();
  int acting_player() const { return acting_player_; }
  bool hand_in_progress() const { return hand_active_; }
  PlayerView current_view() const;
  // Receives no input; returns this actor's public history and private view.
  // The history span is valid only until the next game mutation.
  PlayerObservation current_observation() const;
  void apply_action(const PlayerDecision &decision);

  // Reuse allocations/providers/evaluator between independent experiments.
  // reset() restores equal configured stacks. reset_episode accepts explicit
  // seat stacks and button position; at least two funded seats are required.
  void reset(std::uint32_t seed);
  void reset_episode(std::span<const int> stacks, int dealer_id,
                     std::uint32_t seed);

  // Enable history before starting a hand. Capacity is reused across hands;
  // long histories grow only when necessary. Disabled history allocates
  // nothing.
  void enable_history(bool enabled, std::size_t reserve_actions = 128);
  std::span<const ActionRecord> action_history() const { return history_; }
  const std::array<int, MAX_PLAYERS> &last_payouts() const { return payouts_; }
  // Rewards are final cash minus cash before blinds, so their sum is zero.
  const std::array<int, MAX_PLAYERS> &last_rewards() const { return rewards_; }
  std::uint64_t decisions_applied() const { return decisions_applied_; }

  // Opaque snapshots contain deck, RNG, pot/betting state, and optional
  // history. They exclude evaluator tables, observers, and strategy/model
  // state. Copying is explicit so normal actions do not pay the cost of a
  // game-state copy.
  class Snapshot {
    friend class PokerGame;
    GameConfig config_{};
    Table table_{};
    PotManager pots_{};
    BettingRoundState betting_{};
    std::mt19937 rng_{};
    PokerStage stage_{};
    int hand_number_ = 0, actor_ = -1, after_ = -1;
    bool active_ = false, history_enabled_ = false;
    LegalActions legal_{};
    std::array<int, MAX_PLAYERS> payouts_{}, rewards_{}, starting_stacks_{};
    std::uint64_t decisions_ = 0;
    std::vector<ActionRecord> history_;
    Snapshot() = default;

  public:
    Snapshot(const Snapshot &) = default;
    Snapshot(Snapshot &&) = default;
    Snapshot &operator=(const Snapshot &) = default;
    Snapshot &operator=(Snapshot &&) = default;
  };
  Snapshot snapshot() const;
  // Restoring emits no callbacks. Providers/observer stay attached to THIS
  // game; restore strategy RNG/hidden state separately if the strategy has it.
  void restore(const Snapshot &snapshot);

private:
  GameConfig config_;
  // Live table/contribution state belongs to this game. The evaluator pointer
  // refers to shared read-only tables and is deliberately excluded from
  // snapshots.
  Table table_;
  PotManager pot_manager_;
  BettingRoundState betting_state_;
  const FastHandEvaluator *evaluator_;
  PokerStage current_stage_ = PokerStage::PreFlop;
  std::array<DecisionProvider *, MAX_PLAYERS> decision_providers_{};
  GameObserver *observer_ = nullptr;
  std::mt19937 random_generator_;
  int hand_number_ = 0;
  // active means chips have not yet been paid out. actor >= 0 means the game
  // is paused with cached legal actions; after_player_ is where the next search
  // starts after an action or when entering a new betting street.
  bool hand_active_ = false;
  int acting_player_ = -1, after_player_ = -1;
  LegalActions current_legal_{};
  bool history_enabled_ = false;
  std::vector<ActionRecord> history_;
  // Payouts are gross chips received; rewards subtract each seat's hand-start
  // stack. Keeping starting_stacks_ also accounts for posted blinds in rewards.
  std::array<int, MAX_PLAYERS> payouts_{}, rewards_{}, starting_stacks_{};
  std::uint64_t decisions_applied_ = 0;

  void start_new_hand();
  void post_blinds();
  void enter_stage(PokerStage stage);
  void finish_uncontested_hand(int winner_id);
  void run_showdown();
  void finish_hand();
};
} // namespace poker
#endif
