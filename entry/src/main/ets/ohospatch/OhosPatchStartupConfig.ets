import { StartupConfig, StartupConfigEntry } from '@kit.AbilityKit';

export default class OhosPatchStartupConfig extends StartupConfigEntry {
  onConfig(): StartupConfig {
    return {
      timeoutMs: 6000
    };
  }
}
