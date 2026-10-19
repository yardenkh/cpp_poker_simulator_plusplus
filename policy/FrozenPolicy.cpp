#include "policy/FrozenPolicy.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif
namespace poker::policy {
void append_u64(std::string &out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i)
    out.push_back(static_cast<char>(value >> (8 * i)));
}
std::uint64_t read_u64(const std::string &in, std::size_t &p) {
  if (p > in.size() || in.size() - p < 8)
    throw std::runtime_error("Truncated artifact");
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i)
    value |= std::uint64_t(static_cast<unsigned char>(in[p++])) << (8 * i);
  return value;
}
void append_string(std::string &out, const std::string &value) {
  append_u64(out, value.size());
  out += value;
}
std::string read_string(const std::string &in, std::size_t &p) {
  auto n = read_u64(in, p);
  if (n > in.size() - p)
    throw std::runtime_error("Truncated string");
  auto value = in.substr(p, static_cast<std::size_t>(n));
  p += n;
  return value;
}
void append_double(std::string &out, double value) {
  append_u64(out, std::bit_cast<std::uint64_t>(value));
}
double read_double(const std::string &in, std::size_t &p) {
  double value = std::bit_cast<double>(read_u64(in, p));
  if (!std::isfinite(value))
    throw std::runtime_error("Nonfinite artifact value");
  return value;
}
namespace {
// Receives bytes; returns an integrity checksum (not a cryptographic
// signature).
std::uint64_t checksum(const std::string &bytes) {
  std::uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : bytes) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}
// Receives a file/directory path; returns void after flushing or throws.
void flush_path(const std::filesystem::path &path, bool directory = false) {
#ifdef __linux__
  int fd = ::open(path.c_str(), O_RDONLY | (directory ? O_DIRECTORY : 0));
  if (fd < 0)
    throw std::runtime_error("Cannot open artifact for fsync");
  int result = ::fsync(fd);
  ::close(fd);
  if (result != 0)
    throw std::runtime_error("Artifact fsync failed");
#else
  (void)path;
  (void)directory;
#endif
}
} // namespace
void write_artifact(const std::filesystem::path &path, const std::string &magic,
                    const std::string &payload) {
  auto parent = path.parent_path();
  if (parent.empty())
    parent = ".";
  std::filesystem::create_directories(parent);
  auto temporary = path;
  temporary += ".tmp";
  std::string bytes;
  append_string(bytes, magic);
  append_string(bytes, payload);
  append_u64(bytes, checksum(payload));
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out)
      throw std::runtime_error("Cannot write artifact");
  }
  flush_path(temporary);
  // Single writer only. Keep the old complete generation before publication.
  if (std::filesystem::exists(path)) {
    auto previous = path;
    previous += ".previous";
    std::filesystem::copy_file(
        path, previous, std::filesystem::copy_options::overwrite_existing);
    flush_path(previous);
  }
  std::filesystem::rename(temporary, path);
  flush_path(parent, true);
}
std::string read_artifact(const std::filesystem::path &path,
                          const std::string &magic) {
  auto n = std::filesystem::file_size(path);
  if (n > (1ull << 31))
    throw std::runtime_error("Artifact exceeds prototype 2 GiB loader limit");
  std::string bytes(static_cast<std::size_t>(n), '\0');
  std::ifstream in(path, std::ios::binary);
  in.read(bytes.data(), static_cast<std::streamsize>(n));
  if (!in)
    throw std::runtime_error("Cannot read artifact");
  std::size_t p = 0;
  if (read_string(bytes, p) != magic)
    throw std::runtime_error("Artifact version mismatch");
  auto payload = read_string(bytes, p);
  auto hash = read_u64(bytes, p);
  if (p != bytes.size() || hash != checksum(payload))
    throw std::runtime_error("Artifact checksum/length mismatch");
  return payload;
}
PolicyRow FrozenPolicy::lookup(const DecisionNode &node, bool *fallback) const {
  PolicyRow result;
  result.count = node.candidates.count;
  if (!result.count || result.count > 9)
    throw std::logic_error("Invalid policy menu");
  for (std::size_t i = 0; i < result.count; ++i) {
    result.tokens[i] = node.candidates.actions[i].token;
    result.probabilities[i] = 1.0 / result.count;
  }
  auto it = rows.find(node.key);
  if (fallback)
    *fallback = it == rows.end();
  if (it == rows.end())
    return result;
  if (it->second.count != result.count)
    throw std::logic_error("Policy layout mismatch");
  for (std::size_t i = 0; i < result.count; ++i)
    if (it->second.tokens[i] != result.tokens[i])
      throw std::logic_error("Policy token mismatch");
  return it->second;
}
void save_policy(const FrozenPolicy &policy,
                 const std::filesystem::path &path) {
  std::string out;
  append_string(out, policy.manifest);
  append_u64(out, policy.rows.size());
  std::vector<std::string> keys;
  keys.reserve(policy.rows.size());
  for (const auto &[key, row] : policy.rows) {
    (void)row;
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end());
  for (const auto &key : keys) {
    const auto &row = policy.rows.at(key);
    append_string(out, key);
    append_u64(out, row.count);
    for (std::size_t i = 0; i < row.count; ++i) {
      append_u64(out, static_cast<unsigned>(row.tokens[i]));
      append_double(out, row.probabilities[i]);
    }
  }
  write_artifact(path, "POKER-POLICY-1", out);
}
FrozenPolicy load_policy(const std::filesystem::path &path) {
  auto bytes = read_artifact(path, "POKER-POLICY-1");
  std::size_t p = 0;
  FrozenPolicy result;
  result.manifest = read_string(bytes, p);
  auto count = read_u64(bytes, p);
  if (count > bytes.size() / 16)
    throw std::runtime_error("Invalid row count");
  for (std::uint64_t n = 0; n < count; ++n) {
    auto key = read_string(bytes, p);
    PolicyRow row;
    row.count = read_u64(bytes, p);
    if (!row.count || row.count > 9)
      throw std::runtime_error("Invalid policy layout");
    double sum = 0;
    for (std::size_t i = 0; i < row.count; ++i) {
      auto token = read_u64(bytes, p);
      if (token > static_cast<unsigned>(ActionToken::Shove))
        throw std::runtime_error("Invalid token");
      row.tokens[i] = static_cast<ActionToken>(token);
      row.probabilities[i] = read_double(bytes, p);
      if (row.probabilities[i] < 0)
        throw std::runtime_error("Negative probability");
      sum += row.probabilities[i];
    }
    if (std::abs(sum - 1) > 1e-9 || !result.rows.emplace(key, row).second)
      throw std::runtime_error("Invalid/duplicate policy row");
  }
  if (p != bytes.size())
    throw std::runtime_error("Trailing policy data");
  return result;
}
} // namespace poker::policy
