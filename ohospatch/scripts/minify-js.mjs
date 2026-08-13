import { minifyFixitRuntime } from '../../tools/fixit-runtime-build/minifier.mjs';

const [inputPath, outputPath] = process.argv.slice(2);

if (!inputPath || !outputPath) {
  console.error('Usage: node minify-js.mjs <input.js> <output.js>');
  process.exit(2);
}

try {
  await minifyFixitRuntime(inputPath, outputPath);
} catch (error) {
  console.error(error && error.message ? error.message : error);
  process.exit(1);
}
