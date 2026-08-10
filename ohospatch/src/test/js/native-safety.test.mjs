import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

const cppUrl = new URL('../../main/cpp/ohospatch.cpp', import.meta.url);
const cmakeUrl = new URL('../../main/cpp/CMakeLists.txt', import.meta.url);

test('native layer is built without C++ exceptions', async () => {
  const [source, cmake] = await Promise.all([
    readFile(cppUrl, 'utf8'),
    readFile(cmakeUrl, 'utf8')
  ]);

  assert.doesNotMatch(source, /\bthrow\b/);
  assert.doesNotMatch(source, /\bcatch\b/);
  assert.doesNotMatch(source, /std::(?:exception|runtime_error)/);
  assert.doesNotMatch(source, /(?:napi_throw|OH_JSVM_Throw)/);
  assert.match(source, /OH_LOG_Print\(LOG_APP, LOG_ERROR/);
  assert.match(cmake, /target_compile_options\(ohospatch PRIVATE -fno-exceptions\)/);
});
