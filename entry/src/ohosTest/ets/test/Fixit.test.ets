import { afterEach, beforeEach, describe, expect, it } from '@ohos/hypium';
import { Want } from '@kit.AbilityKit';
import { abilityDelegatorRegistry, Driver, ON } from '@kit.TestKit';
import { OhosPatch } from '@rickytan/ohospatch';
import { DemoScenario, DemoViewModel, Point } from '@vendor/business_page';

export default function fixitTest(): void {
  describe('FixitRuntime', () => {
    // Clear all patches before each test to ensure isolation.
    beforeEach(() => {
      OhosPatch.clear();
      OhosPatch.init(abilityDelegatorRegistry.getAbilityDelegator().getAppContext());
    });

    afterEach(() => {
      OhosPatch.clear();
    });

    it('executeScript installs a method patch that overrides instance behavior', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            return 'class method patched';
          });
      `;
      OhosPatch.executeScript(script);
      expect(DemoViewModel.crash()).assertEqual('class method patched');
    });

    it('target loading falls back to host moduleInfo when a package path is parsed as HSP', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/missing_hsp/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            var Point = Fixit.import(
              'com.rickytan.ohospatch/missing_hsp/@vendor/business_page/src/main/ets/ViewModel/Point#Point'
            );
            return Point.textOf(new Point(7, 9));
          });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);
      expect(DemoViewModel.crash()).assertEqual('(7, 9)');
    });

    it('full path parser accepts scoped OH package names before src/main/ets', 0, () => {
      const script = `
        var scopedImportError = '';
        try {
          Fixit.import('com.example.app/entry/@google/somelib/src/main/ets/foo/Bar#Bar');
        } catch (err) {
          scopedImportError = String(err && err.message ? err.message : err);
        }
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            return scopedImportError.indexOf('OhosPatch path must use') === -1 &&
              scopedImportError.indexOf('Fixit.import cannot load target module') !== -1;
          });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);
      expect(DemoViewModel.crash()).assertTrue();
    });

    it('target parser accepts host package and host module shorthand paths', 0, () => {
      const script = `
        try {
          Fixit.import('/src/main/ets/patch/DemoPatchStartupTask#default');
        } catch (_) {}
        Fixit.fix('@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            return 'short-path-ok';
          });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);
      expect(DemoViewModel.crash()).assertEqual('short-path-ok');
    });

    it('patch context exposes builtin helpers with observable values', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            return [
              Fixit.runtimeVersion,
              typeof require,
              typeof YES,
              typeof NO,
              typeof nil,
              typeof Nil,
              typeof isNil,
              typeof nilToNull,
              typeof nullToNil
            ].join('|');
          });
      `;
      OhosPatch.executeScript(script);
      expect(DemoViewModel.crash()).assertEqual('1.16.0|function|undefined|undefined|undefined|undefined|undefined|undefined|undefined');
    });

    it('patch context can read app resources and runtime information', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            var color = $r('app.color.start_window_background');
            var icon = $r('app.media.app_icon');
            var info = Fixit.runtimeInfo();
            return [
              $r('app.string.app_name'),
              typeof color,
              typeof $r('app.float.ohospatch_demo_float'),
              typeof icon === 'string' && icon.length > 0,
              typeof info.sdkApiVersion,
              info.sdkApiVersion > 0,
              typeof info.osFullName,
              typeof info.bundleName,
              typeof info.appVersionName,
              typeof info.appVersionCode,
              info.patchRuntimeVersion
            ].join('|');
          });
      `;
      OhosPatch.executeScript(script);
      expect(DemoViewModel.crash()).assertEqual('OhosPatch|number|number|true|number|true|string|string|string|number|1.16.0');
    });

    it('component DSL validates direct value API and unsupported event rules', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            var direct = Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/PatchablePanel#PatchablePanel')
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
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/PatchablePanel#PatchablePanel')
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
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/PatchablePanel#PatchablePanel')
                .node({ type: 'Text', occurrence: 0 })
                .attr('fontColor', function () { return '#C44736'; }, 'extra');
            } catch (err) {
              attrExtraRejected = err instanceof TypeError;
            }
            var missingStateReplacementRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/PatchablePanel#PatchablePanel')
                .state('tapCount');
            } catch (err) {
              missingStateReplacementRejected = err instanceof TypeError;
            }
            var missingParamReplacementRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/PatchablePanel#PatchablePanel')
                .param('message');
            } catch (err) {
              missingParamReplacementRejected = err instanceof TypeError;
            }
            var emptyWhereRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/PatchablePanel#PatchablePanel')
                .node({ type: 'Button', where: {} })
                .attr('height', 48);
            } catch (err) {
              emptyWhereRejected = err instanceof TypeError;
            }
            var mixedSelectorRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/PatchablePanel#PatchablePanel')
                .node({ type: 'Button', occurrence: 0, where: { height: 44 } })
                .attr('height', 48);
            } catch (err) {
              mixedSelectorRejected = err instanceof TypeError;
            }
            var builtInParamRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/PatchablePanel#PatchablePanel')
                .node({ type: 'Button', occurrence: 0 })
                .param('title', 'bad');
            } catch (err) {
              builtInParamRejected = err instanceof TypeError;
            }
            var childAttrRejected = false;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/ChildParamPanel#ParentChildParamPanel')
                .node({ type: 'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/component/ChildParamPanel#ChildParamPanel' })
                .attr('height', 48);
            } catch (err) {
              childAttrRejected = err instanceof TypeError;
            }
            return typeof direct + '|' + objectRuleRejected + '|' + attrExtraRejected + '|' +
              missingStateReplacementRejected + '|' + missingParamReplacementRejected + '|' +
              emptyWhereRejected + '|' + mixedSelectorRejected + '|' + builtInParamRejected + '|' +
              childAttrRejected;
          });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);
      expect(DemoViewModel.crash()).assertEqual('function|true|true|true|true|true|true|true|true');
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
        var componentV1 = Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/PatchablePanel#PatchablePanel'
        );
        componentV1.state('tapCount', 40);
        componentV1.state('statusText', 'Patched V1 state before render');
        var component = Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/PatchablePanelV2#PatchablePanelV2'
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
        component.node({
          type: 'Button',
          where: { id: 'v2-primary-action', height: 44 }
        }).attr('id', 'patched-v2-primary-action');
        component.node({ type: 'Button', where: { id: 'missing-action' } })
          .attr('id', 'unexpected-action');
        var origin = component.node({ type: 'Button', where: { height: 44 } })
          .event('onClick', function () {
            this.markPrimary(10);
            var result = origin.apply(this, arguments);
            this.statusText = 'Patched V2 test event, taps=' + this.tapCount;
            return result;
          });
        Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/ChildParamPanel#ParentChildParamPanel'
        )
          .node({
            type: 'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/ChildParamPanel#ChildParamPanel',
            occurrence: 1
          })
          .param('title', function (originValue) {
            return 'scoped patched ' + originValue;
          });
        var builderRoot = Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamRoot'
        );
        builderRoot.node({
          type: 'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamShell',
          occurrence: 0
        }).builder({ type: 'Text', occurrence: 0 })
          .attr('id', 'patched-builder-slot-text');
        var builderSlotOrigin = builderRoot.node({
          type: 'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamShell',
          occurrence: 0
        }).builder({ type: 'Button', occurrence: 0 })
          .attr('id', 'patched-builder-slot-button')
          .event('onClick', function () {
            var result = builderSlotOrigin.apply(this, arguments);
            this.statusText = 'Patched BuilderParam slot event';
            return result;
          });
        builderRoot.node({
          type: 'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamShell',
          occurrence: 1
        }).builder({ type: 'Text', occurrence: 0 })
          .attr('id', 'patched-inline-builder-slot-text');
        var inlineBuilderSlotOrigin = builderRoot.node({
          type: 'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/BuilderParamPanel#BuilderParamShell',
          occurrence: 1
        }).builder({ type: 'Button', occurrence: 0 })
          .attr('id', 'patched-inline-builder-slot-button')
          .event('onClick', function () {
            var result = inlineBuilderSlotOrigin.apply(this, arguments);
            this.statusText = 'Patched inline BuilderParam slot event';
            return result;
          });
        var multiBuilderNode = builderRoot.node({
          type: 'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/BuilderParamPanel#MultiBuilderParamShell',
          occurrence: 0
        });
        multiBuilderNode.builder('headerBuilder', { type: 'Text', occurrence: 0 })
          .attr('id', 'patched-multi-builder-header-text');
        multiBuilderNode.builder('contentBuilder', { type: 'Text', occurrence: 0 })
          .attr('id', 'patched-multi-builder-content-text');
        var multiContentOrigin = multiBuilderNode
          .builder('contentBuilder', { type: 'Button', occurrence: 0 })
          .attr('id', 'patched-multi-builder-content-button')
          .event('onClick', function () {
            var result = multiContentOrigin.apply(this, arguments);
            this.statusText = 'Patched explicit contentBuilder event';
            return result;
          });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);
      await openButton.click();

      const v1State = await driver.waitForComponent(ON.text('tapCount=40, secondary=1'), 5000);
      expect(v1State !== undefined).assertTrue();
      const v1Status = await driver.waitForComponent(ON.text('status=Patched V1 state before render'), 5000);
      expect(v1Status !== undefined).assertTrue();

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

      const firstChild = await driver.waitForComponent(ON.text('first:static child'), 5000);
      expect(firstChild !== undefined).assertTrue();

      const secondChild = await driver.waitForComponent(ON.text('second:scoped patched dynamic child'), 5000);
      expect(secondChild !== undefined).assertTrue();

      const eventButton = await driver.waitForComponent(ON.id('patched-v2-primary-action'), 5000);
      if (eventButton === undefined || eventButton === null) {
        throw new Error('patched-v2-primary-action was not found');
      }
      expect(await eventButton.getText()).assertEqual('V2 primary action');
      await eventButton.click();
      const eventStatus = await driver.waitForComponent(ON.text('status=Patched V2 test event, taps=111'), 5000);
      expect(eventStatus !== undefined).assertTrue();

      const secondaryButton = await driver.waitForComponent(ON.id('v2-secondary-action'), 5000);
      expect(secondaryButton !== undefined).assertTrue();
      await secondaryButton.click();
      const secondaryStatus = await driver.waitForComponent(ON.text('status=V2 event ran, taps=113'), 5000);
      expect(secondaryStatus !== undefined).assertTrue();

      await driver.swipe(660, 2200, 660, 900, 500);
      const builderText = await driver.waitForComponent(ON.id('patched-builder-slot-text'), 5000);
      if (builderText === undefined || builderText === null) {
        throw new Error('patched-builder-slot-text was not found');
      }
      expect(await builderText.getText()).assertEqual('slot text=Original builder-param state');
      const builderButton = await driver.waitForComponent(ON.id('patched-builder-slot-button'), 5000);
      expect(builderButton !== undefined).assertTrue();
      await builderButton.click();
      const builderStatus = await driver.waitForComponent(
        ON.text('root status=Patched BuilderParam slot event'), 5000
      );
      expect(builderStatus !== undefined).assertTrue();
      await driver.swipe(660, 2200, 660, 1100, 500);
      const inlineBuilderText = await driver.waitForComponent(ON.id('patched-inline-builder-slot-text'), 5000);
      if (inlineBuilderText === undefined || inlineBuilderText === null) {
        throw new Error('patched-inline-builder-slot-text was not found');
      }
      expect(await inlineBuilderText.getText()).assertEqual('inline slot text=Patched BuilderParam slot event');
      const inlineBuilderButton = await driver.waitForComponent(ON.id('patched-inline-builder-slot-button'), 5000);
      expect(inlineBuilderButton !== undefined).assertTrue();
      await inlineBuilderButton.click();
      const inlineBuilderStatus = await driver.waitForComponent(
        ON.text('root status=Patched inline BuilderParam slot event'), 5000
      );
      expect(inlineBuilderStatus !== undefined).assertTrue();
      await driver.swipe(660, 2200, 660, 900, 500);
      const multiHeaderText = await driver.waitForComponent(ON.id('patched-multi-builder-header-text'), 5000);
      expect(multiHeaderText !== undefined).assertTrue();
      const multiContentText = await driver.waitForComponent(ON.id('patched-multi-builder-content-text'), 5000);
      expect(multiContentText !== undefined).assertTrue();
      const originalHeaderButton = await driver.waitForComponent(ON.id('multi-builder-header-button'), 5000);
      expect(originalHeaderButton !== undefined).assertTrue();
      const multiContentButton = await driver.waitForComponent(ON.id('patched-multi-builder-content-button'), 5000);
      expect(multiContentButton !== undefined).assertTrue();
      await multiContentButton.click();
      const multiContentStatus = await driver.waitForComponent(
        ON.text('root status=Patched explicit contentBuilder event'), 5000
      );
      expect(multiContentStatus !== undefined).assertTrue();

      OhosPatch.clear();
      const reinstallScript = `
        Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/PatchablePanelV2#PatchablePanelV2'
        ).state('tapCount', 200);
      `;
      expect(OhosPatch.executeScript(reinstallScript)).assertLarger(0);
    });

    it('invalid full path input fails closed and later valid input still works', 0, () => {
      const invalid = `
        var rejected = false;
        try {
          Fixit.fix('entry/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
            .instanceMethod('crashIt', function () { return 'should-not-install'; });
        } catch (err) {
          rejected = String(err && err.message ? err.message : err)
            .indexOf('bundleName/moduleName before package path') !== -1;
        }
        if (!rejected) {
          throw new Error('invalid path was accepted');
        }
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
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'valid-after-invalid'; });
      `;
      expect(OhosPatch.executeScript(valid)).assertLarger(0);
      expect(vm.crashIt()).assertEqual('valid-after-invalid');
    });

    it('patched method can delegate to origin via origin.apply', 0, () => {
      // DemoViewModel.locationOf throws on out-of-bounds index.
      // Patch it to return a fallback point instead of throwing.
      const script = `
        var origin = Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        var origin = Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        var Point = Fixit.import('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/Point#Point');
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        var Point = require('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/Point#Point');
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'console-ok'; });
      `;
      const count = OhosPatch.executeScript(script);
      expect(count).assertLarger(0);
    });

    it('timer callbacks run in the real Patch JSVM and can update later patch results', 0, async () => {
      const script = `
        var timerValue = 'pending';
        setTimeout(function (value) {
          timerValue = value;
        }, 10, 'timer-fired');
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        var fix = Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel');
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
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
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
        var fix = Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel');
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
