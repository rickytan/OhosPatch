import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname } from 'node:path';
import { minify } from 'terser';

export async function minifyFixitRuntime(inputPath, outputPath) {
  if (!inputPath || !outputPath) {
    throw new Error('minifyFixitRuntime requires inputPath and outputPath');
  }

  const source = await readFile(inputPath, 'utf8');
  const result = await minify(source, {
    compress: {
      passes: 2
    },
    mangle: {
      toplevel: false
    },
    format: {
      ascii_only: true,
      comments: false
    }
  });

  if (!result.code) {
    throw new Error('Terser produced empty output');
  }

  await mkdir(dirname(outputPath), { recursive: true });
  await writeFile(outputPath, `${result.code}\n`);
}
