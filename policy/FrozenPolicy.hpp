// One read-only deployable policy; no mutable regret table or trainer
// dependency.
#pragma once
#include "policy/Observation.hpp"
#include <array>
#include <filesystem>
#include <unordered_map>
namespace poker::policy {
struct PolicyRow {
  std::array<ActionToken, 9> tokens{};
  std::array<double, 9> probabilities{};
  std::size_t count = 0;
};
struct FrozenPolicy {
  // Manifest identifies rules and whether weights are current or
  // simple-average.
  std::string manifest;
  std::unordered_map<std::string, PolicyRow> rows;
  /** Receives a node and optional fallback flag; returns matching
   * probabilities. Missing/zero-data rows use uniform legal weights. Layout
   * mismatch throws. Used identically for evaluation and normal
   * observation-driven inference. */
  PolicyRow lookup(const DecisionNode &node, bool *fallback = nullptr) const;
};
/** Receives an artifact destination, ASCII magic and payload bytes; returns
 * void. Writes checksummed, bounded-size data through a flushed temporary file
 * then atomically renames on Linux. Previous file survives a failed
 * publication. Used by both policy exports and training checkpoints. */
void write_artifact(const std::filesystem::path &path, const std::string &magic,
                    const std::string &payload);
/** Receives a file and expected magic; returns verified payload bytes.
 * Rejects bad checksum, truncated/oversized files and incompatible versions. */
std::string read_artifact(const std::filesystem::path &path,
                          const std::string &magic);
// Explicit binary field helpers. Readers advance position and throw on
// overflow.
void append_u64(std::string &out, std::uint64_t value);
std::uint64_t read_u64(const std::string &in, std::size_t &position);
void append_string(std::string &out, const std::string &value);
std::string read_string(const std::string &in, std::size_t &position);
void append_double(std::string &out, double value);
double read_double(const std::string &in, std::size_t &position);
/** Receives a frozen policy/path, returns void; saves rows in canonical key
 * order. */
void save_policy(const FrozenPolicy &policy, const std::filesystem::path &path);
/** Receives an artifact path; returns a validated read-only policy. */
FrozenPolicy load_policy(const std::filesystem::path &path);
} // namespace poker::policy
