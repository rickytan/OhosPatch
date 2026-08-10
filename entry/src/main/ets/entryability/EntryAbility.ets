import { AbilityConstant, startupManager, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';

export default class EntryAbility extends UIAbility {
  private patchStartup: Promise<void> = Promise.resolve();

  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(0x0000, 'OhosPatch', 'Ability onCreate');
    this.patchStartup = startupManager.run(['OhosPatchStartupTask'], {
      timeoutMs: 6000
    }).catch((err: Error) => {
      hilog.error(0x0000, 'OhosPatch', 'Patch startup failed: %{public}s', err.message);
    });
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    this.patchStartup.then(() => {
      this.loadDemo(windowStage);
    });
  }

  private loadDemo(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(0x0000, 'OhosPatch', 'Failed to load page: %{public}s', JSON.stringify(err));
      }
    });
  }
}
