#pragma once
// legalvrp::domain — JSON (de)serialisation of the domain records (nlohmann::json, ADL).
//
// Reading is strict: missing keys throw nlohmann::json::out_of_range naming the key.
// Optional fields (lat/lon, postponed_from, break_after_order_id, last_duty_end) are
// omitted when empty. WeekInstance excludes the matrix (stored in matrix.json).

#include <nlohmann/json.hpp>

#include <string>

#include "legalvrp/domain/models.hpp"

namespace legalvrp {

[[nodiscard]] std::string to_string(CustomerType t);
[[nodiscard]] CustomerType customer_type_from_string(const std::string& s);

void to_json(nlohmann::json& j, const Customer& x);
void from_json(const nlohmann::json& j, Customer& x);
void to_json(nlohmann::json& j, const Order& x);
void from_json(const nlohmann::json& j, Order& x);
void to_json(nlohmann::json& j, const Truck& x);
void from_json(const nlohmann::json& j, Truck& x);
void to_json(nlohmann::json& j, const Driver& x);
void from_json(const nlohmann::json& j, Driver& x);
void to_json(nlohmann::json& j, const DriverWeekState& x);
void from_json(const nlohmann::json& j, DriverWeekState& x);
void to_json(nlohmann::json& j, const Depot& x);
void from_json(const nlohmann::json& j, Depot& x);
void to_json(nlohmann::json& j, const Matrix& x);    // rows: {"node_ids", "time_min", "dist_km"}
void from_json(const nlohmann::json& j, Matrix& x);
void to_json(nlohmann::json& j, const Rules& x);
void from_json(const nlohmann::json& j, Rules& x);
void to_json(nlohmann::json& j, const Contract& x);
void from_json(const nlohmann::json& j, Contract& x);
void to_json(nlohmann::json& j, const Costs& x);
void from_json(const nlohmann::json& j, Costs& x);
void to_json(nlohmann::json& j, const WeekInstance& x);
void from_json(const nlohmann::json& j, WeekInstance& x);
void to_json(nlohmann::json& j, const Route& x);
void from_json(const nlohmann::json& j, Route& x);
void to_json(nlohmann::json& j, const SolverStats& x);
void from_json(const nlohmann::json& j, SolverStats& x);
void to_json(nlohmann::json& j, const DayPlan& x);
void from_json(const nlohmann::json& j, DayPlan& x);

// Canonical text: 2-space indent, sorted keys, LF, trailing newline. Byte-stable across platforms.
[[nodiscard]] std::string to_canonical_text(const nlohmann::json& j);

}  // namespace legalvrp
