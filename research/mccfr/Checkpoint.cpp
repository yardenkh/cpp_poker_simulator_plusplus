#include "research/mccfr/Checkpoint.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>
namespace poker::mccfr {
using namespace policy;
void save_checkpoint(const ExternalSamplingTrainer &t,
                     const std::filesystem::path &path) {
  std::string out;
  append_string(out, t.config.schema);
  for (auto value :
       {t.config.seed, std::uint64_t(t.config.players),
        std::uint64_t(t.config.starting_chips),
        std::uint64_t(t.config.small_blind), std::uint64_t(t.config.big_blind),
        std::uint64_t(t.config.max_entries),
        std::uint64_t(t.config.max_table_bytes),
        t.config.max_nodes_per_traversal, t.next_traversal, t.total_nodes,
        t.total_terminals})
    append_u64(out, value);
  append_u64(out, t.table.rows.size());
  std::vector<std::string> keys;
  keys.reserve(t.table.rows.size());
  for (const auto &[key, entry] : t.table.rows) {
    (void)entry;
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());
  for (const auto &key : keys) {
    const auto &entry = t.table.rows.at(key);
    append_string(out, key);
    append_u64(out, entry.count);
    append_u64(out, entry.visits);
    for (std::size_t i = 0; i < entry.count; ++i) {
      append_u64(out, static_cast<unsigned>(entry.tokens[i]));
      append_double(out, entry.regrets[i]);
      append_double(out, entry.average[i]);
    }
  }
  append_u64(out, t.config.linear_regret_weights);
  append_u64(out, t.config.linear_average_weights);
  write_artifact(path, "POKER-MCCFR-2", out);
}
ExternalSamplingTrainer load_checkpoint(const std::filesystem::path &path) {
  std::string bytes;
  bool legacy = false;
  try {
    bytes = read_artifact(path, "POKER-MCCFR-2");
  } catch (const std::runtime_error &) {
    bytes = read_artifact(path, "POKER-MCCFR-1");
    legacy = true;
  }
  std::size_t p = 0;
  TrainingConfig config;
  config.schema = read_string(bytes, p);
  config.seed = read_u64(bytes, p);
  // Receives nothing; returns a checked signed configuration integer.
  const auto integer = [&]() {
    auto value = read_u64(bytes, p);
    if (value > std::numeric_limits<int>::max())
      throw std::runtime_error("Oversized checkpoint setting");
    return static_cast<int>(value);
  };
  config.players = integer();
  config.starting_chips = integer();
  config.small_blind = integer();
  config.big_blind = integer();
  config.max_entries = read_u64(bytes, p);
  config.max_table_bytes = read_u64(bytes, p);
  config.max_nodes_per_traversal = read_u64(bytes, p);
  config.validate();
  if (config.schema != TrainingConfig{}.schema)
    throw std::runtime_error("Unsupported checkpoint training schema");
  ExternalSamplingTrainer trainer(config);
  trainer.next_traversal = read_u64(bytes, p);
  trainer.total_nodes = read_u64(bytes, p);
  trainer.total_terminals = read_u64(bytes, p);
  auto count = read_u64(bytes, p);
  if (count > config.max_entries || count > bytes.size() / 24)
    throw std::runtime_error("Invalid checkpoint row count");
  for (std::uint64_t n = 0; n < count; ++n) {
    auto key = read_string(bytes, p);
    StrategyEntry entry;
    entry.count = read_u64(bytes, p);
    entry.visits = read_u64(bytes, p);
    if (!entry.count || entry.count > 9)
      throw std::runtime_error("Invalid checkpoint layout");
    for (std::size_t i = 0; i < entry.count; ++i) {
      auto token = read_u64(bytes, p);
      if (token > static_cast<unsigned>(ActionToken::Shove))
        throw std::runtime_error("Invalid action identity");
      entry.tokens[i] = static_cast<ActionToken>(token);
      entry.regrets[i] = read_double(bytes, p);
      entry.average[i] = read_double(bytes, p);
      if (entry.average[i] < 0)
        throw std::runtime_error("Invalid average weight");
    }
    if (!trainer.table.rows.emplace(key, entry).second)
      throw std::runtime_error("Duplicate checkpoint key");
  }
  if (!legacy) {
    auto regret = read_u64(bytes, p), average = read_u64(bytes, p);
    if (regret > 1 || average > 1)
      throw std::runtime_error("Invalid weighting variant");
    trainer.config.linear_regret_weights = regret != 0;
    trainer.config.linear_average_weights = average != 0;
  }
  if (p != bytes.size())
    throw std::runtime_error("Trailing checkpoint bytes");
  trainer.table.recount_memory();
  if (trainer.table.estimated_bytes() > config.max_table_bytes)
    throw std::runtime_error("Checkpoint exceeds memory budget");
  return trainer;
}
} // namespace poker::mccfr
