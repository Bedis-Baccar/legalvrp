# Documentation

| Document | Content |
|---|---|
| [PROJECT_BRIEF.md](PROJECT_BRIEF.md) | Original V0 specification: problem, rules, data model, tasks T0–T11 and acceptance criteria |
| [MODEL.md](MODEL.md) | The daily MILP (sets, variables, constraints C1–C18, big-M values) and its amendments |
| [DECISIONS.md](DECISIONS.md) | Dated log of every design and modelling decision, with reasons and verification |
| [REVIEW_V0.md](REVIEW_V0.md) | Review of the specification before implementation (findings F1–F9) |
| [FORMULATION_RESEARCH.md](FORMULATION_RESEARCH.md) | Literature on tighter formulations and what was adopted |
| [SIZING.md](SIZING.md) | Problem size, complexity expectations and data sources |
| [DATA.md](DATA.md) | Instance files, generator, using real data |
| [BENCHMARK_V0.md](BENCHMARK_V0.md) | MILP vs baseline on two instances; formulations; realism |
| [SCALING_V0.md](SCALING_V0.md) | Runtime and gap vs number of orders (8–40), with [data and figures](scaling/) |
| [MYOPIA_V0.md](MYOPIA_V0.md) | Rolling week vs clairvoyant weekly MILP, with [data](myopia/) |
| [V1_PLAN.md](V1_PLAN.md) | V1 plan: ALNS at scale, route-pool MIP, look-ahead week policies, uncertain service times, legal relaxations |
| [BENCHMARK_V1.md](BENCHMARK_V1.md) | V1: ALNS vs MILP vs route pool on 60 instances (8–100 orders), time to quality, with [data](compare/) |
| [POLICIES_V1.md](POLICIES_V1.md) | V1: week policies (look-ahead, scarce-skill reserve) vs myopic and clairvoyant; fairness cost-vs-Gini curve, with [data](policies/) |
| [ROBUSTNESS_V1.md](ROBUSTNESS_V1.md) | V1: plans executed with true (uncertain) service times: share of late or illegal duties per solver, bias vs noise, with [data](robustness/) |
| [ROBUST_V1.md](ROBUST_V1.md) | V1: service times learned from history, per-stop buffers vs a pooled time reserve, cost-vs-risk curves, with [data](robust/) |
| [validation/](validation/) | Model validation document (LaTeX + PDF) |
