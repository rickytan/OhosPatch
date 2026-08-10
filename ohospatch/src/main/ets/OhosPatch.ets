import { fileIo } from '@kit.CoreFileKit';
import { clear, executeScript } from 'libohospatch.so';

export class OhosPatch {
  static executeScript(script: string): number {
    return executeScript(script);
  }

  static executeFile(absolutePath: string): number {
    if (!absolutePath.startsWith('/')) {
      throw new Error('Patch file path must be absolute');
    }
    try {
      return OhosPatch.executeScript(fileIo.readTextSync(absolutePath));
    } catch (err) {
      throw err instanceof Error ? err : new Error(String(err));
    }
  }

  static clear(): void {
    clear();
  }
}
