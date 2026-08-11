import assert from 'node:assert/strict';
import test from 'node:test';
import {
  createRuntime,
  findSpec,
  invoke,
  identityOf,
  findUiRule,
  valueRuleId,
  attrRuleId,
  eventRuleId,
  plain,
  PANEL_PATH
} from './_runtime.mjs';

test('runtime installs Fixit globals with version, nil helpers, and console forwarding', () => {
  const { context, logs } = createRuntime();

  // Fixit is installed and usable: fix() returns a target with instanceMethod.
  assert.equal(
    typeof context.Fixit.fix('com.example.app/entry/src/main/ets/model/M#M').instanceMethod,
    'function'
  );
  assert.equal(context.Fixit.runtimeVersion, '1.6.0');
  // require is a public alias of import.
  assert.equal(context.require, context.Fixit.import);
  // nil / Nil are null sentinels; YES / NO are intentionally absent.
  assert.equal(context.nil, null);
  assert.equal(context.Nil, null);
  assert.equal(context.YES, undefined);
  assert.equal(context.NO, undefined);
  // isNil / nilToNull / nullToNil behave as null-coalescing helpers.
  assert.equal(context.isNil(null), true);
  assert.equal(context.isNil(undefined), true);
  assert.equal(context.isNil(0), false);
  assert.equal(context.nilToNull(undefined), null);
  assert.equal(context.nilToNull('x'), 'x');
  assert.equal(context.nullToNil(null), null);
  // console forwards formatted output to HiLog, serializing objects.
  context.console.warn('patch', { count: 2 });
  assert.deepEqual(logs, [{ level: 'warn', message: 'patch {"count":2}' }]);
});

test('timers schedule, fire once or repeatedly, and clear', async () => {
  const { context, scheduledTimers, cancelledTimers } = createRuntime();
  const calls = [];

  // setTimeout fires a single time even if invoked twice.
  const timeoutId = context.setTimeout((...args) => calls.push(['timeout', ...args]), 12.9, 'a', 2);
  assert.deepEqual(scheduledTimers[0], { id: timeoutId, delay: 12, repeating: false });
  context.__ohospatch_fireTimer(timeoutId);
  context.__ohospatch_fireTimer(timeoutId);
  assert.deepEqual(calls, [['timeout', 'a', 2]]);

  // setInterval fires on every tick until cleared; a zero delay normalizes to 1ms.
  const intervalId = context.setInterval(() => calls.push(['interval']), 0);
  assert.deepEqual(scheduledTimers[1], { id: intervalId, delay: 1, repeating: true });
  context.__ohospatch_fireTimer(intervalId);
  context.__ohospatch_fireTimer(intervalId);
  context.clearInterval(intervalId);
  assert.deepEqual(cancelledTimers, [intervalId]);
  assert.deepEqual(calls.slice(1), [['interval'], ['interval']]);

  // setImmediate schedules a zero-delay one-shot and can be cancelled before firing.
  const immediateId = context.setImmediate((value) => calls.push(['immediate', value]), 3);
  assert.deepEqual(scheduledTimers[2], { id: immediateId, delay: 0, repeating: false });
  context.clearImmediate(immediateId);
  assert.deepEqual(cancelledTimers, [intervalId, immediateId]);

  // clear() cancels every active timer; a stale timer never fires afterwards.
  const staleId = context.setTimeout(() => calls.push(['stale']), 100);
  context.__ohospatch_clear();
  context.__ohospatch_fireTimer(staleId);
  assert.deepEqual(cancelledTimers, [intervalId, immediateId, staleId]);
  assert.equal(calls.some((call) => call[0] === 'stale'), false);

  // queueMicrotask runs after the current turn on the microtask queue.
  context.queueMicrotask(() => calls.push(['microtask']));
  await Promise.resolve();
  assert.deepEqual(calls.at(-1), ['microtask']);
  assert.throws(() => context.setTimeout('not a function', 0), /must be a function/);
  assert.throws(() => context.queueMicrotask(null), /must be a function/);
});

