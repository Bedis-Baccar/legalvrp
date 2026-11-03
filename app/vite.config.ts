// Dev server: the app on http://localhost:5173 and the bundles of scripts/build_demo.py under
// /data (results/demo). The production build (dist/) is served the same way by scripts/demo_server.py.
import { defineConfig, type Plugin } from "vite";
import react from "@vitejs/plugin-react";
import { createReadStream, existsSync, statSync } from "node:fs";
import { resolve, normalize, sep, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = dirname(fileURLToPath(import.meta.url));
const DATA = resolve(ROOT, "..", "results", "demo");

function serveDemoData(): Plugin {
  return {
    name: "legalvrp-demo-data",
    configureServer(server) {
      server.middlewares.use("/data", (req, res, next) => {
        const rel = decodeURIComponent((req.url ?? "/").split("?")[0]);
        const file = normalize(resolve(DATA, "." + rel));
        if (!file.startsWith(DATA + sep) || !existsSync(file) || !statSync(file).isFile()) return next();
        res.setHeader("Content-Type", "application/json; charset=utf-8");
        res.setHeader("Cache-Control", "no-store");
        createReadStream(file).pipe(res);
      });
    },
  };
}

export default defineConfig({
  plugins: [react(), serveDemoData()],
  base: "./",
  server: { port: 5173 },
  build: {
    outDir: "dist",
    chunkSizeWarningLimit: 1000,
    rollupOptions: { input: { demo: resolve(ROOT, "index.html"), client: resolve(ROOT, "client.html") } },
  },
});
