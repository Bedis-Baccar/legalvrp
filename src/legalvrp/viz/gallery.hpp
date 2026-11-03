#pragma once
// legalvrp::viz — the checker's gallery (docs/APP_PLAN.md, C2-C4 and C12 without a server).
//
// The labelled duty corpus (tests/fixtures/duties.json: one driver, hand-built legs, departure,
// service starts and breaks, the expected broken rules) becomes one gallery item per duty: the
// duty's timeline, the checker's violations with their amounts, and whether they match the label.
// The duty is built as tests/unit/test_checker_corpus.cpp builds it (legs along the route, 999
// elsewhere, km = minutes, consistent arrivals unless given); test_viz checks that every item's
// verdict equals its label, so the application shows exactly what the tests prove.

#include <utility>

#include <nlohmann/json.hpp>

#include "legalvrp/domain/models.hpp"
#include "legalvrp/domain/rules.hpp"

namespace legalvrp::viz {

// One corpus duty -> (day with one driver "k1" and one truck, plan with its route).
[[nodiscard]] std::pair<DayInstance, DayPlan> corpus_duty(const nlohmann::json& duty, const nlohmann::json& defaults,
                                                          const Config& config);

// {"schema", "description", "items": [{"name", "expect", "got", "matches", "violations",
//   "stops", "legs", "route", "timeline", "state", "shift"}]}
[[nodiscard]] nlohmann::json duty_gallery(const nlohmann::json& corpus, const Config& config);

}  // namespace legalvrp::viz
