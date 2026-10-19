#include "research/games/leduc/LeducGame.hpp"
#include <algorithm>
#include <random>
#include <stdexcept>
namespace poker::research {
LeducGame::LeducGame(std::uint64_t seed) {
  std::array<int, 6> cards{0, 0, 1, 1, 2, 2};
  std::mt19937_64 rng(seed);
  std::shuffle(cards.begin(), cards.end(), rng);
  for (int i = 0; i < 3; ++i)
    ranks_[i] = cards[i];
  board_ = cards[3];
}
policy::DecisionNode LeducGame::node() const {
  if (actor_ < 0)
    throw std::logic_error("Terminal Leduc observation");
  policy::DecisionNode n;
  n.key = "LEDUC3-v1:" + std::to_string(actor_) + ":" +
          std::to_string(ranks_[actor_]) + ":" +
          (street_ ? std::to_string(board_) : "?") + ":" + history_;
  // Receives type/token; returns void, packing the fixture's stable menu.
  const auto add = [&](ActionType type, ActionToken token) {
    n.candidates.actions[n.candidates.count++] = {{type, 0}, 0, token};
  };
  if (round_[actor_] < bet_) {
    add(ActionType::Fold, ActionToken::Fold);
    add(ActionType::Call, ActionToken::Call);
  } else {
    add(ActionType::Check, ActionToken::Check);
    if (!bet_)
      add(ActionType::RaiseTo, ActionToken::MinRaise);
  }
  return n;
}
void LeducGame::apply(std::size_t index) {
  auto menu = node().candidates;
  if (index >= menu.count)
    throw std::out_of_range("Leduc action");
  auto action = menu.actions[index].decision.action;
  history_.push_back(static_cast<char>('0' + actor_));
  history_.push_back(static_cast<char>('a' + static_cast<int>(action)));
  if (action == ActionType::Fold)
    live_[actor_] = false;
  else if (action == ActionType::RaiseTo) {
    bet_ = street_ ? 2 : 1;
    acted_.fill(false);
    paid_[actor_] += bet_ - round_[actor_];
    round_[actor_] = bet_;
  } else if (action == ActionType::Call) {
    paid_[actor_] += bet_ - round_[actor_];
    round_[actor_] = bet_;
  }
  acted_[actor_] = true;
  int live_count = 0;
  bool done = true;
  for (int i = 0; i < 3; ++i)
    if (live_[i]) {
      ++live_count;
      done &= acted_[i] && round_[i] == bet_;
    }
  if (live_count == 1 || (done && street_ == 1)) {
    actor_ = -1;
    return;
  }
  if (done) {
    street_ = 1;
    bet_ = 0;
    acted_.fill(false);
    round_.fill(0);
    actor_ = 2;
    history_ += '/';
  }
  do {
    actor_ = (actor_ + 1) % 3;
  } while (!live_[actor_]);
}
double LeducGame::utility(int seat) const {
  if (actor_ >= 0 || seat < 0 || seat >= 3)
    throw std::logic_error("Leduc payoff boundary");
  int best = -1, winners = 0, pot = 0;
  for (int i = 0; i < 3; ++i) {
    pot += paid_[i];
    if (!live_[i])
      continue;
    int score = ranks_[i] + (ranks_[i] == board_ ? 3 : 0);
    if (score > best) {
      best = score;
      winners = 1;
    } else if (score == best)
      ++winners;
  }
  int score = ranks_[seat] + (ranks_[seat] == board_ ? 3 : 0);
  return -paid_[seat] +
         (live_[seat] && score == best ? double(pot) / winners : 0);
}
} // namespace poker::research
