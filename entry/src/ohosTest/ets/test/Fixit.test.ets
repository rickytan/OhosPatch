import { afterEach, beforeEach, describe, expect, it } from '@ohos/hypium';
import { Want } from '@kit.AbilityKit';
import { abilityDelegatorRegistry, Driver, ON } from '@kit.TestKit';
import { OhosPatch } from '@rickytan/ohospatch';
import { DemoScenario, DemoViewModel, Point } from '@vendor/business_page';

async function selectPatchTab(driver: Driver, label: string): Promise<void> {
  const tab = await driver.waitForComponent(ON.text(label), 5000);
  if (tab === undefined || tab === null) {
    throw new Error(`Patch tab ${label} was not found`);
  }
  await tab.click();
  await driver.delayMs(300);
}

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
      const result = OhosPatch.executeScript(script);
      expect(result.installedCount).assertLarger(0);

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
      expect(OhosPatch.executeScript(script).installedCount).assertLarger(0);
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
      expect(OhosPatch.executeScript(script).installedCount).assertLarger(0);
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
      expect(OhosPatch.executeScript(script).installedCount).assertLarger(0);
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
      expect(DemoViewModel.crash()).assertEqual('1.17.0|function|undefined|undefined|undefined|undefined|undefined|undefined|undefined');
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
      expect(DemoViewModel.crash()).assertEqual('OhosPatch|number|number|true|number|true|string|string|string|number|1.17.0');
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
            var multiArgumentAttributeAccepted = true;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/PatchablePanelV2#PatchablePanelV2')
                .node({ type: 'Text', occurrence: 1 })
                .attr('backgroundImage', $r('app.media.app_icon'), { syncLoad: true });
            } catch (err) {
              multiArgumentAttributeAccepted = false;
            }
            var nestedFunctionAttributeAccepted = true;
            try {
              Fixit.component('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/PatchablePanelV2#PatchablePanelV2')
                .node({ type: 'Text', occurrence: 0 })
                .attr('bindMenu', [{
                  value: 'Patch function menu',
                  action: function () {
                    this.statusText = 'Nested function action ran';
                  }
                }]);
            } catch (err) {
              nestedFunctionAttributeAccepted = false;
            }
            return typeof direct + '|' + objectRuleRejected + '|' + attrExtraRejected + '|' +
              missingStateReplacementRejected + '|' + missingParamReplacementRejected + '|' +
              emptyWhereRejected + '|' + mixedSelectorRejected + '|' + builtInParamRejected + '|' +
              childAttrRejected + '|' + multiArgumentAttributeAccepted + '|' + nestedFunctionAttributeAccepted;
          });
      `;
      expect(OhosPatch.executeScript(script).installedCount).assertLarger(0);
      expect(DemoViewModel.crash()).assertEqual('function|true|true|true|true|true|true|true|true|true|true');
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
      let openButton = await driver.waitForComponent(ON.text('Open patch target screen'), 2000);
      if (openButton === undefined || openButton === null) {
        await driver.pressBack();
        openButton = await driver.waitForComponent(ON.text('Open patch target screen'), 5000);
      }
      if (openButton === undefined || openButton === null) {
        throw new Error('Open patch target screen button was not found');
      }

      await openButton.click();
      const baselineTabs = await driver.waitForComponent(ON.id('patch-target-tabs'), 5000);
      if (baselineTabs === undefined || baselineTabs === null) {
        throw new Error('patch-target-tabs was not found before patch installation');
      }
      const baselineCreateText = await driver.waitForComponent(ON.text('abcd'), 5000);
      if (baselineCreateText === undefined || baselineCreateText === null) {
        throw new Error('Text create marker was not found before patch installation');
      }
      await selectPatchTab(driver, 'BuilderParam');
      const baselineMultiContent = await driver.waitForComponent(ON.id('multi-builder-content-text'), 5000);
      if (baselineMultiContent === undefined || baselineMultiContent === null) {
        throw new Error('multi-builder-content-text was not found before patch installation');
      }
      expect(await baselineMultiContent.getText()).assertEqual('multi content text=Original builder-param state');
      await driver.pressBack();
      openButton = await driver.waitForComponent(ON.text('Open patch target screen'), 5000);
      if (openButton === undefined || openButton === null) {
        throw new Error('Open patch target screen button was not found after baseline verification');
      }

      const script = `
        var componentV1 = Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/PatchablePanel#PatchablePanel'
        );
        Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/DemoPatch#DemoPatchScreen'
        ).builder('ComponentTab')
          .node({ type: 'Text', occurrence: 0 })
          .create('Patched abcd');
        componentV1.param('message', 'Patched V1 test parameter');
        componentV1.state('tapCount', 40);
        componentV1.state('statusText', 'Patched V1 state before render');
        var v2EventProbe = 'not-run';
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () {
            return v2EventProbe;
          });
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
          .attr('backgroundColor', '#E7F7EE')
          .attr('backgroundImage', $r('app.media.app_icon'), { syncLoad: true });
        component.node({
          type: 'Button',
          where: { id: 'v2-primary-action', height: 44 }
        }).attr('id', 'patched-v2-primary-action');
        component.node({ type: 'Button', where: { id: 'missing-action' } })
          .attr('id', 'unexpected-action');
        var primaryOrigin = component.node({ type: 'Button', occurrence: 0 })
          .event('onClick', function () {
            this.markPrimary(10);
            var result = primaryOrigin.apply(this, arguments);
            this.statusText = 'Patched V2 test event, taps=' + this.tapCount;
            v2EventProbe = 'primary taps=' + this.tapCount;
            return result;
          });
        var secondaryOrigin = component.node({ type: 'Button', occurrence: 1 })
          .event('onClick', function () {
            var result = secondaryOrigin.apply(this, arguments);
            v2EventProbe = 'secondary taps=' + this.tapCount;
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
        var builderMethod = Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/BuilderMethodPanel#BuilderMethodPanel'
        ).builder('contentBuilder');
        builderMethod.node({ type: 'Column', occurrence: 0 })
          .create({ space: 18 });
        builderMethod.node({ type: 'Text', occurrence: 0 })
          .create('Patched Builder method title')
          .attr('id', 'patched-builder-method-title');
        var builderMethodOrigin = builderMethod.node({
          type: 'Button',
          where: { id: 'builder-method-action', height: 42 }
        })
          .attr('id', 'patched-builder-method-action')
          .event('onClick', function () {
            var result = builderMethodOrigin.apply(this, arguments);
            this.statusText = 'Patched @Builder event';
            return result;
          });
      `;
      expect(OhosPatch.executeScript(script).installedCount).assertLarger(0);
      await openButton.click();
      const patchedTabs = await driver.waitForComponent(ON.id('patch-target-tabs'), 5000);
      if (patchedTabs === undefined || patchedTabs === null) {
        throw new Error('patch-target-tabs was not found after patch installation');
      }

      const patchedCreateText = await driver.waitForComponent(ON.text('Patched abcd'), 5000);
      if (patchedCreateText === undefined || patchedCreateText === null) {
        throw new Error('patched Text create argument was not found in Components TabContent');
      }
      expect(await patchedCreateText.getText()).assertEqual('Patched abcd');

      const v1Parameter = await driver.waitForComponent(ON.text('Patched V1 test parameter'), 5000);
      if (v1Parameter === undefined || v1Parameter === null) {
        throw new Error('patched V1 parameter was not found in Components TabContent');
      }
      expect(await v1Parameter.getText()).assertEqual('Patched V1 test parameter');
      const v1State = await driver.waitForComponent(ON.id('v1-state-text'), 5000);
      if (v1State === undefined || v1State === null) {
        throw new Error('patched V1 state was not found in Components TabContent');
      }
      expect(await v1State.getText()).assertEqual('tapCount=40, secondary=1');
      const v1Status = await driver.waitForComponent(ON.id('v1-status-text'), 5000);
      if (v1Status === undefined || v1Status === null) {
        throw new Error('patched V1 status was not found in Components TabContent');
      }
      expect(await v1Status.getText()).assertEqual('status=Patched V1 state before render');

      await selectPatchTab(driver, 'V2');
      const parameter = await driver.waitForComponent(ON.id('v2-message-text'), 5000);
      if (parameter === undefined || parameter === null) {
        throw new Error('patched V2 parameter was not found in V2 TabContent');
      }
      expect(await parameter.getText())
        .assertEqual('Patched V2 test parameter, origin=Original V2 component parameter, count=0');
      const initialState = await driver.waitForComponent(ON.id('v2-state-text'), 5000);
      if (initialState === undefined || initialState === null) {
        throw new Error('patched V2 state was not found in V2 TabContent');
      }
      expect(await initialState.getText()).assertEqual('tapCount=100');
      const initialStatus = await driver.waitForComponent(ON.id('v2-status-text'), 5000);
      if (initialStatus === undefined || initialStatus === null) {
        throw new Error('patched V2 status was not found in V2 TabContent');
      }
      expect(await initialStatus.getText()).assertEqual('status=Patched V2 state callback, origin=0');

      const eventButton = await driver.waitForComponent(ON.id('patched-v2-primary-action'), 5000);
      if (eventButton === undefined || eventButton === null) {
        throw new Error('patched-v2-primary-action was not found in V2 TabContent');
      }
      expect(await eventButton.getText()).assertEqual('V2 primary action');
      const eventButtonCenter = await eventButton.getBoundsCenter();
      await driver.click(eventButtonCenter.x, eventButtonCenter.y);
      await driver.delayMs(500);
      expect(DemoViewModel.crash()).assertEqual('primary taps=111');

      const secondaryButton = await driver.waitForComponent(ON.id('v2-secondary-action'), 5000);
      if (secondaryButton === undefined || secondaryButton === null) {
        throw new Error('v2-secondary-action was not found in V2 TabContent');
      }
      await secondaryButton.click();
      await driver.delayMs(500);
      expect(DemoViewModel.crash()).assertEqual('secondary taps=113');

      const firstChild = await driver.waitForComponent(ON.text('first:static child'), 5000);
      if (firstChild === undefined || firstChild === null) {
        throw new Error('first child parameter was not found in V2 TabContent');
      }
      const secondChild = await driver.waitForComponent(ON.text('second:scoped patched dynamic child'), 5000);
      if (secondChild === undefined || secondChild === null) {
        throw new Error('scoped second child parameter was not found in V2 TabContent');
      }

      await selectPatchTab(driver, 'BuilderParam');
      const builderText = await driver.waitForComponent(ON.id('patched-builder-slot-text'), 5000);
      if (builderText === undefined || builderText === null) {
        throw new Error('patched-builder-slot-text was not found in BuilderParam TabContent');
      }
      expect(await builderText.getText()).assertEqual('slot text=Original builder-param state');
      const builderButton = await driver.waitForComponent(ON.id('patched-builder-slot-button'), 5000);
      if (builderButton === undefined || builderButton === null) {
        throw new Error('patched-builder-slot-button was not found in BuilderParam TabContent');
      }
      await builderButton.click();
      const inlineBuilderText = await driver.waitForComponent(ON.id('patched-inline-builder-slot-text'), 5000);
      if (inlineBuilderText === undefined || inlineBuilderText === null) {
        throw new Error('patched-inline-builder-slot-text was not found in BuilderParam TabContent');
      }
      const inlineBuilderStatus = await driver.waitForComponent(
        ON.text('inline slot text=Patched BuilderParam slot event'), 5000
      );
      if (inlineBuilderStatus === undefined || inlineBuilderStatus === null) {
        throw new Error('patched inline BuilderParam slot state was not found in BuilderParam TabContent');
      }
      const inlineBuilderButton = await driver.waitForComponent(ON.id('patched-inline-builder-slot-button'), 5000);
      if (inlineBuilderButton === undefined || inlineBuilderButton === null) {
        throw new Error('patched-inline-builder-slot-button was not found in BuilderParam TabContent');
      }
      await inlineBuilderButton.click();
      const multiHeaderText = await driver.waitForComponent(ON.id('patched-multi-builder-header-text'), 5000);
      if (multiHeaderText === undefined || multiHeaderText === null) {
        throw new Error('patched-multi-builder-header-text was not found in BuilderParam TabContent');
      }
      const originalHeaderButton = await driver.waitForComponent(ON.id('multi-builder-header-button'), 5000);
      if (originalHeaderButton === undefined || originalHeaderButton === null) {
        throw new Error('unpatched multi-builder-header-button was not found in BuilderParam TabContent');
      }
      expect(await originalHeaderButton.getText()).assertEqual('Multi header action');
      const multiContentText = await driver.waitForComponent(ON.id('patched-multi-builder-content-text'), 5000);
      if (multiContentText === undefined || multiContentText === null) {
        throw new Error('patched-multi-builder-content-text was not found in BuilderParam TabContent');
      }
      expect(await multiContentText.getText())
        .assertEqual('multi content text=Patched inline BuilderParam slot event');
      const multiContentButton = await driver.waitForComponent(ON.id('patched-multi-builder-content-button'), 5000);
      if (multiContentButton === undefined || multiContentButton === null) {
        throw new Error('patched-multi-builder-content-button was not found in BuilderParam TabContent');
      }
      await multiContentButton.click();
      const multiContentEventStatus = await driver.waitForComponent(
        ON.text('multi content text=Patched explicit contentBuilder event'), 5000
      );
      if (multiContentEventStatus === undefined || multiContentEventStatus === null) {
        throw new Error('patched explicit contentBuilder event status was not found in BuilderParam TabContent');
      }

      await selectPatchTab(driver, 'BuilderMethod');
      const outsideBuilderMethodText = await driver.waitForComponent(ON.id('builder-method-outside'), 5000);
      if (outsideBuilderMethodText === undefined || outsideBuilderMethodText === null) {
        throw new Error('unpatched node outside Builder method was not found in BuilderMethod TabContent');
      }
      expect(await outsideBuilderMethodText.getText()).assertEqual('Outside Builder method');
      const builderMethodTitle = await driver.waitForComponent(ON.id('patched-builder-method-title'), 5000);
      if (builderMethodTitle === undefined || builderMethodTitle === null) {
        throw new Error('patched node inside Builder method was not found in BuilderMethod TabContent');
      }
      expect(await builderMethodTitle.getText()).assertEqual('Patched Builder method title');
      const builderMethodButton = await driver.waitForComponent(ON.id('patched-builder-method-action'), 5000);
      if (builderMethodButton === undefined || builderMethodButton === null) {
        throw new Error('patched Builder method event was not found in BuilderMethod TabContent');
      }
      await builderMethodButton.click();
      const builderMethodStatus = await driver.waitForComponent(
        ON.text('builder method status=Patched @Builder event'), 5000
      );
      if (builderMethodStatus === undefined || builderMethodStatus === null) {
        throw new Error('patched Builder method event state was not found in BuilderMethod TabContent');
      }

      OhosPatch.clear();
      const reinstallScript = `
        Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/PatchablePanelV2#PatchablePanelV2'
        ).state('tapCount', 200);
      `;
      expect(OhosPatch.executeScript(reinstallScript).installedCount).assertLarger(0);
      await driver.pressBack();
      const reopenButton = await driver.waitForComponent(ON.text('Open patch target screen'), 5000);
      if (reopenButton === undefined || reopenButton === null) {
        throw new Error('Open patch target screen button was not found before state-only reinstall verification');
      }
      await reopenButton.click();
      const stateOnlyTabs = await driver.waitForComponent(ON.id('patch-target-tabs'), 5000);
      if (stateOnlyTabs === undefined || stateOnlyTabs === null) {
        throw new Error('patch-target-tabs was not found for state-only reinstall verification');
      }
      await selectPatchTab(driver, 'V2');
      const stateOnlyV2State = await driver.waitForComponent(ON.id('v2-state-text'), 5000);
      if (stateOnlyV2State === undefined || stateOnlyV2State === null) {
        throw new Error('state-only ComponentV2 patch was not found in V2 TabContent');
      }
      expect(await stateOnlyV2State.getText()).assertEqual('tapCount=200');
    });

    it('component DSL patches system nodes inside NavDestination content builder', 0, async () => {
      const script = `
        Fixit.component(
          'com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/components/MainPage#MainPage'
        ).node({ type: 'Text', occurrence: 0 })
          .create('Patched NavDestination content')
          .attr('id', 'patched-nav-destination-text');
      `;
      expect(OhosPatch.executeScript(script).installedCount).assertLarger(0);

      const delegator = abilityDelegatorRegistry.getAbilityDelegator();
      const want: Want = {
        bundleName: 'com.rickytan.ohospatch',
        moduleName: 'entry',
        abilityName: 'EntryAbility'
      };
      await delegator.startAbility(want);

      const driver = Driver.create();
      let openButton = await driver.waitForComponent(ON.text('Open patch target screen'), 2000);
      if (openButton === undefined || openButton === null) {
        await driver.pressBack();
        openButton = await driver.waitForComponent(ON.text('Open patch target screen'), 5000);
      }
      if (openButton === undefined || openButton === null) {
        throw new Error('Open patch target screen button was not found');
      }

      await openButton.click();
      const patchedText = await driver.waitForComponent(ON.id('patched-nav-destination-text'), 5000);
      if (patchedText === undefined || patchedText === null) {
        throw new Error('patched NavDestination content Text was not found');
      }
      expect(await patchedText.getText()).assertEqual('Patched NavDestination content');
      await driver.pressBack();
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
      expect(OhosPatch.executeScript(invalid).installedCount).assertEqual(0);

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
      expect(OhosPatch.executeScript(valid).installedCount).assertLarger(0);
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
      const result = OhosPatch.executeScript(script);
      expect(result.installedCount).assertLarger(0);
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
      expect(OhosPatch.executeScript(script).installedCount).assertLarger(0);
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
      const result = OhosPatch.executeScript(script);
      expect(result.installedCount).assertEqual(0);

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
