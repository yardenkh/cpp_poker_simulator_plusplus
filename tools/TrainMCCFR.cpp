// Serial eight-player push/fold training. Normal gameplay is not modified.
#include "research/mccfr/Checkpoint.hpp"
#include "research/mccfr/HoldemTrainingGame.hpp"
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
using namespace poker;
namespace {
// Receives numeric text; returns a complete unsigned value or throws.
std::uint64_t number(const std::string &text) {
  std::uint64_t value;
  auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if (r.ec != std::errc{} || r.ptr != text.data() + text.size())
    throw std::invalid_argument("Invalid integer: " + text);
  return value;
}
} // namespace
int main(int argc, char **argv) {
  try {
    mccfr::TrainingConfig settings;
    std::filesystem::path directory = "runs/pushfold";
    std::filesystem::path resume;
    std::uint64_t traversals = 1000, interval = 1000;
    bool limits_set = false, nodes_set = false, memory_set = false,
         seed_set = false;
    for (int i = 1; i < argc; ++i) {
      std::string flag = argv[i];
      if (flag == "--help") {
        std::cout << "poker_train_mccfr [--traversals N] [--out DIR] [--seed "
                     "N] [--resume FILE]\n"
                     "  [--checkpoint-every N] [--max-entries N] [--max-nodes "
                     "N] [--table-mib N]\n"
                     "Runs eight-player 25-BB push/fold only; traversals are "
                     "additional on resume.\n";
        return 0;
      }
      if (i + 1 == argc)
        throw std::invalid_argument("Missing option value");
      std::string value = argv[++i];
      if (flag == "--out")
        directory = value;
      else if (flag == "--resume")
        resume = value;
      else if (flag == "--traversals")
        traversals = number(value);
      else if (flag == "--seed") {
        settings.seed = number(value);
        seed_set = true;
      } else if (flag == "--checkpoint-every")
        interval = number(value);
      else if (flag == "--max-entries") {
        settings.max_entries = number(value);
        limits_set = true;
      } else if (flag == "--table-mib") {
        auto mib = number(value);
        if (mib > std::numeric_limits<std::size_t>::max() / (1ull << 20))
          throw std::invalid_argument("Memory budget overflow");
        settings.max_table_bytes = mib * (1ull << 20);
        memory_set = true;
      } else if (flag == "--max-nodes") {
        settings.max_nodes_per_traversal = number(value);
        nodes_set = true;
      } else
        throw std::invalid_argument("Unknown option " + flag);
    }
    if (!interval)
      throw std::invalid_argument("Checkpoint interval must be positive");
    if (!resume.empty() && seed_set)
      throw std::invalid_argument("Resume uses checkpoint seed; omit --seed");
    if (resume.empty() && std::filesystem::exists(directory / "latest.mccfr"))
      throw std::invalid_argument("Output already has a checkpoint; use "
                                  "--resume or a new --out directory");
    auto trainer = resume.empty() ? mccfr::ExternalSamplingTrainer(settings)
                                  : mccfr::load_checkpoint(resume);
    if (!resume.empty()) {
      if (limits_set)
        trainer.config.max_entries = settings.max_entries;
      if (nodes_set)
        trainer.config.max_nodes_per_traversal =
            settings.max_nodes_per_traversal;
      if (memory_set)
        trainer.config.max_table_bytes = settings.max_table_bytes;
    }
    trainer.config.validate();
    if (traversals >
        std::numeric_limits<std::uint64_t>::max() - trainer.next_traversal)
      throw std::invalid_argument("Traversal count overflow");
    auto end = trainer.next_traversal + traversals;
    auto start = std::chrono::steady_clock::now();
    std::filesystem::create_directories(directory);
    bool new_metrics = !std::filesystem::exists(directory / "metrics.csv");
    std::ofstream metrics(directory / "metrics.csv", std::ios::app);
    if (!metrics)
      throw std::runtime_error("Cannot open run metrics");
    if (new_metrics)
      metrics << "traversals,nodes,terminals,entries,estimated_table_bytes,"
                 "elapsed_seconds\n";
    std::ofstream manifest(directory / "manifest.txt");
    manifest << trainer.config.manifest() << "\nseed=" << trainer.config.seed
             << "\nformat=serial-pushfold-reference-v1\n";
    // Receives nothing; returns void, appending a committed-progress record.
    const auto report = [&] {
      double elapsed = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - start)
                           .count();
      metrics << trainer.next_traversal << ',' << trainer.total_nodes << ','
              << trainer.total_terminals << ',' << trainer.table.rows.size()
              << ',' << trainer.table.estimated_bytes() << ',' << elapsed
              << '\n';
      metrics.flush();
      if (!metrics)
        throw std::runtime_error("Run metrics write failed");
    };
    try {
      while (trainer.next_traversal < end) {
        mccfr::HoldemTrainingGame game(trainer.config, trainer.next_traversal);
        trainer.step(game);
        if (trainer.next_traversal % interval == 0) {
          mccfr::save_checkpoint(trainer, directory / "latest.mccfr");
          report();
          std::cout << "Traversals " << trainer.next_traversal << " | entries "
                    << trainer.table.rows.size() << " | estimated table bytes "
                    << trainer.table.estimated_bytes() << '\n';
        }
      }
    } catch (const std::exception &error) {
      mccfr::save_checkpoint(trainer, directory / "latest.mccfr");
      std::cerr << "Stopped before traversal " << trainer.next_traversal << ": "
                << error.what()
                << "\nCommitted state saved; resume retries that same "
                   "traversal/deal.\n";
      return 2;
    }
    mccfr::save_checkpoint(trainer, directory / "latest.mccfr");
    report();
    std::size_t zero_average = 0;
    policy::save_policy(
        trainer.table.export_policy(trainer.config.manifest(), false),
        directory / "current.policy");
    policy::save_policy(trainer.table.export_policy(trainer.config.manifest(),
                                                    true, &zero_average),
                        directory / "average.policy");
    double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    std::cout << "Completed traversals: " << trainer.next_traversal
              << "\nEntries: " << trainer.table.rows.size()
              << "\nNodes: " << trainer.total_nodes
              << "\nTerminals: " << trainer.total_terminals
              << "\nZero-average rows (uniform export): " << zero_average
              << "\nSeconds: " << seconds
              << "\nPolicies: " << directory / "current.policy" << " and "
              << directory / "average.policy" << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