test('fix and component parse full OHM source paths and reject invalid input', () => {
  const { context } = createRuntime();

  assert.deepEqual(plain(context.Fixit.fix(
    'com.example.app/feature/src/main/ets/model/FeatureModel'
  ).target), {
    className: 'FeatureModel',
    modulePath: 'feature/src/main/ets/model/FeatureModel',
    moduleInfo: 'com.example.app/feature',
    exportName: 'FeatureModel',
    bundleName: 'com.example.app',
    moduleName: 'feature',
    packageName: 'feature'
  });
  assert.deepEqual(plain(context.Fixit.component(
    '@bundle:com.example.app/entry/entry_api/src/main/ets/model/DefaultModel.ets#default'
  ).target), {
    className: 'DefaultModel',
    modulePath: 'entry_api/src/main/ets/model/DefaultModel',
    moduleInfo: 'com.example.app/entry',
    exportName: 'default',
    bundleName: 'com.example.app',
    moduleName: 'entry',
    packageName: 'entry_api'
  });

  assert.throws(() => context.Fixit.fix('entry/src/main/ets/Test'), /must use/);
  assert.throws(() => context.Fixit.component('/com.example/entry/src/main/ets/Test'), /must use/);
  assert.throws(
    () => context.Fixit.fix('com.example/entry/src/main/ets/Test#invalid-name'),
    /export name is invalid/
  );
});

test('import returns a persistent proxy supporting construct, static, and instance access', () => {
  const { context, registerImport, setProxyRoot } = createRuntime();

  class Point {
    constructor(x, y) {
      this.x = x;
      this.y = y;
    }

    toText() {
      return `(${this.x}, ${this.y})`;
    }

    static textOf(point) {
      return point.toText();
    }
  }

  const pointPath = 'com.example.app/entry/src/main/ets/model/Point#Point';
  registerImport(pointPath, Point);
  const ImportedPoint = context.Fixit.import(pointPath);
  const RequiredPoint = context.require(pointPath);
  const point = new ImportedPoint(1.25, 2.5);

  // Static, instance, and property access all route through the persistent proxy.
  assert.equal(RequiredPoint.textOf(point), '(1.25, 2.5)');
  assert.equal(point.toText(), '(1.25, 2.5)');
  assert.equal(ImportedPoint.textOf(point), '(1.25, 2.5)');
  point.x = 3;
  assert.equal(point.x, 3);

  // A patch handler can return an imported object; the caller receives an imported
  // reference, and assigning it onto `this` lands the real object on the ArkTS side.
  const fix = context.Fixit.fix(
    'com.example.app/entry/src/main/ets/model/ViewModel#ViewModel'
  );
  fix.instanceMethod('makePoint', function () {
    this.point = point;
    return point;
  });
  const target = { point: null };
  setProxyRoot(target);
  const result = invoke(context, fix, 'makePoint', false, []);

  assert.ok(target.point instanceof Point);
  assert.equal(target.point.x, 3);
  assert.equal(result.result.kind, 'imported');
});

test('method patch replaces instance and class method behavior and delegates to origin', () => {
  const { context, origins, setProxyRoot } = createRuntime();
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

  const target = { lastIndex: -1 };
  setProxyRoot(target);

  // In-bounds: the handler returns the element and writes a side effect on `this`.
  const hit = invoke(context, fix, 'locationOf', false, [['zero'], 0]);
  assert.equal(hit.result.value, 'zero');
  assert.equal(target.lastIndex, 0);

  // Out-of-bounds: the handler delegates to the saved original.
  const miss = invoke(context, fix, 'locationOf', false, [[], 3]);
  assert.equal(miss.result.value, 'origin-result');
  assert.equal(origins.length, 1);
  assert.deepEqual(origins[0].args, [[], 3]);

  // Class method: a formerly-throwing static method now returns normally.
  setProxyRoot({});
  const classResult = invoke(context, fix, 'crash', true, []);
  assert.equal(classResult.result.value, 'fixed');
});

test('method patch proxies nested this properties and methods to the original object', () => {
  const { context, setProxyRoot } = createRuntime();
  const fix = context.Fixit.fix(
    'com.example.app/entry/src/main/ets/model/ViewModel#ViewModel'
  );
  fix.instanceMethod('repair', function () {
    const profile = this.account.profile;
    this.account.profile.name = profile.name.toUpperCase();
    this.account.profile.increment(2);
    this.alias = this.account.profile;
    return this.account.profile;
  });

  const target = {
    account: {
      profile: {
        name: 'patch',
        count: 3,
        increment(amount) {
          this.count += amount;
          return this.count;
        }
      }
    },
    alias: null
  };
  setProxyRoot(target);
  const result = invoke(context, fix, 'repair', false, []);

  // Nested writes, method calls, and aliasing all reach the real ArkTS object.
  assert.equal(target.account.profile.name, 'PATCH');
  assert.equal(target.account.profile.count, 5);
  assert.equal(target.alias, target.account.profile);
  // The returned ArkTS object is surfaced as a remote proxy reference.
  assert.equal(result.result.kind, 'remote');
});

