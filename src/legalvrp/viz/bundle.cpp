#include "legalvrp/viz/bundle.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "legalvrp/domain/json.hpp"
#include "legalvrp/kpi/report.hpp"
#include "legalvrp/sim/realise.hpp"
#include "legalvrp/viz/timeline.hpp"

namespace legalvrp::viz {

using nlohmann::json;

namespace {

json to_json_facts(const check::DriverFacts& f) {
  return {{"driver_id", f.driver_id}, {"used", f.used}, {"service_minutes", f.service_minutes},
          {"driving_minutes", f.driving_minutes}, {"km", f.km}, {"cost", f.cost}};
}

json to_json_check(const check::DayCheck& c) {
  json drivers = json::array();
  for (const auto& f : c.drivers) drivers.push_back(to_json_facts(f));
  return {{"ok", c.ok()}, {"violations", c.violations}, {"objective", c.objective},
          {"postponement_cost", c.postponement_cost}, {"drivers", drivers}};
}

bool has_bound(const SolverStats& s) {
  return !(s.status.empty() || s.status == "ALNS" || s.status == "BASELINE" || s.status == "HAND");
}

json to_json_solver(const SolverStats& s) {
  return {{"status", s.status},
          {"runtime_s", s.runtime_s},
          {"objective", s.objective},
          {"gap", has_bound(s) ? json(s.gap) : json(nullptr)},
          {"best_bound", has_bound(s) ? json(s.best_bound) : json(nullptr)},
          {"baseline_fallback", s.baseline_fallback}};
}

std::string truck_of(const DayInstance& day, const std::string& driver_id) {
  const auto it = std::ranges::find(day.drivers, driver_id, &Driver::id);
  return it == day.drivers.end() ? std::string{} : it->truck_id;
}

json to_json_route(const DayInstance& day, const Route& r) {
  json j = r;  // driver_id, order_ids, breaks, departure, arrivals, service_starts, return_time
  j["truck_id"] = truck_of(day, r.driver_id);
  j["timeline"] = timeline_json(build_timeline(day, r));
  return j;
}

json to_json_instance(const WeekInstance& w) {
  json j = w;  // without the matrix (stored separately)
  j.erase("orders");  // per day below, with carried orders and effective penalties
  j.erase("matrix");
  return j;
}

}  // namespace

json timeline_json(const Timeline& t) {
  json segs = json::array();
  for (const auto& s : t.segments) {
    json j = {{"kind", s.kind}, {"start", s.start}, {"end", s.end},
              {"drive_since_break", s.drive_since_break}, {"work_since_break", s.work_since_break}};
    if (!s.order_id.empty()) j["order_id"] = s.order_id;
    segs.push_back(std::move(j));
  }
  return {{"segments", segs}, {"duty_start", t.duty_start}, {"duty_end", t.duty_end}, {"driving", t.driving},
          {"work", t.work}, {"breaks", t.breaks}, {"km", t.km}, {"pallets", t.pallets}};
}

DayInstance judged_day(const DayInstance& planned, const WeekInstance& real) {
  DayInstance d = planned;
  d.rules = real.rules;
  d.customers = real.customers;
  d.contracts = real.contracts;
  for (auto& drv : d.drivers) {
    const auto it = std::ranges::find(real.drivers, drv.id, &Driver::id);
    if (it != real.drivers.end()) drv = *it;
  }
  for (auto& o : d.orders) {  // the real planning durations (the truth replaces them in sim)
    const auto it = std::ranges::find(real.orders, o.id, &Order::id);
    if (it != real.orders.end()) o.service_mu = it->service_mu;
  }
  return d;
}

json run_bundle(const RunData& run) {
  if (!run.week || !run.days || !run.plans || !run.day_checks || !run.week_check || !run.kpis) {
    throw std::invalid_argument("viz::run_bundle: incomplete RunData");
  }
  const WeekInstance& week = *run.week;
  const WeekInstance& real = run.real_week ? *run.real_week : week;
  const auto& days = *run.days;
  const auto& plans = *run.plans;
  if (days.size() != plans.size() || run.day_checks->size() != plans.size()) {
    throw std::invalid_argument("viz::run_bundle: days, plans and checks differ in number");
  }

  json out;
  out["schema"] = kBundleSchema;
  out["run"] = run.run;
  out["solver"] = run.solver;
  out["options"] = run.options;
  out["instance"] = to_json_instance(real);

  json jdays = json::array();
  std::vector<DayInstance> real_days;
  std::vector<DayPlan> real_plans;
  for (std::size_t d = 0; d < plans.size(); ++d) {
    const DayInstance& day = days[d];
    const DayPlan& plan = plans[d];
    json jd;
    jd["day"] = day.day;
    json orders = json::array();
    for (const auto& o : day.orders) {
      json jo = {{"id", o.id}, {"customer_id", o.customer_id}, {"pallets", o.pallets},
                 {"service_planned", o.service_mu}, {"penalty", o.postpone_penalty}};
      if (o.postponed_from) jo["postponed_from"] = *o.postponed_from;
      if (run.truth) {
        if (const auto it = run.truth->minutes.find(o.id); it != run.truth->minutes.end()) jo["service_true"] = it->second;
      }
      const auto ro = std::ranges::find(real.orders, o.id, &Order::id);
      if (ro != real.orders.end()) jo["service_mu"] = ro->service_mu;
      orders.push_back(std::move(jo));
    }
    jd["orders"] = orders;
    json drivers = json::array();
    for (const auto& drv : day.drivers) drivers.push_back(drv.id);
    jd["drivers"] = drivers;
    jd["states"] = day.states;
    json routes = json::array();
    for (const auto& r : plan.routes) {
      if (!r.order_ids.empty()) routes.push_back(to_json_route(day, r));
    }
    jd["routes"] = routes;
    jd["postponed"] = plan.postponed_order_ids;
    jd["check"] = to_json_check((*run.day_checks)[d]);
    jd["solver"] = to_json_solver(plan.solver_stats);

    if (run.truth) {
      const DayInstance judge = judged_day(day, real);
      auto rd = sim::realise_day(judge, plan, run.truth->minutes);
      json rroutes = json::array();
      for (std::size_t i = 0; i < rd.plan.routes.size(); ++i) {
        const Route& r = rd.plan.routes[i];
        if (r.order_ids.empty()) continue;
        const auto& o = rd.duties[i];
        json jr = to_json_route(rd.day, r);
        jr["outcome"] = {{"late", o.late}, {"illegal", o.illegal}, {"late_stops", o.late_stops},
                         {"rules", o.rules}, {"max_delay", o.max_delay}, {"overrun", o.overrun}};
        rroutes.push_back(std::move(jr));
      }
      jd["realised"] = {{"routes", rroutes}, {"check", to_json_check(rd.check)}};
      real_days.push_back(std::move(rd.day));
      real_plans.push_back(std::move(rd.plan));
    }
    jdays.push_back(std::move(jd));
  }
  out["days"] = jdays;
  out["kpis"] = kpi::to_json(*run.kpis);
  out["week_check"] = {{"ok", run.week_check->ok()},
                       {"violations", run.week_check->violations},
                       {"unserved_order_ids", run.week_check->unserved_order_ids}};

  if (run.truth) {  // the realised week, judged in week mode on the real instance (as legalvrp-robustness)
    WeekInstance real_week = real;
    for (auto& o : real_week.orders) {
      if (const auto it = run.truth->minutes.find(o.id); it != run.truth->minutes.end()) o.service_mu = it->second;
    }
    const auto rc = check::check_week(real_week, real_plans);
    const auto rk = kpi::compute_week_kpis(real_week, real_days, real_plans, rc);
    out["realised_kpis"] = kpi::to_json(rk);
  }
  return out;
}

}  // namespace legalvrp::viz
