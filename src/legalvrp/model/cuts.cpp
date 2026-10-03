#include "legalvrp/model/cuts.hpp"

#include <algorithm>
#include <memory>
#include <queue>

namespace legalvrp::model {

namespace {

// Edmonds-Karp on a dense capacity matrix. Returns the max-flow value and, in `sink_side`,
// the nodes NOT reachable from the source in the final residual graph.
double max_flow(std::vector<std::vector<double>> cap, int source, int sink, std::vector<bool>& sink_side) {
  const auto V = cap.size();
  double flow = 0.0;
  std::vector<int> parent(V);
  for (;;) {
    std::fill(parent.begin(), parent.end(), -1);
    parent[static_cast<std::size_t>(source)] = source;
    std::queue<int> q;
    q.push(source);
    while (!q.empty() && parent[static_cast<std::size_t>(sink)] < 0) {
      const int a = q.front();
      q.pop();
      for (std::size_t b = 0; b < V; ++b) {
        if (parent[b] < 0 && cap[static_cast<std::size_t>(a)][b] > 1e-9) {
          parent[b] = a;
          q.push(static_cast<int>(b));
        }
      }
    }
    if (parent[static_cast<std::size_t>(sink)] < 0) break;
    double push = 1e18;
    for (int v = sink; v != source; v = parent[static_cast<std::size_t>(v)]) {
      push = std::min(push, cap[static_cast<std::size_t>(parent[static_cast<std::size_t>(v)])][static_cast<std::size_t>(v)]);
    }
    for (int v = sink; v != source; v = parent[static_cast<std::size_t>(v)]) {
      const auto u = static_cast<std::size_t>(parent[static_cast<std::size_t>(v)]);
      cap[u][static_cast<std::size_t>(v)] -= push;
      cap[static_cast<std::size_t>(v)][u] += push;
    }
    flow += push;
  }
  sink_side.assign(V, true);
  for (std::size_t v = 0; v < V; ++v) sink_side[v] = parent[v] < 0;
  return flow;
}

}  // namespace

ConnectivityCuts::ConnectivityCuts(int n, int K, std::vector<CutArc> arcs, bool per_driver, double per_driver_nodes)
    : n_(n), K_(K), per_driver_(per_driver), per_driver_nodes_(per_driver_nodes), arcs_(std::move(arcs)) {
  for (const auto& a : arcs_) vars_.push_back(a.var);
}

void ConnectivityCuts::callback() {
  if (where != GRB_CB_MIPNODE || getIntInfo(GRB_CB_MIPNODE_STATUS) != GRB_OPTIMAL) return;
  const std::unique_ptr<double[]> val(getNodeRel(vars_.data(), static_cast<int>(vars_.size())));
  const auto V = static_cast<std::size_t>(n_) + 1;  // customers + depot (index n)
  const bool per_driver = per_driver_ && getDoubleInfo(GRB_CB_MIPNODE_NODCNT) < per_driver_nodes_;
  const int groups = per_driver ? K_ + 1 : 1;       // group 0 = aggregated, g = driver g-1

  std::vector<bool> in_s;
  for (int g = 0; g < groups; ++g) {
    std::vector<std::vector<double>> cap(V, std::vector<double>(V, 0.0));
    std::vector<double> visit(static_cast<std::size_t>(n_), 0.0);
    for (std::size_t a = 0; a < arcs_.size(); ++a) {
      if (g > 0 && arcs_[a].k != g - 1) continue;
      const auto f = static_cast<std::size_t>(arcs_[a].from);
      const auto t = static_cast<std::size_t>(arcs_[a].to);
      cap[f][t] += val[a];
      visit[t] += val[a];
    }
    for (int i = 0; i < n_; ++i) {
      const double v = visit[static_cast<std::size_t>(i)];
      if (v < 0.1) continue;
      const double flow = max_flow(cap, n_, i, in_s);
      if (flow >= v - 0.05) continue;
      std::string key(V, '0');
      for (std::size_t s = 0; s < V; ++s) key[s] = in_s[s] ? '1' : '0';
      key += ":" + std::to_string(i) + ":" + std::to_string(g);
      if (!seen_.insert(key).second) continue;
      GRBLinExpr inflow = 0, visit_i = 0;
      for (const auto& a : arcs_) {
        if (g > 0 && a.k != g - 1) continue;
        const bool from_in = a.from < n_ && in_s[static_cast<std::size_t>(a.from)];
        const bool to_in = in_s[static_cast<std::size_t>(a.to)];
        if (!from_in && to_in) inflow += a.var;
        if (a.to == i) visit_i += a.var;
      }
      addCut(inflow >= visit_i);
      ++cuts_;
    }
  }
}

}  // namespace legalvrp::model