test('same-named classes in different modules patch independently', () => {
  const { context, setProxyRoot } = createRuntime();
  const entryFix = context.Fixit.fix(
    'com.example.app/entry/src/main/ets/model/ViewModel#ViewModel'
  );
  entryFix.instanceMethod('value', function () { return 'entry'; });
  const featureFix = context.Fixit.fix(
    'com.example.app/feature/src/main/ets/model/ViewModel#ViewModel'
  );
  featureFix.instanceMethod('value', function () { return 'feature'; });

  setProxyRoot({});
  assert.equal(invoke(context, entryFix, 'value', false, []).result.value, 'entry');
  assert.equal(invoke(context, featureFix, 'value', false, []).result.value, 'feature');
});

test('registerTarget alias resolves, dedupes, and clears', () => {
  const { context, setProxyRoot } = createRuntime();
  context.Fixit.registerTarget('DemoViewModel', {
    modulePath: 'entry/src/main/ets/demo/DemoViewModel',
    moduleInfo: 'com.rickytan.ohospatch/entry'
  });

  const fix = context.Fixit.fix('DemoViewModel');
  fix.instanceMethod('crashIt', function () {
    return 'fixed';
  });
  // Registering the same method twice is rejected.
  assert.throws(
    () => fix.instanceMethod('crashIt', function () {}),
    /Duplicate patch/
  );

  setProxyRoot({});
  const key = identityOf(context, fix, 'crashIt', false);
  assert.equal(
    context.__ohospatch_callPatch(key, 'crashIt', false, 0, []).result.value,
    'fixed'
  );

  // After clear, the handler no longer fires (fail closed), and the alias is gone.
  context.__ohospatch_clear();
  assert.equal(context.__ohospatch_callPatch(key, 'crashIt', false, 0, []).handled, false);
  assert.throws(() => context.Fixit.fix('DemoViewModel'), /className and modulePath/);
});

test('component param and state overrides transform incoming values', () => {
  const { context } = createRuntime();
  const component = context.Fixit.component(PANEL_PATH);
  component.param('title').transform((value) => value || 'patched title');
  component.state('rows').replace([]);

  const titleId = valueRuleId(context, component, 'param', 'title');
  const rowsId = valueRuleId(context, component, 'state', 'rows');

  // transform rewrites an empty param; replace overrides state with a fixed value.
  assert.deepEqual(plain(context.__ohospatch_callUiValue(titleId, '')), {
    handled: true,
    value: 'patched title'
  });
  assert.deepEqual(plain(context.__ohospatch_callUiValue(rowsId, ['old'])), {
    handled: true,
    value: []
  });

  // After clear, value rules no longer handle the input (fail closed).
  context.__ohospatch_clear();
  assert.deepEqual(plain(context.__ohospatch_callUiValue(titleId, 'old')), { handled: false });
});

test('component attribute handler resolves dynamically bound to the instance', () => {
  const { context, setProxyRoot } = createRuntime();
  const component = context.Fixit.component(PANEL_PATH);
  const button = component.node({ type: 'Button', occurrence: 0 });
  button.attrs({
    height: function () { return this.count + 10; },
    backgroundColor: function () { return this.theme.color; }
  });

  const heightId = attrRuleId(context, component, 'Button', 0, 'height');
  const colorId = attrRuleId(context, component, 'Button', 0, 'backgroundColor');

  const owner = { count: 5, theme: { color: '#FF0000' } };
  setProxyRoot(owner);
  assert.deepEqual(plain(context.__ohospatch_callUiAttr(heightId, 0)), { handled: true, value: 15 });
  assert.deepEqual(plain(context.__ohospatch_callUiAttr(colorId, 0)), { handled: true, value: '#FF0000' });

  // The handler is dynamic: mutating the instance changes the next resolved value.
  owner.count = 20;
  owner.theme.color = '#00FF00';
  assert.deepEqual(plain(context.__ohospatch_callUiAttr(heightId, 0)), { handled: true, value: 30 });
  assert.deepEqual(plain(context.__ohospatch_callUiAttr(colorId, 0)), { handled: true, value: '#00FF00' });

  // A throwing handler fails closed and leaves the attribute untouched.
  component.node({ type: 'Text', occurrence: 0 })
    .attr('fontColor', function () { throw new Error('boom'); });
  const failId = attrRuleId(context, component, 'Text', 0, 'fontColor');
  setProxyRoot({});
  assert.deepEqual(plain(context.__ohospatch_callUiAttr(failId, 0)), { handled: false });
});

