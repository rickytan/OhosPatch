import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const runtimeUrl = new URL('../../main/cpp/runtime/fixit.js', import.meta.url);
const runtimeSource = await readFile(runtimeUrl, 'utf8');

function createRuntime() {
  const logs = [];
  const origins = [];
  const scheduledTimers = [];
  const cancelledTimers = [];
  const context = vm.createContext({
    __ohospatch_hilog(level, message) {
      logs.push({ level, message });
    },
    __ohospatch_origin(...args) {
      origins.push({ receiver: this, args });
      return 'origin-result';
    },
    __ohospatch_scheduleTimer(id, delay, repeating) {
      scheduledTimers.push({ id, delay, repeating });
      return true;
    },
    __ohospatch_cancelTimer(id) {
      cancelledTimers.push(id);
      return true;
    }
  });
  vm.runInContext(runtimeSource, context, { filename: 'fixit.js' });
  return { context, logs, origins, scheduledTimers, cancelledTimers };
}

function plain(value) {
  return JSON.parse(JSON.stringify(value));
}

test('installs Fixit and common globals', () => {
  const { context, logs } = createRuntime();

  assert.equal(typeof context.Fixit, 'function');
  assert.equal(context.Fixit.runtimeVersion, '1.3.0');
  assert.equal(context.nil, null);
  assert.equal(context.Nil, null);
  assert.equal(context.YES, undefined);
  assert.equal(context.NO, undefined);
  assert.equal(context.isNil(null), true);
  assert.equal(context.isNil(undefined), true);
  assert.equal(context.isNil(0), false);
  assert.equal(context.nilToNull(undefined), null);
  assert.equal(context.nullToNil(null), null);
  context.console.warn('patch', { count: 2 });
  assert.deepEqual(logs, [{ level: 'warn', message: 'patch {"count":2}' }]);
});

test('provides timeout, interval, immediate, and microtask globals', async () => {
  const { context, scheduledTimers, cancelledTimers } = createRuntime();
  const calls = [];

  const timeoutId = context.setTimeout((...args) => calls.push(['timeout', ...args]), 12.9, 'a', 2);
  assert.deepEqual(scheduledTimers[0], { id: timeoutId, delay: 12, repeating: false });
  context.__ohospatch_fireTimer(timeoutId);
  context.__ohospatch_fireTimer(timeoutId);
  assert.deepEqual(calls, [['timeout', 'a', 2]]);

  const intervalId = context.setInterval(() => calls.push(['interval']), 0);
  assert.deepEqual(scheduledTimers[1], { id: intervalId, delay: 1, repeating: true });
  context.__ohospatch_fireTimer(intervalId);
  context.__ohospatch_fireTimer(intervalId);
  context.clearInterval(intervalId);
  assert.deepEqual(cancelledTimers, [intervalId]);
  assert.deepEqual(calls.slice(1), [['interval'], ['interval']]);

  const immediateId = context.setImmediate((value) => calls.push(['immediate', value]), 3);
  assert.deepEqual(scheduledTimers[2], { id: immediateId, delay: 0, repeating: false });
  context.clearImmediate(immediateId);
  assert.deepEqual(cancelledTimers, [intervalId, immediateId]);

  const staleId = context.setTimeout(() => calls.push(['stale']), 100);
  context.__ohospatch_clear();
  context.__ohospatch_fireTimer(staleId);
  assert.deepEqual(cancelledTimers, [intervalId, immediateId, staleId]);
  assert.equal(calls.some((call) => call[0] === 'stale'), false);

  context.queueMicrotask(() => calls.push(['microtask']));
  await Promise.resolve();
  assert.deepEqual(calls.at(-1), ['microtask']);
  assert.throws(() => context.setTimeout('not a function', 0), /must be a function/);
  assert.throws(() => context.queueMicrotask(null), /must be a function/);
});

test('require parses a full OHM source path into a target descriptor', () => {
  const { context } = createRuntime();

  assert.deepEqual(plain(context.require(
    'com.example.app/feature/src/main/ets/model/FeatureModel'
  )), {
    className: 'FeatureModel',
    modulePath: 'feature/src/main/ets/model/FeatureModel',
    moduleInfo: 'com.example.app/feature',
    exportName: 'FeatureModel',
    bundleName: 'com.example.app',
    moduleName: 'feature',
    packageName: 'feature'
  });
  assert.deepEqual(plain(context.require(
    '@bundle:com.example.app/entry/entry_api/src/main/ets/model/DefaultModel.ets#default'
  )), {
    className: 'DefaultModel',
    modulePath: 'entry_api/src/main/ets/model/DefaultModel',
    moduleInfo: 'com.example.app/entry',
    exportName: 'default',
    bundleName: 'com.example.app',
    moduleName: 'entry',
    packageName: 'entry_api'
  });

  assert.throws(() => context.require('entry/src/main/ets/Test'), /must use/);
  assert.throws(() => context.require('/com.example/entry/src/main/ets/Test'), /must use/);
  assert.throws(
    () => context.require('com.example/entry/src/main/ets/Test#invalid-name'),
    /export name is invalid/
  );
});

