import { common, StartupTask } from '@kit.AbilityKit';
import { http } from '@kit.NetworkKit';
import { OhosPatch } from '@rickytan/ohospatch';

@Sendable
export default class DemoPatchStartupTask extends StartupTask {
  async init(context: common.AbilityStageContext): Promise<void> {
    const request = http.createHttp();
    try {
      const patchUrl = context.resourceManager.getStringByNameSync('ohospatch_patch_url');
      if (patchUrl.length === 0) {
        return;
      }

      const response = await request.request(patchUrl, {
        method: http.RequestMethod.GET,
        expectDataType: http.HttpDataType.STRING,
        connectTimeout: 5000,
        readTimeout: 5000,
        usingCache: false
      });
      if (response.responseCode < 200 || response.responseCode >= 300) {
        throw new Error(`Patch request failed with HTTP ${response.responseCode}`);
      }
      if (typeof response.result !== 'string') {
        throw new Error('Patch response is not JavaScript text');
      }

      // A production host verifies the patch signature before this call.
      const count = OhosPatch.executeScript(response.result);
      console.info(`Demo host installed ${count} OhosPatch hooks from ${patchUrl}`);
    } catch (err) {
      console.error(`Demo patch startup skipped: ${(err as Error).message}`);
    } finally {
      request.destroy();
    }
  }
}
