import { defineConfig, loadEnv } from "vite";
import preact from "@preact/preset-vite";
import { compression } from "vite-plugin-compression2";
import path from "path";
import { mockPlugin } from "./mock/plugin.ts";

// Every route the SPA uses. It is deliberately short: AGENTS.md criterion 4 forbids the
// UI from having a path the same API does not expose to everyone else.
const proxyRoutes = ["/api", "/ws"];  // every path the firmware serves, and no other

// Post-build: replace type="module" with plain script tag for IIFE compatibility
function fixScriptType() {
  return {
    name: "fix-script-type",
    enforce: "post" as const,
    generateBundle(_: unknown, bundle: Record<string, { type: string; source?: string }>) {
      for (const file of Object.values(bundle)) {
        if (file.type === "asset" && typeof file.source === "string" && file.source.includes("type=\"module\"")) {
          file.source = file.source
            .replace(' type="module" crossorigin', " defer")
            .replace(' crossorigin', "");
        }
      }
    },
  };
}

export default defineConfig(({ mode }) => {
  // Device IP the dev server proxies /api and /ws to — set VITE_DEVICE_IP in web/.env.local.
  // loadEnv is REQUIRED: Vite does not copy .env files into process.env at config-eval time,
  // so reading process.env.VITE_DEVICE_IP alone silently ignored .env.local and fell back to
  // the default. The last arg "" loads all vars, not only the VITE_-prefixed client ones.
  const env = loadEnv(mode, process.cwd(), "");
  const deviceIP = env.VITE_DEVICE_IP || "192.168.50.92";
  const target = `http://${deviceIP}`;
  // `npm run dev:mock`: the whole device API is served in-process by mock/plugin.ts, so the UI
  // runs with no device on the LAN. The plugin is added ONLY in this mode (and is apply:"serve"),
  // so `vite build` never runs it and the production bundle cannot change because of it.
  const mocking = mode === "mock";

  return {
  // A dependency cache of its own in mock mode. The mode is part of Vite's cache hash, so sharing
  // node_modules/.vite would make a mock server and a device server running side by side
  // re-optimise over each other's files on every start.
  ...(mocking ? { cacheDir: "node_modules/.vite-mock" } : {}),
  plugins: [
    preact(),
    fixScriptType(),
    compression({
      algorithms: ["gzip"],
      include: /\.(js|css|html)$/,
      deleteOriginalAssets: true,
    }),
    ...(mocking ? [mockPlugin()] : []),
  ],
  build: {
    // Consumed by the firmware build through EMBED_FILES; see main/CMakeLists.txt.
    outDir: path.resolve(__dirname, "dist"),
    emptyOutDir: true,
    target: "es2018",
    modulePreload: false,
    cssCodeSplit: false,
    assetsInlineLimit: 4096,
    rollupOptions: {
      output: {
        manualChunks: undefined,
        format: "iife",
        entryFileNames: "app.js",
        assetFileNames: "[name][extname]",
      },
    },
  },
  server: mocking
    ? {} // the mock answers /api, /ws and POST /update itself; DO NOT add the proxy here too
    : {
        // `/ws` MUST enable the WebSocket upgrade (`ws: true`) or the push socket that carries the
        // live device state never connects and the dashboard sits on "Connecting…". changeOrigin
        // rewrites the Host header so the firmware sees a request for itself, not for the dev
        // server.
        proxy: Object.fromEntries(
          proxyRoutes.map((r) => [r, { target, changeOrigin: true, ws: r === "/ws" }]),
        ),
      },
  };
});
