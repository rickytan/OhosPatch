import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const runtimeUrl = new URL('../../main/cpp/runtime/fixit.js', import.meta.url);
const runtimeSource = await readFile(runtimeUrl, 'utf8');

function createRuntime() {
  const logs = [];
  const origins = [];
  const context = vm.createContext({
    __ohospatch_log(level, message) {
      logs.push({ level, message });
    },
    __ohospatch_origin(...args) {
      origins.push({ receiver: this, args });
      return 'origin-result';
    }
  });
  vm.runInContext(runtimeSource, context, { filename: 'fixit.js' });
  return { context, logs, origins };
}

function plain(value) {
  return JSON.parse(JSON.stringify(value));
}

test('installs Fixit and common globals', () => {
  const { context, logs } = createRuntime();

  assert.equal(typeof context.Fixit, 'function');
  assert.equal(context.Fixit.runtimeVersion, '1.0.0');
  assert.equal(context.nil, null);
  assert.equal(context.Nil, null);
  assert.equal(context.YES, true);
  assert.equal(context.NO, false);
  assert.equal(context.isNil(null), true);
  assert.equal(context.isNil(undefined), true);
  assert.equal(context.isNil(0), false);
  assert.equal(context.nilToNull(undefined), null);
  assert.equal(context.nullToNil(null), null);
  assert.deepEqual(plain(context.CGRectMake(1, 2, 3, 4)), {
    x: 1,
    y: 2,
    width: 3,
    height: 4
  });
  assert.deepEqual(plain(context.CGPointMake(1, 2)), { x: 1, y: 2 });
  assert.deepEqual(plain(context.CGSizeMake(3, 4)), { width: 3, height: 4 });
  assert.deepEqual(plain(context.NSMakeRange(5, 6)), { location: 5, length: 6 });

  context.console.warn('patch', { count: 2 });
  assert.deepEqual(logs, [{ level: 'warn', message: 'patch {"count":2}' }]);
  assert.throws(() => context.require('SomeClass'), /isolated JSVM/);
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
    'DemoViewModel', 'locationOf', false, target, [['zero'], 0]
  );
  assert.deepEqual(plain(handled), {
    handled: true,
    result: 'zero',
    target: { lastIndex: 0 }
  });

  const fallback = context.__ohospatch_callPatch(
    'DemoViewModel', 'locationOf', false, target, [[], 3]
  );
  assert.equal(fallback.result, 'origin-result');
  assert.equal(origins.length, 1);
  assert.deepEqual(origins[0].args, [[], 3]);

  const classResult = context.__ohospatch_callPatch(
    'DemoViewModel', 'crash', true, {}, []
  );
  assert.equal(classResult.result, 'fixed');
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
