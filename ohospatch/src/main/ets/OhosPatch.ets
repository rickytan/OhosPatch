import { http } from '@kit.NetworkKit';
import { clear, executeScript, init } from 'libohospatch.so';

export class OhosPatch {
  static init(): void {
    init();
  }

  static executeScript(script: string): number {
    return executeScript(script);
  }

  static clear(): void {
    clear();
  }

  static async installFrom(url: string): Promise<number> {
    const request = http.createHttp();
    try {
      const response = await request.request(url, {
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
      return OhosPatch.executeScript(response.result);
    } catch (err) {
      throw err instanceof Error ? err : new Error(String(err));
    } finally {
      request.destroy();
    }
  }
}
