import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { minifyFixitRuntime } from './minifier.mjs';

const buildRoot = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(buildRoot, '../..');

function readArg(name) {
  const index = process.argv.indexOf(name);
  if (index === -1 || index + 1 >= process.argv.length) {
    return '';
  }
  return process.argv[index + 1];
}

function projectPath(path) {
  return resolve(projectRoot, path);
}

export async function fixitRuntime() {
  const input = readArg('--input') ||
    'ohospatch/src/main/cpp/runtime/fixit.js';
  const output = readArg('--output') ||
    'ohospatch/src/main/resources/rawfile/ohospatch/fixit.min.js';

  await minifyFixitRuntime(projectPath(input), projectPath(output));
}

export default fixitRuntime;
