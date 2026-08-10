import { common, StartupTask } from '@kit.AbilityKit';
import { OhosPatch } from './OhosPatch';

@Sendable
export default class OhosPatchStartupTask extends StartupTask {
  async init(context: common.AbilityStageContext): Promise<void> {
    try {
      const patchUrl = context.resourceManager.getStringByNameSync('ohospatch_patch_url');
      if (patchUrl.length === 0) {
        return;
      }

      const count = await OhosPatch.installFrom(patchUrl);
      console.info(`OhosPatch installed ${count} hooks from ${patchUrl}`);
    } catch (err) {
      console.error(`OhosPatch startup skipped: ${(err as Error).message}`);
    }
  }
}
