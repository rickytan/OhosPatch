import { common, StartupTask } from '@kit.AbilityKit';
import { OhosPatch } from '@rickytan/ohospatch';

@Sendable
export default class DemoPatchStartupTask extends StartupTask {
  async init(context: common.AbilityStageContext): Promise<void> {
    OhosPatch.init(context);
  }
}
