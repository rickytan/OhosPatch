import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const declarationUrl = new URL('../../../../fixit.d.js', import.meta.url);
const runtimeUrl = new URL('../../main/cpp/runtime/fixit.js', import.meta.url);

test('Patch context declaration is valid JavaScript and covers the public API', async () => {
  const declaration = await readFile(declarationUrl, 'utf8');
  new vm.Script(declaration, { filename: 'fixit.d.js' });

  for (const symbol of [
    'class Fixit',
    'static runtimeVersion',
    'static fix',
    'static component',
    'static import',
    'static registerTarget',
    'instanceMethod',
    'classMethod',
    'OhosPatchTarget',
    'OhosPatchMethodHandler',
    'OhosPatchOriginalMethod',
    'OhosPatchImportedClass',
    'function require',
    'var nil',
    'var Nil',
    'function isNil',
    'function nilToNull',
    'function nullToNil',
    'function setTimeout',
    'function clearTimeout',
    'function setInterval',
    'function clearInterval',
    'function setImmediate',
    'function clearImmediate',
    'function queueMicrotask',
    'OhosPatchConsole',
    'OhosPatchComponentFix',
    'OhosPatchComponentValueFix',
    'OhosPatchComponentNodeFix',
    'OhosPatchComponentEventRule',
    'OhosPatchComponentEventContext',
    'OhosPatchOriginalEvent'
  ]) {
    assert.ok(declaration.includes(symbol), `missing declaration: ${symbol}`);
  }
  assert.doesNotMatch(declaration, /\b(?:YES|NO)\b/);
  assert.doesNotMatch(declaration, /__ohospatch_/);
});

test('Patch context declaration version matches the embedded runtime', async () => {
  const [declaration, runtime] = await Promise.all([
    readFile(declarationUrl, 'utf8'),
    readFile(runtimeUrl, 'utf8')
  ]);
  const declarationVersion = declaration.match(/@version\s+([0-9.]+)/)?.[1];
  const runtimeVersion = runtime.match(/runtimeVersion'[\s\S]*?value:\s*'([0-9.]+)'/)?.[1];

  assert.equal(declarationVersion, runtimeVersion);
});
