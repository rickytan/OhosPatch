import { common, StartupTask } from '@kit.AbilityKit';
import { loadPatch } from './PatchLoader';

@Sendable
export default class DemoPatchStartupTask extends StartupTask {
  async init(context: common.AbilityStageContext): Promise<void> {
    try {
      const result = await loadPatch(context);
      console.info(`Demo patch startup: source=${result.source}, hooks=${result.hookCount}, ${result.detail}`);
    } catch (err) {
      console.error(`Demo patch startup failed: ${(err as Error).message}`);
    }
  }
}
