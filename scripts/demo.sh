#!/usr/bin/env bash
# Build (if needed) and serve the visual application of legalvrp (docs/APP.md).
#   ./scripts/demo.sh             build what is missing, start the server on http://127.0.0.1:8765/
#   ./scripts/demo.sh --rebuild   recompute every scenario
# Environment: WITH_GUROBI=ON to build the C++ tools with Gurobi (GUROBI_HOME set), PORT=8765.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
rebuild=0
[[ "${1:-}" == "--rebuild" ]] && rebuild=1
py="${PYTHON:-python3}"
"$py" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)' || { echo "Python >= 3.11 required" >&2; exit 1; }

bin="$root/build/release/apps"
if [[ ! -x "$bin/legalvrp-export" ]]; then
  echo "== building the C++ tools (build/release, Gurobi=${WITH_GUROBI:-OFF})"
  cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEGALVRP_BUILD_TESTS=OFF \
        -DLEGALVRP_WITH_GUROBI="${WITH_GUROBI:-OFF}"
  cmake --build build/release
fi

if [[ $rebuild == 1 || ! -f results/demo/index.json ]]; then
  echo "== building the scenarios"
  "$py" scripts/build_demo.py --bin "$bin" $([[ $rebuild == 1 ]] && echo --force)
fi

if [[ ! -f app/dist/index.html ]]; then
  command -v npm >/dev/null || { echo "app/dist is missing and npm is not installed (Node.js >= 20)" >&2; exit 1; }
  echo "== building the web application"
  (cd app && npm install --no-fund --no-audit && npm run build)
fi

exec "$py" scripts/demo_server.py --port "${PORT:-8765}"
