#include "legalvrp/estimate/learned.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "legalvrp/domain/json.hpp"

namespace legalvrp::estimate {

namespace {

constexpr double kLevels[] = {0.5, 0.8, 0.9, 0.95};

double round2(double x) { return std::floor(x * 100.0 + 0.5) / 100.0; }

}  // namespace

double TypeModel::mu(Pallets p) const noexcept {
  return std::max(1.0, fixed + per_pallet * static_cast<double>(p));
}

std::vector<Observation> observations(const WeekInstance& week, const std::map<std::string, Minutes>& minutes) {
  std::vector<Observation> out;
  for (const auto& o : week.orders) {
    const auto m = minutes.find(o.id);
    if (m == minutes.end()) continue;
    const auto c = std::ranges::find(week.customers, o.customer_id, &Customer::id);
    if (c == week.customers.end()) throw std::out_of_range("unknown customer " + o.customer_id);
    out.push_back({c->type, o.pallets, m->second});
  }
  return out;
}

LearnedModel fit(const std::vector<Observation>& history) {
  LearnedModel model;
  for (const CustomerType t : {CustomerType::grocery, CustomerType::restaurant, CustomerType::site}) {
    std::vector<const Observation*> obs;
    for (const auto& o : history) {
      if (o.type == t) obs.push_back(&o);
    }
    if (obs.empty()) continue;
    TypeModel m;
    m.type = t;
    m.n = obs.size();
    const double n = static_cast<double>(obs.size());
    double sp = 0, sy = 0;
    for (const auto* o : obs) {
      sp += static_cast<double>(o->pallets);
      sy += static_cast<double>(o->minutes);
    }
    const double pbar = sp / n, ybar = sy / n;
    double sxx = 0, sxy = 0;
    for (const auto* o : obs) {
      const double dp = static_cast<double>(o->pallets) - pbar;
      sxx += dp * dp;
      sxy += dp * (static_cast<double>(o->minutes) - ybar);
    }
    m.per_pallet = sxx > 0.0 ? sxy / sxx : 0.0;
    m.fixed = ybar - m.per_pallet * pbar;
    std::vector<double> ratio;
    double ss = 0;
    for (const auto* o : obs) {
      const double r = static_cast<double>(o->minutes) / m.mu(o->pallets);
      ratio.push_back(r);
      ss += (r - 1.0) * (r - 1.0);
    }
    m.cv = std::sqrt(ss / n);
    std::ranges::sort(ratio);
    for (const double level : kLevels) {  // nearest rank
      const auto rank = static_cast<std::size_t>(std::ceil(level * n));
      m.ratio_quantiles.emplace_back(level, ratio[std::min(ratio.size(), std::max<std::size_t>(rank, 1)) - 1]);
    }
    model.types.push_back(std::move(m));
  }
  return model;
}

std::vector<ServiceEstimate> LearnedEstimator::estimate(std::span<const Order> orders,
                                                        std::span<const Customer> customers) const {
  std::vector<ServiceEstimate> out;
  out.reserve(orders.size());
  for (const auto& o : orders) {
    const auto c = std::ranges::find(customers, o.customer_id, &Customer::id);
    if (c == customers.end()) throw std::out_of_range("unknown customer " + o.customer_id);
    const auto m = std::ranges::find(model_.types, c->type, &TypeModel::type);
    if (m == model_.types.end()) {  // no history for this type: keep the planning rule
      out.push_back({static_cast<double>(o.service_mu), 0.0});
    } else {
      out.push_back({m->mu(o.pallets), m->sigma(o.pallets)});
    }
  }
  return out;
}

nlohmann::json to_json(const LearnedModel& model) {
  using nlohmann::json;
  json types = json::object();
  json table = json::array();
  for (const auto& m : model.types) {
    json q = json::object();
    for (const auto& [level, r] : m.ratio_quantiles) q[std::to_string(static_cast<int>(level * 100 + 0.5))] = r;
    types[to_string(m.type)] = {{"n", m.n}, {"fixed", m.fixed}, {"per_pallet", m.per_pallet}, {"cv", m.cv},
                                {"ratio_quantiles", q}};
    for (Pallets p = 1; p <= model.max_pallets; ++p) {
      json row = {{"type", to_string(m.type)}, {"pallets", p}, {"mu", round2(m.mu(p))}, {"sigma", round2(m.sigma(p))}};
      for (const auto& [level, r] : m.ratio_quantiles) {
        row["q" + std::to_string(static_cast<int>(level * 100 + 0.5))] = round2(m.mu(p) * r);
      }
      table.push_back(row);
    }
  }
  return {{"model", "per type: mu = fixed + per_pallet * pallets, sigma = cv * mu, empirical ratio quantiles"},
          {"source", model.source},
          {"max_pallets", model.max_pallets},
          {"types", types},
          {"table", table}};
}

LearnedModel model_from_json(const nlohmann::json& j) {
  LearnedModel model;
  model.source = j.at("source").get<std::string>();
  model.max_pallets = j.at("max_pallets").get<Pallets>();
  for (const auto& item : j.at("types").items()) {
    const auto& t = item.value();
    TypeModel m;
    m.type = customer_type_from_string(item.key());
    m.n = t.at("n").get<std::size_t>();
    m.fixed = t.at("fixed").get<double>();
    m.per_pallet = t.at("per_pallet").get<double>();
    m.cv = t.at("cv").get<double>();
    for (const auto& q : t.at("ratio_quantiles").items()) {
      m.ratio_quantiles.emplace_back(std::stod(q.key()) / 100.0, q.value().get<double>());
    }
    std::ranges::sort(m.ratio_quantiles);
    model.types.push_back(std::move(m));
  }
  return model;
}

}  // namespace legalvrp::estimate
