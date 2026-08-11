import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';
import { Hypium } from '@ohos/hypium';
import tests from '../test/List.test';

export default class TestAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(0x0000, 'OhosPatchTest', 'TestAbility onCreate');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    hilog.info(0x0000, 'OhosPatchTest', 'TestAbility onWindowStageCreate');
    windowStage.loadContent('testability/pages/TestPage', (err) => {
      if (err.code) {
        hilog.error(0x0000, 'OhosPatchTest', 'Failed to load test page: %{public}s', JSON.stringify(err));
      }
    });

    const abilityDelegator = abilityDelegatorRegistry.getAbilityDelegator();
    const abilityDelegatorArguments = abilityDelegatorRegistry.getArguments();
    Hypium.hypiumTest(abilityDelegator, abilityDelegatorArguments, tests);
  }
}
