export const init: (context?: object) => void;
export const clear: () => void;
export interface PatchInstallResult {
  installedCount: number;
}
export const executeScript: (script: string, context?: object) => PatchInstallResult;
