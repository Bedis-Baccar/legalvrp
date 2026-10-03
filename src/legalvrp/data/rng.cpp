#include "legalvrp/data/rng.hpp"

#include <cstdint>
#include <cassert>
#include <limits>

namespace legalvrp::data {

namespace {
constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }
}  // namespace

std::uint64_t splitmix64(std::uint64_t& state) noexcept {
  std::uint64_t z = (state += UINT64_C(0x9e3779b97f4a7c15));
  z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
  return z ^ (z >> 31);
}

double portable_exp(double x) noexcept {
  // exp(x) = exp(x / 2^m)^(2^m) with |x / 2^m| <= 1/16, Taylor series to 14 terms.
  int m = 0;
  double r = x;
  while (r > 0.0625 || r < -0.0625) {
    r *= 0.5;
    ++m;
  }
  double term = 1.0;
  double sum = 1.0;
  for (int k = 1; k <= 14; ++k) {
    term = term * r / k;
    sum += term;
  }
  for (int i = 0; i < m; ++i) sum *= sum;
  return sum;
}

Rng::Rng(std::uint64_t seed) noexcept : seed_(seed) {
  std::uint64_t st = seed;
  for (auto& w : s_) w = splitmix64(st);
}

std::uint64_t Rng::next_u64() noexcept {  // xoshiro256** (Blackman & Vigna)
  const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
  const std::uint64_t t = s_[1] << 17;
  s_[2] ^= s_[0];
  s_[3] ^= s_[1];
  s_[1] ^= s_[2];
  s_[0] ^= s_[3];
  s_[2] ^= t;
  s_[3] = rotl(s_[3], 45);
  return result;
}

double Rng::uniform01() noexcept {
  return static_cast<double>(next_u64() >> 11) * 0x1.0p-53;
}

double Rng::uniform(double lo, double hi) noexcept { return lo + (hi - lo) * uniform01(); }

std::int64_t Rng::uniform_int(std::int64_t lo, std::int64_t hi) noexcept {
  assert(lo <= hi);
  const std::uint64_t range = static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo) + 1;
  if (range == 0) return static_cast<std::int64_t>(next_u64());  // full 64-bit range
  // Rejection: accept only the largest multiple of `range` to remove modulo bias.
  const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() -
                              std::numeric_limits<std::uint64_t>::max() % range;
  std::uint64_t x = next_u64();
  while (x >= limit) x = next_u64();
  return lo + static_cast<std::int64_t>(x % range);
}

bool Rng::bernoulli(double p) noexcept { return uniform01() < p; }

int Rng::poisson(double lambda) noexcept {
  assert(lambda >= 0.0 && lambda <= 50.0);
  // Inversion: walk the CDF with p_k = p_{k-1} * lambda / k.
  const double u = uniform01();
  double p = portable_exp(-lambda);
  double cdf = p;
  int k = 0;
  while (u >= cdf && k < 1000) {
    ++k;
    p = p * lambda / k;
    cdf += p;
  }
  return k;
}

std::size_t Rng::categorical(std::span<const double> weights) noexcept {
  double total = 0.0;
  for (const double w : weights) total += w;
  assert(total > 0.0);
  const double u = uniform01() * total;
  double acc = 0.0;
  std::size_t last_positive = 0;
  for (std::size_t i = 0; i < weights.size(); ++i) {
    if (weights[i] <= 0.0) continue;
    last_positive = i;
    acc += weights[i];
    if (u < acc) return i;
  }
  return last_positive;  // u rounded up to total
}

Rng Rng::split(std::uint64_t stream_id) const noexcept {
  std::uint64_t st = seed_ ^ (UINT64_C(0xd1b54a32d192ed03) * (stream_id + 1));
  return Rng(splitmix64(st));
}

}  // namespace legalvrp::data
