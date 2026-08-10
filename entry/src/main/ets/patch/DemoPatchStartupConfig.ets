import { StartupConfig, StartupConfigEntry } from '@kit.AbilityKit';

export default class DemoPatchStartupConfig extends StartupConfigEntry {
  onConfig(): StartupConfig {
    return {
      timeoutMs: 6000
    };
  }
}
