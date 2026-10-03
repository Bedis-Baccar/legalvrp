#include "legalvrp/data/orders.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace legalvrp::data {

namespace {
const CustomerTypeParams& params_of(const InstanceConfig& cfg, CustomerType t) {
  return *std::ranges::find(cfg.customer_types, t, &CustomerTypeParams::type);
}
}  // namespace

std::vector<Order> make_orders(const InstanceConfig& cfg, const std::vector<Customer>& customers,
                               Pallets largest_truck, Euros postpone_penalty,
                               const Rng& orders_stream) {
  std::vector<Order> out;
  for (int d = 0; d < cfg.days; ++d) {
    Rng rng = orders_stream.split(static_cast<std::uint64_t>(d));

    const auto base = static_cast<double>(rng.uniform_int(cfg.orders_min, cfg.orders_max));
    const double factor = cfg.weekday_factor[static_cast<std::size_t>(d)];
    const auto scaled = static_cast<int>(std::floor(base * factor + 0.5));
    const int n = std::min(std::clamp(scaled, cfg.orders_min, cfg.orders_max),
                           static_cast<int>(customers.size()));

    // Weighted sampling without replacement.
    std::vector<double> w;
    for (const auto& c : customers) w.push_back(params_of(cfg, c.type).order_weight);
    std::vector<std::size_t> picked;
    for (int k = 0; k < n; ++k) {
      const std::size_t i = rng.categorical(w);
      picked.push_back(i);
      w[i] = 0.0;
    }
    std::ranges::sort(picked);

    int seq = 0;
    for (const std::size_t i : picked) {
      const Customer& c = customers[i];
      const Pallets pallets =
          std::min(1 + rng.poisson(params_of(cfg, c.type).pallets_lambda), largest_truck);
      char id[32];
      std::snprintf(id, sizeof id, "d%d-o%02d", d, ++seq);
      Order o;
      o.id = id;
      o.customer_id = c.id;
      o.day = d;
      o.pallets = pallets;
      o.service_mu = service_minutes(pallets);
      o.service_sigma = 0;
      o.postpone_penalty = postpone_penalty;
      out.push_back(std::move(o));
    }
  }
  return out;
}

double true_service_mean(const CustomerTypeParams& type, Pallets pallets) noexcept {
  return type.service_fixed + type.service_per_pallet * static_cast<double>(pallets);
}

Minutes sample_service(double mean, double cv, Rng& rng) noexcept {
  const double x = rng.lognormal(mean, cv);
  return std::max<Minutes>(1, static_cast<Minutes>(std::floor(x + 0.5)));
}

TrueService make_truth(const InstanceConfig& cfg, const std::vector<Customer>& customers,
                       const std::vector<Order>& orders, Rng rng) {
  TrueService t;
  for (const auto& o : orders) {
    const Customer& c = *std::ranges::find(customers, o.customer_id, &Customer::id);
    const CustomerTypeParams& p = params_of(cfg, c.type);
    const double mean = true_service_mean(p, o.pallets);
    t.mean[o.id] = mean;
    t.sigma[o.id] = std::floor(p.service_cv * mean * 1000.0 + 0.5) / 1000.0;
    t.minutes[o.id] = sample_service(mean, p.service_cv, rng);
  }
  return t;
}

}  // namespace legalvrp::data
