import { describe, it, expect, beforeEach, afterEach } from '@ohos/hypium';
import { OhosPatch } from '@rickytan/ohospatch';
import { DemoViewModel } from '../../../main/ets/demo/DemoViewModel';

export default function perfTest(): void {
  describe('Performance', () => {
    beforeEach(() => {
      OhosPatch.clear();
    });

    afterEach(() => {
      OhosPatch.clear();
    });

    it('executeScript completes within a reasonable time for a small patch', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'perf-test'; });
      `;
      const start = Date.now();
      OhosPatch.executeScript(script);
      const elapsed = Date.now() - start;
      expect(elapsed < 500).assertTrue();
    });

    it('patched method call overhead is within acceptable bounds', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'overhead-ok'; });
      `;
      OhosPatch.executeScript(script);

      const vm = new DemoViewModel();
      const N = 100;
      const start = Date.now();
      for (let i = 0; i < N; i++) {
        vm.crashIt();
      }
      const elapsed = Date.now() - start;
      expect(elapsed < 2000).assertTrue();
    });

    it('clear and re-patch cycle is fast', 0, () => {
      const script = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'cycle'; });
      `;
      const start = Date.now();
      for (let i = 0; i < 10; i++) {
        OhosPatch.clear();
        OhosPatch.executeScript(script);
      }
      const elapsed = Date.now() - start;
      expect(elapsed < 5000).assertTrue();
    });
  });
}
