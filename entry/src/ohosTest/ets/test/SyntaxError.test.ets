import { describe, it, expect, beforeEach, afterEach } from '@ohos/hypium';
import { OhosPatch } from '@rickytan/ohospatch';
import { DemoViewModel } from '@vendor/business_page';

export default function syntaxErrorTest(): void {
  describe('SyntaxErrorHandling', () => {
    beforeEach(() => {
      OhosPatch.clear();
    });

    afterEach(() => {
      OhosPatch.clear();
    });

    it('syntax error in patch script does not crash and returns 0 hooks', 0, () => {
      const malformed = 'Fixit.fix( { ; ))) broken syntax !!! @@@';
      const count = OhosPatch.executeScript(malformed);
      expect(count).assertEqual(0);
    });

    it('runtime recovers and loads a valid patch after a malformed script', 0, () => {
      // Step 1: malformed script fails.
      const malformed = ')))(invalid {{{ javascript';
      OhosPatch.executeScript(malformed);

      // Step 2: valid patch loads successfully.
      const valid = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .instanceMethod('crashIt', function () { return 'recovered'; });
      `;
      const count = OhosPatch.executeScript(valid);
      expect(count).assertLarger(0);

      const vm = new DemoViewModel();
      expect(vm.crashIt()).assertEqual('recovered');
    });

    it('runtime error (throw) in patch script is caught and returns 0 hooks', 0, () => {
      const runtimeError = 'throw new Error("patch runtime failure");';
      const count = OhosPatch.executeScript(runtimeError);
      expect(count).assertEqual(0);
    });

    it('valid patch works after a runtime-error script', 0, () => {
      const runtimeError = 'throw new Error("boom");';
      OhosPatch.executeScript(runtimeError);

      const valid = `
        Fixit.fix('com.rickytan.ohospatch/entry_test/@vendor/business_page/src/main/ets/ViewModel/DemoViewModel#DemoViewModel')
          .classMethod('crash', function () { return 'post-error-ok'; });
      `;
      const count = OhosPatch.executeScript(valid);
      expect(count).assertLarger(0);
      expect(DemoViewModel.crash()).assertEqual('post-error-ok');
    });

    it('empty script returns 0 hooks without crashing', 0, () => {
      expect(OhosPatch.executeScript('')).assertEqual(0);
    });

    it('script with only comments returns 0 hooks', 0, () => {
      const commentOnly = '// just a comment\n/* block comment */';
      expect(OhosPatch.executeScript(commentOnly)).assertEqual(0);
    });
  });
}
