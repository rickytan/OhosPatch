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

test('native timer bridge uses the host event loop', async () => {
  const [source, cmake] = await Promise.all([
    readFile(cppUrl, 'utf8'),
    readFile(cmakeUrl, 'utf8')
  ]);

  assert.match(source, /napi_get_uv_event_loop/);
  assert.match(source, /__ohospatch_scheduleTimer/);
  assert.match(source, /__ohospatch_cancelTimer/);
  assert.match(source, /OH_JSVM_PerformMicrotaskCheckpoint/);
  assert.match(cmake, /libuv\.so/);
});

test('JSVM calls own handle scopes and native callbacks have stable storage', async () => {
  const source = await readFile(cppUrl, 'utf8');

  assert.match(source, /OH_JSVM_OpenHandleScope\(env_, &scope\).*patch install/s);
  assert.match(source, /OH_JSVM_OpenHandleScope\(env_, &scope\).*method patch/s);
  assert.match(source, /OH_JSVM_OpenHandleScope\(env_, &scope\).*clear registry/s);
  assert.match(source, /static JSVM_CallbackStruct originCallback/);
  assert.match(source, /static JSVM_CallbackStruct proxyGetCallback/);
  assert.match(source, /static JSVM_CallbackStruct proxySetCallback/);
  assert.match(source, /static JSVM_CallbackStruct proxyCallCallback/);
  assert.match(source, /static JSVM_CallbackStruct scheduleTimerCallback/);
});

test('native proxy bridge resolves live ArkTS values without target snapshot writeback', async () => {
  const source = await readFile(cppUrl, 'utf8');

  assert.match(source, /__ohospatch_proxyGet/);
  assert.match(source, /__ohospatch_proxySet/);
  assert.match(source, /__ohospatch_proxyCall/);
  assert.match(source, /ResolveProxyWireValue/);
  assert.match(source, /proxyValues\[0\] = receiver/);
  assert.doesNotMatch(source, /targetJson/);
});

test('native component adapter covers values, node builders, attributes, and events', async () => {
  const source = await readFile(cppUrl, 'utf8');

  assert.match(source, /__ohospatch_uiSpecs/);
  assert.match(source, /setInitiallyProvidedValue/);
  assert.match(source, /updateStateVars/);
  assert.match(source, /initialRender/);
  assert.match(source, /observeComponentCreation2/);
  assert.match(source, /PrepareUiEventCaptures/);
  assert.match(source, /ApplyUiAttributes/);
  assert.match(source, /__ohospatch_callUiEvent/);
});
