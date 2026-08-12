import { fileIo } from '@kit.CoreFileKit';
import { common } from '@kit.AbilityKit';
import { clear, executeScript, init } from 'libohospatch.so';

export class OhosPatch {
  static init(context: common.Context): void {
    init(context);
  }

  static executeScript(script: string, context?: common.Context): number {
    if (context) {
      return executeScript(script, context);
    }
    return executeScript(script);
  }

  static executeFile(absolutePath: string, context?: common.Context): number {
    if (!absolutePath.startsWith('/')) {
      throw new Error('Patch file path must be absolute');
    }
    try {
      return OhosPatch.executeScript(fileIo.readTextSync(absolutePath), context);
    } catch (err) {
      throw err instanceof Error ? err : new Error(String(err));
    }
  }

  static clear(): void {
    clear();
  }
}
