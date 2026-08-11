import { describe, it, expect, beforeEach, afterEach } from '@ohos/hypium';
import { Want } from '@kit.AbilityKit';
import { Driver, ON, abilityDelegatorRegistry } from '@kit.TestKit';
import { OhosPatch } from '@rickytan/ohospatch';
import { DemoViewModel } from '../../../main/ets/demo/DemoViewModel';
import { Point } from '../../../main/ets/demo/Point';
import { DemoScenario } from '../../../main/ets/demo/DemoScenario';

export default function fixitTest(): void {
  describe('FixitRuntime', () => {
    // Clear all patches before each test to ensure isolation.
    beforeEach(() => {
      OhosPatch.clear();
    });

    afterEach(() => {
      OhosPatch.clear();
    });

    it('executeScript installs a method patch that overrides instance behavior', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () {
            return 'patched by ohtest';
          });
      `;
      const hookCount = OhosPatch.executeScript(script);
      expect(hookCount).assertLarger(0);

      const vm = new DemoViewModel();
      expect(vm.crashIt()).assertEqual('patched by ohtest');
    });

    it('executeScript installs a class (static) method patch', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            return 'class method patched';
          });
      `;
      OhosPatch.executeScript(script);
      expect(DemoViewModel.crash()).assertEqual('class method patched');
    });

    it('patch context exposes builtin helpers with observable values', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            return [
              Fixit.runtimeVersion,
              typeof require,
              nil === null,
              Nil === null,
              typeof YES,
              typeof NO,
              isNil(null),
              isNil(undefined),
              isNil(0),
              nilToNull(undefined) === null,
              nilToNull('x'),
              nullToNil(null) === null
            ].join('|');
          });
      `;
      OhosPatch.executeScript(script);
      expect(DemoViewModel.crash()).assertEqual('1.10.0|function|true|true|undefined|undefined|true|true|false|true|x|true');
    });

    it('component DSL rejects removed value chains and unsupported event rules', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            var direct = Fixit.component('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/PatchablePanel#PatchablePanel')
              .node({ type: 'Button', occurrence: 2 })
              .event('onClick', function () {
                try {
                  return direct.apply(this, arguments);
                } catch (err) {
                  this.tagText = 'component-event-recovered';
                }
              });
            var objectRuleRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/PatchablePanel#PatchablePanel')
                .node({ type: 'Button', occurrence: 1 })
                .event('onClick', {
                  mode: 'replace',
                  capture: ['tapCount'],
                  handler: function () {}
                });
            } catch (err) {
              objectRuleRejected = err instanceof TypeError;
            }
            var attrExtraRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/PatchablePanel#PatchablePanel')
                .node({ type: 'Text', occurrence: 0 })
                .attr('fontColor', function () { return '#C44736'; }, 'extra');
            } catch (err) {
              attrExtraRejected = err instanceof TypeError;
            }
            var legacyStateRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/PatchablePanel#PatchablePanel')
                .state('tapCount').replace(40);
            } catch (err) {
              legacyStateRejected = err instanceof TypeError;
            }
            var legacyParamRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/PatchablePanel#PatchablePanel')
                .param('message').replace('legacy');
            } catch (err) {
              legacyParamRejected = err instanceof TypeError;
            }
            return typeof direct + '|' + objectRuleRejected + '|' + attrExtraRejected + '|' +
              legacyStateRejected + '|' + legacyParamRejected;
          });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);
      expect(DemoViewModel.crash()).assertEqual('function|true|true|true|true');
    });

    it('ComponentV2 supports param state node and event DSL behavior', 0, async () => {
      const delegator = abilityDelegatorRegistry.getAbilityDelegator();
      const want: Want = {
        bundleName: 'com.rickytan.ohospatch',
        moduleName: 'entry',
        abilityName: 'EntryAbility'
      };
      await delegator.startAbility(want);

      const driver = Driver.create();
      const openButton = await driver.waitForComponent(ON.text('Open patch target screen'), 5000);
      expect(openButton !== undefined).assertTrue();

      const script = `
        var component = Fixit.component(
          'com.rickytan.ohospatch/entry/src/main/ets/demo/PatchablePanelV2#PatchablePanelV2'
        );
        component.param('message', function (originValue) {
          return 'Patched V2 test parameter, origin=' + originValue + ', count=' + this.tapCount;
        });
        component.state('tapCount', function (originValue) {
          this.statusText = 'Patched V2 state callback, origin=' + originValue;
          return originValue + 100;
        });
        component.node({ type: 'Text', occurrence: 1 })
          .attr('backgroundColor', '#E7F7EE');
        var origin = component.node({ type: 'Button', occurrence: 0 })
          .event('onClick', function () {
            this.markPrimary(10);
            var result = origin.apply(this, arguments);
            this.statusText = 'Patched V2 test event, taps=' + this.tapCount;
            return result;
          });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);
      await openButton.click();

      const parameter = await driver.waitForComponent(
        ON.text('Patched V2 test parameter, origin=Original V2 component parameter, count=0'), 5000
      );
      expect(parameter !== undefined).assertTrue();
      const initialState = await driver.waitForComponent(ON.text('tapCount=100'), 5000);
      expect(initialState !== undefined).assertTrue();
      const initialStatus = await driver.waitForComponent(
        ON.text('status=Patched V2 state callback, origin=0'), 5000
      );
      expect(initialStatus !== undefined).assertTrue();

      const eventButton = await driver.waitForComponent(ON.text('V2 primary action'), 5000);
      expect(eventButton !== undefined).assertTrue();
      await eventButton.click();
      const eventStatus = await driver.waitForComponent(ON.text('status=Patched V2 test event, taps=111'), 5000);
      expect(eventStatus !== undefined).assertTrue();

      OhosPatch.clear();
      const reinstallScript = `
        Fixit.component(
          'com.rickytan.ohospatch/entry/src/main/ets/demo/PatchablePanelV2#PatchablePanelV2'
        ).state('tapCount', 200);
      `;
      expect(OhosPatch.executeScript(reinstallScript)).assertLarger(0);
    });

    it('invalid full path input fails closed and later valid input still works', 0, () => {
      const invalid = `
        Fixit.fix('entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'should-not-install'; });
      `;
      expect(OhosPatch.executeScript(invalid)).assertEqual(0);

      const vm = new DemoViewModel();
      let threw = false;
      try {
        vm.crashIt();
      } catch (err) {
        threw = true;
      }
      expect(threw).assertTrue();

      const valid = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'valid-after-invalid'; });
      `;
      expect(OhosPatch.executeScript(valid)).assertLarger(0);
      expect(vm.crashIt()).assertEqual('valid-after-invalid');
    });

    it('patched method can delegate to origin via origin.apply', 0, () => {
      // DemoViewModel.locationOf throws on out-of-bounds index.
      // Patch it to return a fallback point instead of throwing.
      const script = `
        var origin = Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('locationOf', function (locations, index, fallback) {
            if (index < 0 || index >= locations.length) {
              return fallback;
            }
            return origin.apply(this, arguments);
          });
      `;
      OhosPatch.executeScript(script);

      const vm = new DemoViewModel();
      const points: Array<Point> = [new Point(1, 2)];
      const fallback = new Point(99, 99);

      // Out of bounds: patch returns fallback instead of throwing.
      const result = vm.locationOf(points, 5, fallback);
      expect(result.x).assertEqual(99);

      // In bounds: origin.apply delegates to the original method.
      const hit = vm.locationOf(points, 0, fallback);
      expect(hit.x).assertEqual(1);
    });

    it('patched method can catch an exception thrown by origin.apply', 0, () => {
      const script = `
        var origin = Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () {
            try {
              return origin.apply(this, arguments);
            } catch (err) {
              var message = err && err.message ? err.message : String(err);
              this.buttonTitle = message;
              return 'origin-caught:' + message;
            }
          });
      `;
      OhosPatch.executeScript(script);

      const vm = new DemoViewModel();
      expect(vm.crashIt()).assertEqual('origin-caught:instance crash');
      expect(vm.buttonTitle).assertEqual('instance crash');
    });

    it('patched method can mutate this properties (nested proxy access)', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('revealNote', function () {
            this.buttonTitle = 'mutated by patch';
            this.backgroundColor = '#FF0000';
            return this.hiddenNote() + ':patched';
          });
      `;
      OhosPatch.executeScript(script);

      const vm = new DemoViewModel();
      const result = vm.revealNote();
      expect(result).assertEqual('hidden-42:patched');
      expect(vm.buttonTitle).assertEqual('mutated by patch');
      expect(vm.backgroundColor).assertEqual('#FF0000');
    });

    it('patched method can access and mutate multi-level this properties', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('revealNote', function () {
            this.profile.badge.text = 'nested';
            this.profile.badge.advance(3);
            return this.profile.summary();
          });
      `;
      OhosPatch.executeScript(script);

      const vm = new DemoViewModel();
      expect(vm.revealNote()).assertEqual('nested@3');
      expect(vm.profile.badge.text).assertEqual('nested');
      expect(vm.profile.badge.revision).assertEqual(3);
    });

    it('Fixit.import returns a usable proxy for cross-module class access', 0, () => {
      const script = `
        var Point = Fixit.import('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/Point#Point');
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            var p = new Point(7, 9);
            return Point.textOf(p) + '|' + p.toText() + '|' + p.x + ',' + p.y;
          });
      `;
      OhosPatch.executeScript(script);

      const result = DemoViewModel.crash();
      expect(result).assertEqual('(7, 9)|(7, 9)|7,9');
    });

    it('require is an alias of Fixit.import', 0, () => {
      const script = `
        var Point = require('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/Point#Point');
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            var p = new Point(3, 4);
            return p.toText();
          });
      `;
      OhosPatch.executeScript(script);
      expect(DemoViewModel.crash()).assertEqual('(3, 4)');
    });

    it('console.log/info/warn forward to HiLog without crashing', 0, () => {
      const script = `
        console.log('ohtest log message');
        console.info('ohtest info message');
        console.warn('ohtest warn message');
        console.error('ohtest error message');
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'console-ok'; });
      `;
      const count = OhosPatch.executeScript(script);
      expect(count).assertLarger(0);
    });

    it('registerTarget alias resolves a short name to a full target', 0, () => {
      const script = `
        Fixit.registerTarget('DemoViewModel', {
          modulePath: 'entry/src/main/ets/demo/DemoViewModel',
          moduleInfo: 'com.rickytan.ohospatch/entry_test'
        });
        Fixit.fix('DemoViewModel')
          .instanceMethod('crashIt', function () { return 'alias-works'; });
      `;
      OhosPatch.executeScript(script);
      const vm = new DemoViewModel();
      expect(vm.crashIt()).assertEqual('alias-works');
    });

    it('timer callbacks run in the real Patch JSVM and can update later patch results', 0, async () => {
      const script = `
        var timerValue = 'pending';
        setTimeout(function (value) {
          timerValue = value;
        }, 10, 'timer-fired');
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            return timerValue;
          });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);
      expect(DemoViewModel.crash()).assertEqual('pending');

      await new Promise<void>((resolve) => {
        setTimeout(resolve, 60);
      });

      expect(DemoViewModel.crash()).assertEqual('timer-fired');
    });

    it('duplicate patch registration is rejected (returns 0 hooks)', 0, () => {
      const script = `
        var fix = Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel');
        fix.instanceMethod('crashIt', function () { return 'first'; });
        fix.instanceMethod('crashIt', function () { return 'second'; });
      `;
      const count = OhosPatch.executeScript(script);
      expect(count).assertEqual(0);

      const vm = new DemoViewModel();
      let threw = false;
      try {
        vm.crashIt();
      } catch (err) {
        threw = true;
      }
      expect(threw).assertTrue();
    });

    it('clear removes all patches and original behavior is restored', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'patched'; });
      `;
      OhosPatch.executeScript(script);
      const vm = new DemoViewModel();
      expect(vm.crashIt()).assertEqual('patched');

      OhosPatch.clear();

      // After clear, the original method throws again.
      let threw = false;
      try {
        vm.crashIt();
      } catch (err) {
        threw = true;
      }
      expect(threw).assertTrue();
    });

    it('DemoScenario.run produces expected results after patching', 0, () => {
      const script = `
        var fix = Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel');
        fix.instanceMethod('crashIt', function () { return 'scenario-fixed'; });
        fix.instanceMethod('locationOf', function (locations, index, fallback) {
          return fallback;
        });
        fix.classMethod('crash', function () { return 'class-scenario-fixed'; });
      `;
      OhosPatch.executeScript(script);

      const result = DemoScenario.run();
      // With the patch, crashIt returns normally and locationOf returns fallback.
      expect(result.rows.some(r => r.includes('scenario-fixed'))).assertTrue();
      expect(result.rows.some(r => r.includes('class-scenario-fixed'))).assertTrue();
    });
  });
}
