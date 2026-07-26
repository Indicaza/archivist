import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";

const ideHostRoot = fileURLToPath(
  new URL(".", import.meta.url),
);
const ideDistributionPath = fileURLToPath(
  new URL("../../../../../dist/ide", import.meta.url),
);

export default defineConfig({
  root: ideHostRoot,
  base: "/ide/",
  build: {
    target: "es2022",
    outDir: ideDistributionPath,
    emptyOutDir: true,
    minify: false,
    sourcemap: false,
    reportCompressedSize: false,
    cssCodeSplit: false,
    rollupOptions: {
      output: {
        manualChunks(moduleId) {
          if (moduleId.includes("monaco-editor")) {
            return "monaco";
          }

          if (moduleId.includes("@xterm")) {
            return "terminal";
          }

          return undefined;
        },
      },
    },
  },
});
