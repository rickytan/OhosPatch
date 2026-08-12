import { describe, it, expect, beforeEach, afterEach } from '@ohos/hypium';
import { systemDateTime } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { OhosPatch } from '@rickytan/ohospatch';
import { DemoViewModel } from '../../../main/ets/demo/DemoViewModel';

const PERF_DOMAIN = 0x3900;
const PERF_TAG = 'OhosPatchPerf';
const SAMPLE_COUNT = 3;
const CALL_COUNT = 20;
const WORK_ROUNDS = 500000;
const MAX_DEGRADATION_RATIO = 1.2;
const MAX_SCRIPT_LOAD_NANOSECONDS = 20 * 1000 * 1000;
let benchmarkResult = 0;

function activeTimeNanoseconds(): number {
  try {
    return systemDateTime.getUptime(systemDateTime.TimeType.ACTIVE, true);
  } catch (_) {
    return Date.now() * 1000000;
  }
}

function measureExpensiveCalls(vm: DemoViewModel): number {
  let bestElapsed = Number.MAX_VALUE;
  for (let sample = 0; sample < SAMPLE_COUNT; sample++) {
    let result = 0;
    const start = activeTimeNanoseconds();
    for (let call = 0; call < CALL_COUNT; call++) {
      result += vm.expensiveCalculation(call + 1, WORK_ROUNDS);
    }
    const elapsed = activeTimeNanoseconds() - start;
    benchmarkResult = result;
    bestElapsed = Math.min(bestElapsed, elapsed);
  }
  return bestElapsed;
}

export default function perfTest(): void {
  describe('Performance', () => {
    beforeEach(() => {
      OhosPatch.clear();
    });

    afterEach(() => {
      OhosPatch.clear();
    });

    it('loads and installs a patch script within 20 milliseconds', 0, () => {
      const script = `
        Fixit.fix(
          'com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel'
        ).instanceMethod('crashIt', function () {
          return 'performance-test';
        });
      `;
      const startedAt = activeTimeNanoseconds();
      const installedCount = OhosPatch.executeScript(script);
      const elapsed = activeTimeNanoseconds() - startedAt;

      hilog.info(PERF_DOMAIN, PERF_TAG, 'scriptLoad=%{public}d ns', elapsed);
      expect(installedCount).assertLarger(0);
      expect(elapsed <= MAX_SCRIPT_LOAD_NANOSECONDS).assertTrue();
    });

    it('origin-delegating patch degrades an expensive method by no more than 20 percent', 0, () => {
      const vm = new DemoViewModel();

      // Warm up the ArkTS method before recording the unpatched baseline.
      for (let index = 0; index < 3; index++) {
        vm.expensiveCalculation(index + 1, WORK_ROUNDS);
      }
      const baselineElapsed = measureExpensiveCalls(vm);
      const baselineResult = benchmarkResult;
      expect(baselineElapsed > 0).assertTrue();

      const script = `
        var origin = Fixit.fix(
          'com.rickytan.ohospatch/entry_test/entry/src/main/ets/demo/DemoViewModel#DemoViewModel'
        ).instanceMethod('expensiveCalculation', function () {
          return origin.apply(this, arguments);
        });
      `;
      expect(OhosPatch.executeScript(script)).assertLarger(0);

      // Warm up the JSVM dispatch and origin bridge before recording patched samples.
      for (let index = 0; index < 3; index++) {
        vm.expensiveCalculation(index + 1, WORK_ROUNDS);
      }
      const patchedElapsed = measureExpensiveCalls(vm);
      const patchedResult = benchmarkResult;
      const degradationRatio = patchedElapsed / baselineElapsed;

      hilog.info(
        PERF_DOMAIN,
        PERF_TAG,
        'baseline=%{public}d ns, patched=%{public}d ns, ratio=%{public}s',
        baselineElapsed,
        patchedElapsed,
        degradationRatio.toFixed(3)
      );

      expect(patchedResult).assertEqual(baselineResult);
      expect(degradationRatio <= MAX_DEGRADATION_RATIO).assertTrue();
    });
  });
}
