import { afterEach, beforeEach, describe, expect, it } from '@ohos/hypium';
import { Want } from '@kit.AbilityKit';
import { abilityDelegatorRegistry, Component, Driver, ON, On, Point as UiPoint } from '@kit.TestKit';
import { OhosPatch } from '@rickytan/ohospatch';
import { DemoScenario, DemoViewModel, Point } from '@vendor/business_page';

async function findWithForwardScroll(driver: Driver, on: On, label: string, displaySize: UiPoint): Promise<Component> {
  const swipeX = Math.floor(displaySize.x * 0.5);
  const startY = Math.floor(displaySize.y * 0.68);
  const endY = Math.floor(displaySize.y * 0.53);
  for (let attempt = 0; attempt < 20; attempt++) {
    const component = await driver.findComponent(on);
    if (component !== undefined && component !== null) {
      return component;
    }
    await driver.swipe(swipeX, startY, swipeX, endY, 5000);
  }
  const textComponents = await driver.findComponents(ON.type('Text'));
  const visibleTexts: Array<string> = [];
  for (let index = 0; index < textComponents.length; index++) {
    visibleTexts.push(await textComponents[index].getText());
  }
  throw new Error(
    `${label} was not found after scrolling; display=${displaySize.x}x${displaySize.y}; ` +
    `visibleTexts=${visibleTexts.join(' | ')}`
  );
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
      const displaySize = await driver.getDisplaySize();
      const baselineScroll = await driver.waitForComponent(ON.id('patch-target-scroll'), 5000);
      if (baselineScroll === undefined || baselineScroll === null) {
        throw new Error('patch-target-scroll was not found before patch installation');
      }
      const baselineCreateText = await driver.waitForComponent(ON.text('abcd'), 5000);
      if (baselineCreateText === undefined || baselineCreateText === null) {
        throw new Error('Text create marker was not found before patch installation');
      }
      await baselineScroll.scrollToBottom(5000);
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
        ).node({ type: 'Text', occurrence: 0 })
          .create('Patched abcd');
        componentV1.param('message', 'Patched V1 test parameter');
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
          .attr('backgroundColor', '#E7F7EE')
          .attr('backgroundImage', $r('app.media.app_icon'), { syncLoad: true });
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
            this.getUIContext().showAlertDialog({
              title: 'OhosPatch',
              message: 'Nested Patch JSVM function callback test',
              buttons: [{
                value: 'Patch action',
                action: function () {
                  this.statusText = 'Patch alert button action, taps=' + this.tapCount;
                }
              }]
            });
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
      expect(OhosPatch.executeScript(script).installedCount).assertLarger(0);
      await openButton.click();
      const patchedScroll = await driver.waitForComponent(ON.id('patch-target-scroll'), 5000);
      if (patchedScroll === undefined || patchedScroll === null) {
        throw new Error('patch-target-scroll was not found after patch installation');
      }
      await patchedScroll.scrollToTop(5000);

      const patchedCreateText = await findWithForwardScroll(
        driver, ON.text('Patched abcd'), 'patched Text create argument', displaySize
      );
      expect(await patchedCreateText.getText()).assertEqual('Patched abcd');

      const v1Parameter = await findWithForwardScroll(
        driver, ON.text('Patched V1 test parameter'), 'patched V1 parameter with state rules', displaySize
      );
      expect(await v1Parameter.getText()).assertEqual('Patched V1 test parameter');
      const v1State = await findWithForwardScroll(driver, ON.id('v1-state-text'), 'patched V1 state', displaySize);
      expect(await v1State.getText()).assertEqual('tapCount=40, secondary=1');
      const v1Status = await findWithForwardScroll(driver, ON.id('v1-status-text'), 'patched V1 status', displaySize);
      expect(await v1Status.getText()).assertEqual('status=Patched V1 state before render');

      const parameter = await findWithForwardScroll(
        driver, ON.id('v2-message-text'), 'patched V2 parameter', displaySize
      );
      expect(await parameter.getText())
        .assertEqual('Patched V2 test parameter, origin=Original V2 component parameter, count=0');
      const initialState = await findWithForwardScroll(
        driver, ON.id('v2-state-text'), 'patched V2 state', displaySize
      );
      expect(await initialState.getText()).assertEqual('tapCount=100');
      const initialStatus = await findWithForwardScroll(
        driver, ON.id('v2-status-text'), 'patched V2 status', displaySize
      );
      expect(await initialStatus.getText()).assertEqual('status=Patched V2 state callback, origin=0');

      const eventButton = await findWithForwardScroll(
        driver, ON.id('patched-v2-primary-action'), 'patched-v2-primary-action', displaySize
      );
      expect(await eventButton.getText()).assertEqual('V2 primary action');
      await eventButton.click();
      const eventStatus = await driver.waitForComponent(
        ON.text('status=Patched V2 test event, taps=111'), 5000
      );
      if (eventStatus === undefined || eventStatus === null) {
        throw new Error('patched V2 event status was not updated after click');
      }
      const alertAction = await driver.waitForComponent(ON.text('Patch action'), 5000);
      if (alertAction === undefined || alertAction === null) {
        throw new Error('Patch alert action button was not shown');
      }
      await alertAction.click();
      const alertActionStatus = await driver.waitForComponent(
        ON.text('status=Patch alert button action, taps=111'), 5000
      );
      if (alertActionStatus === undefined || alertActionStatus === null) {
        throw new Error('Patch alert button callback did not update component state');
      }

      const secondaryButton = await findWithForwardScroll(
        driver, ON.id('v2-secondary-action'), 'v2-secondary-action', displaySize
      );
      await secondaryButton.click();
      const refreshedScroll = await driver.waitForComponent(ON.id('patch-target-scroll'), 5000);
      if (refreshedScroll === undefined || refreshedScroll === null) {
        throw new Error('patch-target-scroll was not found after V2 secondary click');
      }
      await refreshedScroll.scrollToTop(5000);
      const secondaryStatus = await driver.waitForComponent(ON.text('status=V2 event ran, taps=113'), 5000);
      if (secondaryStatus === undefined || secondaryStatus === null) {
        throw new Error('V2 secondary event status was not updated after click');
      }

      await findWithForwardScroll(driver, ON.text('first:static child'), 'first child parameter', displaySize);
      await findWithForwardScroll(
        driver, ON.text('second:scoped patched dynamic child'), 'scoped second child parameter', displaySize
      );

      const builderText = await findWithForwardScroll(
        driver, ON.id('patched-builder-slot-text'), 'patched-builder-slot-text', displaySize
      );
      expect(await builderText.getText()).assertEqual('slot text=Original builder-param state');
      const builderButton = await findWithForwardScroll(
        driver, ON.id('patched-builder-slot-button'), 'patched-builder-slot-button', displaySize
      );
      await builderButton.click();
      await findWithForwardScroll(
        driver, ON.id('patched-inline-builder-slot-text'), 'patched-inline-builder-slot-text', displaySize
      );
      await findWithForwardScroll(
        driver,
        ON.text('inline slot text=Patched BuilderParam slot event'),
        'patched inline BuilderParam slot state',
        displaySize
      );
      const inlineBuilderButton = await findWithForwardScroll(
        driver, ON.id('patched-inline-builder-slot-button'), 'patched-inline-builder-slot-button', displaySize
      );
      await inlineBuilderButton.click();
      const multiHeaderText = await findWithForwardScroll(
        driver, ON.id('patched-multi-builder-header-text'), 'patched-multi-builder-header-text', displaySize
      );
      const originalHeaderButton = await findWithForwardScroll(
        driver, ON.id('multi-builder-header-button'), 'unpatched multi-builder-header-button', displaySize
      );
      expect(await originalHeaderButton.getText()).assertEqual('Multi header action');
      const multiContentText = await findWithForwardScroll(
        driver, ON.id('patched-multi-builder-content-text'), 'patched-multi-builder-content-text', displaySize
      );
      expect(await multiContentText.getText())
        .assertEqual('multi content text=Patched inline BuilderParam slot event');
      const multiContentButton = await findWithForwardScroll(
        driver, ON.id('patched-multi-builder-content-button'), 'patched-multi-builder-content-button', displaySize
      );
      await multiContentButton.click();
      await findWithForwardScroll(
        driver,
        ON.text('root status=Patched explicit contentBuilder event'),
        'patched explicit contentBuilder event status',
        displaySize
      );

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
      const stateOnlyScroll = await driver.waitForComponent(ON.id('patch-target-scroll'), 5000);
      if (stateOnlyScroll === undefined || stateOnlyScroll === null) {
        throw new Error('patch-target-scroll was not found for state-only reinstall verification');
      }
      await stateOnlyScroll.scrollToTop(5000);
      const stateOnlyV2State = await findWithForwardScroll(
        driver, ON.id('v2-state-text'), 'state-only ComponentV2 patch', displaySize
      );
      expect(await stateOnlyV2State.getText()).assertEqual('tapCount=200');
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