test('registers declarative component value, attribute, and event rules', () => {
  const { context } = createRuntime();
  const target = context.require(
    'com.example.app/entry/src/main/ets/components/DemoPanel#DemoPanel'
  );
  const component = context.Fixit.component(target);

  component.param('title').transform((value) => value || 'patched title');
  component.state('rows').replace([]);
  const button = component.node({ type: 'Button', occurrence: 0 });
  button.attrs({ height: 48, backgroundColor: '#1677FF' });
  button.event('onClick', {
    capture: ['title', 'rows'],
    handler(event, componentContext) {
      componentContext.setState({ title: `clicked:${event.source}` });
      return 'event handled';
    }
  });

  const specs = JSON.parse(context.__ohospatch_uiSpecs());
  assert.equal(specs.length, 5);
  assert.deepEqual(specs.map((spec) => spec.kind), [
    'param', 'state', 'attribute', 'attribute', 'event'
  ]);
  assert.deepEqual(specs[2].arguments, [48]);
  assert.equal(specs[4].nodeType, 'Button');
  assert.equal(specs[4].occurrence, 0);
  assert.deepEqual(specs[4].capture, ['title', 'rows']);

  assert.deepEqual(
    plain(context.__ohospatch_callUiValue(specs[0].ruleId, '')),
    { handled: true, value: 'patched title' }
  );
  assert.deepEqual(
    plain(context.__ohospatch_callUiValue(specs[1].ruleId, ['old'])),
    { handled: true, value: [] }
  );
  assert.deepEqual(
    plain(context.__ohospatch_callUiEvent(
      specs[4].ruleId,
      { source: 'button' },
      { title: 'before', rows: ['row'] }
    )),
    {
      handled: true,
      result: 'event handled',
      statePatch: { title: 'clicked:button' }
    }
  );

  assert.throws(() => button.attr('height', 52), /Duplicate component patch rule/);
  assert.throws(
    () => component.node('Button').event('onChange', { mode: 'around', handler() {} }),
    /Only replace/
  );
  assert.throws(() => component.node({ type: 'Text', occurrence: -1 }), /non-negative uint32 integer/);
  assert.throws(
    () => component.node('Text').event('onClick', { capture: Array(17).fill('value'), handler() {} }),
    /at most 16/
  );

  context.__ohospatch_clear();
  assert.deepEqual(JSON.parse(context.__ohospatch_uiSpecs()), []);
  assert.deepEqual(plain(context.__ohospatch_callUiValue(specs[0].ruleId, 'old')), { handled: false });
});

test('registers instance and class methods and invokes the original method', () => {
  const { context, origins } = createRuntime();
  const fix = context.Fixit.fix({
    className: 'DemoViewModel',
    modulePath: 'entry/src/main/ets/demo/DemoViewModel',
    moduleInfo: 'com.rickytan.ohospatch/entry',
    exportName: 'DemoViewModel'
  });

  let origin;
  origin = fix.instanceMethod('locationOf', function (items, index) {
    this.lastIndex = index;
    return index < items.length ? items[index] : origin.apply(this, arguments);
  });
  fix.classMethod('crash', function () {
    return 'fixed';
  });

  const specs = JSON.parse(context.__ohospatch_specs());
  assert.equal(specs.length, 2);
  assert.deepEqual(specs.map((spec) => spec.classMethod), [false, true]);

  const target = { lastIndex: -1 };
  const handled = context.__ohospatch_callPatch(
    specs[0].targetKey, 'locationOf', false, target, [['zero'], 0]
  );
  assert.deepEqual(plain(handled), {
    handled: true,
    result: 'zero',
    target: { lastIndex: 0 }
  });

  const fallback = context.__ohospatch_callPatch(
    specs[0].targetKey, 'locationOf', false, target, [[], 3]
  );
  assert.equal(fallback.result, 'origin-result');
  assert.equal(origins.length, 1);
  assert.deepEqual(origins[0].args, [[], 3]);

  const classResult = context.__ohospatch_callPatch(
    specs[1].targetKey, 'crash', true, {}, []
  );
  assert.equal(classResult.result, 'fixed');
});

test('keeps same-named classes from different modules isolated', () => {
  const { context } = createRuntime();
  const entryTarget = context.require(
    'com.example.app/entry/src/main/ets/model/ViewModel#ViewModel'
  );
  const featureTarget = context.require(
    'com.example.app/feature/src/main/ets/model/ViewModel#ViewModel'
  );
  context.Fixit.fix(entryTarget).instanceMethod('value', function () { return 'entry'; });
  context.Fixit.fix(featureTarget).instanceMethod('value', function () { return 'feature'; });

  const specs = JSON.parse(context.__ohospatch_specs());
  assert.notEqual(specs[0].targetKey, specs[1].targetKey);
  assert.equal(
    context.__ohospatch_callPatch(specs[0].targetKey, 'value', false, {}, []).result,
    'entry'
  );
  assert.equal(
    context.__ohospatch_callPatch(specs[1].targetKey, 'value', false, {}, []).result,
    'feature'
  );
});

test('supports class-name aliases and clears registrations', () => {
  const { context } = createRuntime();
  context.Fixit.registerTarget('DemoViewModel', {
    modulePath: 'entry/src/main/ets/demo/DemoViewModel',
    moduleInfo: 'com.rickytan.ohospatch/entry'
  });

  const fix = context.Fixit.fix('DemoViewModel');
  fix.instanceMethod('crashIt', function () {
    return 'fixed';
  });
  assert.equal(JSON.parse(context.__ohospatch_specs()).length, 1);
  assert.throws(
    () => fix.instanceMethod('crashIt', function () {}),
    /Duplicate patch/
  );

  context.__ohospatch_clear();
  assert.deepEqual(JSON.parse(context.__ohospatch_specs()), []);
  assert.throws(() => context.Fixit.fix('DemoViewModel'), /className and modulePath/);
});
