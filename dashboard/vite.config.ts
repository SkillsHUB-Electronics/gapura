import { readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';
import { defineConfig, type Plugin } from 'vite';

// PAGES=1 builds the public copy (GitHub Pages): plain files, relative paths.
const pages = process.env.PAGES === '1';
const outDir = fileURLToPath(new URL(pages ? './dist-pages' : '../firmware/data', import.meta.url));

// LittleFS is small (190 KB): gzip every file and drop the original.
// ESPAsyncWebServer serves `x.gz` for a request to `x` automatically.
function gzipOutput(): Plugin {
  return {
    name: 'gzip-output',
    apply: 'build',
    closeBundle() {
      const walk = (dir: string) => {
        for (const name of readdirSync(dir)) {
          const path = join(dir, name);
          if (statSync(path).isDirectory()) walk(path);
          else if (!name.endsWith('.gz')) {
            writeFileSync(`${path}.gz`, gzipSync(readFileSync(path), { level: 9 }));
            rmSync(path);
          }
        }
      };
      walk(outDir);
    },
  };
}

export default defineConfig({
  base: pages ? './' : '/',
  plugins: pages ? [] : [gzipOutput()],
  build: {
    outDir,
    emptyOutDir: true,
    assetsDir: '',
    modulePreload: false,
    rollupOptions: {
      // LittleFS names are limited to 31 characters: no hashes.
      output: { entryFileNames: 'app.js', assetFileNames: 'app[extname]' },
    },
  },
});
