import { common } from '@kit.AbilityKit';
import { fileIo } from '@kit.CoreFileKit';
import { http } from '@kit.NetworkKit';
import { util } from '@kit.ArkTS';
import { OhosPatch } from '@rickytan/ohospatch';

export enum PatchSource {
  Network = 'network',
  Cache = 'cache',
  Builtin = 'builtin'
}

export interface PatchLoadResult {
  source: PatchSource;
  hookCount: number;
  detail: string;
}

export interface PatchLoadOptions {
  forceNetwork?: boolean;
}

const CACHE_DIR_NAME = 'ohospatch';
const CACHE_FILE_NAME = 'patch.js';
const BUILTIN_FILE_NAME = 'patch.js';
const PATCH_URL_RESOURCE = 'ohospatch_patch_url';

function cachePath(context: common.Context): string {
  return `${context.filesDir}/${CACHE_DIR_NAME}/${CACHE_FILE_NAME}`;
}

function readCache(path: string): string | null {
  try {
    if (!fileIo.accessSync(path)) {
      return null;
    }
    const text = fileIo.readTextSync(path);
    return text.length > 0 ? text : null;
  } catch (_e) {
    return null;
  }
}

function writeCache(path: string, content: string): void {
  const dir = path.substring(0, path.lastIndexOf('/'));
  try {
    if (!fileIo.accessSync(dir)) {
      fileIo.mkdirSync(dir, true);
    }
    const file = fileIo.openSync(path, fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
    fileIo.writeSync(file.fd, content);
    fileIo.closeSync(file);
  } catch (err) {
    console.warn(`OhosPatch cache write skipped: ${(err as Error).message}`);
  }
}

async function fetchNetwork(context: common.Context): Promise<string> {
  const request = http.createHttp();
  try {
    const url = context.resourceManager.getStringByNameSync(PATCH_URL_RESOURCE);
    if (url.length === 0) {
      throw new Error(`${PATCH_URL_RESOURCE} is empty`);
    }
    const response = await request.request(url, {
      method: http.RequestMethod.GET,
      expectDataType: http.HttpDataType.STRING,
      connectTimeout: 5000,
      readTimeout: 5000,
      usingCache: false
    });
    if (response.responseCode < 200 || response.responseCode >= 300) {
      throw new Error(`HTTP ${response.responseCode}`);
    }
    if (typeof response.result !== 'string') {
      throw new Error('Patch response is not JavaScript text');
    }
    return response.result;
  } catch (err) {
    throw new Error(`Patch network fetch failed: ${(err as Error).message}`);
  } finally {
    request.destroy();
  }
}

function readBuiltin(context: common.Context): string {
  try {
    const buffer = context.resourceManager.getRawFileContentSync(BUILTIN_FILE_NAME);
    const decoder = util.TextDecoder.create('utf-8');
    return decoder.decodeToString(buffer);
  } catch (err) {
    throw new Error(`Failed to read builtin patch: ${(err as Error).message}`);
  }
}

export async function loadPatch(
  context: common.Context,
  options: PatchLoadOptions = {}
): Promise<PatchLoadResult> {
  OhosPatch.init(context);
  const path = cachePath(context);

  // Cache first: reuse the last successfully fetched patch without hitting the network.
  if (!options.forceNetwork) {
    const cached = readCache(path);
    if (cached !== null) {
      const result = OhosPatch.executeScript(cached);
      return { source: PatchSource.Cache, hookCount: result.installedCount, detail: 'loaded from local cache' };
    }
  }

  // Network: fetch, install, and persist for next launch.
  try {
    const script = await fetchNetwork(context);
    writeCache(path, script);
    const result = OhosPatch.executeScript(script);
    return { source: PatchSource.Network, hookCount: result.installedCount, detail: 'loaded from network' };
  } catch (err) {
    const message = (err as Error).message;
    // Force-network callers fall back to a stale cache before the builtin.
    if (options.forceNetwork) {
      const cached = readCache(path);
      if (cached !== null) {
        const result = OhosPatch.executeScript(cached);
        return {
          source: PatchSource.Cache,
          hookCount: result.installedCount,
          detail: `network failed, fell back to cache: ${message}`
        };
      }
    }
    // Last resort: the patch shipped inside the HAP.
    const builtin = readBuiltin(context);
    const result = OhosPatch.executeScript(builtin);
    return {
      source: PatchSource.Builtin,
      hookCount: result.installedCount,
      detail: `network failed, used builtin fallback: ${message}`
    };
  }
}
