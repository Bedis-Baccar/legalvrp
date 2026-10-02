#pragma once
// legalvrp::data — portable seeded RNG (D-003).
//
// Same seed => same numbers on every compiler and OS. Therefore:
//   * the engine is xoshiro256** seeded through SplitMix64 (fully specified bit operations);
//   * all distributions are implemented here using only + - * / and sqrt, which IEEE 754
//     rounds exactly; no std::*_distribution, no std::exp/sin/cos (libm differs by 1 ulp).
// Use split() to give each generator component its own stream, so changing one
// component never shifts the draws of another.

#include <cstddef>
#include <cstdint>
#include <span>

namespace legalvrp::data {

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t& state) noexcept;

// exp(x) using basic arithmetic only: deterministic across platforms (not correctly rounded).
[[nodiscard]] double portable_exp(double x) noexcept;

class Rng {
 public:
  explicit Rng(std::uint64_t seed) noexcept;

  [[nodiscard]] std::uint64_t next_u64() noexcept;
  [[nodiscard]] double uniform01() noexcept;                     // [0, 1), 53 random bits
  [[nodiscard]] double uniform(double lo, double hi) noexcept;   // [lo, hi)
  [[nodiscard]] std::int64_t uniform_int(std::int64_t lo, std::int64_t hi) noexcept;  // [lo, hi], unbiased
  [[nodiscard]] bool bernoulli(double p) noexcept;
  [[nodiscard]] int poisson(double lambda) noexcept;             // inversion; lambda in [0, 50]
  [[nodiscard]] std::size_t categorical(std::span<const double> weights) noexcept;  // weights >= 0, sum > 0

  // Independent stream derived from this generator's seed and a stream id.
  [[nodiscard]] Rng split(std::uint64_t stream_id) const noexcept;

 private:
  std::uint64_t seed_;
  std::uint64_t s_[4];
};

}  // namespace legalvrp::data
