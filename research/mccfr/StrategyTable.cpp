#include "research/mccfr/StrategyTable.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace poker::mccfr {
namespace {
// Receives entries; returns void or throws on incompatible action identities.
void compatible(const StrategyEntry &a, const StrategyEntry &b) {
  if (a.count != b.count)
    throw std::logic_error("Strategy action count mismatch");
  for (std::size_t i = 0; i < a.count; ++i)
    if (a.tokens[i] != b.tokens[i])
      throw std::logic_error("Strategy token mismatch");
}
// Conservative per-row estimate includes owned key, hash node and index slack.
std::size_t row_bytes(const std::string &key) {
  return sizeof(StrategyEntry) + key.capacity() + 160;
}
} // namespace
StrategyEntry empty_entry(const policy::DecisionNode &node) {
  StrategyEntry entry;
  entry.count = node.candidates.count;
  if (!entry.count || entry.count > 9)
    throw std::logic_error("Invalid training menu");
  for (std::size_t i = 0; i < entry.count; ++i)
    entry.tokens[i] = node.candidates.actions[i].token;
  return entry;
}
std::array<double, 9> regret_match(const StrategyEntry &entry) {
  if (!entry.count || entry.count > 9)
    throw std::logic_error("Invalid regret layout");
  std::array<double, 9> result{};
  double sum = 0;
  for (std::size_t i = 0; i < entry.count; ++i) {
    result[i] = std::max(0.0, entry.regrets[i]);
    sum += result[i];
  }
  if (!std::isfinite(sum))
    throw std::runtime_error("Nonfinite regrets");
  for (std::size_t i = 0; i < entry.count; ++i)
    result[i] = sum > 0 ? result[i] / sum : 1.0 / entry.count;
  return result;
}
StrategyEntry StrategyTable::lookup(const policy::DecisionNode &node) const {
  auto expected = empty_entry(node);
  auto it = rows.find(node.key);
  if (it == rows.end())
    return expected;
  compatible(it->second, expected);
  return it->second;
}
std::size_t StrategyTable::estimated_bytes() const {
  return row_bytes_ + rows.bucket_count() * sizeof(void *);
}
void StrategyTable::recount_memory() {
  row_bytes_ = 0;
  for (const auto &[key, entry] : rows) {
    (void)entry;
    row_bytes_ += row_bytes(key);
  }
}
void StrategyTable::commit(const StrategyRows &deltas,
                           const TrainingConfig &config) {
  std::size_t new_count = 0, bytes = estimated_bytes();
  StrategyRows prepared;
  for (const auto &[key, delta] : deltas) {
    auto it = rows.find(key);
    StrategyEntry next;
    if (it == rows.end()) {
      next = delta;
      ++new_count;
      bytes += row_bytes(key) + 16;
    } else {
      compatible(it->second, delta);
      next = it->second;
      next.visits += delta.visits;
      for (std::size_t i = 0; i < next.count; ++i) {
        next.regrets[i] += delta.regrets[i];
        next.average[i] += delta.average[i];
      }
    }
    for (std::size_t i = 0; i < next.count; ++i)
      if (!std::isfinite(next.regrets[i]) || !std::isfinite(next.average[i]) ||
          next.average[i] < 0)
        throw std::runtime_error("Invalid numeric strategy update");
    prepared.emplace(key, next);
  }
  if (rows.size() + new_count > config.max_entries ||
      bytes > config.max_table_bytes)
    throw std::runtime_error(
        "Strategy capacity limit: stop and resume with higher limits");
  // Allocate all replacement nodes and index capacity before any learning
  // changes.
  rows.reserve(rows.size() + new_count);
  for (auto it = prepared.begin(); it != prepared.end();) {
    auto existing = rows.find(it->first);
    if (existing != rows.end()) {
      existing->second = it->second;
      it = prepared.erase(it);
    } else
      ++it;
  }
  for (const auto &[key, entry] : prepared) {
    (void)entry;
    row_bytes_ += row_bytes(key);
  }
  rows.merge(prepared);
}
policy::FrozenPolicy
StrategyTable::export_policy(const std::string &manifest, bool average,
                             std::size_t *zero_average_rows) const {
  policy::FrozenPolicy result;
  result.manifest =
      manifest + (average ? ";export=simple-average" : ";export=current");
  if (zero_average_rows)
    *zero_average_rows = 0;
  for (const auto &[key, entry] : rows) {
    policy::PolicyRow row;
    row.count = entry.count;
    row.tokens = entry.tokens;
    row.probabilities = regret_match(entry);
    if (average) {
      double sum = 0;
      for (std::size_t i = 0; i < entry.count; ++i)
        sum += entry.average[i];
      if (sum <= 0 && zero_average_rows)
        ++*zero_average_rows;
      for (std::size_t i = 0; i < entry.count; ++i)
        row.probabilities[i] =
            sum > 0 ? entry.average[i] / sum : 1.0 / entry.count;
    }
    result.rows.emplace(key, row);
  }
  return result;
}
} // namespace poker::mccfr