test('component event handler replaces callback, binds this, and delegates to origin', () => {
  const { context, eventOrigins, setProxyRoot } = createRuntime();
  const component = context.Fixit.component(PANEL_PATH);
  const origin = component.node('Button').event('onClick', {
    capture: ['tapCount'],
    handler: function (event, componentContext) {
      this.tapCount = this.tapCount + event.delta;
      this.profile.title = this.profile.title.toUpperCase();
      componentContext.setState({ tapCount: this.tapCount });
      return `${this.describe(this.profile.title)}:${origin.apply(this, arguments)}`;
    }
  });
  assert.equal(typeof origin, 'function');

  const owner = {
    tapCount: 2,
    profile: { title: 'patched' },
    describe(title) {
      return `${title}:${this.tapCount}`;
    }
  };
  setProxyRoot(owner);
  const eventId = eventRuleId(context, component, 'Button', 0, 'onClick');
  const result = context.__ohospatch_callUiEvent(eventId, { delta: 3 }, { tapCount: 2 }, 0);

  // this is bound to the component instance, setState produces a state patch, and
  // origin.apply forwards to the original callback with the original event args.
  assert.equal(owner.tapCount, 5);
  assert.equal(owner.profile.title, 'PATCHED');
  assert.deepEqual(plain(result), {
    handled: true,
    result: 'PATCHED:5:event-origin-result',
    statePatch: { tapCount: 5 }
  });
  assert.equal(eventOrigins.length, 1);
  assert.deepEqual(eventOrigins[0].args, [{ delta: 3 }]);
});

test('component event forwards original arguments before the injected context', () => {
  const { context, eventOrigins, setProxyRoot } = createRuntime();
  const component = context.Fixit.component(PANEL_PATH);
  const origin = component.node('Toggle').event('onChange', {
    capture: ['switchOn'],
    handler: function (isOn, componentContext) {
      this.switchOn = isOn;
      componentContext.setState({ switchOn: isOn });
      return origin.apply(this, arguments);
    }
  });

  const owner = { switchOn: true };
  setProxyRoot(owner);
  const eventId = eventRuleId(context, component, 'Toggle', 0, 'onChange');
  const result = context.__ohospatch_callUiEvent(eventId, [false], { switchOn: true }, 0);

  // The original event argument (false) reaches both the handler and the origin;
  // the injected context is dropped before delegating.
  assert.equal(owner.switchOn, false);
  assert.deepEqual(plain(result), {
    handled: true,
    result: 'event-origin-result',
    statePatch: { switchOn: false }
  });
  assert.equal(eventOrigins.length, 1);
  assert.deepEqual(eventOrigins[0].args, [false]);
});

test('component DSL rejects duplicate rules and invalid inputs', () => {
  const { context } = createRuntime();
  const component = context.Fixit.component(PANEL_PATH);
  const button = component.node({ type: 'Button', occurrence: 0 });
  button.attrs({ height: 48, backgroundColor: '#1677FF' });

  // Duplicate attribute rule for the same node.
  assert.throws(() => button.attr('height', 52), /Duplicate component patch rule/);
  // Only replace mode is supported.
  assert.throws(
    () => component.node('Button').event('onChange', { mode: 'around', handler() {} }),
    /Only replace/
  );
  // occurrence must be a non-negative uint32.
  assert.throws(() => component.node({ type: 'Text', occurrence: -1 }), /non-negative uint32 integer/);
  // capture supports at most 16 properties.
  assert.throws(
    () => component.node('Text').event('onClick', { capture: Array(17).fill('value'), handler() {} }),
    /at most 16/
  );
});
