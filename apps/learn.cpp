// Offline training of the service-time estimator — V1-T7.
//   legalvrp-learn --config config/instance_small.yaml [--weeks 20] [--first-seed 101]
//                  [--holdout 5] [--out results/estimators]
// History = the true minutes of `weeks` generated weeks (seeds first-seed, first-seed + 1, ...),
// disjoint from the evaluation seeds (1-5). Fits estimate::fit and writes <out>/<name>.json
// (parameters + exported table) and <name>.md: learned vs generator parameters, and the
// calibration on `holdout` further weeks (share of observations <= mu + z sigma, <= quantiles;
// mean absolute error of the planning rule and of the learned mean).
#include <CLI/CLI.hpp>

#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/orders.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/estimate/learned.hpp"

namespace fs = std::filesystem;
using namespace legalvrp;

namespace {
std::string fx(double v, int p = 2) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(p) << v;
  return os.str();
}
}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Train the service-time estimator on generated history (V1-T7)"};
  fs::path config, out = results_dir() / "estimators";
  int weeks = 20, holdout = 5;
  std::uint64_t first_seed = 101;
  app.add_option("--config", config, "instance config")->required()->check(CLI::ExistingFile);
  app.add_option("--weeks", weeks, "history weeks");
  app.add_option("--first-seed", first_seed, "first history seed (keep away from evaluation seeds)");
  app.add_option("--holdout", holdout, "further weeks for calibration");
  app.add_option("--out", out, "output directory");
  CLI11_PARSE(app, argc, argv);

  try {
    const auto ic = data::load_instance_config(config);
    const Config rules = load_config();
    auto history = [&](std::uint64_t from, int n) {
      std::vector<estimate::Observation> obs;
      for (int k = 0; k < n; ++k) {
        const auto g = data::generate_week(ic, rules, from + static_cast<std::uint64_t>(k));
        for (const auto& o : estimate::observations(g.week, g.truth.minutes)) obs.push_back(o);
      }
      return obs;
    };
    const auto train = history(first_seed, weeks);
    const auto test = history(first_seed + static_cast<std::uint64_t>(weeks), holdout);
    estimate::LearnedModel model = estimate::fit(train);
    model.source = config.filename().string() + ", history seeds " + std::to_string(first_seed) + "-" +
                   std::to_string(first_seed + static_cast<std::uint64_t>(weeks) - 1) + " (" +
                   std::to_string(train.size()) + " stops)";
    Pallets largest = 1;
    for (const auto& t : ic.trucks) largest = std::max(largest, t.capacity_pallets);
    model.max_pallets = largest;

    fs::create_directories(out);
    const std::string name = config.stem().string();
    data::write_text_file(out / (name + ".json"), to_canonical_text(estimate::to_json(model)));

    std::ostringstream md;
    md << "# Learned service-time estimator, `" << config.filename().string() << "`\n\n"
       << "Training: " << model.source << ". Hold-out: " << holdout << " further weeks (" << test.size()
       << " stops).\n\n| type | n | fixed (learned / true) | per pallet (learned / true) | cv (learned / true) |\n"
          "|---|---|---|---|---|\n";
    for (const auto& m : model.types) {
      const auto& tp = *std::ranges::find(ic.customer_types, m.type, &data::CustomerTypeParams::type);
      md << "| " << to_string(m.type) << " | " << m.n << " | " << fx(m.fixed) << " / " << fx(tp.service_fixed) << " | "
         << fx(m.per_pallet) << " / " << fx(tp.service_per_pallet) << " | " << fx(m.cv, 3) << " / "
         << fx(tp.service_cv, 3) << " |\n";
    }
    // Calibration on the hold-out weeks.
    const estimate::LearnedEstimator est(model);
    auto model_of = [&](CustomerType t) { return &*std::ranges::find(model.types, t, &estimate::TypeModel::type); };
    md << "\n**Calibration (hold-out)**: share of stops whose true minutes are <= the planned minutes\n\n"
          "| planned minutes | share covered | mean planned (min) |\n|---|---|---|\n";
    double mae_rule = 0, mae_learned = 0;
    for (const auto& o : test) {
      mae_rule += std::abs(static_cast<double>(o.minutes) - static_cast<double>(data::service_minutes(o.pallets)));
      mae_learned += std::abs(static_cast<double>(o.minutes) - model_of(o.type)->mu(o.pallets));
    }
    {
      int covered = 0;
      double planned = 0;
      for (const auto& o : test) {
        covered += o.minutes <= data::service_minutes(o.pallets) ? 1 : 0;
        planned += static_cast<double>(data::service_minutes(o.pallets));
      }
      md << "| planning rule 10 + 6 x pallets (V0) | " << fx(100.0 * covered / static_cast<double>(test.size()), 1)
         << " % | " << fx(planned / static_cast<double>(test.size()), 1) << " |\n";
    }
    for (const double z : {0.0, 0.5, 1.0, 1.5, 2.0}) {
      int covered = 0;
      double planned = 0;
      for (const auto& o : test) {
        const auto* m = model_of(o.type);
        const double s = std::floor(m->mu(o.pallets) + z * m->sigma(o.pallets) + 0.5);
        covered += static_cast<double>(o.minutes) <= s ? 1 : 0;
        planned += s;
      }
      md << "| learned mu + " << fx(z, 1) << " sigma | " << fx(100.0 * covered / static_cast<double>(test.size()), 1)
         << " % | " << fx(planned / static_cast<double>(test.size()), 1) << " |\n";
    }
    for (std::size_t qi = 0; qi < model.types.front().ratio_quantiles.size(); ++qi) {
      int covered = 0;
      for (const auto& o : test) {
        const auto* m = model_of(o.type);
        covered += static_cast<double>(o.minutes) <= m->mu(o.pallets) * m->ratio_quantiles[qi].second ? 1 : 0;
      }
      md << "| learned empirical quantile " << fx(model.types.front().ratio_quantiles[qi].first, 2) << " | "
         << fx(100.0 * covered / static_cast<double>(test.size()), 1) << " % | |\n";
    }
    md << "\nMean absolute error per stop (hold-out): planning rule " << fx(mae_rule / static_cast<double>(test.size()), 1)
       << " min, learned mean " << fx(mae_learned / static_cast<double>(test.size()), 1) << " min.\n";
    data::write_text_file(out / (name + ".md"), md.str());
    std::cout << md.str() << "written: " << (out / (name + ".json")).string() << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-learn: " << e.what() << "\n";
    return 2;
  }
}
