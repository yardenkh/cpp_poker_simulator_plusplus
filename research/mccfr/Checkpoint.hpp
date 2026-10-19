#pragma once
#include "research/mccfr/ExternalSamplingTrainer.hpp"
namespace poker::mccfr {
/** Receives committed trainer/path; returns void after durable publication.
 * Stores regrets, simple averages, exact configuration and next logical ID.
 * In-flight recursion is never checkpointed; failed IDs are retried on resume.
 */
void save_checkpoint(const ExternalSamplingTrainer &trainer,
                     const std::filesystem::path &path);
/** Receives a checkpoint path; returns a validated trainer with identical
 * progress. Rejects corruption, incompatible schemas and invalid numeric/layout
 * fields. */
ExternalSamplingTrainer load_checkpoint(const std::filesystem::path &path);
} // namespace poker::mccfr
