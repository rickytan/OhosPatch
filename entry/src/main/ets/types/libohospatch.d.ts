declare module 'libohospatch.so' {
  export const init: () => void;
  export const clear: () => void;
  export const executeScript: (script: string) => number;
}
